#ifndef BARCODE_STORE_H
#define BARCODE_STORE_H
#include <stdint.h>
#include <stdbool.h>
#include "barcode_rx.h"
#define BARCODE_STORE_CAPACITY 150u
typedef struct {
    Barcode entries[BARCODE_STORE_CAPACITY];
    uint16_t head;
    uint16_t count;
} BarcodeStore;
void BarcodeStore_Init(BarcodeStore *s);
bool BarcodeStore_Add(BarcodeStore *s, const Barcode *b);
bool BarcodeStore_Contains(const BarcodeStore *s, const Barcode *b);
uint16_t BarcodeStore_Count(const BarcodeStore *s);
#endif
