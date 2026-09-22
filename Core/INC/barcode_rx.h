#ifndef BARCODE_RX_H
#define BARCODE_RX_H
#include "barcode_store.h"
typedef struct { uint8_t data[23]; uint8_t used; bool last_cr, discard; } BarcodeRx;
void BarcodeRx_Init(BarcodeRx *r);
void BarcodeRx_Invalidate(BarcodeRx *r);
bool BarcodeRx_Feed(BarcodeRx *r, uint8_t byte, bool busy, Barcode *out);
#endif
