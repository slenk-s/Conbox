#include "barcode_app.h"
#include <string.h>
/* LIGHT_NONE is only a sentinel so force_light() cannot hit the same-mode exit. */
enum { LIGHT_OFF, LIGHT_YELLOW, LIGHT_GREEN, LIGHT_RED, LIGHT_ALL_BLINK, LIGHT_NONE };
static AppSnapshot app;
static uint32_t release_started, blink_started;
static uint8_t light_mode;
static bool blink_enabled, blink_on;
static bool elapsed_at_least(uint32_t now, uint32_t start, uint32_t delay)
{
    uint32_t elapsed = now - start;
    return elapsed < 0x80000000u && elapsed >= delay;
}
static void light_gpio(void)
{
    bool all = light_mode == LIGHT_ALL_BLINK && blink_on;
    BarcodePort_SetLights(blink_enabled && (light_mode == LIGHT_RED || all) && blink_on,
                          (!blink_enabled && light_mode == LIGHT_GREEN) || all,
                          blink_enabled && (light_mode == LIGHT_YELLOW || all) && blink_on);
}
static void set_light(uint8_t mode, uint32_t at)
{
    if (light_mode == mode) return;
    light_mode = mode;
    blink_enabled = mode == LIGHT_YELLOW || mode == LIGHT_RED || mode == LIGHT_ALL_BLINK;
    blink_on = blink_enabled;
    if (blink_enabled) blink_started = at;
    light_gpio();
}
/* A business event reasserts its own light even if the mode is unchanged, so a
   manual override never survives the event that is supposed to end it. */
static void force_light(uint8_t mode, uint32_t at)
{
    light_mode = LIGHT_NONE;
    set_light(mode, at);
}
static void clear_light(uint32_t at) { force_light(LIGHT_OFF, at); }
void BarcodeApp_CommunicationFault(void) { app.comm_fault = true; }
void BarcodeApp_Init(uint32_t t0)
{
    (void)t0;
    memset(&app, 0, sizeof(app));
    release_started = blink_started = 0;
    light_mode = LIGHT_OFF;
    blink_enabled = blink_on = false;
    BarcodePort_SetRelay(false);
}
bool BarcodeApp_IsBusy(void) { return app.state != APP_IDLE || app.comm_fault; }
void BarcodeApp_GetSnapshot(AppSnapshot *out) { *out = app; }
void BarcodeApp_OnBarcode(const Barcode *b, uint32_t now)
{
    uint8_t wire[23];
    if (b->len == 0u || b->len > BARCODE_MAX_LEN) return;
    if (app.state == APP_RELEASING || app.comm_fault) return;
    app.current = *b;
    app.state = APP_WAIT_RESULT;
    app.view = VIEW_WAIT;
    memcpy(wire, b->data, b->len);
    wire[b->len] = 0x0D;
    wire[b->len + 1u] = 0x0A;
    if (!BarcodePort_SendHost(wire, (uint8_t)(b->len + 2u))) BarcodeApp_CommunicationFault();
    force_light(LIGHT_YELLOW, now);
}
static bool send_ack(uint8_t addr, uint8_t status)
{
    uint8_t f[HOST_TX_MAX], n = HostAck_BuildPayload(f, addr, status, 0, 0);
    return n != 0u && BarcodePort_SendHost(f, n);
}
/* <1>: ACK1 then the version ACK2. Pure query, so it must not touch the
   business state, the relay, the lamps or the upload stream, and it must not
   end a light override. */
