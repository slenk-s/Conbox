#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "barcode_store.h"
int main(void) {
    BarcodeStore s; Barcode a={3,{'A',' ','b'}}, b={3,{'A',' ','B'}};
    unsigned i; Barcode code={2,{0,0}};
    BarcodeStore_Init(&s);
    assert(s.count==0 && !BarcodeStore_Contains(&s,&a));
    BarcodeStore_Add(&s,&a); BarcodeStore_Add(&s,&a);
    assert(s.count==1 && BarcodeStore_Contains(&s,&a));
    assert(!BarcodeStore_Contains(&s,&b));
    b.len=2; memcpy(b.data,a.data,2); assert(!BarcodeStore_Contains(&s,&b));
    BarcodeStore_Init(&s);
    for(i=0;i<150;i++){code.data[0]=(uint8_t)i; BarcodeStore_Add(&s,&code);}
    assert(s.count==150);
    code.data[0]=0; BarcodeStore_Add(&s,&code); /* Must not refresh oldest. */
    code.data[0]=150; BarcodeStore_Add(&s,&code);
    code.data[0]=0; assert(!BarcodeStore_Contains(&s,&code));
    code.data[0]=1; assert(BarcodeStore_Contains(&s,&code));
    for(i=151;i<750;i++){code.data[0]=(uint8_t)i;code.data[1]=(uint8_t)(i>>8);BarcodeStore_Add(&s,&code);}
    assert(s.count==150 && s.head<150);
    code.len=0; BarcodeStore_Add(&s,&code); assert(s.count==150);
    BarcodeStore_Init(&s); assert(s.count==0);
    puts("PASS store: exact bytes, duplicate order, 150/151 FIFO, multi-wrap, reset"); return 0;
}
