#include "stationbox.h"
#include "siacp.h"
#include "usart.h"
#include "gpio.h"
#include "oled.h"
#include "string.h"

/* 灯引脚: 上位机"绿灯"->PA5, "黄灯"->PA6, "红灯"->PA4, 本地拦截->PA7 */
#define SB_LED_RED_PORT    LED_R_Port
#define SB_LED_RED_PIN     LED_R_Pin
#define SB_LED_GRN_PORT    LED_G_Port
#define SB_LED_GRN_PIN     LED_G_Pin
#define SB_LED_YEL_PORT    LED_B_Port
#define SB_LED_YEL_PIN     LED_B_Pin
#define SB_LED_KEY_PORT    LED_TEST_Port
#define SB_LED_KEY_PIN     LED_TEST_Pin

/* 继电器引脚: PA12 (Studio 模板中的 KEY_Con), 低电平吸合 */
#define SB_RELAY_PORT      KEY_Con_Port
#define SB_RELAY_PIN       KEY_Con_Pin

#define SB_SCAN_TIMEOUT_MS    8
#define SB_FRAME_TIMEOUT_MS   20
#define SB_LED_PERIOD_MS      150
#define SB_SCROLL_PERIOD_MS   150   /* 滚动步长, 同时作为显示刷新周期 */
#define SB_VERDICT_HOLD_MS    8000
#define SB_REJECT_BLINK_MS    1200
#define SB_PENDING_HOLD_MS    30000
#define SB_RELAY_HOLD_MS      500   /* 吸合保持时长, ms; 0 = 锁存直到收到 NG */

#define SB_OLED_Y0           5
#define SB_OLED_Y1           17
#define SB_OLED_Y2           29
#define SB_SCR_CHARS         14     /* 6x8 字模: 88px / 6px = 一屏字符数 */
#define SB_SCR_PAD           12     /* 条码后的空格, 作为滚动间隔 */

typedef struct
{
  uint8_t data[SB_BARCODE_MAX];
  uint8_t len;
  uint8_t used;
} SbEntry_t;

typedef struct
{
  SbLedMode_t mode;
  uint32_t    until;
  uint8_t     phase;
} SbLed_t;

static SbEntry_t s_pending;
static SbEntry_t s_block[SB_BLOCK_SIZE];
static uint8_t   s_blockCount = 0;
static uint8_t   s_blockHead = 0;

static uint8_t   s_scanBuf[SB_BARCODE_MAX];
static uint8_t   s_scanLen = 0;
static uint32_t  s_scanTick = 0;

static uint8_t   s_hostBuf[SIACP_FRAME_MAX];
static uint8_t   s_hostLen = 0;
static uint32_t  s_hostTick = 0;

static SiacpFrame_t s_hostFrame;
static uint8_t      s_hostReady = 0;

static SbLed_t   s_ledR, s_ledG, s_ledY, s_ledK;
static uint32_t  s_cntPass, s_cntNg, s_cntReject;
static uint32_t  s_pendingTick = 0;
static uint8_t   s_triggerScan = 0;

static uint8_t   s_relayOn = 0;
static uint32_t  s_relayUntil = 0;

static uint32_t  s_cntByte = 0;      /* U1 原始字节计数, 诊断扫码枪是否送数据 */

static char      s_scroll[SB_SCR_CHARS + 1];
static char      s_scrollSrc[SB_BARCODE_MAX + SB_SCR_PAD];
static uint8_t   s_scrollLen = 0;
static uint8_t   s_scrollIdx = 0;

/* ------------------------------------------------------------------ */

static void Sb_SetLed(SbLed_t *led, SbLedMode_t mode, uint32_t holdMs)
{
  led->mode = mode;
  led->until = (mode == SB_LED_BLINK) ? 0 : (HAL_GetTick() + holdMs);
  led->phase = 0;
}

static void Sb_LedApply(GPIO_TypeDef *port, uint16_t pin, SbLed_t *led, uint32_t now)
{
  if (led->mode != SB_LED_OFF && led->until != 0 && now >= led->until)
  {
    led->mode = SB_LED_OFF;
    led->until = 0;
  }

  switch (led->mode)
  {
    case SB_LED_ON:
    case SB_LED_ONCE:
      HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
      break;
    case SB_LED_BLINK:
      led->phase ^= 1;
      HAL_GPIO_WritePin(port, pin, led->phase ? GPIO_PIN_SET : GPIO_PIN_RESET);
      break;
    default:
      HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
      break;
  }
}

