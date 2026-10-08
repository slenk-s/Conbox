#include "barcode_store.h"
#include <string.h>
void BarcodeStore_Init(BarcodeStore *s)
{
    s->head = 0u;
    s->count = 0u;
}
bool BarcodeStore_Add(BarcodeStore *s, const Barcode *b)
{
    uint16_t slot;
    if (b->len == 0u || b->len > BARCODE_MAX_LEN) return false;
    if (s->count < BARCODE_STORE_CAPACITY) {
        slot = (uint16_t)((s->head + s->count) % BARCODE_STORE_CAPACITY);
        s->count = (uint16_t)(s->count + 1u);
    } else {
        slot = s->head;
        s->head = (uint16_t)((s->head + 1u) % BARCODE_STORE_CAPACITY);
    }
    s->entries[slot] = *b;
    return true;
}
bool BarcodeStore_Contains(const BarcodeStore *s, const Barcode *b)
{
    if (b->len == 0u || b->len > BARCODE_MAX_LEN) return false;
    for (uint16_t i = 0u; i < s->count; ++i) {
        const Barcode *e = &s->entries[i % BARCODE_STORE_CAPACITY];
        if (e->len == b->len && memcmp(e->data, b->data, b->len) == 0) return true;
    }
    return false;
}
uint16_t BarcodeStore_Count(const BarcodeStore *s) { return s->count; }