static void version_reply(void)
{
    uint8_t f[HOST_TX_MAX], v[1], n;
    if (!send_ack(HOST_ADDR_BOX, HOST_ACK_OK)) { BarcodeApp_CommunicationFault(); return; }
    v[0] = (uint8_t)HOST_FW_VERSION;
    n = HostAck_BuildPayload(f, HOST_ADDR_BOX, HOST_CMD_VERSION, v, 1u);
    if (n == 0u || !BarcodePort_SendHost(f, n)) BarcodeApp_CommunicationFault();
}
/* <26>: ID query is also a pure query: ACK1 then a fixed ID payload. */
static void mobile_id_reply(void)
{
    static const uint8_t id[] = {0x05,0xD4,0xFF,0x35,0x31,0x32,0x53,0x43,0x43,0x22,0x51,0x22,0x91};
    uint8_t f[HOST_TX_MAX], n;
    if (!send_ack(HOST_ADDR_SN, HOST_ACK_OK)) { BarcodeApp_CommunicationFault(); return; }
    n = HostAck_BuildPayload(f, HOST_ADDR_SN, HOST_CMD_ID, id, (uint8_t)sizeof(id));
    if (n == 0u || !BarcodePort_SendHost(f, n)) BarcodeApp_CommunicationFault();
}
static void sn_reply(void)
{
    bool live = app.state != APP_IDLE;
    if (!send_ack(HOST_ADDR_SN, live ? HOST_ACK_OK : HOST_ACK_FAIL)) BarcodeApp_CommunicationFault();
    if (!live) return;
    {
        uint8_t f[HOST_TX_MAX], n;
        n = HostAck_BuildPayload(f, HOST_ADDR_SN, HOST_CMD_SN, app.current.data, app.current.len);
        if (n == 0u || !BarcodePort_SendHost(f, n)) BarcodeApp_CommunicationFault();
    }
}
void BarcodeApp_OnFrame(const HostFrame *fr, uint32_t handled_at)
{
    int lamp, state;
    if (!fr->ok) return;
    if (fr->addr == HOST_ADDR_SN && fr->cmd == HOST_CMD_SN) { sn_reply(); return; }
    if (fr->addr == HOST_ADDR_SN && fr->cmd == HOST_CMD_ID) { mobile_id_reply(); return; }
    /* Anything else is addressed to the box itself; other addrs and unknown
       commands are dropped without an ACK. */
    if (fr->addr != HOST_ADDR_BOX) return;
    if (fr->cmd == HOST_CMD_VERSION) { version_reply(); return; }
    if (fr->cmd != HOST_CMD_PASS && fr->cmd != HOST_CMD_LIGHT) return;
    if (!send_ack(HOST_ADDR_BOX, HOST_ACK_OK)) BarcodeApp_CommunicationFault();
    /* Light commands are manual overrides; the next barcode, PASS or release
       hands the panel back to the state machine below. */
    if (fr->cmd == HOST_CMD_LIGHT && fr->has_param &&
        LightCommand_Decode(fr->param, &lamp, &state)) {
        if (state == LAMP_OFF) clear_light(handled_at);
        else if (lamp == LAMP_YELLOW) set_light(LIGHT_YELLOW, handled_at);
        else if (lamp == LAMP_GREEN) set_light(LIGHT_GREEN, handled_at);
        else if (lamp == LAMP_RED) set_light(LIGHT_RED, handled_at);
        /* LAMP_ALL survives the LAMP_OFF test above only as the FF blink. */
        else if (lamp == LAMP_ALL) set_light(LIGHT_ALL_BLINK, handled_at);
    }
    if (app.comm_fault || app.state != APP_WAIT_RESULT) return;
    if (fr->cmd == HOST_CMD_PASS) {
        ++app.count;
        app.state = APP_RELEASING;
        app.view = VIEW_RELEASE;
        release_started = handled_at;
        BarcodePort_SetRelay(true);
        force_light(LIGHT_GREEN, handled_at);
    } else if (fr->cmd == HOST_CMD_LIGHT && fr->has_param &&
               fr->param == HOST_PARAM_RED_FAIL) {
        app.state = APP_IDLE;
        app.view = VIEW_FAILED;
        force_light(LIGHT_RED, handled_at);
        if (!BarcodePort_SendRescan(BARCODE_RESCAN, (uint8_t)sizeof(BARCODE_RESCAN)))
            BarcodeApp_CommunicationFault();
    }
}
void BarcodeApp_AdvanceTime(uint32_t now)
{
    if (app.state == APP_RELEASING && elapsed_at_least(now, release_started, 1000u)) {
        BarcodePort_SetRelay(false);
        app.state = APP_IDLE;
        app.view = VIEW_IDLE;
        clear_light(now);
    }
}
void BarcodeApp_Tick(uint32_t now)
{
    BarcodeApp_AdvanceTime(now);
    if (blink_enabled && elapsed_at_least(now, blink_started, 0u)) {
        uint32_t periods = (now - blink_started) / 500u;
        /* Rebase each service so a days-long blink survives tick wrap. */
        if (periods != 0u) {
            blink_started += periods * 500u;
            if ((periods & 1u) != 0u) {
                blink_on = !blink_on;
                light_gpio();
            }
        }
    }
}