static void Sb_LedTask(void)
{
  static uint32_t last = 0;
  uint32_t now = HAL_GetTick();

  if ((now - last) < SB_LED_PERIOD_MS)
    return;
  last = now;

  Sb_LedApply(SB_LED_RED_PORT, SB_LED_RED_PIN, &s_ledR, now);
  Sb_LedApply(SB_LED_GRN_PORT, SB_LED_GRN_PIN, &s_ledG, now);
  Sb_LedApply(SB_LED_YEL_PORT, SB_LED_YEL_PIN, &s_ledY, now);
  Sb_LedApply(SB_LED_KEY_PORT, SB_LED_KEY_PIN, &s_ledK, now);
}

/* ------------------------------------------------------------------ */
/* 继电器: PA12 低电平吸合。直通开(01 27 01)=吸合, 直通关(01 27 00)=释放 */

static void Sb_RelayRelease(void)
{
  s_relayOn = 0;
  s_relayUntil = 0;
  HAL_GPIO_WritePin(SB_RELAY_PORT, SB_RELAY_PIN, GPIO_PIN_SET);
}

static void Sb_RelayEngage(void)
{
  s_relayOn = 1;
  s_relayUntil = (SB_RELAY_HOLD_MS == 0) ? 0 : (HAL_GetTick() + SB_RELAY_HOLD_MS);
  HAL_GPIO_WritePin(SB_RELAY_PORT, SB_RELAY_PIN, GPIO_PIN_RESET);
}

static void Sb_RelayTask(void)
{
  if ((s_relayOn != 0) && (s_relayUntil != 0) &&
      (HAL_GetTick() >= s_relayUntil))
    Sb_RelayRelease();
}

/* ------------------------------------------------------------------ */

static uint8_t Sb_FindInBlock(const uint8_t *code, uint8_t len)
{
  uint8_t i;
  for (i = 0; i < s_blockCount; i++)
    if (s_block[i].len == len && memcmp(s_block[i].data, code, len) == 0)
      return 1;
  return 0;
}

static void Sb_AddToBlock(const uint8_t *code, uint8_t len)
{
  uint8_t idx;

  if (Sb_FindInBlock(code, len))
    return;

  idx = (uint8_t)((s_blockHead + s_blockCount) % SB_BLOCK_SIZE);
  s_block[idx].used = 1;
  s_block[idx].len = len;
  memcpy(s_block[idx].data, code, len);

  if (s_blockCount < SB_BLOCK_SIZE)
    s_blockCount++;
  else
    s_blockHead = (uint8_t)(s_blockHead + 1) % SB_BLOCK_SIZE;
}

static void Sb_TriggerScanner(void)
{
  static const uint8_t cmd[] = {0x16, 0x54};
  (void)HAL_UART_Transmit(&husart1, (uint8_t *)cmd, 2, 50);
}

static uint8_t Sb_Num(char *dst, uint32_t v, uint8_t digits)
{
  uint8_t  i;
  uint32_t div = 1;

  for (i = 1; i < digits; i++)
    div *= 10;

  for (i = 0; i < digits; i++)
  {
    dst[i] = (char)('0' + (uint8_t)((v / div) % 10));
    div /= 10;
  }
  return digits;
}

/* 0.1 秒节拍, 0-99 循环。
 * 数字在动 = 主循环活着; 数字停住 = 卡在 OLED_Refresh 的软 I2C 等待或
 * HAL_UART_Transmit 超时里(那些是阻塞, 停住时中断仍在跑, 只是主循环没回来) */
static uint32_t Sb_Time100(void)
{
  return (HAL_GetTick() / 100) % 100;
}

