#ifndef BARCODE_STORE_H
#define BARCODE_STORE_H
#include <stdint.h>
#include <stdbool.h>
#define BARCODE_MAX_LEN 21u
#define BARCODE_STORE_CAPACITY 150u
typedef struct { uint8_t len; uint8_t data[21]; } Barcode;
typedef struct { Barcode entries[150]; uint16_t head, count; } BarcodeStore;
void BarcodeStore_Init(BarcodeStore *s);
bool BarcodeStore_Contains(const BarcodeStore *s, const Barcode *b);
void BarcodeStore_Add(BarcodeStore *s, const Barcode *b);
#endif
