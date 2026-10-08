#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "barcode_app.h"
#include "barcode_store.h"
#include "fake_port.h"
enum { T_YELLOW = 1, T_GREEN, T_RED, T_PASS };
static const uint8_t cmd_wire[5][2] = {
    {0, 0},
    {HOST_CMD_LIGHT, 0x2Fu},
    {HOST_CMD_LIGHT, 0x31u},
    {HOST_CMD_LIGHT, HOST_PARAM_RED_FAIL},
    {HOST_CMD_PASS, HOST_ACK_OK}};
static Barcode a = {3, {'A', 'b', '1'}};
static Barcode b = {3, {'B', 'c', '2'}};
static AppSnapshot s;
static void start(void) { FakePort_Reset(); BarcodeApp_Init(0); }
static void snapshot(void) { BarcodeApp_GetSnapshot(&s); }
static void send_cmd(unsigned which, uint32_t t)
{
    HostFrame f = { true, HOST_ADDR_BOX, cmd_wire[which][0], cmd_wire[which][1], true };
    BarcodeApp_OnFrame(&f, t);
}
static void assert_off(void) { assert(!fake_red && !fake_green && !fake_yellow); }

/* CRLF suffix matrix: last byte decides which single byte gets appended, or
   both bytes when neither is present. Doc explicitly warns against a
   double-tail detector, so a payload ending "AB\r\n" still gets one more \r. */
static void test_crlf_suffix(void)
{
    Barcode plain = {3, {'A', 'B', 'C'}};
    Barcode ends_cr = {3, {'A', 'B', '\r'}};
    Barcode ends_lf = {3, {'A', 'B', '\n'}};
    Barcode ends_crlf = {4, {'A', 'B', '\r', '\n'}};
    Barcode cr_only = {1, {'\r'}};
    Barcode lf_only = {1, {'\n'}};
    Barcode empty = {0, {0}};

    start();
    BarcodeApp_OnBarcode(&plain, 1);
    assert(fake_host[0].len == 5u);
    assert(memcmp(fake_host[0].data, "ABC\r\n", 5u) == 0);

    start();
    BarcodeApp_OnBarcode(&ends_cr, 1);
    assert(fake_host[0].len == 4u);
    assert(memcmp(fake_host[0].data, "AB\r\n", 4u) == 0);

    start();
    BarcodeApp_OnBarcode(&ends_lf, 1);
    assert(fake_host[0].len == 4u);
    assert(memcmp(fake_host[0].data, "AB\n\r", 4u) == 0);

    /* Double-tail NOT detected: adds one more \r. */
    start();
    BarcodeApp_OnBarcode(&ends_crlf, 1);
    assert(fake_host[0].len == 5u);
    assert(memcmp(fake_host[0].data, "AB\r\n\r", 5u) == 0);

    start();
    BarcodeApp_OnBarcode(&cr_only, 1);
    assert(fake_host[0].len == 2u);
    assert(fake_host[0].data[0] == '\r' && fake_host[0].data[1] == '\n');

    start();
    BarcodeApp_OnBarcode(&lf_only, 1);
    assert(fake_host[0].len == 2u);
    assert(fake_host[0].data[0] == '\n' && fake_host[0].data[1] == '\r');

    /* Empty barcode is dropped, not uploaded. */
    start();
    BarcodeApp_OnBarcode(&empty, 1);
    assert(fake_host_count == 0u);
    snapshot();
    assert(s.state == APP_IDLE);
}

/* Dedup is only consulted in APP_IDLE. Once a PASS has been received the code
   enters WAIT_RESULT regardless of whether it appears in the store. */
