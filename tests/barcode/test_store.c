#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "barcode_store.h"

static void test_init_empty(void)
{
    BarcodeStore s;
    BarcodeStore_Init(&s);
    assert(BarcodeStore_Count(&s) == 0u);
    assert(s.head == 0u && s.count == 0u);
    Barcode empty = {0, {0}};
    Barcode ok = {1, {'A'}};
    assert(!BarcodeStore_Contains(&s, &ok));
    assert(!BarcodeStore_Add(&s, &empty));
    assert(BarcodeStore_Count(&s) == 0u);
}

static void test_add_and_contains(void)
{
    BarcodeStore s;
    Barcode a = {2, {'A', 'B'}};
    Barcode b = {2, {'C', 'D'}};
    Barcode a2 = {3, {'A', 'B', 'C'}};
    Barcode a_diff_len = {1, {'A'}};
    BarcodeStore_Init(&s);
    assert(BarcodeStore_Add(&s, &a));
    assert(BarcodeStore_Count(&s) == 1u);
    assert(BarcodeStore_Contains(&s, &a));
    assert(!BarcodeStore_Contains(&s, &b));
    assert(!BarcodeStore_Contains(&s, &a2));
    assert(!BarcodeStore_Contains(&s, &a_diff_len));
    assert(BarcodeStore_Add(&s, &b));
    assert(BarcodeStore_Count(&s) == 2u);
    assert(BarcodeStore_Contains(&s, &b));
}

static void test_overwrite_same_barcode(void)
{
    BarcodeStore s;
    Barcode a = {1, {'A'}};
    Barcode b = {1, {'B'}};
    Barcode c = {1, {'C'}};
    BarcodeStore_Init(&s);
    BarcodeStore_Add(&s, &a);
    BarcodeStore_Add(&s, &b);
    BarcodeStore_Add(&s, &a);
    assert(BarcodeStore_Count(&s) == 3u);
    assert(BarcodeStore_Contains(&s, &a));
    assert(BarcodeStore_Contains(&s, &b));
    assert(!BarcodeStore_Contains(&s, &c));
}

static void test_wrap_drops_oldest(void)
{
    BarcodeStore s;
    Barcode code = {1, {'X'}};
    unsigned i;
    BarcodeStore_Init(&s);
    /* Fill to capacity with distinct bytes then wrap. */
    for (i = 0; i < BARCODE_STORE_CAPACITY; ++i) {
        code.data[0] = (uint8_t)(i & 0xFFu);
        assert(BarcodeStore_Add(&s, &code));
    }
    assert(BarcodeStore_Count(&s) == BARCODE_STORE_CAPACITY);
    assert(s.head == 0u);
    /* Add one more: head advances to 1. Byte 0 dropped, byte 255 kept. */
    code.data[0] = 0xFFu;
    BarcodeStore_Add(&s, &code);
    assert(BarcodeStore_Count(&s) == BARCODE_STORE_CAPACITY);
    assert(s.head == 1u);
    code.data[0] = 0u;
    assert(!BarcodeStore_Contains(&s, &code));
    code.data[0] = 0xFFu;
    assert(BarcodeStore_Contains(&s, &code));
}

static void test_reject_bad_len(void)
{
    BarcodeStore s;
    Barcode too_long = {BARCODE_MAX_LEN + 1u, {0}};
    Barcode zero = {0, {0}};
    BarcodeStore_Init(&s);
    assert(!BarcodeStore_Add(&s, &too_long));
    assert(!BarcodeStore_Add(&s, &zero));
    assert(!BarcodeStore_Contains(&s, &too_long));
    assert(!BarcodeStore_Contains(&s, &zero));
    assert(BarcodeStore_Count(&s) == 0u);
}

static void test_exact_capacity_roundtrip(void)
{
    BarcodeStore s;
    Barcode probe = {1, {0}};
    unsigned i;
    BarcodeStore_Init(&s);
    for (i = 0; i < BARCODE_STORE_CAPACITY; ++i) {
        probe.data[0] = (uint8_t)(i & 0xFFu);
        BarcodeStore_Add(&s, &probe);
    }
    /* Every distinct value added is still present. */
    for (i = 0; i < BARCODE_STORE_CAPACITY; ++i) {
        probe.data[0] = (uint8_t)(i & 0xFFu);
        assert(BarcodeStore_Contains(&s, &probe));
    }
    assert(BarcodeStore_Count(&s) == BARCODE_STORE_CAPACITY);
}

int main(void)
{
    test_init_empty();
    test_add_and_contains();
    test_overwrite_same_barcode();
    test_wrap_drops_oldest();
    test_reject_bad_len();
    test_exact_capacity_roundtrip();
    puts("PASS store: init/add/contains, wrap drops oldest, bad len rejected, "
         "exact-capacity roundtrip");
    return 0;
}
