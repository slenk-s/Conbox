#ifndef HOST_PROTOCOL_H
#define HOST_PROTOCOL_H
#include <stdint.h>
#include <stdbool.h>
#include "barcode_rx.h"
/* Wire frame: AA len FA addr cmd [param] CRCH CRCL. The FA byte is the fixed
   box function code, so len counts head through CRC: 7 without a param byte,
   8 with one. CRC16/CCITT-FALSE covers everything except the final two bytes
   and is transmitted high byte first. Replies mirror the request addr and use
   AB as head: ACK1 is always AB 07 FA <addr> <status> CRCH CRCL, ACK2 is
   AB <len> FA <addr> <cmd> <payload> CRCH CRCL. */
enum {
    HOST_CMD_PASS = 0x03u,      /* recognition passed */
    HOST_CMD_LIGHT = 0x06u,     /* <11>-<14>: alarm lamps */
    HOST_CMD_SN = 0x17u,        /* <7>: query box serial number */
    HOST_CMD_ID = 0x26u,        /* mobile ID query */
    HOST_CMD_VERSION = 0xFFu    /* <1>: query firmware version */
};
/* <1> ACK2 payload: the build number as one byte. Bump per release. */
#define HOST_FW_VERSION 101u
enum { HOST_ADDR_BOX = 0x01u, HOST_ADDR_SN = 0x02u };
enum { HOST_ACK_OK = 0x0Au, HOST_ACK_FAIL = 0x0Eu };
#define HOST_FUNC_BOX 0xFAu     /* fixed function byte of every box frame */
/* <11>-<14>: 00 all off, FF all blink in sync, 1x red, 2x yellow, 3x green,
   low nibble 0 off / 1 on / F blink. There is no "all on" command. FF is the
   only case where more than one lamp is lit at once. */
enum { LAMP_ALL, LAMP_RED, LAMP_YELLOW, LAMP_GREEN };
enum { LAMP_OFF, LAMP_ON, LAMP_BLINK };
/* Design section 5: the red-blink param also means "current recognition failed". */
#define HOST_PARAM_RED_FAIL 0x1Fu
#define HOST_FRAME_MAX 8u
#define HOST_ACK_PAYLOAD_MAX BARCODE_MAX_LEN  /* <7> ACK2 carries the barcode */

typedef struct { uint8_t window[HOST_FRAME_MAX]; uint8_t used; } HostParser;
typedef struct {
    bool ok;          /* frame complete and CRC valid */
    uint8_t addr, cmd, param;
    bool has_param;
} HostFrame;

extern const uint8_t BARCODE_ACK[7];
/* Scanner trigger byte sequence, sent to USART2 after a recognition failure. */
extern const uint8_t BARCODE_RESCAN[3];
void HostParser_Init(HostParser *p);
HostFrame HostParser_Feed(HostParser *p, uint8_t byte);
uint16_t HostCrc16(const uint8_t *p, uint8_t n);
/* Builds AB len FA addr cmd [payload] CRCH CRCL into out, returning the byte
   count, or 0 when the payload is too long. Size out for 7 + n bytes. */
uint8_t HostAck_BuildPayload(uint8_t *out, uint8_t addr, uint8_t cmd, const uint8_t *payload, uint8_t n);
bool LightCommand_Decode(uint8_t param, int *lamp, int *state);
#endif
