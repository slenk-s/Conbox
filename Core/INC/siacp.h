#ifndef __SIACP_H
#define __SIACP_H

#include "main.h"

/* SIACP 帧协议 (上位机 InitAndLogo / DataFile/SIACP.xml 定义)
 *
 * 帧结构:
 *   [Head] [Len] [Cmd] [Mark ...] [CRC16_H] [CRC16_L]
 *
 *   Head  = 0xAA 上位机->盒子 请求帧
 *           0xAB 盒子->上位机 应答帧
 *   Len   = 整帧字节数 (含 Head/Cmd/CRC)
 *   Cmd   = 命令码, 工位盒相关命令固定 0xFA
 *   Mark  = 命令参数, 0~(Len-5) 字节
 *   CRC16 = 覆盖前 (Len-2) 字节, 大端存放
 *
 * CRC16 = CCITT-FALSE: poly 0x1021, init 0xFFFF, 逐 nibble, 无反射无 xorout
 */

#define SIACP_HEAD_REQ      0xAA
#define SIACP_HEAD_ACK      0xAB
#define SIACP_CMD_MAIN      0xFA

#define SIACP_MARK_MAX      32
#define SIACP_FRAME_MAX     (5 + SIACP_MARK_MAX)
#define SIACP_TX_TIMEOUT_MS 100

/* Mark 值 (SIACP.xml) */
#define SIACP_MARK_PASS       {0x01, 0x27, 0x01}  /* 工位盒直通开 = PASS */
#define SIACP_MARK_FAIL       {0x01, 0x27, 0x00}  /* 工位盒直通关 = NG   */
#define SIACP_MARK_GREEN_ON   {0x01, 0x06, 0x31}  /* 绿灯常亮 */
#define SIACP_MARK_GREEN_ONE  {0x01, 0x11}        /* 绿灯 单次 */
#define SIACP_MARK_YELLOW_BL  {0x01, 0x06, 0x2F}  /* 黄灯闪烁 = WAIT 忙 */
#define SIACP_MARK_YELLOW_ONE {0x01, 0x12}        /* 黄灯 单次 */
#define SIACP_MARK_RED_BL     {0x01, 0x06, 0x1F}  /* 红灯闪烁 */
#define SIACP_MARK_RED_ON     {0x01, 0x13}        /* 红灯常亮 */
#define SIACP_MARK_READ_SN    {0x02, 0x17}        /* 读取移动盒条码 */

typedef struct
{
  uint8_t  head;
  uint8_t  cmd;
  uint8_t  mark[SIACP_MARK_MAX];
  uint8_t  markLen;
} SiacpFrame_t;

uint16_t Siacp_CRC16(const uint8_t *buf, uint16_t len);

/* 构造完整帧, 返回字节数; 0=空间不足 */
uint16_t Siacp_BuildFrame(uint8_t *out, uint16_t outMax, uint8_t head,
                          uint8_t cmd, const uint8_t *mark, uint8_t markLen);

/* 校验并解析帧; 返回 1=有效 */
uint8_t  Siacp_ParseFrame(const uint8_t *buf, uint16_t len, SiacpFrame_t *f);

uint16_t Siacp_Transmit(const uint8_t *buf, uint16_t len, uint16_t timeoutMs);

/* 发送通用 ACK: AB 07 FA 01 0A 30 52, 返回实际发送字节数 */
uint16_t Siacp_SendAck(void);

#endif