static void test_dedup_idle_only(void)
{
    unsigned host;
    start();
    BarcodeApp_OnBarcode(&a, 1);
    send_cmd(T_PASS, 2);
    snapshot();
    assert(s.state == APP_RELEASING && s.pass_count == 1u && s.buffer_count == 1u);
    BarcodeApp_Tick(3002);
    snapshot();
    assert(s.state == APP_IDLE);
    /* Same barcode now: dedup kicks in, no upload, state flips to DUPLICATE. */
    host = fake_host_count;
    BarcodeApp_OnBarcode(&a, 4000);
    assert(fake_host_count == host);
    snapshot();
    assert(s.state == APP_DUPLICATE && s.view == VIEW_DUPLICATE);
    assert_off();
    /* New barcode after dedup: WAIT_RESULT again, upload goes out. */
    host = fake_host_count;
    BarcodeApp_OnBarcode(&b, 4100);
    assert(fake_host_count == host + 1u);
    assert(fake_host[host].data[0] == 'B');
    snapshot();
    assert(s.state == APP_WAIT_RESULT);
    assert(fake_yellow);
    /* In WAIT_RESULT, a second scan is silently dropped and counted as
       rejected — the in-flight 'current' must not be overwritten so the
       host's PASS/RED_FAIL applies to the right barcode. */
    host = fake_host_count;
    BarcodeApp_OnBarcode(&a, 4200);
    assert(fake_host_count == host);
    snapshot();
    assert(s.state == APP_WAIT_RESULT && s.current.data[0] == 'B');
    assert(s.rejected_count == 1u);
}

/* Store only fills on PASS. Duplicates at IDLE never write. Rejected scans
   during RELEASING never write. RED_FAIL never writes. */
static void test_store_write_only_on_pass(void)
{
    unsigned i;
    Barcode code = {3, {'X', '0', '0'}};
    start();
    for (i = 0; i < 3; ++i) {
        code.data[2] = (uint8_t)('0' + i);
        BarcodeApp_OnBarcode(&code, 10u * (i + 1u));
        send_cmd(T_PASS, 10u * (i + 1u) + 1u);
        BarcodeApp_Tick(10u * (i + 1u) + 3001u);
    }
    snapshot();
    assert(s.pass_count == 3u && s.buffer_count == 3u);
    /* No additional entries on late ACKs. */
    send_cmd(T_PASS, 1000);
    snapshot();
    assert(s.pass_count == 3u && s.buffer_count == 3u);
}

/* Store ring wraps: after 150 PASSes, the oldest entry is dropped so the
   earliest one no longer deduplicates. */
static void test_store_wrap(void)
{
    unsigned i;
    Barcode code = {1, {'A'}};
    start();
    for (i = 0; i < BARCODE_STORE_CAPACITY; ++i) {
        code.data[0] = (uint8_t)(i & 0xFFu);
        BarcodeApp_OnBarcode(&code, 10u * i);
        send_cmd(T_PASS, 10u * i + 1u);
        /* Let the pass-release clock advance far past GREEN_MS so IDLE is
           reached again before the next distinct barcode. */
        BarcodeApp_Tick(10u * i + 4000u);
    }
    BarcodeApp_Tick(10u * BARCODE_STORE_CAPACITY + 4000u);
    snapshot();
    assert(s.pass_count == BARCODE_STORE_CAPACITY && s.buffer_count == BARCODE_STORE_CAPACITY);
    /* Push a brand-new distinct barcode (byte 0xFF was never seen). */
    code.data[0] = 0xFFu;
    BarcodeApp_OnBarcode(&code, 30000u);
    send_cmd(T_PASS, 30001u);
    BarcodeApp_Tick(34001u);
    snapshot();
    assert(s.pass_count == BARCODE_STORE_CAPACITY + 1u);
    assert(s.buffer_count == BARCODE_STORE_CAPACITY);
    /* Now the same 0xFF barcode hits the dedup path: no upload, DUPLICATE. */
    {
        unsigned host = fake_host_count;
        BarcodeApp_OnBarcode(&code, 40000u);
        assert(fake_host_count == host);
        snapshot();
        assert(s.state == APP_DUPLICATE);
    }
}

