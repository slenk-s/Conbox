#include "barcode_store.h"
#include <string.h>
void BarcodeStore_Init(BarcodeStore *s) { memset(s, 0, sizeof(*s)); }
bool BarcodeStore_Contains(const BarcodeStore *s, const Barcode *b)
{
    uint16_t i;
    if (b->len == 0u || b->len > BARCODE_MAX_LEN) return false;
    for (i = 0; i < s->count; ++i) {
        const Barcode *entry = &s->entries[(s->head + i) % BARCODE_STORE_CAPACITY];
        if (entry->len == b->len && memcmp(entry->data, b->data, b->len) == 0) return true;
    }
    return false;
}
void BarcodeStore_Add(BarcodeStore *s, const Barcode *b)
{
    if (b->len == 0u || b->len > BARCODE_MAX_LEN || BarcodeStore_Contains(s, b)) return;
    if (s->count < BARCODE_STORE_CAPACITY) {
        s->entries[(s->head + s->count) % BARCODE_STORE_CAPACITY] = *b;
        ++s->count;
    } else {
        s->entries[s->head] = *b;
        s->head = (uint16_t)((s->head + 1u) % BARCODE_STORE_CAPACITY);
    }
}
