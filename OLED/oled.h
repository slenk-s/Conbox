#ifndef __OLED_H
#define __OLED_H 

#include "py32f0xx_hal.h"
#include "stdio.h"
#include "stdlib.h"
#include "gpio.h"	// 引脚号统一取自 Mcu Studio 生成的 pin map，勿在此硬编码

#define OLED_SCL_Clr() HAL_GPIO_WritePin(OLED_SCL_Port, OLED_SCL_Pin, GPIO_PIN_RESET)
#define OLED_SCL_Set() HAL_GPIO_WritePin(OLED_SCL_Port, OLED_SCL_Pin, GPIO_PIN_SET)

#define OLED_SDA_Clr() HAL_GPIO_WritePin(OLED_SDA_Port, OLED_SDA_Pin, GPIO_PIN_RESET)
#define OLED_SDA_Set() HAL_GPIO_WritePin(OLED_SDA_Port, OLED_SDA_Pin, GPIO_PIN_SET)

#define OLED_RES_Clr() HAL_GPIO_WritePin(OLED_RES_Port, OLED_RES_Pin, GPIO_PIN_RESET)//RES
#define OLED_RES_Set() HAL_GPIO_WritePin(OLED_RES_Port, OLED_RES_Pin, GPIO_PIN_SET)


#define OLED_CMD  0	
#define OLED_DATA 1	


void OLED_ClearPoint(uint8_t x,uint8_t y);
void OLED_ColorTurn(uint8_t i);
void OLED_DisplayTurn(uint8_t angle);  //0=0°, 1=90°, 2=180°, 3=270°
void I2C_Start(void);
void I2C_Stop(void);
uint8_t I2C_WaitAck(void);
void Send_Byte(uint8_t dat);
uint8_t OLED_Probe(void);
void OLED_WR_Byte(uint8_t dat,uint8_t mode);
void OLED_DisPlay_On(void);
void OLED_DisPlay_Off(void);
void OLED_Refresh(void);
void OLED_Clear(void);
void OLED_DrawPoint(uint8_t x,uint8_t y,uint8_t t);
void OLED_DrawLine(uint8_t x1,uint8_t y1,uint8_t x2,uint8_t y2,uint8_t mode);
void OLED_DrawCircle(uint8_t x,uint8_t y,uint8_t r);
void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t chr,uint8_t size1,uint8_t mode);
void OLED_ShowChar6x8(uint8_t x,uint8_t y,uint8_t chr,uint8_t mode);
void OLED_ShowString(uint8_t x,uint8_t y,char *chr,uint8_t size1,uint8_t mode);
void OLED_ShowNum(uint8_t x,uint8_t y,uint32_t num,uint8_t len,uint8_t size1,uint8_t mode);
void OLED_ShowChinese(uint8_t x,uint8_t y,uint8_t num,uint8_t size1,uint8_t mode);
void OLED_ScrollDisplay(uint8_t num,uint8_t space,uint8_t mode);
void OLED_ScrollText(uint8_t line, char *buf, uint8_t len, uint8_t y, uint8_t mode);
void OLED_ScrollTextTick(uint8_t line);
void OLED_ShowPicture(uint8_t x,uint8_t y,uint8_t sizex,uint8_t sizey,uint8_t BMP[],uint8_t mode);
void OLED_ShowPicture(uint8_t x,uint8_t y,uint8_t sizex,uint8_t sizey,uint8_t BMP[],uint8_t mode);
void OLED_Init(void);

//extern declarations for main.c self-diagnosis
extern uint8_t x_low_offset;
extern uint8_t x_high_offset;
extern uint8_t display_angle;

#endif