/* Red-fail command only takes effect in WAIT_RESULT; elsewhere it just ACKs. */
static void test_red_fail_gating(void)
{
    /* IDLE: red fail is inert. */
    start();
    send_cmd(T_RED, 5);
    assert(fake_scanner_count == 0u);
    snapshot();
    assert(s.state == APP_IDLE && s.view == VIEW_IDLE && s.ng_count == 0u);

    /* RELEASING: red fail is inert. */
    start();
    BarcodeApp_OnBarcode(&a, 5);
    send_cmd(T_PASS, 10);
    send_cmd(T_RED, 20);
    assert(fake_scanner_count == 0u);
    snapshot();
    assert(s.state == APP_RELEASING && s.ng_count == 0u);

    /* WAIT_RESULT: fires, and schedules exactly one rescan after RESCAN_MS. */
    start();
    BarcodeApp_OnBarcode(&a, 5);
    send_cmd(T_RED, 10);
    assert(fake_scanner_count == 0u);
    snapshot();
    assert(s.state == APP_FAILED && s.view == VIEW_FAILED && s.ng_count == 1u);
    assert(fake_red);
    BarcodeApp_Tick(3010u);
    assert(fake_scanner_count == 1u);
    assert(memcmp(fake_scanner[0].data, BARCODE_RESCAN, 3u) == 0);
    /* No second rescan without another fail command. */
    BarcodeApp_Tick(6010u);
    assert(fake_scanner_count == 1u);
}

/* Red-fail is per-NG: each WAIT_RESULT red-fail schedules one rescan. While
   state is already FAILED the second red-fail is inert. */
static void test_one_rescan_per_ng(void)
{
    unsigned host;
    start();
    BarcodeApp_OnBarcode(&a, 5);
    send_cmd(T_RED, 10);
    assert(fake_scanner_count == 0u);
    BarcodeApp_Tick(3010);
    assert(fake_scanner_count == 1u);
    /* Now in FAILED; another red-fail is inert. */
    send_cmd(T_RED, 3015);
    BarcodeApp_Tick(6015);
    assert(fake_scanner_count == 1u);
    /* A new scan enters WAIT_RESULT, so the next red-fail is live again. */
    host = fake_host_count;
    BarcodeApp_OnBarcode(&b, 6016);
    assert(fake_host_count == host + 1u);
    send_cmd(T_RED, 6017);
    BarcodeApp_Tick(9017);
    assert(fake_scanner_count == 2u);
    snapshot();
    assert(s.ng_count == 2u);
}

/* Late PASS (in FAILED/DUPLICATE/IDLE) only ACKs — no relay, no count bump. */
static void test_late_pass_inert(void)
{
    start();
    /* IDLE: late PASS. */
    send_cmd(T_PASS, 5);
    assert(!fake_relay);
    snapshot();
    assert(s.state == APP_IDLE && s.pass_count == 0u);

    /* FAILED: late PASS. */
    start();
    BarcodeApp_OnBarcode(&a, 5);
    send_cmd(T_RED, 10);
    send_cmd(T_PASS, 20);
    assert(!fake_relay);
    snapshot();
    assert(s.state == APP_FAILED && s.pass_count == 0u && s.ng_count == 1u);

    /* DUPLICATE: late PASS. */
    start();
    BarcodeApp_OnBarcode(&a, 5);
    send_cmd(T_PASS, 10);
    BarcodeApp_Tick(3010);
    BarcodeApp_OnBarcode(&a, 3100);
    snapshot();
    assert(s.state == APP_DUPLICATE);
    send_cmd(T_PASS, 3200);
    assert(!fake_relay);
    snapshot();
    assert(s.state == APP_DUPLICATE && s.pass_count == 1u);
}

