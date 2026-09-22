#include "host_protocol.h"
#include <string.h>
const uint8_t BARCODE_ACK[7] = {0xAB,0x07,0xFA,0x01,0x0A,0x30,0x52};
const uint8_t BARCODE_RESCAN[3] = {0x16,0x54,0x0D};
uint16_t HostCrc16(const uint8_t *p, uint8_t n)
{
    uint16_t crc = 0xFFFFu;
    uint8_t i;
    int bit;
    for (i = 0; i < n; ++i) {
        crc ^= (uint16_t)p[i] << 8;
        for (bit = 0; bit < 8; ++bit) {
            uint16_t next = (uint16_t)(crc << 1);
            if (crc & 0x8000u) next ^= 0x1021u;
            crc = next;
        }
    }
    return crc;
}
void HostParser_Init(HostParser *p) { memset(p, 0, sizeof(*p)); }
HostFrame HostParser_Feed(HostParser *p, uint8_t byte)
{
    HostFrame fr;
    memset(&fr, 0, sizeof(fr));
    if (p->used == HOST_FRAME_MAX) {
        memmove(p->window, p->window + 1, HOST_FRAME_MAX - 1u);
        p->used = HOST_FRAME_MAX - 1u;
    }
    p->window[p->used++] = byte;
    /* The length byte fixes the frame size, so a 7- or 8-byte frame is only
       completed once the whole CRC is in. A bad CRC keeps sliding until the
       next header resynchronizes. */
    if (p->used >= 3u && p->window[0] == 0xAAu && p->window[2] == HOST_FUNC_BOX) {
        uint8_t len = p->window[1];
        if ((len == 7u || len == 8u) && p->used >= len) {
            uint16_t want = (uint16_t)((uint16_t)p->window[len - 2u] << 8 | p->window[len - 1u]);
            if (HostCrc16(p->window, (uint8_t)(len - 2u)) == want) {
                fr.ok = true;
                fr.addr = p->window[3];
                fr.cmd = p->window[4];
                if (len == 8u) { fr.param = p->window[5]; fr.has_param = true; }
                if (p->used > len) memmove(p->window, p->window + len, (uint8_t)(p->used - len));
                p->used = (uint8_t)(p->used - len);
            }
        }
    }
    return fr;
}
uint8_t HostAck_BuildPayload(uint8_t *out, uint8_t addr, uint8_t cmd, const uint8_t *payload, uint8_t n)
{
    uint8_t len, i;
    if (n > HOST_ACK_PAYLOAD_MAX) return 0;
    len = (uint8_t)(7u + n);
    out[0] = 0xABu;
    out[1] = len;
    out[2] = HOST_FUNC_BOX;
    out[3] = addr;
    out[4] = cmd;
    for (i = 0; i < n; ++i) out[5u + i] = payload[i];
    {
        uint16_t crc = HostCrc16(out, (uint8_t)(len - 2u));
        out[len - 2u] = (uint8_t)(crc >> 8);
        out[len - 1u] = (uint8_t)crc;
    }
    return len;
}
bool LightCommand_Decode(uint8_t param, int *lamp, int *state)
{
    switch (param) {
        case 0x00u: *lamp = LAMP_ALL;    *state = LAMP_OFF;   return true;
        case 0xFFu: *lamp = LAMP_ALL;    *state = LAMP_BLINK; return true;
        case 0x10u: *lamp = LAMP_RED;    *state = LAMP_OFF;   return true;
        case 0x11u: *lamp = LAMP_RED;    *state = LAMP_ON;    return true;
        case 0x1Fu: *lamp = LAMP_RED;    *state = LAMP_BLINK; return true;
        case 0x20u: *lamp = LAMP_YELLOW; *state = LAMP_OFF;   return true;
        case 0x21u: *lamp = LAMP_YELLOW; *state = LAMP_ON;    return true;
        case 0x2Fu: *lamp = LAMP_YELLOW; *state = LAMP_BLINK; return true;
        case 0x30u: *lamp = LAMP_GREEN;  *state = LAMP_OFF;   return true;
        case 0x31u: *lamp = LAMP_GREEN;  *state = LAMP_ON;    return true;
        case 0x3Fu: *lamp = LAMP_GREEN;  *state = LAMP_BLINK; return true;
    }
    return false;
}
