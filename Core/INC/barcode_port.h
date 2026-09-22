#ifndef BARCODE_PORT_H
#define BARCODE_PORT_H
#include <stdint.h>
#include <stdbool.h>
#include "barcode_store.h"
/* Longest host frame: the <7> ACK2 reply carrying a full barcode. */
#define HOST_TX_MAX (5u + BARCODE_MAX_LEN + 2u)
bool BarcodePort_SendHost(const uint8_t *data, uint8_t len);
bool BarcodePort_SendScanner(const uint8_t *data, uint8_t len);
bool BarcodePort_SendStartupRescan(void);
void BarcodePort_CancelStartupRescans(void);
void BarcodePort_SetRelay(bool active);
void BarcodePort_SetLights(bool red, bool green, bool yellow);
void BarcodePort_Init(void);
void BarcodePort_Poll(void);
bool BarcodePort_HasFault(void);
/* ISR counters observable in debugger; fault is latched until reset. */
typedef struct { uint32_t rx_overflow, uart_errors, rearm_errors, tx_errors; } BarcodePortStats;
void BarcodePort_GetStats(BarcodePortStats *out);
#endif
