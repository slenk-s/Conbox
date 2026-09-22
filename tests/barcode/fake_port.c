#include "fake_port.h"
#include "barcode_port.h"
#include "barcode_app.h"
#include <assert.h>
#include <string.h>
FakeFrame fake_host[1024], fake_scanner[32];
unsigned fake_host_count, fake_scanner_count, fake_relay_changes, fake_light_changes;
bool fake_relay, fake_red, fake_green, fake_yellow, fake_send_ok;
void FakePort_Reset(void){
 memset(fake_host,0,sizeof(fake_host));memset(fake_scanner,0,sizeof(fake_scanner));
 fake_host_count=fake_scanner_count=fake_relay_changes=fake_light_changes=0;
 fake_relay=fake_red=fake_green=fake_yellow=false;fake_send_ok=true;
}
bool BarcodePort_SendHost(const uint8_t *p,uint8_t n){
 AppSnapshot s; BarcodeApp_GetSnapshot(&s);
 /* Locked BEFORE upload. 0xAB marks an ACK, so <7> ACK2 skips this. */
 if(n!=7u && p[0]!=0xABu) assert(s.state==APP_WAIT_RESULT);
 if(!fake_send_ok)return false;
 assert(fake_host_count<1024 && n<=HOST_TX_MAX);
 fake_host[fake_host_count].len=n;memcpy(fake_host[fake_host_count++].data,p,n);return true;
}
bool BarcodePort_SendScanner(const uint8_t *p,uint8_t n){
 if(!fake_send_ok)return false;
 assert(fake_scanner_count<32 && n==3);
 fake_scanner[fake_scanner_count].len=n;memcpy(fake_scanner[fake_scanner_count++].data,p,n);return true;
}
void BarcodePort_SetRelay(bool on){fake_relay=on;++fake_relay_changes;}
/* Mutual exclusion: at most one lamp, except the FF all-blink override. */
void BarcodePort_SetLights(bool r,bool g,bool y){
 unsigned n=(unsigned)r+(unsigned)g+(unsigned)y;
 fake_red=r;fake_green=g;fake_yellow=y;++fake_light_changes;
 assert(n<=1u||n==3u);}

bool BarcodePort_SendStartupRescan(void){return BarcodePort_SendScanner(BARCODE_RESCAN,3);}
void BarcodePort_CancelStartupRescans(void){}
