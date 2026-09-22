#ifndef OLED_STUB_H
#define OLED_STUB_H
#include <stdint.h>
#include <stdbool.h>
extern char stub_screen[6][15];
extern unsigned stub_draw_count,stub_refresh_count;
void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t c,uint8_t size,uint8_t mode);
void OLED_RefreshBegin(void);
void OLED_RefreshStep(uint8_t budget);
bool OLED_RefreshPending(void);
void OledStub_Reset(void);
#endif
