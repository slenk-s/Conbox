#ifndef BARCODE_APP_H
#define BARCODE_APP_H
#include "host_protocol.h"
#include "barcode_port.h"
typedef enum { APP_IDLE, APP_WAIT_RESULT, APP_RELEASING } AppState;
typedef enum { VIEW_IDLE, VIEW_WAIT, VIEW_RELEASE, VIEW_FAILED } ViewState;
/* count is the number of PASSes, shown on the panel; it is not a history log. */
typedef struct {
    AppState state; ViewState view; Barcode current; uint16_t count;
    bool comm_fault;
} AppSnapshot;
void BarcodeApp_Init(uint32_t t0);
void BarcodeApp_OnBarcode(const Barcode *b, uint32_t now);
void BarcodeApp_OnFrame(const HostFrame *fr, uint32_t handled_at);
/* Advance event time while draining an RX backlog, so the relay release
   deadline does not underflow against events that were received earlier. */
void BarcodeApp_AdvanceTime(uint32_t now);
void BarcodeApp_Tick(uint32_t now);
bool BarcodeApp_IsBusy(void);
void BarcodeApp_GetSnapshot(AppSnapshot *out);
void BarcodeApp_CommunicationFault(void);
#endif
