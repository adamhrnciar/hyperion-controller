// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol-independent receiver frame. Portable, no SDK dependency.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RC_MAX_CHANNELS 16
#define RC_CENTER_US    1500

typedef struct {
    uint16_t us[RC_MAX_CHANNELS];   // pulse-width equivalent, 1000..2000 nominal
    uint8_t count;                  // valid channels
    bool failsafe;                  // receiver reports failsafe (SBUS flag, CRSF LQ = 0)
    bool frame_lost;                // SBUS frame-lost flag, informational only
} rc_frame_t;

// Unpack n little-endian 11-bit values (SBUS and CRSF channel packing).
void rc_unpack_11bit(const uint8_t *src, uint16_t *dst, int n);

// SBUS/CRSF raw value (172..1811, centre 992) to microseconds (988..2012, centre 1500).
static inline uint16_t rc_raw11_to_us(uint16_t raw) {
    return (uint16_t)((raw * 5u) / 8u + 880u);
}
