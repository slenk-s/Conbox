#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host_protocol.h"
/* Ground truth: vendor command sheet, wire order
   AA len FA addr cmd [param] CRCH CRCL. */
static const uint8_t frames[][8] = {
    {0xAA, 8, 0xFA, 1, 0x06, 0x00, 0x01, 0x67},  /* <11> all lamps off    */
    {0xAA, 8, 0xFA, 1, 0x06, 0xFF, 0x1F, 0x97},  /* <11> all lamps blink  */
    {0xAA, 8, 0xFA, 1, 0x06, 0x10, 0x13, 0x56},  /* <12> red off          */
    {0xAA, 8, 0xFA, 1, 0x06, 0x11, 0x03, 0x77},  /* <12> red on           */
    {0xAA, 8, 0xFA, 1, 0x06, 0x1F, 0xE2, 0xB9},  /* <12> red blink        */
    {0xAA, 8, 0xFA, 1, 0x06, 0x20, 0x25, 0x05},  /* <13> yellow off       */
    {0xAA, 8, 0xFA, 1, 0x06, 0x21, 0x35, 0x24},  /* <13> yellow on        */
    {0xAA, 8, 0xFA, 1, 0x06, 0x2F, 0xD4, 0xEA},  /* <13> yellow blink     */
    {0xAA, 8, 0xFA, 1, 0x06, 0x30, 0x37, 0x34},  /* <14> green off        */
    {0xAA, 8, 0xFA, 1, 0x06, 0x31, 0x27, 0x15},  /* <14> green on         */
    {0xAA, 8, 0xFA, 1, 0x06, 0x3F, 0xC6, 0xDB},  /* <14> green blink      */
    {0xAA, 8, 0xFA, 1, 0x03, 0x0A, 0x5F, 0xD8},  /* PASS                  */
    {0xAA, 7, 0xFA, 1, 0xFF, 0x25, 0xB9},        /* <1> version query     */
    {0xAA, 7, 0xFA, 2, 0x17, 0x0C, 0xCC},      /* <7> SN query           */
    {0xAA, 7, 0xFA, 2, 0x26, 0x2A, 0xBE}};      /* mobile ID query        */
