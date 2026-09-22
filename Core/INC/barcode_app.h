#ifndef BARCODE_APP_H
#define BARCODE_APP_H
#include "barcode_store.h"
#include "host_protocol.h"
typedef enum { APP_IDLE, APP_WAIT_RESULT, APP_RELEASING } AppState;
typedef enum { VIEW_IDLE, VIEW_WAIT, VIEW_RELEASE, VIEW_FAILED, VIEW_DUPLICATE } ViewState;
typedef struct {
    AppState state; ViewState view; Barcode current; uint16_t count;
    bool comm_fault; uint8_t boot_sent; uint32_t boot_late;
} AppSnapshot;
void BarcodeApp_Init(uint32_t t0);
void BarcodeApp_OnBarcode(const Barcode *b, uint32_t now);
void BarcodeApp_OnFrame(const HostFrame *fr, uint32_t handled_at);
/* Advance event time without issuing startup rescans from historical data. */
void BarcodeApp_AdvanceTime(uint32_t now);
void BarcodeApp_Tick(uint32_t now);
bool BarcodeApp_IsBusy(void);
void BarcodeApp_GetSnapshot(AppSnapshot *out);
void BarcodeApp_CommunicationFault(void);
#endif
