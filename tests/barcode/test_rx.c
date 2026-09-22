#include <assert.h>
#include <stdio.h>
#include "barcode_rx.h"
static BarcodeRx r; static Barcode out;
static unsigned feed(const uint8_t *p,unsigned n,bool busy){unsigned count=0;while(n--) if(BarcodeRx_Feed(&r,*p++,busy,&out))++count;return count;}
int main(void){
    unsigned n,i; uint8_t data[102];
    BarcodeRx_Init(&r);
    assert(feed((const uint8_t *)"A\r",2,false)==0);
    assert(feed((const uint8_t *)"\n",1,false)==1 && out.len==1 && out.data[0]=='A');
    assert(feed((const uint8_t *)"\r\n",2,false)==0);
    for(n=1;n<=100;n++){
        for(i=0;i<n;i++) { data[i]='x'; }
        data[n]=13;data[n+1]=10;
        assert(feed(data,n+2,false)==(n<=21?1u:0u));
        assert(feed((const uint8_t *)"OK\r\n",4,false)==1 && out.len==2);
    }
    assert(feed((const uint8_t *)" A b\r\n",6,false)==1 && out.len==4 && out.data[0]==' ');
    assert(feed((const uint8_t *)"A\rX\nB\r\n",7,false)==1 && out.len==5);
    assert(feed((const uint8_t *)"A\r\nB\r\n",6,false)==2);
    assert(feed((const uint8_t *)"busy",4,true)==0);
    assert(feed((const uint8_t *)"tail\r\n",6,false)==0);
    assert(feed((const uint8_t *)"start",5,false)==0);
    assert(feed((const uint8_t *)"\r\n",2,true)==0);
    assert(feed((const uint8_t *)"bad",3,false)==0); BarcodeRx_Invalidate(&r);
    assert(feed((const uint8_t *)"tail\r\n",6,false)==0);
    assert(feed((const uint8_t *)"good\r\n",6,false)==1);
    puts("PASS rx: lengths 0..100, CRLF, exact bytes, cross-state discard, recovery");return 0;
}