#define NFRAMES ((unsigned)(sizeof(frames) / sizeof(frames[0])))
static const uint8_t ack_ok[7] = {0xAB, 7, 0xFA, 1, 0x0A, 0x30, 0x52};
static void feed_range(HostParser *p, unsigned f, unsigned from, unsigned to)
{
    unsigned i, len = frames[f][1];
    for (i = from; i < to; ++i) {
        HostFrame fr = HostParser_Feed(p, frames[f][i]);
        if (i + 1u != len) { assert(!fr.ok); continue; }
        assert(fr.ok);
        assert(fr.addr == frames[f][3]);
        assert(fr.cmd == frames[f][4]);
        if (len == 8u) assert(fr.has_param && fr.param == frames[f][5]);
        else assert(!fr.has_param);
    }
}
int main(void)
{
    unsigned f, i;
    HostParser p;
    uint8_t bad[8], out[HOST_ACK_PAYLOAD_MAX + 7u];
    /* Independent oracles: the CCITT-FALSE check value, then every documented
       frame. The builder must not be used here, or this proves nothing. */
    assert(HostCrc16((const uint8_t *)"123456789", 9) == 0x29B1u);
    for (f = 0; f < NFRAMES; ++f) {
        unsigned len = frames[f][1];
        assert(HostCrc16(frames[f], (uint8_t)(len - 2u)) ==
               (uint16_t)((uint16_t)frames[f][len - 2u] << 8 | frames[f][len - 1u]));
    }
    assert(memcmp(ack_ok, BARCODE_ACK, 7) == 0);
    /* Builder: ACK1 for both addrs, both status codes, and a <7> ACK2. */
    assert(HostAck_BuildPayload(out, HOST_ADDR_BOX, HOST_ACK_OK, 0, 0) == 7u);
    assert(memcmp(out, ack_ok, 7) == 0);
    assert(HostAck_BuildPayload(out, HOST_ADDR_SN, HOST_ACK_OK, 0, 0) == 7u);
    assert(memcmp(out, "\xAB\x07\xFA\x02\x0A\x65\x01", 7) == 0);
    assert(HostAck_BuildPayload(out, HOST_ADDR_SN, HOST_ACK_FAIL, 0, 0) == 7u);
    assert(memcmp(out, "\xAB\x07\xFA\x02\x0E\x25\x85", 7) == 0);
    {
        uint8_t sn_payload[2] = {0x41u, 0x42u};
        assert(HostAck_BuildPayload(out, HOST_ADDR_SN, HOST_CMD_SN, sn_payload, 2) == 9u);
        assert(memcmp(out, "\xAB\x09\xFA\x02\x17\x41\x42\x33\x14", 9) == 0);
    }
    {
        uint8_t ver[1] = {HOST_FW_VERSION};
        assert(HostAck_BuildPayload(out, HOST_ADDR_BOX, HOST_CMD_VERSION, ver, 1) == 8u);
        assert(memcmp(out, "\xAB\x08\xFA\x01\xFF\x65\xD1\x9D", 8) == 0);
    }
    {
        uint8_t id_payload[13] = {0x05, 0xD4, 0xFF, 0x35, 0x31, 0x32,
                                  0x53, 0x43, 0x43, 0x22, 0x51, 0x22, 0x91};
        assert(HostAck_BuildPayload(out, HOST_ADDR_SN, HOST_CMD_ID, id_payload, 13) == 0x14u);
        assert(memcmp(out,
                      "\xAB\x14\xFA\x02\x26\x05\xD4\xFF\x35\x31\x32\x53\x43\x43\x22\x51\x22\x91\xB4\xAE",
                      0x14u) == 0);
        assert(HostCrc16(out, 0x12u) == 0xB4AEu);
    }
    {
        uint8_t pad[HOST_ACK_PAYLOAD_MAX + 1u];
        assert(HostAck_BuildPayload(out, HOST_ADDR_SN, HOST_CMD_SN, pad,
                                    (uint8_t)(HOST_ACK_PAYLOAD_MAX + 1u)) == 0u);
    }
    /* All documented frames, back to back in one parser. */
    HostParser_Init(&p);
    for (f = 0; f < NFRAMES; ++f) feed_range(&p, f, 0, frames[f][1]);
    /* Every byte boundary. */
    for (f = 0; f < NFRAMES; ++f) for (i = 0; i <= frames[f][1]; ++i) {
        HostParser_Init(&p);
        feed_range(&p, f, 0, i);
        feed_range(&p, f, i, frames[f][1]);
    }
    /* A garbage prefix must not hide the header. Two copies are fed because a
       prefix as long as the frame cannot be evicted by a single frame. */
    for (f = 0; f < NFRAMES; ++f) {
        unsigned len = frames[f][1], k, hits = 0u;
        HostParser_Init(&p);
        assert(!HostParser_Feed(&p, 0).ok);
        assert(!HostParser_Feed(&p, 0xAA).ok);
        for (k = 0; k < len * 2u; ++k)
            if (HostParser_Feed(&p, frames[f][k % len]).ok) ++hits;
        assert(hits == 2u);
    }
    /* Corrupt any byte from the function code through the CRC, and also lie
       about the length. Nothing may parse. */
    for (f = 0; f < NFRAMES; ++f) {
        unsigned len = frames[f][1];
        for (i = 2u; i < len; ++i) {
            unsigned j;
            memcpy(bad, frames[f], 8);
            bad[i] ^= 0xFFu;
            HostParser_Init(&p);
            for (j = 0; j < len; ++j) assert(!HostParser_Feed(&p, bad[j]).ok);
        }
        memcpy(bad, frames[f], 8);
        bad[1] = (len == 8u) ? 7u : 8u;
        HostParser_Init(&p);
        for (i = 0; i < len; ++i) assert(!HostParser_Feed(&p, bad[i]).ok);
    }
    /* A rejected frame must not wedge the parser. Two copies are needed for
       the 7-byte frames: a full window of garbage cannot be evicted by a
       shorter frame, so <1> and <7> lose one frame after corruption. */
    for (f = 0; f < NFRAMES; ++f) {
        unsigned len = frames[f][1], hits = 0u, k;
        memcpy(bad, frames[f], 8);
        bad[len - 1u] ^= 1u;
        HostParser_Init(&p);
        for (i = 0; i < len; ++i) assert(!HostParser_Feed(&p, bad[i]).ok);
        for (k = 0; k < len * 2u; ++k)
            if (HostParser_Feed(&p, frames[f][k % len]).ok) ++hits;
        assert(hits == 2u);
    }
    /* Light params decode to the full <11>-<14> table, rejecting the rest. */
    {
        static const uint8_t ok_params[11] = {0x00, 0xFF, 0x10, 0x11, 0x1F, 0x20,
                                              0x21, 0x2F, 0x30, 0x31, 0x3F};
        int lamp, state, k;
        for (k = 0; k < 11; ++k)
            assert(LightCommand_Decode(ok_params[k], &lamp, &state));
        for (i = 0; i < 256; ++i)
            if (!LightCommand_Decode((uint8_t)i, &lamp, &state)) {
                int listed = 0;
                for (k = 0; k < 11; ++k) if (ok_params[k] == i) listed = 1;
                assert(!listed);
            }
    }
    puts("PASS protocol: CRC vectors, field mapping, splits, corruption rejection, "
         "resync, ACK1/ACK2 builder, lamp table");
    return 0;
}
