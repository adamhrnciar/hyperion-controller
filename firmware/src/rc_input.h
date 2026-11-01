// SPDX-License-Identifier: GPL-3.0-or-later
// Receiver input. The protocol is chosen at build time (RC_PROTOCOL in config.h).
// Backends decode in interrupt context and publish complete frames here.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/rc.h"

typedef struct {
    uint32_t frames;        // good frames since boot
    uint32_t errors;        // parser errors (parity, CRC, framing, truncated)
} rc_stats_t;

void rc_input_init(void);
const char *rc_input_protocol_name(void);

// Copy the newest frame. Returns false if no frame has arrived since boot.
// *age_ms is the time since that frame was received.
bool rc_input_latest(rc_frame_t *out, uint32_t *age_ms);
void rc_input_stats(rc_stats_t *out);

// Backend interface.
void rc_input_publish(const rc_frame_t *f);
void rc_input_count_errors(uint32_t errors_total);

void sbus_rx_init(void);
void crsf_rx_init(void);
void rc_pwm_init(void);
