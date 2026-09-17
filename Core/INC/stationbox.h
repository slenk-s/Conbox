#ifndef __STATIONBOX_H
#define __STATIONBOX_H

#include "main.h"

/* 工位盒业务层: 扫码枪收帧 + 防重 + 状态机 + U2 命令分派 + LED/OLED 反馈
 *
 * U1 (PA2/PA3) 接扫码枪: 收 ASCII+CRLF 条码, 发 0x16 0x54 触发扫描
 * U2 (PA0/PA1) 接上位机: SIACP 帧协议, 见 siacp.h
 *
 * 防重规则:
 *   - 已上报未判定(在途)期间, 收到同一条码 -> 拦截
 *   - PASS 判定后条码进入封禁表(仅 RAM, 64 条 FIFO), 再次收到 -> 拦截
 *   - NG 判定后释放, 允许重新扫描
 *   - 在途期间收到不同条码 -> 以最新为准(上位机尚未拉取)
 *
 * 执行机构:
 *   - PA12 继电器, 低电平吸合: 直通开(PASS) 吸合, 直通关(NG) 释放
 *   - 吸合保持 SB_RELAY_HOLD_MS 后自动释放, 设为 0 则锁存到收到 NG
 *
 * OLED (CH1115 88x48):
 *   - 第 1 行: 条码 6x8 滚动显示, 非阻塞步进(SB_SCROLL_PERIOD_MS)
 *   - 第 2 行: P/N 判定计数, D = U1 原始字节数(诊断扫码枪)
 *   - 第 3 行: R 拦截计数, U:PP 自发自收结果, T 0.1s 计时(诊断卡死)
 */

#define SB_BARCODE_MAX   32
#define SB_BLOCK_SIZE    64

typedef enum
{
  SB_LED_OFF = 0,
  SB_LED_ON,
  SB_LED_BLINK,
  SB_LED_ONCE
} SbLedMode_t;

void  StationBox_Init(void);
void  StationBox_Task(void);
void  StationBox_OnScannerByte(uint8_t b);
void  StationBox_OnHostByte(uint8_t b);

#endif
