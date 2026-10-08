#ifndef BARCODE_APP_H
#define BARCODE_APP_H
#include "host_protocol.h"
#include "barcode_port.h"
#include "barcode_store.h"
enum { APP_IDLE = 0, APP_WAIT_RESULT = 1, APP_RELEASING = 2, APP_DUPLICATE = 3, APP_FAILED = 4 };
enum { VIEW_IDLE, VIEW_WAIT, VIEW_RELEASE, VIEW_FAILED, VIEW_DUPLICATE };
#define RELAY_MS 200u
#define GREEN_MS 2000u
#define RESCAN_MS 3000u
#define WAIT_TIMEOUT_MS 30000u
/* pass_count=PASS 次数; ng_count=红失败+等待超时; buffer_count=FIFO 长度;
   rejected_count=WAIT_RESULT 中被拒的扫描。 */
typedef struct {
    int state, view;
    Barcode current;
    uint16_t pass_count, ng_count, buffer_count, rejected_count;
    bool comm_fault;
} AppSnapshot;
void BarcodeApp_Init(uint32_t t0);
void BarcodeApp_OnBarcode(const Barcode *b, uint32_t now);
void BarcodeApp_OnFrame(const HostFrame *fr, uint32_t handled_at);
void BarcodeApp_AdvanceTime(uint32_t now);
void BarcodeApp_Tick(uint32_t now);
bool BarcodeApp_IsBusy(void);
void BarcodeApp_GetSnapshot(AppSnapshot *out);
void BarcodeApp_CommunicationFault(void);
#endif