static void Sb_ScrollReset(const uint8_t *code, uint8_t len)
{
  uint8_t i, c, n = 0, k;

  for (i = 0; i < len; i++)
  {
    c = code[i];
    s_scrollSrc[n++] = ((c >= ' ') && (c <= '~')) ? (char)c : (char)'.';
  }
  for (i = 0; i < SB_SCR_PAD; i++)
    s_scrollSrc[n++] = ' ';

  s_scrollLen = n;
  s_scrollIdx = 0;

  /* 立刻左对齐上屏, 不等字符从右边滚进来 */
  memset(s_scroll, ' ', SB_SCR_CHARS);
  for (k = 0; (k < n) && (k < SB_SCR_CHARS); k++)
    s_scroll[k] = s_scrollSrc[k];
}

static void Sb_ScrollTick(void)
{
  uint8_t i;

  if (s_scrollLen == 0)
  {
    /* 常驻空闲标记: 能看到 READY 说明显示链路正常, 问题在扫码枪 */
    memset(s_scroll, ' ', SB_SCR_CHARS);
    memcpy(s_scroll, "READY", 5);
  }
  else
  {
    for (i = 0; i < (uint8_t)(SB_SCR_CHARS - 1); i++)
      s_scroll[i] = s_scroll[i + 1];
    s_scroll[SB_SCR_CHARS - 1] = s_scrollSrc[s_scrollIdx % s_scrollLen];
    s_scrollIdx++;
  }
  s_scroll[SB_SCR_CHARS] = 0;
  /* mode=1 时空模像素写 0, 每次全量覆盖 84px 带, 无残影 */
  OLED_ShowString(0, SB_OLED_Y0, s_scroll, 8, 1);
}

static void Sb_Display(void)
{
  static uint32_t last = 0;
  static char l2[15];
  static char l3[15];
  uint32_t now = HAL_GetTick();

  if ((now - last) < SB_SCROLL_PERIOD_MS)
    return;
  last = now;

  Sb_ScrollTick();

  l2[0] = 'P';
  l2[1] = ':';
  Sb_Num(&l2[2], s_cntPass > 99 ? 99 : s_cntPass, 2);
  l2[4] = ' ';
  l2[5] = 'N';
  l2[6] = ':';
  Sb_Num(&l2[7], s_cntNg > 99 ? 99 : s_cntNg, 2);
  l2[9] = ' ';
  l2[10] = 'D';
  l2[11] = ':';
  Sb_Num(&l2[12], s_cntByte % 100, 2);
  l2[14] = 0;

  l3[0] = 'R';
  l3[1] = ':';
  Sb_Num(&l3[2], s_cntReject > 99 ? 99 : s_cntReject, 2);
  l3[4] = ' ';
  l3[5] = 'B';
  l3[6] = ':';
  Sb_Num(&l3[7], s_blockCount, 2);
  l3[9] = ' ';
  l3[10] = 'T';
  l3[11] = ':';
  Sb_Num(&l3[12], Sb_Time100(), 2);
  l3[14] = 0;

  OLED_ShowString(0, SB_OLED_Y1, l2, 8, 1);
  OLED_ShowString(0, SB_OLED_Y2, l3, 8, 1);
  OLED_Refresh();
}

/* ------------------------------------------------------------------ */

static void Sb_OnBarcode(const uint8_t *code, uint8_t len)
{
  if (len == 0)
    return;

  if (s_pending.used && (s_pending.len == len) &&
      (memcmp(s_pending.data, code, len) == 0))
  {
    s_cntReject++;
    Sb_SetLed(&s_ledK, SB_LED_BLINK, SB_REJECT_BLINK_MS);
    return;
  }

  s_pending.used = 1;
  s_pending.len = len;
  memcpy(s_pending.data, code, len);
  s_pendingTick = HAL_GetTick();
  Sb_ScrollReset(code, len);
  Sb_SetLed(&s_ledY, SB_LED_BLINK, 0);
}

static void Sb_ScanTask(void)
{
  if ((s_scanLen > 0) && ((HAL_GetTick() - s_scanTick) >= SB_SCAN_TIMEOUT_MS))
  {
    Sb_OnBarcode(s_scanBuf, s_scanLen);
    s_scanLen = 0;
  }
}

