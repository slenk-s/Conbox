#include "barcode_rx.h"
#include <string.h>
void BarcodeRx_Init(BarcodeRx *r) { memset(r, 0, sizeof(*r)); }
void BarcodeRx_Invalidate(BarcodeRx *r)
{
    BarcodeRx_Init(r);
    r->discard = true;
}
bool BarcodeRx_Feed(BarcodeRx *r, uint8_t byte, bool busy, Barcode *out)
{
    bool ended = r->last_cr && byte == 0x0Au;
    bool ready = false;
    r->discard = r->discard || busy;
    r->last_cr = byte == 0x0Du;
    if (r->used < sizeof(r->data)) r->data[r->used++] = byte;
    else r->discard = true;
    if (ended) {
        if (!r->discard && r->used >= 3u && r->used <= 23u) {
            memset(out, 0, sizeof(*out));
            out->len = (uint8_t)(r->used - 2u);
            memcpy(out->data, r->data, out->len);
            ready = true;
        }
        BarcodeRx_Init(r);
    }
    return ready;
}