/* RELEASING: scans are dropped; PASS/RED_FAIL are inert. */
static void test_releasing_masks_scans(void)
{
    unsigned host;
    start();
    BarcodeApp_OnBarcode(&a, 5);
    send_cmd(T_PASS, 10);
    snapshot();
    assert(s.state == APP_RELEASING);
    host = fake_host_count;
    BarcodeApp_OnBarcode(&b, 50);
    assert(fake_host_count == host);
    send_cmd(T_RED, 60);
    assert(fake_scanner_count == 0u);
    snapshot();
    assert(s.state == APP_RELEASING && s.ng_count == 0u && s.current.data[0] == 'A');
}

/* Full pass cycle: relay on at t, off at RELAY_MS, state clears at GREEN_MS.
   In between, another scan is silently dropped and the pass timer continues. */
static void test_pass_timing(void)
{
    unsigned host;
    start();
    BarcodeApp_OnBarcode(&a, 10);
    send_cmd(T_PASS, 20);
    snapshot();
    assert(s.state == APP_RELEASING && s.pass_count == 1u && s.buffer_count == 1u);
    assert(fake_relay && fake_green);
    /* 200 ms: relay drops, state stays RELEASING, green stays on. */
    BarcodeApp_Tick(219);
    assert(fake_relay && fake_green);
    BarcodeApp_Tick(220);
    snapshot();
    assert(!fake_relay && fake_green && s.state == APP_RELEASING);
    /* A scan during RELEASING is dropped. */
    host = fake_host_count;
    BarcodeApp_OnBarcode(&b, 221);
    assert(fake_host_count == host);
    /* 2 s from PASS: state → IDLE, lights clear. */
    BarcodeApp_Tick(2020);
    snapshot();
    assert(s.state == APP_IDLE && s.view == VIEW_IDLE);
    assert_off();
    assert(!fake_relay);
}

/* The rescan deadline is independent of the pass-release clock. A red-fail
   that is delayed until after a PASS's release still fires its rescan. */
static void test_rescan_independent_of_release(void)
{
    start();
    BarcodeApp_OnBarcode(&a, 5);
    send_cmd(T_RED, 10);
    snapshot();
    assert(s.state == APP_FAILED);
    /* Simulate the upper computer stalling: we don't Tick until well past
       both deadlines. The rescan fires exactly once. */
    BarcodeApp_Tick(4000);
    assert(fake_scanner_count == 1u);
    snapshot();
    assert(s.state == APP_FAILED); /* No auto-return. */
}

/* During APP_WAIT_RESULT a second scan is silently dropped and counted in
   `rejected_count` — no overwrite of `current`, no extra upload, no state
   change, no light flicker. `upload_started` keeps ticking so a stray scan
   can't reset the host timeout. */
static void test_wait_result_rejects_scans(void)
{
    unsigned host, lights;
    start();
    BarcodeApp_OnBarcode(&a, 100);
    snapshot();
    assert(s.state == APP_WAIT_RESULT && s.current.data[0] == 'A');
    assert(s.rejected_count == 0u);

    host = fake_host_count;
    lights = fake_light_changes;
    BarcodeApp_OnBarcode(&b, 200);
    assert(fake_host_count == host);
    assert(fake_light_changes == lights);
    snapshot();
    assert(s.state == APP_WAIT_RESULT);
    assert(s.current.data[0] == 'A');
    assert(s.rejected_count == 1u);

    /* A third scan also rejected, still on the same in-flight barcode. */
    host = fake_host_count;
    BarcodeApp_OnBarcode(&a, 300);
    assert(fake_host_count == host);
    snapshot();
    assert(s.current.data[0] == 'A');
    assert(s.rejected_count == 2u);
    /* The rejected scan must NOT have reset the timeout clock: after 30s
       from the original upload (t=100) the host-failure fires, not after
       the last rejected scan. */
    BarcodeApp_Tick(WAIT_TIMEOUT_MS + 100u);
    snapshot();
    assert(s.state == APP_FAILED && s.ng_count == 1u);
}

