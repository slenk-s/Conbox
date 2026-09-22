#ifndef BARCODE_VIEW_H
#define BARCODE_VIEW_H
#include <stdint.h>
#include <stdbool.h>
#include "barcode_app.h"
void BarcodeView_Init(void);
void BarcodeView_Format(const AppSnapshot *s, char lines[6][15]);
void BarcodeView_Poll(void);
/* BARCODE_ECHO_MODE: the panel shows the raw byte stream of one UART instead
   of the business view. Off by default; never compiled into the production
   firmware. BARCODE_ECHO_PORT picks the channel: 0 = USART1 host,
   1 = USART2 scanner. */
#ifdef BARCODE_ECHO_MODE
#ifndef BARCODE_ECHO_PORT
#define BARCODE_ECHO_PORT 0u
#endif
#define BARCODE_ECHO_BYTES 16
void BarcodeEcho_OnByte(uint8_t b);
void BarcodeEcho_Format(char lines[6][15]);
#endif
#endif
