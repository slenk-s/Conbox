#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "oled.h"
extern uint8_t OLED_GRAM[88][6];
GPIO_TypeDef oled_test_port;unsigned oled_test_edges;
static unsigned disabled;
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState value){(void)p;(void)value;if(pin==OLED_SCL_Pin)++oled_test_edges;}
void HAL_Delay(uint32_t n){(void)n;}
uint32_t HAL_GetTick(void){return 0;}
void __disable_irq(void){++disabled;}
void __enable_irq(void){}
int main(void){unsigned n,edges,guard=1000;OLED_Init();memset(OLED_GRAM,0,sizeof(OLED_GRAM));
 OLED_DrawPoint(0,0,1);OLED_DrawPoint(87,47,1);assert(OLED_GRAM[0][0]==1 && OLED_GRAM[87][5]==128);
 OLED_RefreshBegin();n=0;while(OLED_RefreshPending()){
  edges=oled_test_edges;OLED_RefreshStep(2);assert(oled_test_edges-edges<=100);assert(guard--);++n;
 }
 assert(n==282 && disabled==0);edges=oled_test_edges;OLED_RefreshStep(2);assert(edges==oled_test_edges);
 OLED_RefreshBegin();edges=oled_test_edges;OLED_RefreshStep(0);assert(edges==oled_test_edges);
 puts("PASS oled: actual driver all 48 rows, bounded refresh bus clocks, no IRQ masking");return 0;}