/* 30s after a successful upload with no host reply: state → FAILED, ng,
   red lamp, and an immediate rescan command on the scanner port. */
static void test_wait_result_timeout(void)
{
    start();
    BarcodeApp_OnBarcode(&a, 1000);
    assert(fake_scanner_count == 0u);
    /* 30s elapsed: transition fires exactly at WAIT_TIMEOUT_MS after upload. */
    BarcodeApp_Tick(1000u + WAIT_TIMEOUT_MS);
    snapshot();
    assert(s.state == APP_FAILED && s.view == VIEW_FAILED);
    assert(s.ng_count == 1u);
    assert(fake_red);
    assert(fake_scanner_count == 1u);
    assert(memcmp(fake_scanner[0].data, BARCODE_RESCAN, 3u) == 0);
    /* The rescan is not re-fired by later ticks. */
    BarcodeApp_Tick(20000u);
    assert(fake_scanner_count == 1u);
    snapshot();
    assert(s.ng_count == 1u);
}

/* The timeout clock starts at upload time, not at the scan instant. If the
   upload itself fails, the clock never starts and the timeout cannot fire. */
static void test_wait_timeout_not_armed_on_failed_send(void)
{
    start();
    fake_send_ok = false;
    BarcodeApp_OnBarcode(&a, 5);
    snapshot();
    assert(s.comm_fault);
    /* Tick far past the timeout — comm_fault still blocks the transition. */
    BarcodeApp_Tick(100000u);
    snapshot();
    assert(s.state == APP_WAIT_RESULT);
    assert(s.ng_count == 0u);
    assert(fake_scanner_count == 0u);
}

/* A PASS that arrives just before the timeout still wins — the timeout
   guard is only evaluated while state is WAIT_RESULT, so once PASS flips
   the state to RELEASING it can never fire. */
static void test_wait_timeout_lost_race_to_pass(void)
{
    start();
    BarcodeApp_OnBarcode(&a, 1);
    send_cmd(T_PASS, WAIT_TIMEOUT_MS - 1u);
    snapshot();
    assert(s.state == APP_RELEASING && s.pass_count == 1u);
    /* Late Tick past the timeout: the release timer finishes (state → IDLE),
       but no rescan and no extra NG is recorded. */
    BarcodeApp_Tick(WAIT_TIMEOUT_MS + 5000u);
    snapshot();
    assert(s.state == APP_IDLE);
    assert(s.ng_count == 0u);
    assert(fake_scanner_count == 0u);
}
/* The comm-fault latch blocks uploads and PASS but not queries/ACKs. SN
   query still returns the barcode if it is in flight (state is WAIT_RESULT
   from the failed upload), so the payload ACK goes out. */
static void test_comm_fault(void)
{
    unsigned host;
    start();
    fake_send_ok = false;
    BarcodeApp_OnBarcode(&a, 5);
    snapshot();
    assert(s.comm_fault);
    /* PASS during fault: only ACK out, no relay, no count. */
    fake_send_ok = true;
    send_cmd(T_PASS, 10);
    assert(!fake_relay);
    snapshot();
    assert(s.pass_count == 0u && s.buffer_count == 0u);
    /* SN query still returns the barcode if it is in flight. */
    host = fake_host_count;
    {
        HostFrame f = { true, HOST_ADDR_SN, HOST_CMD_SN, 0, false };
        BarcodeApp_OnFrame(&f, 20);
    }
    assert(fake_host_count == host + 2u);
    assert(fake_host[host].data[4] == HOST_ACK_OK);
    assert(fake_host[host + 1u].data[5u] == 'A');
    /* Scans still refused while fault is latched. */
    host = fake_host_count;
    BarcodeApp_OnBarcode(&b, 30);
    assert(fake_host_count == host);
}

/* Manual lamp overrides survive state transitions; the next business event
   takes the panel back. Red-fail's own lamp override is business-driven. */