static void Sb_SendBarcode(void)
{
  static uint8_t mark[2 + SB_BARCODE_MAX];
  static uint8_t buf[SIACP_FRAME_MAX];
  uint8_t  i;
  uint16_t n;

  mark[0] = 0x02;
  mark[1] = 0x17;
  for (i = 0; i < s_pending.len; i++)
    mark[2 + i] = s_pending.data[i];

  n = Siacp_BuildFrame(buf, sizeof(buf), SIACP_HEAD_ACK, SIACP_CMD_MAIN,
                       mark, (uint8_t)(2 + s_pending.len));
  (void)Siacp_Transmit(buf, n, SIACP_TX_TIMEOUT_MS);
}

static uint8_t Sb_MarkEq(const uint8_t *m, uint8_t ml, const uint8_t *ref, uint8_t rl)
{
  return (uint8_t)((ml == rl) && (memcmp(m, ref, rl) == 0));
}

static void Sb_ApplyVerdict(uint8_t pass)
{
  if (pass != 0)
  {
    if (s_pending.used != 0)
    {
      Sb_AddToBlock(s_pending.data, s_pending.len);
      s_pending.used = 0;
    }
    s_cntPass++;
    Sb_SetLed(&s_ledY, SB_LED_OFF, 0);
    Sb_SetLed(&s_ledG, SB_LED_ON, SB_VERDICT_HOLD_MS);
    Sb_RelayEngage();       /* 直通开: PA12 拉低, 吸合 */
  }
  else
  {
    s_pending.used = 0;
    s_cntNg++;
    Sb_SetLed(&s_ledY, SB_LED_OFF, 0);
    Sb_SetLed(&s_ledR, SB_LED_ON, SB_VERDICT_HOLD_MS);
    Sb_RelayRelease();     /* 直通关: PA12 拉高, 释放 */
    s_triggerScan = 1;
  }
}

static uint8_t Sb_Dispatch(const uint8_t *m, uint8_t ml)
{
  static const uint8_t pass[]   = SIACP_MARK_PASS;
  static const uint8_t fail[]   = SIACP_MARK_FAIL;
  static const uint8_t gOn[]    = SIACP_MARK_GREEN_ON;
  static const uint8_t gOnce[]  = SIACP_MARK_GREEN_ONE;
  static const uint8_t yBl[]    = SIACP_MARK_YELLOW_BL;
  static const uint8_t yOnce[]  = SIACP_MARK_YELLOW_ONE;
  static const uint8_t rBl[]    = SIACP_MARK_RED_BL;
  static const uint8_t rOn[]    = SIACP_MARK_RED_ON;
  static const uint8_t readSn[] = SIACP_MARK_READ_SN;
  uint8_t needData = 0;

  if (Sb_MarkEq(m, ml, pass, (uint8_t)sizeof(pass)))
  {
    Sb_ApplyVerdict(1);
  }
  else if (Sb_MarkEq(m, ml, fail, (uint8_t)sizeof(fail)))
  {
    Sb_ApplyVerdict(0);
  }
  else if (Sb_MarkEq(m, ml, gOn,    (uint8_t)sizeof(gOn)))   Sb_SetLed(&s_ledG, SB_LED_ON,    0);
  else if (Sb_MarkEq(m, ml, gOnce,  (uint8_t)sizeof(gOnce)))  Sb_SetLed(&s_ledG, SB_LED_ONCE,  SB_VERDICT_HOLD_MS);
  else if (Sb_MarkEq(m, ml, yBl,    (uint8_t)sizeof(yBl)))    Sb_SetLed(&s_ledY, SB_LED_BLINK, 0);
  else if (Sb_MarkEq(m, ml, yOnce,  (uint8_t)sizeof(yOnce)))  Sb_SetLed(&s_ledY, SB_LED_ONCE,  SB_VERDICT_HOLD_MS);
  else if (Sb_MarkEq(m, ml, rBl,    (uint8_t)sizeof(rBl)))    Sb_SetLed(&s_ledR, SB_LED_BLINK, 0);
  else if (Sb_MarkEq(m, ml, rOn,    (uint8_t)sizeof(rOn)))    Sb_SetLed(&s_ledR, SB_LED_ON,    SB_VERDICT_HOLD_MS);
  else if (Sb_MarkEq(m, ml, readSn, (uint8_t)sizeof(readSn))) needData = 1;

  return needData;
}

