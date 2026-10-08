#include "barcode_view.h"
#ifdef BARCODE_HOST_TEST
#include "oled_stub.h"
#else
#include "oled.h"
#endif
#include <string.h>

static char display_lines[6][15];
static uint8_t draw_cursor;

#ifndef BARCODE_ECHO_MODE
static AppSnapshot previous;
static bool valid;
#endif

#ifdef BARCODE_ECHO_MODE
static uint8_t echo_bytes[BARCODE_ECHO_BYTES];
static uint8_t echo_filled;
static uint16_t echo_total;
static bool echo_dirty;
static const char hex_digits[] = "0123456789ABCDEF";

#if BARCODE_ECHO_PORT == 1
#define ECHO_LABEL "U2 RX ECHO"
#else
#define ECHO_LABEL "U1 RX ECHO"
#endif

void BarcodeEcho_OnByte(uint8_t b)
{
    uint8_t i;
    if (echo_total < 9999u) ++echo_total;
    if (echo_filled < BARCODE_ECHO_BYTES) ++echo_filled;
    for (i = 0; i < BARCODE_ECHO_BYTES - 1u; ++i) echo_bytes[i] = echo_bytes[i + 1u];
    echo_bytes[BARCODE_ECHO_BYTES - 1u] = b;
    echo_dirty = true;
}
void BarcodeEcho_Format(char lines[6][15])
{
    uint8_t i, first = (uint8_t)(BARCODE_ECHO_BYTES - echo_filled);
    uint16_t total = echo_total;
    for (i = 0; i < 6u; ++i) { memset(lines[i], ' ', 14); lines[i][14] = '\0'; }
    memcpy(lines[0], ECHO_LABEL, 10);
    memcpy(lines[1], "BYTES:", 6);
    lines[1][6] = (char)('0' + total / 1000u);
    lines[1][7] = (char)('0' + (total / 100u) % 10u);
    lines[1][8] = (char)('0' + (total / 10u) % 10u);
    lines[1][9] = (char)('0' + total % 10u);
    /* A shift-right window holds the newest byte last, so a partially filled
       window starts at first instead of index zero. */
    for (i = 0; i < echo_filled; ++i) {
        uint8_t v = echo_bytes[first + i];
        uint8_t row = (uint8_t)(2u + i / 4u), col = (uint8_t)(3u * (i % 4u));
        lines[row][col] = hex_digits[v >> 4u];
        lines[row][col + 1u] = hex_digits[v & 0x0Fu];
        if (i % 4u != 3u) lines[row][col + 2u] = ' ';
    }
}
#endif
void BarcodeView_Init(void)
{
    draw_cursor = 84;
#ifndef BARCODE_ECHO_MODE
    valid = false;
#else
    memset(echo_bytes, 0, sizeof(echo_bytes));
    echo_filled = 0;
    echo_total = 0;
    echo_dirty = true;
#endif
}
void BarcodeView_Format(const AppSnapshot *s, char lines[6][15])
{
    static const char *const labels[] = {"IDLE", "RECOGNIZING", "RELEASING", "FAILED", "DUPLICATE"};
    static const char pref[4] = {'P', 'N', 'R', 'C'};
    static const uint8_t row_of[4] = {1u, 1u, 2u, 2u};
    static const uint8_t col_of[4] = {0u, 7u, 0u, 7u};
    uint16_t counts[4];
    uint8_t i, k;
    const char *label;
    counts[0] = s->pass_count;
    counts[1] = s->ng_count;
    counts[2] = s->rejected_count;
    counts[3] = s->buffer_count;
    label = labels[(unsigned)s->view <= VIEW_DUPLICATE ? s->view : VIEW_IDLE];
    for (i = 0; i < 6u; ++i) { memset(lines[i], ' ', 14); lines[i][14] = '\0'; }
    memcpy(lines[0], label, strlen(label));
    if (s->comm_fault) memcpy(lines[0] + strlen(label) + 1u, "ERR", 4);
    for (k = 0u; k < 4u; ++k) {
        char *row = lines[row_of[k]];
        uint8_t c = col_of[k];
        row[c]     = pref[k];
        row[c + 1u] = ':';
        row[c + 2u] = (char)('0' + counts[k] / 100u);
        row[c + 3u] = (char)('0' + (counts[k] / 10u) % 10u);
        row[c + 4u] = (char)('0' + counts[k] % 10u);
    }
    memcpy(lines[4u], "BC:", 3);
    for (i = 0; i < s->current.len && i < BARCODE_MAX_LEN; ++i) {
        uint8_t c = s->current.data[i];
        if (i < 11u) lines[4u][3u + i] = (char)(c >= 32u && c <= 126u ? c : '.');
        else lines[5u][i - 11u] = (char)(c >= 32u && c <= 126u ? c : '.');
    }
}
static void draw_next(void)
{
    if (draw_cursor >= 84u) return;
    {
        uint8_t row = draw_cursor / 14u, col = draw_cursor % 14u;
        OLED_ShowChar((uint8_t)(col * 6u), (uint8_t)(row * 8u),
                      (uint8_t)display_lines[row][col], 8, 1);
        ++draw_cursor;
        if (draw_cursor == 84u) OLED_RefreshBegin();
    }
}
void BarcodeView_Poll(void)
{
    /* Finish a started refresh before redrawing: otherwise a snapshot change
       mid-refresh abandons it and the panel keeps stale half-written rows. */
    if (OLED_RefreshPending()) { OLED_RefreshStep(2); return; }
#ifdef BARCODE_ECHO_MODE
    if (echo_dirty) {
        echo_dirty = false;
        BarcodeEcho_Format(display_lines);
        draw_cursor = 0;
    }
    draw_next();
    return;
#else
    AppSnapshot s;
    BarcodeApp_GetSnapshot(&s);
    if (!valid || s.view != previous.view || s.pass_count != previous.pass_count ||
        s.ng_count != previous.ng_count || s.buffer_count != previous.buffer_count ||
        s.rejected_count != previous.rejected_count ||
        s.comm_fault != previous.comm_fault || s.current.len != previous.current.len ||
        memcmp(s.current.data, previous.current.data, BARCODE_MAX_LEN) != 0) {
        previous = s;
        valid = true;
        BarcodeView_Format(&s, display_lines);
        draw_cursor = 0;
    }
    draw_next();
#endif
}