static void test_manual_lamps_survive(void)
{
    start();
    BarcodeApp_OnBarcode(&a, 5);
    send_cmd(T_GREEN, 10);
    assert(fake_green && !fake_red && !fake_yellow);
    /* Version query does not touch the manual green. */
    {
        HostFrame f = { true, HOST_ADDR_BOX, HOST_CMD_VERSION, 0, false };
        BarcodeApp_OnFrame(&f, 20);
    }
    assert(fake_green && !fake_red && !fake_yellow);
    /* Yellow override replaces green (blink). */
    send_cmd(T_YELLOW, 30);
    assert(fake_yellow && !fake_green && !fake_red);
    /* Red-fail reasserts red via the business event. */
    send_cmd(T_RED, 40);
    assert(fake_red && !fake_green && !fake_yellow);
    /* Next barcode takes yellow back. */
    BarcodeApp_OnBarcode(&b, 50);
    assert(fake_yellow && !fake_green && !fake_red);
    snapshot();
    assert(s.state == APP_WAIT_RESULT);
}

/* SN query: succeeds only in WAIT_RESULT or RELEASING. FAILED/DUPLICATE/IDLE
   return the failure ACK. */
static void test_sn_query(void)
{
    unsigned host;
    start();
    host = fake_host_count;
    {
        HostFrame f = { true, HOST_ADDR_SN, HOST_CMD_SN, 0, false };
        BarcodeApp_OnFrame(&f, 5);
    }
    assert(fake_host_count == host + 1u);
    assert(fake_host[host].data[4] == HOST_ACK_FAIL);

    start();
    BarcodeApp_OnBarcode(&a, 10);
    host = fake_host_count;
    {
        HostFrame f = { true, HOST_ADDR_SN, HOST_CMD_SN, 0, false };
        BarcodeApp_OnFrame(&f, 20);
    }
    assert(fake_host_count == host + 2u);
    assert(fake_host[host].data[4] == HOST_ACK_OK);
    assert(fake_host[host + 1u].len == 10u);
    assert(fake_host[host + 1u].data[5u] == 'A');

    /* RELEASING: still live. */
    send_cmd(T_PASS, 30);
    host = fake_host_count;
    {
        HostFrame f = { true, HOST_ADDR_SN, HOST_CMD_SN, 0, false };
        BarcodeApp_OnFrame(&f, 40);
    }
    assert(fake_host_count == host + 2u);
    assert(fake_host[host].data[4] == HOST_ACK_OK);

    /* FAILED: no live barcode. */
    start();
    BarcodeApp_OnBarcode(&a, 5);
    send_cmd(T_RED, 10);
    host = fake_host_count;
    {
        HostFrame f = { true, HOST_ADDR_SN, HOST_CMD_SN, 0, false };
        BarcodeApp_OnFrame(&f, 20);
    }
    assert(fake_host_count == host + 1u);
    assert(fake_host[host].data[4] == HOST_ACK_FAIL);
}

int main(void)
{
    test_crlf_suffix();
    test_dedup_idle_only();
    test_store_write_only_on_pass();
    test_store_wrap();
    test_red_fail_gating();
    test_one_rescan_per_ng();
    test_late_pass_inert();
    test_releasing_masks_scans();
    test_pass_timing();
    test_rescan_independent_of_release();
    test_wait_result_rejects_scans();
    test_wait_result_timeout();
    test_wait_timeout_not_armed_on_failed_send();
    test_wait_timeout_lost_race_to_pass();
    test_comm_fault();
    test_manual_lamps_survive();
    test_sn_query();
    puts("PASS app: crlf matrix, idle dedup, store gate/wrap, red-fail gating, "
         "one-rescan-per-ng, late-pass inert, releasing masks scans, pass timing, "
         "rescan vs release, wait-rejects, wait-timeout, timeout-not-armed-on-fail, "
         "timeout-lost-to-pass, comm fault, manual lamps, sn query");
    return 0;
}
