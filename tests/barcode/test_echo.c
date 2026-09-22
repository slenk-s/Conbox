#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "barcode_view.h"
#include "oled_stub.h"
#if BARCODE_ECHO_PORT == 1u
#define LABEL "U2 RX ECHO"
#else
#define LABEL "U1 RX ECHO"
#endif
static char grid[6][15];
static void init(void){OledStub_Reset();BarcodeView_Init();}
static void settle(void){unsigned i;for(i=0;i<600u;i++)BarcodeView_Poll();}
static void fmt(void){unsigned i;BarcodeEcho_Format(grid);for(i=0;i<6;i++)assert(grid[i][14]==0);}
static void test_init_frame(void){unsigned i;
 init();settle();
 assert(!strncmp(stub_screen[0],LABEL,10));
 assert(!strncmp(stub_screen[1],"BYTES:0000",10));
 for(i=28;i<84;i++)assert(stub_screen[i/14][i%14]==' ');
 assert(stub_draw_count==84u && !OLED_RefreshPending());
}
static void test_hex_order(void){unsigned i;
 const uint8_t frame[]={0xAA,8,0xFA,1,6,0x2F,0xD4,0xEA};
 init();settle();
 for(i=0;i<4;i++)BarcodeEcho_OnByte(frame[i]);
 settle();
 assert(!strncmp(stub_screen[1],"BYTES:0004",10));
 assert(!strncmp(stub_screen[2],"AA 08 FA 01",11) && stub_screen[2][11]==' ');
 assert(stub_screen[3][0]==' ');
 fmt();
 assert(!strncmp(grid[2],"AA 08 FA 01",11) && grid[2][11]==' ');
 assert(grid[3][0]==' ' && grid[5][13]==' ');
 for(i=4;i<8;i++)BarcodeEcho_OnByte(frame[i]);
 settle();
 assert(!strncmp(stub_screen[2],"AA 08 FA 01",11));
 assert(!strncmp(stub_screen[3],"06 2F D4 EA",11) && stub_screen[3][11]==' ');
 assert(!strncmp(stub_screen[4],"        ",8));
 assert(!strncmp(stub_screen[1],"BYTES:0008",10));
 fmt();
 assert(!strncmp(grid[1],"BYTES:0008",10));
}
static void test_wrap(void){unsigned i;
 init();settle();
 for(i=0;i<20;i++)BarcodeEcho_OnByte((uint8_t)(0x10+i));
 settle();
 assert(!strncmp(stub_screen[1],"BYTES:0020",10));
 fmt();
 assert(!strncmp(grid[2],"14 15 16 17",11));
 assert(!strncmp(grid[3],"18 19 1A 1B",11));
 assert(!strncmp(grid[4],"1C 1D 1E 1F",11));
 assert(!strncmp(grid[5],"20 21 22 23",11));
}
static void test_binary_bytes(void){
 init();settle();
 BarcodeEcho_OnByte(0);BarcodeEcho_OnByte(0xFF);BarcodeEcho_OnByte(0x0D);BarcodeEcho_OnByte(0x0A);
 settle();
 assert(!strncmp(stub_screen[1],"BYTES:0004",10));
 assert(!strncmp(stub_screen[2],"00 FF 0D 0A",11) && stub_screen[2][11]==' ');
}
static void test_no_idle_redraw(void){unsigned i,n;
 init();settle();
 BarcodeEcho_OnByte(0xAA);settle();
 n=stub_draw_count;
 for(i=0;i<50;i++)BarcodeView_Poll();
 assert(stub_draw_count==n);
}
static void test_refresh_not_abandoned(void){unsigned i;
 init();settle();
 BarcodeEcho_OnByte(0x2A);
 for(i=0;i<84;i++)BarcodeView_Poll();
 assert(OLED_RefreshPending());
 for(i=0;i<40;i++)BarcodeView_Poll();
 BarcodeEcho_OnByte(0x2B);
 for(i=0;i<700;i++)BarcodeView_Poll();
 assert(!OLED_RefreshPending());
 assert(stub_screen[2][0]=='2'&&stub_screen[2][1]=='A'&&stub_screen[2][3]=='2'&&stub_screen[2][4]=='B');
}
static void test_total_cap(void){unsigned i;
 init();settle();
 for(i=0;i<10005;i++)BarcodeEcho_OnByte((uint8_t)(i&0xFFu));
 settle();
 assert(!strncmp(stub_screen[1],"BYTES:9999",10));
 fmt();
 assert(!strncmp(grid[2],"05 06 07 08",11));
 assert(!strncmp(grid[5],"11 12 13 14",11));
}
int main(void){test_init_frame();test_hex_order();test_wrap();test_binary_bytes();
 test_no_idle_redraw();test_refresh_not_abandoned();test_total_cap();
 puts("PASS echo: raw U1 hex window, chronological order, wrap, binary bytes, no idle redraw, refresh not abandoned, total cap");return 0;}
