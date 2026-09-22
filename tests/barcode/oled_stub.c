#include "oled_stub.h"
#include <assert.h>
#include <string.h>
char stub_screen[6][15];unsigned stub_draw_count,stub_refresh_count;static unsigned remaining;
void OledStub_Reset(void){memset(stub_screen,'?',sizeof(stub_screen));stub_draw_count=stub_refresh_count=remaining=0;}
/* A redraw while a refresh is in flight abandons the refresh and leaves the
   panel holding stale rows from the previous frame. */
void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t c,uint8_t size,uint8_t mode){
 assert(!OLED_RefreshPending() && size==8 && mode==1 && x<84 && y<48);
 stub_screen[y/8][x/6]=(char)c;++stub_draw_count;
}
void OLED_RefreshBegin(void){remaining=282;}
void OLED_RefreshStep(uint8_t budget){assert(budget>0 && budget<=4);assert(remaining>0);--remaining;++stub_refresh_count;}
bool OLED_RefreshPending(void){return remaining!=0;}
