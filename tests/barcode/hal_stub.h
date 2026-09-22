#ifndef HAL_STUB_H
#define HAL_STUB_H
#include <stdint.h>
#include <stdbool.h>
typedef enum {HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT} HAL_StatusTypeDef;
typedef struct { unsigned channel; uint32_t ErrorCode; uint8_t *rx; bool receiving; } UART_HandleTypeDef;
typedef enum {GPIO_PIN_RESET=0,GPIO_PIN_SET=1} GPIO_PinState;
extern UART_HandleTypeDef husart1,husart2;
extern uint32_t stub_now,stub_primask;
extern HAL_StatusTypeDef stub_tx_status[2],stub_rx_status[2];
extern uint8_t *stub_tx_ptr[2];
extern uint16_t stub_tx_len[2];
extern unsigned stub_tx_calls[2];
extern bool stub_relay,stub_red,stub_green,stub_yellow;
extern uint8_t stub_wire[2][4096];extern unsigned stub_wire_len[2];
#define LED_R_Port 1
#define LED_G_Port 1
#define LED_B_Port 1
#define KEY_Con_Port 1
#define LED_R_Pin 16
#define LED_G_Pin 32
#define LED_B_Pin 64
#define KEY_Con_Pin 4096
#define HAL_UART_ERROR_NONE 0u
#define HAL_UART_ERROR_PE 1u
#define HAL_UART_ERROR_NE 2u
#define HAL_UART_ERROR_FE 4u
#define HAL_UART_ERROR_ORE 8u
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
void __DMB(void);
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *,uint8_t *,uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *,uint8_t *,uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
void HAL_GPIO_WritePin(unsigned,uint16_t,GPIO_PinState);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *);
void Stub_Reset(void);
void Stub_Byte(unsigned ch,uint8_t b,uint32_t at);
void Stub_Bytes(unsigned ch,const uint8_t *p,unsigned n,uint32_t at);
void Stub_Complete(unsigned ch);
void Stub_Drain(void);
#endif
