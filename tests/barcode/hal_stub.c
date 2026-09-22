#include "hal_stub.h"
#include "barcode_port.h"
#include <assert.h>
#include <string.h>
UART_HandleTypeDef husart1,husart2;
uint32_t stub_now,stub_primask;
HAL_StatusTypeDef stub_tx_status[2],stub_rx_status[2];
uint8_t *stub_tx_ptr[2];uint16_t stub_tx_len[2];unsigned stub_tx_calls[2];
bool stub_relay,stub_red,stub_green,stub_yellow;
uint8_t stub_wire[2][4096];unsigned stub_wire_len[2];
void Stub_Reset(void){
 memset(&husart1,0,sizeof(husart1));memset(&husart2,0,sizeof(husart2));husart2.channel=1;
 memset(stub_tx_ptr,0,sizeof(stub_tx_ptr));memset(stub_tx_len,0,sizeof(stub_tx_len));
 memset(stub_tx_calls,0,sizeof(stub_tx_calls));memset(stub_wire_len,0,sizeof(stub_wire_len));
 stub_tx_status[0]=stub_tx_status[1]=stub_rx_status[0]=stub_rx_status[1]=HAL_OK;
 stub_now=stub_primask=0;stub_relay=stub_red=stub_green=stub_yellow=false;
}
uint32_t __get_PRIMASK(void){return stub_primask;}
void __disable_irq(void){stub_primask=1;}
void __set_PRIMASK(uint32_t v){stub_primask=v;}
void __DMB(void){}
uint32_t HAL_GetTick(void){return stub_now;}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u,uint8_t *p,uint16_t n){
 assert(n==1);if(stub_rx_status[u->channel]!=HAL_OK)return stub_rx_status[u->channel];
 if(u->receiving)return HAL_BUSY;
 u->rx=p;u->receiving=true;u->ErrorCode=0;return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *u,uint8_t *p,uint16_t n){
 unsigned ch=u->channel;++stub_tx_calls[ch];assert(stub_primask==1);
 if(stub_tx_status[ch]!=HAL_OK)return stub_tx_status[ch];
 if(stub_tx_ptr[ch])return HAL_BUSY;
 stub_tx_ptr[ch]=p;stub_tx_len[ch]=n;return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u){u->receiving=false;u->ErrorCode=0;return HAL_OK;}
void HAL_GPIO_WritePin(unsigned port,uint16_t pins,GPIO_PinState value){
 assert(port==1);if(pins&KEY_Con_Pin)stub_relay=value==GPIO_PIN_RESET;
 if(pins&LED_R_Pin)stub_red=value==GPIO_PIN_SET;
 if(pins&LED_G_Pin)stub_green=value==GPIO_PIN_SET;
 if(pins&LED_B_Pin)stub_yellow=value==GPIO_PIN_SET;
 assert((unsigned)stub_red+(unsigned)stub_green+(unsigned)stub_yellow<=1);
}
void Stub_Byte(unsigned ch,uint8_t b,uint32_t at){
 UART_HandleTypeDef *u=ch?&husart2:&husart1;assert(u->receiving && stub_primask==0);
 stub_now=at;*u->rx=b;u->receiving=false;HAL_UART_RxCpltCallback(u);
}
void Stub_Bytes(unsigned ch,const uint8_t *p,unsigned n,uint32_t at){while(n--)Stub_Byte(ch,*p++,at);}
void Stub_Complete(unsigned ch){
 UART_HandleTypeDef *u=ch?&husart2:&husart1;
 assert(stub_tx_ptr[ch]);assert(stub_wire_len[ch]+stub_tx_len[ch]<=sizeof(stub_wire[ch]));
 memcpy(stub_wire[ch]+stub_wire_len[ch],stub_tx_ptr[ch],stub_tx_len[ch]);stub_wire_len[ch]+=stub_tx_len[ch];
 stub_tx_ptr[ch]=0;stub_tx_len[ch]=0;HAL_UART_TxCpltCallback(u);
}
void Stub_Drain(void){unsigned guard=64;do{
 BarcodePort_Poll();if(stub_tx_ptr[0])Stub_Complete(0);if(stub_tx_ptr[1])Stub_Complete(1);
 BarcodePort_Poll();assert(guard--);
 }while(stub_tx_ptr[0]||stub_tx_ptr[1]);}
