#ifndef FAKE_PORT_H
#define FAKE_PORT_H
#include <stdbool.h>
#include <stdint.h>
#include "barcode_port.h"
typedef struct { uint8_t data[HOST_TX_MAX]; uint8_t len; } FakeFrame;
extern FakeFrame fake_host[1024], fake_scanner[32];
extern unsigned fake_host_count, fake_scanner_count, fake_relay_changes, fake_light_changes;
extern bool fake_relay, fake_red, fake_green, fake_yellow, fake_send_ok;
void FakePort_Reset(void);
#endif
