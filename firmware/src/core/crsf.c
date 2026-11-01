// SPDX-License-Identifier: GPL-3.0-or-later
#include "crsf.h"

#include <string.h>

void crsf_parser_init(crsf_parser_t *p) {
    memset(p, 0, sizeof(*p));
}

uint8_t crsf_crc8(const uint8_t *data, int len) {
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0xD5 : crc << 1);
        }
    }
    return crc;
}

static crsf_result_t handle_frame(crsf_parser_t *p, rc_frame_t *out) {
    uint8_t len = p->buf[1];
    uint8_t type = p->buf[2];
    const uint8_t *payload = &p->buf[3];
    int payload_len = len - 2;

    if (crsf_crc8(&p->buf[2], len - 1) != p->buf[len + 1]) {
        p->errors++;
        return CRSF_NONE;
    }
    p->frames++;

    if (type == CRSF_TYPE_RC && payload_len == 22) {
        uint16_t raw[16];
        rc_unpack_11bit(payload, raw, 16);
        for (int i = 0; i < 16; i++) {
            out->us[i] = rc_raw11_to_us(raw[i]);
        }
        out->count = 16;
        out->frame_lost = false;
        out->failsafe = p->have_link_stats && p->uplink_lq == 0;
        return CRSF_GOT_RC;
    }
    if (type == CRSF_TYPE_LINK && payload_len == 10) {
        p->uplink_lq = payload[2];
        p->have_link_stats = true;
        return CRSF_GOT_LINK;
    }
    return CRSF_NONE;
}

crsf_result_t crsf_parser_feed(crsf_parser_t *p, uint8_t byte, uint32_t now_us, rc_frame_t *out) {
    bool gap = !p->started || (uint32_t)(now_us - p->last_byte_us) > CRSF_GAP_US;
    p->started = true;
    p->last_byte_us = now_us;
    if (gap && p->pos != 0) {
        p->errors++;    // truncated frame
        p->pos = 0;
    }

    if (p->pos == 0) {
        if (byte == CRSF_SYNC_FC || byte == CRSF_SYNC_TX) {
            p->buf[p->pos++] = byte;
        }
        return CRSF_NONE;
    }
    if (p->pos == 1 && (byte < 2 || byte > CRSF_MAX_FRAME - 2)) {
        p->errors++;
        p->pos = 0;
        return CRSF_NONE;
    }

    p->buf[p->pos++] = byte;
    if (p->pos < p->buf[1] + 2) {
        return CRSF_NONE;
    }
    p->pos = 0;
    return handle_frame(p, out);
}
