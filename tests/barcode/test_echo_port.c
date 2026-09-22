#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "barcode_app.h"
#include "barcode_port.h"
#include "barcode_view.h"
#include "hal_stub.h"
#include "oled_stub.h"
/* BARCODE_ECHO_PORT picks the channel: 0 = USART1 host, 1 = USART2 scanner. */
#if BARCODE_ECHO_PORT == 1u
#define PICK 1u
#define OTHER 0u
#define LABEL "U2 RX ECHO"
#else
#define PICK 0u
#define OTHER 1u
#define LABEL "U1 RX ECHO"
#endif
/* Both channels receive 4 bytes through the real port. Only the selected one
   may reach the panel; the count proves the other was not silently echoed. */
static void test_channel_selection(void)
{
    const uint8_t pick[] = {0x53, 0x43, 0x41, 0x4E};   /* SCAN */
    const uint8_t other[] = {0x48, 0x4F, 0x53, 0x54};  /* HOST */
    unsigned i;
    Stub_Reset();
    OledStub_Reset();
    BarcodeApp_Init(0);
    BarcodeView_Init();
    BarcodePort_Init();
    Stub_Bytes(PICK, pick, 4, 2);
    Stub_Bytes(OTHER, other, 4, 3);
    stub_now = 10;
    BarcodePort_Poll();
    for (i = 0; i < 600u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[0], LABEL, 10));
    assert(!strncmp(stub_screen[1], "BYTES:0004", 10));
    assert(!strncmp(stub_screen[2], "53 43 41 4E", 11));
    assert(stub_screen[2][11] == ' ');
    assert(!strncmp(stub_screen[3], "        ", 8));
    assert(!OLED_RefreshPending());
}
/* Bytes that arrive after the panel settled must keep the label and extend the
   window, and must not switch the port. */
static void test_port_is_stable(void)
{
    const uint8_t more[] = {0xAA, 0x08};
    unsigned i;
    Stub_Bytes(PICK, more, 2, 20);
    stub_now = 30;
    BarcodePort_Poll();
    for (i = 0; i < 600u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[0], LABEL, 10));
    assert(!strncmp(stub_screen[1], "BYTES:0006", 10));
    assert(!strncmp(stub_screen[2], "53 43 41 4E", 11));
    assert(!strncmp(stub_screen[3], "AA 08", 5));
    assert(stub_screen[3][5] == ' ');
}
int main(void)
{
    test_channel_selection();
    test_port_is_stable();
    puts("PASS echo_port: selected channel echoed, other channel ignored, label matches port");
    return 0;
}
