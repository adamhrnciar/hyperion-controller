// SPDX-License-Identifier: GPL-3.0-or-later
// CRSF (ExpressLRS / TBS Crossfire) frame parser. Portable, no SDK dependency.
//
// Frame: [sync][len][type][payload ...][crc8]
//   len counts type + payload + crc. CRC-8/DVB-S2 (poly 0xD5) over type + payload.
//   0x16 RC_CHANNELS_PACKED: 22 bytes, 16 x 11-bit channels (same packing as SBUS)
//   0x14 LINK_STATISTICS:    10 bytes, [2] = uplink link quality (%)

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rc.h"

#define CRSF_SYNC_FC        0xC8
#define CRSF_SYNC_TX        0xEE
#define CRSF_TYPE_LINK      0x14
#define CRSF_TYPE_RC        0x16
#define CRSF_MAX_FRAME      64
#define CRSF_GAP_US         500u    // bytes are 24 us apart inside a frame at 420 kbaud

typedef enum {
    CRSF_NONE = 0,
    CRSF_GOT_RC,        // *out updated with new channels
    CRSF_GOT_LINK,      // link statistics updated (lq)
} crsf_result_t;

typedef struct {
    uint8_t buf[CRSF_MAX_FRAME];
    uint8_t pos;
    bool started;
    uint32_t last_byte_us;
    uint8_t uplink_lq;      // last link quality, 0..100
    bool have_link_stats;
    uint32_t frames;
    uint32_t errors;
} crsf_parser_t;

void crsf_parser_init(crsf_parser_t *p);
crsf_result_t crsf_parser_feed(crsf_parser_t *p, uint8_t byte, uint32_t now_us, rc_frame_t *out);
uint8_t crsf_crc8(const uint8_t *data, int len);
