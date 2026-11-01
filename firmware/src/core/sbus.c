// SPDX-License-Identifier: GPL-3.0-or-later
#include "sbus.h"

#include <string.h>

#define SBUS_SYNC_LOST 0xFF

void sbus_parser_init(sbus_parser_t *p) {
    memset(p, 0, sizeof(*p));
    p->pos = SBUS_SYNC_LOST;
}

static bool end_byte_ok(uint8_t b) {
    return b == 0x00 || (b & 0x0F) == 0x04;
}

bool sbus_parser_feed(sbus_parser_t *p, uint8_t byte, bool parity_ok,
                      uint32_t now_us, rc_frame_t *out) {
    bool gap = !p->started || (uint32_t)(now_us - p->last_byte_us) > SBUS_GAP_US;
    p->started = true;
    p->last_byte_us = now_us;

    if (gap) {
        if (p->pos != SBUS_SYNC_LOST && p->pos != 0) {
            p->errors++;    // truncated frame
        }
        p->pos = 0;
    }
    if (p->pos == SBUS_SYNC_LOST) {
        return false;
    }
    if (!parity_ok || (p->pos == 0 && byte != SBUS_HEADER)) {
        p->errors++;
        p->pos = SBUS_SYNC_LOST;
        return false;
    }

    p->buf[p->pos++] = byte;
    if (p->pos < SBUS_FRAME_LEN) {
        return false;
    }
    p->pos = SBUS_SYNC_LOST;   // next frame starts after the next gap

    if (!end_byte_ok(p->buf[24])) {
        p->errors++;
        return false;
    }

    uint16_t raw[16];
    rc_unpack_11bit(&p->buf[1], raw, 16);
    for (int i = 0; i < 16; i++) {
        out->us[i] = rc_raw11_to_us(raw[i]);
    }
    out->count = 16;
    out->frame_lost = (p->buf[23] & 0x04) != 0;
    out->failsafe = (p->buf[23] & 0x08) != 0;
    p->frames++;
    return true;
}
