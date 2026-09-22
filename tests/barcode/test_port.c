#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "barcode_app.h"
#include "barcode_port.h"
#include "hal_stub.h"
static const uint8_t pass[]={0xAA,8,0xFA,1,3,0x0A,0x5F,0xD8};
static const uint8_t red[]={0xAA,8,0xFA,1,6,0x1F,0xE2,0xB9};
static const uint8_t green[]={0xAA,8,0xFA,1,6,0x31,0x27,0x15};
static const uint8_t yellow[]={0xAA,8,0xFA,1,6,0x2F,0xD4,0xEA};
static void init(void){Stub_Reset();BarcodeApp_Init(0);BarcodePort_Init();}
static void scan(const char *p,uint32_t at){Stub_Bytes(1,(const uint8_t *)p,(unsigned)strlen(p),at);}
static void test_order(void){AppSnapshot s;init();scan("A\r\n",1);Stub_Drain();assert(stub_wire_len[0]==3);
 scan("B\r\n",2);Stub_Bytes(0,red,8,3);scan("C\r\n",4);stub_now=20;Stub_Drain();
 assert(stub_wire_len[0]==13 && memcmp(stub_wire[0],"A\r\n",3)==0);
 assert(memcmp(stub_wire[0]+3,BARCODE_ACK,7)==0 && memcmp(stub_wire[0]+10,"C\r\n",3)==0);
 assert(stub_wire_len[1]==3);BarcodeApp_GetSnapshot(&s);assert(s.current.data[0]=='C' && s.state==APP_WAIT_RESULT);
}
static void test_release_history(void){AppSnapshot s;init();scan("A\r\n",1);Stub_Drain();
 Stub_Bytes(0,pass,8,10);scan("B\r\n",15);stub_now=20;Stub_Drain();assert(stub_relay);
 stub_now=1019;BarcodePort_Poll();assert(stub_relay);scan("part",1019);stub_now=1020;BarcodePort_Poll();assert(!stub_relay);
 scan("tail\r\n",1021);Stub_Drain();assert(stub_wire_len[0]==10);
 scan("C\r\n",1022);Stub_Drain();assert(stub_wire_len[0]==13);
 BarcodeApp_GetSnapshot(&s);assert(s.current.data[0]=='C');
 /* A red frame received while releasing, but processed after deadline, must not rescan. */
 init();scan("A\r\n",1);Stub_Drain();Stub_Bytes(0,pass,8,10);Stub_Drain();
 Stub_Bytes(0,red,8,500);stub_now=1500;Stub_Drain();assert(!stub_relay && stub_wire_len[1]==0);
}
static void test_boot_order(void){init();scan("A\r\n",299);stub_now=301;Stub_Drain();assert(stub_wire_len[1]==0);
 init();scan("\r\n",299);stub_now=300;Stub_Drain();assert(stub_wire_len[1]==3);
 init();stub_now=300;Stub_Drain();assert(stub_wire_len[1]==3);stub_now=400;Stub_Drain();assert(stub_wire_len[1]==6);stub_now=500;Stub_Drain();assert(stub_wire_len[1]==9);
}
static void test_cancel_queued_boot(void){
 init();stub_tx_status[1]=HAL_BUSY;stub_now=300;BarcodePort_Poll();assert(!stub_tx_ptr[1]);
 scan("A\r\n",310);stub_tx_status[1]=HAL_OK;Stub_Drain();assert(stub_wire_len[1]==0);
 /* A normal failure rescan must not be removed by accepting another barcode. */
 Stub_Bytes(0,red,8,320);stub_tx_status[1]=HAL_BUSY;BarcodePort_Poll();
 scan("B\r\n",321);stub_tx_status[1]=HAL_OK;Stub_Drain();assert(stub_wire_len[1]==3);
}
static void test_tx(void){uint8_t data[]={1,2,3};unsigned i;BarcodePortStats stats;
 init();assert(BarcodePort_SendHost(data,3));data[0]=9;stub_tx_status[0]=HAL_BUSY;BarcodePort_Poll();assert(!stub_tx_ptr[0] && !BarcodePort_HasFault());
 stub_tx_status[0]=HAL_OK;Stub_Drain();assert(stub_wire_len[0]==3 && stub_wire[0][0]==1);
 init();for(i=0;i<16;i++)assert(BarcodePort_SendHost(data,3));assert(!BarcodePort_SendHost(data,3));assert(BarcodePort_HasFault());
 init();for(i=0;i<4;i++)assert(BarcodePort_SendScanner(data,3));assert(!BarcodePort_SendScanner(data,3));assert(BarcodePort_HasFault());
 init();assert(BarcodePort_SendHost(data,3));stub_tx_status[0]=HAL_ERROR;BarcodePort_Poll();BarcodePort_GetStats(&stats);assert(stats.tx_errors==1 && BarcodePort_HasFault());
}
static void test_completed_slot(void){
 uint8_t data[]={1,2,3};unsigned i;
 init();for(i=0;i<16;i++)assert(BarcodePort_SendHost(data,3));
 BarcodePort_Poll();Stub_Complete(0);
 assert(BarcodePort_SendHost(data,3)); /* completed slot is already free, no false overflow */
 assert(!BarcodePort_HasFault());Stub_Drain();assert(stub_wire_len[0]==51);
}
static void test_rx_fault(void){AppSnapshot s;BarcodePortStats stats;unsigned i,err;
 init();for(i=0;i<129;i++)Stub_Byte(1,'x',1);BarcodePort_Poll();BarcodePort_GetStats(&stats);
 assert(stats.rx_overflow>0 && BarcodePort_HasFault());scan("\r\nB\r\n",2);Stub_Drain();assert(stub_wire_len[0]==0);
 for(err=1;err<=8;err*=2){
  init();scan("A",1);BarcodePort_Poll();husart2.ErrorCode=err;HAL_UART_ErrorCallback(&husart2);BarcodePort_Poll();
  assert(husart2.receiving);scan("tail\r\nB\r\n",2);Stub_Drain();BarcodeApp_GetSnapshot(&s);assert(s.comm_fault && s.count==0 && stub_wire_len[0]==0);
 }
 init();stub_rx_status[1]=HAL_BUSY;Stub_Byte(1,'A',1);BarcodePort_Poll();assert(BarcodePort_HasFault());stub_rx_status[1]=HAL_OK;BarcodePort_Poll();assert(husart2.receiving);
 /* HAL can invoke RX completion with ErrorCode set before ErrorCallback. Never accept that byte. */
 init();scan("A\r",1);BarcodePort_Poll();husart2.ErrorCode=HAL_UART_ERROR_FE;Stub_Byte(1,'\n',2);HAL_UART_ErrorCallback(&husart2);BarcodePort_Poll();assert(stub_wire_len[0]==0 && BarcodePort_HasFault());
 init();scan("A\r\n",1);Stub_Drain();Stub_Bytes(0,pass,8,10);Stub_Drain();husart1.ErrorCode=HAL_UART_ERROR_ORE;HAL_UART_ErrorCallback(&husart1);stub_now=1010;BarcodePort_Poll();assert(!stub_relay);
}
static void test_commands(void){init();Stub_Bytes(0,green,8,1);Stub_Drain();
 assert(stub_green && !stub_red && !stub_yellow && !stub_relay && stub_wire_len[0]==7);
 Stub_Bytes(0,red,8,2);Stub_Drain();
 assert(stub_red && !stub_green && !stub_yellow && stub_wire_len[1]==0);
 /* NG red blinks at the same 500/500 cadence as yellow. */
 stub_now=502;BarcodePort_Poll();assert(!stub_red);
 stub_now=1002;BarcodePort_Poll();assert(stub_red);
 Stub_Bytes(0,yellow,8,1010);Stub_Drain();assert(stub_yellow && !stub_red && !stub_green);
 stub_now=1512;BarcodePort_Poll();assert(!stub_yellow);
 stub_now=2012;BarcodePort_Poll();assert(stub_yellow);
}
static void test_fault_masks_pass(void){AppSnapshot s;
 /* One scanner UART error latches comm fault: ACKs and lights keep working,
    but PASS no longer engages the relay and barcodes stop being uploaded. */
 init();Stub_Bytes(0,green,8,1);Stub_Drain();
 assert(stub_green && !stub_red && !stub_relay && stub_wire_len[0]==7);
 husart2.ErrorCode=HAL_UART_ERROR_FE;HAL_UART_ErrorCallback(&husart2);
 Stub_Drain();BarcodeApp_GetSnapshot(&s);assert(s.comm_fault);
 Stub_Bytes(0,pass,8,5);Stub_Drain();BarcodeApp_GetSnapshot(&s);
 assert(s.state==APP_IDLE && s.count==0 && !stub_relay && stub_wire_len[0]==14);
 Stub_Bytes(0,green,8,6);Stub_Drain();assert(stub_green && !stub_red && !stub_relay && stub_wire_len[0]==21);
 scan("A\r\n",7);Stub_Drain();BarcodeApp_GetSnapshot(&s);
 assert(s.state==APP_IDLE && stub_wire_len[0]==21);
 /* A frame already queued before the error is dropped with the ring, not parsed. */
 init();husart2.ErrorCode=HAL_UART_ERROR_FE;HAL_UART_ErrorCallback(&husart2);
 Stub_Bytes(0,pass,8,5);Stub_Drain();assert(stub_wire_len[0]==0);
}
int main(void){test_order();test_release_history();test_boot_order();test_tx();test_cancel_queued_boot();test_completed_slot();test_rx_fault();test_commands();test_fault_masks_pass();puts("PASS port: chronological RX, half-frame discard, copied TX/BUSY, ring/queue overflow, UART errors, pins, light phases");return 0;}
