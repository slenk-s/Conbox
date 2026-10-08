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
/* 两列布局：第 1 行 P / N，第 2 行 R / C。 */
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

    /* WAIT + 计数 + 21 字节条码：第 4 行 11 字、第 5 行 10 字。 */
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

    /* 非可打印字节显示为 '.'。 */
    s.current.data[1] = 0;
    BarcodeView_Format(&s, lines);
    assert(lines[4][4] == '.');
    assert(s.current.data[1] == 0);

    /* 短条码：第 5 行留空。 */
    s.current.len = 1;
    s.current.data[0] = 'Z';
    BarcodeView_Format(&s, lines);
    assert(lines[4][3] == 'Z');
    assert(lines[4][4] == ' ');
    assert(lines[5][0] == ' ');

    /* FAILED 视图。 */
    s.view = VIEW_FAILED;
    s.current.len = 0;
    BarcodeView_Format(&s, lines);
    assert(strcmp(lines[0], "FAILED        ") == 0);

    /* DUPLICATE 视图。 */
    s.view = VIEW_DUPLICATE;
    BarcodeView_Format(&s, lines);
    assert(strcmp(lines[0], "DUPLICATE     ") == 0);

    /* 通信故障：状态标签后加 ERR。 */
    s.view = VIEW_IDLE;
    s.comm_fault = true;
    BarcodeView_Format(&s, lines);
    assert(strncmp(lines[0], "IDLE ERR", 8u) == 0);
}
/* 变更检测：任何字段变化触发重绘，无变化时保持面板不动。 */
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

    /* PASS 时 P 和 C 同时递增。 */
    send_pass(20);
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[1], "P:001", 5));
    assert(!strncmp(stub_screen[2] + 7, "C:001", 5));
    assert(!strncmp(stub_screen[0], "RELEASING", 9));

    /* 先推进过释放周期，再走 WAIT_RESULT。 */
    {
        unsigned t;
        for (t = 0; t < 2200u; t += 100u) BarcodeApp_Tick(t);
    }
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[0], "IDLE", 4));
    /* 再推一条条码进入 WAIT_RESULT。 */
    {
        Barcode d = {3, {'D', 'e', '1'}};
        BarcodeApp_OnBarcode(&d, 3000);
    }
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    send_red(3100);
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    assert(!strncmp(stub_screen[0], "FAILED", 6));
    /* N 在第 1 行 col 7。 */
    assert(!strncmp(stub_screen[1] + 7, "N:001", 5));
}
/* 首条仍在 WAIT_RESULT 时收到第二次扫描，R 递增并触发重绘。 */
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
    BarcodeApp_OnBarcode(&b2, 20); /* 被拒：b1 仍在 WAIT_RESULT */
    for (i = 0; i < 300u; ++i) BarcodeView_Poll();
    assert(stub_draw_count > n);
    assert(!strncmp(stub_screen[2], "R:001", 5));
    /* 被拒扫描不覆盖当前条码。 */
    assert(stub_screen[4][3] == 'A');
}
/* 刷新中途快照变化不得中断刷新。 */
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
