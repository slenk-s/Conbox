#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "barcode_view.h"
#include "fake_port.h"
#include "oled_stub.h"
/* Design section 5 red frame: manual fail that flips the snapshot to FAILED. */
static void send_red(uint32_t t)
{
    HostFrame f = { true, HOST_ADDR_BOX, HOST_CMD_LIGHT, HOST_PARAM_RED_FAIL, true };
    BarcodeApp_OnFrame(&f, t);
}
/* A snapshot change that lands mid-refresh must not abandon that refresh:
   the panel would otherwise keep stale rows from the previous frame. */
static void test_refresh_not_abandoned(void){unsigned i;Barcode z={1,{'Z'}};
 FakePort_Reset();OledStub_Reset();BarcodeApp_Init(0);BarcodeView_Init();
 for(i=0;i<366;i++)BarcodeView_Poll();
 assert(!strncmp(stub_screen[0],"IDLE",4) && !OLED_RefreshPending());
 BarcodeApp_OnBarcode(&z,10);
 for(i=0;i<84;i++)BarcodeView_Poll();
 assert(!strncmp(stub_screen[0],"RECOGNIZING",11) && OLED_RefreshPending());
 for(i=0;i<40;i++)BarcodeView_Poll();           /* refresh 40/282 in flight */
 send_red(10);               /* snapshot changes mid-refresh */
 for(i=0;i<700;i++)BarcodeView_Poll();
 assert(!strncmp(stub_screen[0],"FAILED",6) && !strncmp(stub_screen[3],"Z",1) && !OLED_RefreshPending());
}
int main(void){AppSnapshot s;char lines[6][15];unsigned i,n;Barcode b={21,{0}}; memset(&s,0,sizeof(s));s.view=VIEW_WAIT;s.count=150;s.current.len=21;memcpy(s.current.data,"123456789012345678901",21);
 BarcodeView_Format(&s,lines);assert(strcmp(lines[0],"RECOGNIZING   ")==0);
 assert(strncmp(lines[1],"PASS:150",8)==0 && strcmp(lines[3],"12345678901234")==0 && strcmp(lines[4],"5678901       ")==0);
 for(i=0;i<6;i++)assert(lines[i][14]==0);
 s.current.data[1]=0;BarcodeView_Format(&s,lines);assert(lines[3][1]=='.' && s.current.data[1]==0);
 s.current.len=1;BarcodeView_Format(&s,lines);assert(lines[3][1]==' ' && lines[4][0]==' ');
 s.comm_fault=true;BarcodeView_Format(&s,lines);assert(strncmp(lines[5],"COMM ERROR",10)==0);
 FakePort_Reset();OledStub_Reset();BarcodeApp_Init(0);BarcodeView_Init();
 for(i=0;i<600;i++){n=stub_draw_count;BarcodeView_Poll();assert(stub_draw_count-n<=1);}
 assert(strncmp(stub_screen[0],"IDLE",4)==0);n=stub_refresh_count;BarcodeView_Poll();assert(n==stub_refresh_count);
 memcpy(b.data,"123456789012345678901",21);BarcodeApp_OnBarcode(&b,10);
 for(i=0;i<5;i++)BarcodeView_Poll();
 send_red(10);b.len=1;b.data[0]='Z';BarcodeApp_OnBarcode(&b,20);
 for(i=0;i<600;i++)BarcodeView_Poll();
 assert(stub_screen[3][0]=='Z' && stub_screen[3][1]==' ' && stub_screen[4][0]==' ');
 assert(strncmp(stub_screen[0],"RECOGNIZING",11)==0);
 test_refresh_not_abandoned();
 puts("PASS view: six-row layout, full 21-byte barcode, nonprintable copy, incremental render, superseded view");return 0;}
