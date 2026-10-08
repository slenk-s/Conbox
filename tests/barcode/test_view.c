#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "barcode_view.h"
#include "fake_port.h"
#include "oled_stub.h"
static void send_red(uint32_t t)
{
    HostFrame f = { true, HOST_ADDR_BOX, HOST_CMD_LIGHT, HOST_PARAM_RED_FAIL, true };
    BarcodeApp_OnFrame(&f, t);
}
static void send_pass(uint32_t t)
{
    HostFrame f = { true, HOST_ADDR_BOX, HOST_CMD_PASS, 0, false };
    BarcodeApp_OnFrame(&f, t);
}
/* Two-column layout:
     row 1: P:xxx   N:xxx
     row 2: R:xxx   C:xxx
   P/N on row 1 at cols 0 and 7; R/C on row 2 at cols 0 and 7. */
static void test_layout(void)
{
    AppSnapshot s;
    char lines[6][15];
    unsigned i;
    memset(&s, 0, sizeof(s));
    s.view = VIEW_IDLE;
    s.pass_count = 0; s.ng_count = 0; s.buffer_count = 0; s.rejected_count = 0;
    s.current.len = 0;
    BarcodeView_Format(&s, lines);
    assert(strcmp(lines[0], "IDLE          ") == 0);
    assert(!strcmp(lines[1], "P:000  N:000  "));
    assert(!strcmp(lines[2], "R:000  C:000  "));
    assert(!strcmp(lines[3], "              "));
    assert(!strcmp(lines[4], "BC:           "));
    assert(!strcmp(lines[5], "              "));
    for (i = 0; i < 6u; ++i) assert(lines[i][14] == '\0');

    /* WAIT + counts + a 21-byte barcode split 11 on row 4, 10 on row 5. */
    memset(&s, 0, sizeof(s));
    s.view = VIEW_WAIT;
    s.pass_count = 150; s.ng_count = 3; s.buffer_count = 42; s.rejected_count = 7;
    s.current.len = 21;
    memcpy(s.current.data, "123456789012345678901", 21u);
    BarcodeView_Format(&s, lines);
    assert(strcmp(lines[0], "RECOGNIZING   ") == 0);
    assert(!strcmp(lines[1], "P:150  N:003  "));
    assert(!strcmp(lines[2], "R:007  C:042  "));
    assert(!strcmp(lines[4], "BC:12345678901"));
    assert(!strcmp(lines[5], "2345678901    "));

    /* Non-printable byte maps to '.'. */
    s.current.data[1] = 0;
    BarcodeView_Format(&s, lines);
    assert(lines[4][4] == '.');
    assert(s.current.data[1] == 0);

    /* SHORT barcode: row 5 stays blank. */
    s.current.len = 1;
    s.current.data[0] = 'Z';
    BarcodeView_Format(&s, lines);
    assert(lines[4][3] == 'Z');
    assert(lines[4][4] == ' ');
    assert(lines[5][0] == ' ');

    /* FAILED view. */
    s.view = VIEW_FAILED;
    s.current.len = 0;
    BarcodeView_Format(&s, lines);
    assert(strcmp(lines[0], "FAILED        ") == 0);

    /* DUPLICATE view. */
    s.view = VIEW_DUPLICATE;
    BarcodeView_Format(&s, lines);
    assert(strcmp(lines[0], "DUPLICATE     ") == 0);

    /* Comm fault adds "ERR" tag after the state label. */
    s.view = VIEW_IDLE;
    s.comm_fault = true;
    BarcodeView_Format(&s, lines);
    assert(strncmp(lines[0], "IDLE ERR", 8u) == 0);
}
/* Change detection: a change to any of the four count/view/comm-fault/current
   fields triggers a redraw; no change leaves the panel untouched. */
static void test_change_detection(void)
{
    unsigned i, n;
    Barcode b = {3, {'A', 'B', 'C'}};
    FakePort_Reset();
    OledStub_Reset();
    BarcodeApp_Init(0);
    BarcodeView_Init();
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[0], "IDLE", 4));
    assert(!strncmp(stub_screen[1], "P:000", 5));
    n = stub_draw_count;
    BarcodeApp_OnBarcode(&b, 10);
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    assert(stub_draw_count > n);
    assert(!strncmp(stub_screen[0], "RECOGNIZING", 11));
    assert(!strncmp(stub_screen[1], "P:000", 5));
    assert(stub_screen[2][0] == 'R');
    assert(stub_screen[4][3] == 'A');
    assert(stub_screen[4][4] == 'B');
    assert(stub_screen[4][5] == 'C');

    /* A PASS increments P and C together. */
    send_pass(20);
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[1], "P:001", 5));
    assert(!strncmp(stub_screen[2] + 7, "C:001", 5));
    assert(!strncmp(stub_screen[0], "RELEASING", 9));

    /* Red-fail in WAIT_RESULT increments N (this test is at RELEASING now,
       so it is inert — advance past release first). */
    {
        unsigned t;
        for (t = 0; t < 2200u; t += 100u) BarcodeApp_Tick(t);
    }
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[0], "IDLE", 4));
    /* Push a fresh barcode into WAIT_RESULT before the RED_FAIL test. */
    {
        Barcode d = {3, {'D', 'e', '1'}};
        BarcodeApp_OnBarcode(&d, 3000);
    }
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    send_red(3100);
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[0], "FAILED", 6));
    /* N is at col 7 on row 1 (was row 2 before the layout change). */
    assert(!strncmp(stub_screen[1] + 7, "N:001", 5));
}
/* R count is bumped when a second scan arrives while the first is still in
   WAIT_RESULT, and the change triggers a redraw. */
static void test_rejected_count_redraw(void)
{
    unsigned i;
    Barcode b1 = {3, {'A', 'B', 'C'}};
    Barcode b2 = {3, {'X', 'Y', 'Z'}};
    FakePort_Reset();
    OledStub_Reset();
    BarcodeApp_Init(0);
    BarcodeView_Init();
    BarcodeApp_OnBarcode(&b1, 10);
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[2], "R:000", 5));
    unsigned n = stub_draw_count;
    BarcodeApp_OnBarcode(&b2, 20); /* rejected: still WAIT_RESULT on b1 */
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    assert(stub_draw_count > n);
    assert(!strncmp(stub_screen[2], "R:001", 5));
    /* The in-flight barcode stays 'A' — the rejected scan didn't replace it. */
    assert(stub_screen[4][3] == 'A');
}
/* Mid-refresh snapshot change must not abandon the refresh. */
static void test_refresh_not_abandoned(void)
{
    unsigned i;
    Barcode z = {1, {'Z'}};
    FakePort_Reset();
    OledStub_Reset();
    BarcodeApp_Init(0);
    BarcodeView_Init();
    for (i = 0; i < 366u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[0], "IDLE", 4) && !OLED_RefreshPending());
    BarcodeApp_OnBarcode(&z, 10);
    for (i = 0; i < 84u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[0], "RECOGNIZING", 11) && OLED_RefreshPending());
    for (i = 0; i < 40u; ++i) BarcodeView_Poll();
    send_red(10);
    for (i = 0; i < 700u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[0], "FAILED", 6));
    assert(stub_screen[4][3] == 'Z');
    assert(!OLED_RefreshPending());
}
int main(void)
{
    test_layout();
    test_change_detection();
    test_rejected_count_redraw();
    test_refresh_not_abandoned();
    puts("PASS view: P/N on row1 + R/C on row2 layout, state labels, "
         "nonprintable, incremental render, superseded view");
    return 0;
}