static void Sb_OnHostFrame(const SiacpFrame_t *f)
{
  uint8_t needData;

  if (f->cmd != SIACP_CMD_MAIN)
  {
    Siacp_SendAck();
    return;
  }

  needData = Sb_Dispatch(f->mark, f->markLen);
  Siacp_SendAck();

  if (needData != 0 && s_pending.used != 0)
    Sb_SendBarcode();
}

/* ------------------------------------------------------------------ */

void StationBox_Init(void)
{
  memset(&s_pending, 0, sizeof(s_pending));
  memset(s_block, 0, sizeof(s_block));
  s_blockCount = 0;
  s_blockHead = 0;
  s_scanLen = 0;
  s_hostLen = 0;
  s_cntPass = 0;
  s_cntNg = 0;
  s_cntReject = 0;
  s_triggerScan = 0;
  s_pendingTick = 0;
  s_relayOn = 0;
  s_relayUntil = 0;
  s_cntByte = 0;
  s_scrollLen = 0;
  s_scrollIdx = 0;
  memset(s_scroll, ' ', SB_SCR_CHARS);
  memcpy(s_scroll, "READY", 5);
  s_scroll[SB_SCR_CHARS] = 0;
  s_ledR.mode = SB_LED_OFF;
  s_ledG.mode = SB_LED_OFF;
  s_ledY.mode = SB_LED_OFF;
  s_ledK.mode = SB_LED_OFF;
  Sb_RelayRelease();

  OLED_ShowString(0, SB_OLED_Y0, s_scroll, 8, 1);
  OLED_ShowString(0, SB_OLED_Y1, "P:00 N:00 D:00", 8, 1);
  OLED_ShowString(0, SB_OLED_Y2, "R:00 B:00 T:00", 8, 1);	
  OLED_Refresh();
}

void StationBox_Task(void)
{
  if (s_hostReady != 0)
  {
    s_hostReady = 0;
    Sb_OnHostFrame(&s_hostFrame);
  }

  if (s_triggerScan != 0)
  {
    s_triggerScan = 0;
    Sb_TriggerScanner();
  }

  Sb_RelayTask();
  Sb_ScanTask();

  if ((s_hostLen > 0) && (s_hostLen < SIACP_FRAME_MAX) &&
      ((HAL_GetTick() - s_hostTick) > SB_FRAME_TIMEOUT_MS))
    s_hostLen = 0;

  if ((s_pending.used != 0) &&
      ((HAL_GetTick() - s_pendingTick) > SB_PENDING_HOLD_MS))
  {
    s_pending.used = 0;
    Sb_SetLed(&s_ledY, SB_LED_OFF, 0);
  }

  Sb_LedTask();
  Sb_Display();
}

void StationBox_OnScannerByte(uint8_t b)
{
  s_cntByte++;

  if ((b == '\r') || (b == '\n'))
  {
    if (s_scanLen > 0)
    {
      Sb_OnBarcode(s_scanBuf, s_scanLen);
      s_scanLen = 0;
    }
    return;
  }

  if (s_scanLen < SB_BARCODE_MAX)
    s_scanBuf[s_scanLen++] = b;
  s_scanTick = HAL_GetTick();
}

void StationBox_OnHostByte(uint8_t b)
{
  SiacpFrame_t f;

  if (s_hostLen == 0)
  {
    if (b == SIACP_HEAD_REQ)
    {
      s_hostBuf[0] = b;
      s_hostLen = 1;
      s_hostTick = HAL_GetTick();
    }
    return;
  }

  if (s_hostLen >= SIACP_FRAME_MAX)
  {
    s_hostLen = 0;
    return;
  }

  s_hostBuf[s_hostLen++] = b;
  s_hostTick = HAL_GetTick();

  if ((s_hostLen >= 2) && (s_hostLen == s_hostBuf[1]))
  {
    if (Siacp_ParseFrame(s_hostBuf, s_hostLen, &f) != 0)
    {
      /* ISR 内只解析入队, 应答发送放到主循环(阻塞发送) */
      s_hostFrame = f;
      s_hostReady = 1;
    }
    s_hostLen = 0;
  }
}
