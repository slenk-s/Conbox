#include "siacp.h"
#include "usart.h"

/* CRC16/CCITT-FALSE 半字节表, 与上位机 CheckCode.cpp 完全一致 */
static const uint16_t s_crcTable[16] =
{
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
  0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF
};

uint16_t Siacp_CRC16(const uint8_t *buf, uint16_t len)
{
  uint16_t crc = 0xFFFF;
  uint8_t  t;

  while (len--)
  {
    t = (uint8_t)(crc >> 12);
    crc = (uint16_t)(crc << 4);
    crc ^= s_crcTable[t ^ ((*buf) >> 4)];

    t = (uint8_t)(crc >> 12);
    crc = (uint16_t)(crc << 4);
    crc ^= s_crcTable[t ^ ((*buf) & 0x0F)];

    buf++;
  }
  return crc;
}

uint16_t Siacp_BuildFrame(uint8_t *out, uint16_t outMax, uint8_t head,
                          uint8_t cmd, const uint8_t *mark, uint8_t markLen)
{
  uint16_t i, len, crc;

  len = (uint16_t)5 + markLen;
  if ((len > outMax) || (markLen > SIACP_MARK_MAX))
    return 0;

  out[0] = head;
  out[1] = (uint8_t)len;
  out[2] = cmd;
  for (i = 0; i < markLen; i++)
    out[3 + i] = mark[i];

  crc = Siacp_CRC16(out, (uint16_t)(len - 2));
  out[len - 2] = (uint8_t)(crc >> 8);
  out[len - 1] = (uint8_t)(crc & 0x00FF);
  return len;
}

uint8_t Siacp_ParseFrame(const uint8_t *buf, uint16_t len, SiacpFrame_t *f)
{
  uint16_t crc, i;
  uint8_t  markLen;

  if ((len < 5) || (len > SIACP_FRAME_MAX))
    return 0;
  if (buf[1] != (uint8_t)len)
    return 0;

  markLen = (uint8_t)(len - 5);
  if (markLen > SIACP_MARK_MAX)
    return 0;

  crc = Siacp_CRC16(buf, (uint16_t)(len - 2));
  if (crc != ((uint16_t)buf[len - 2] << 8 | (uint16_t)buf[len - 1]))
    return 0;

  f->head = buf[0];
  f->cmd = buf[2];
  f->markLen = markLen;
  for (i = 0; i < markLen; i++)
    f->mark[i] = buf[3 + i];
  return 1;
}

uint16_t Siacp_Transmit(const uint8_t *buf, uint16_t len, uint16_t timeoutMs)
{
  if ((HAL_UART_Transmit(&husart2, (uint8_t *)buf, len, timeoutMs) == HAL_OK))
    return len;
  return 0;
}

uint16_t Siacp_SendAck(void)
{
  static const uint8_t mark[] = {0x01, 0x0A};
  uint8_t  buf[SIACP_FRAME_MAX];
  uint16_t n;

  n = Siacp_BuildFrame(buf, sizeof(buf), SIACP_HEAD_ACK, SIACP_CMD_MAIN, mark, 2);
  return n ? Siacp_Transmit(buf, n, SIACP_TX_TIMEOUT_MS) : 0;
}
