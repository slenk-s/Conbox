#ifndef OLED_TEST_HAL_H
#define OLED_TEST_HAL_H
#include <stdint.h>
typedef struct {uint32_t IDR;} GPIO_TypeDef;
typedef enum {GPIO_PIN_RESET,GPIO_PIN_SET} GPIO_PinState;
extern GPIO_TypeDef oled_test_port;
extern unsigned oled_test_edges;
void HAL_GPIO_WritePin(GPIO_TypeDef *,uint16_t,GPIO_PinState);
void HAL_Delay(uint32_t);
uint32_t HAL_GetTick(void);
void __disable_irq(void);
void __enable_irq(void);
#endif
