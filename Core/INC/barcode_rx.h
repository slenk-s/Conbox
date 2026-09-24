#ifndef BARCODE_RX_H
#define BARCODE_RX_H
#include <stdint.h>
#include <stdbool.h>
/* One decoded scanner line. data is padded because the RX parser stages up to
   23 raw bytes before stripping the CRLF terminator. */
#define BARCODE_MAX_LEN 21u
typedef struct { uint8_t len; uint8_t data[21]; } Barcode;
typedef struct { uint8_t data[23]; uint8_t used; bool last_cr, discard; } BarcodeRx;
void BarcodeRx_Init(BarcodeRx *r);
void BarcodeRx_Invalidate(BarcodeRx *r);
bool BarcodeRx_Feed(BarcodeRx *r, uint8_t byte, bool busy, Barcode *out);
#endif
