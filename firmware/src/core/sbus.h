// SPDX-License-Identifier: GPL-3.0-or-later
// SBUS frame parser. Portable, no SDK dependency.
//
// Frame: 25 bytes at 100000 baud 8E2, line inverted.
//   [0] 0x0F header, [1..22] 16 x 11-bit channels, [23] flags, [24] end byte
//   flags: bit0 CH17, bit1 CH18, bit2 frame lost, bit3 failsafe
//   end byte: 0x00 (SBUS) or 0x04/0x14/0x24/0x34 (SBUS2)
// Frames are re-synchronised on the inter-frame gap: a header is only accepted
// after a gap of SBUS_GAP_US with no bytes.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rc.h"

#define SBUS_FRAME_LEN  25
#define SBUS_HEADER     0x0F
#define SBUS_GAP_US     1000u   // bytes are 120 us apart inside a frame, frames >= 3 ms apart

typedef struct {
    uint8_t buf[SBUS_FRAME_LEN];
    uint8_t pos;            // SBUS_SYNC_LOST while waiting for the next gap
    bool started;
    uint32_t last_byte_us;
    uint32_t frames;        // good frames
    uint32_t errors;        // parity, header, end-byte and truncated-frame errors
} sbus_parser_t;

void sbus_parser_init(sbus_parser_t *p);

// Feed one byte with its parity check result and receive timestamp.
// Returns true and fills *out when a complete, valid frame has been decoded.
bool sbus_parser_feed(sbus_parser_t *p, uint8_t byte, bool parity_ok,
                      uint32_t now_us, rc_frame_t *out);

// Even parity: total number of 1 bits in data + parity bit is even.
static inline bool sbus_parity_ok(uint8_t data, uint8_t parity_bit) {
    uint8_t v = data ^ (uint8_t)(parity_bit & 1u);
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1u) == 0;
}
