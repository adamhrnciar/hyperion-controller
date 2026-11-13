// SPDX-License-Identifier: GPL-3.0-or-later
#include "rc_input.h"

#include "config.h"

#include "hardware/sync.h"
#include "pico/time.h"

static rc_frame_t latest;
// 64-bit: the 32-bit microsecond timer wraps every 71.6 minutes, and a frame that old
// must not look fresh again.
static uint64_t latest_us;
static bool have_frame;
static uint32_t frames;
static uint32_t errors;

void rc_input_init(void) {
    switch (RC_PROTOCOL) {
    case RC_PROTOCOL_SBUS: sbus_rx_init(); break;
    case RC_PROTOCOL_CRSF: crsf_rx_init(); break;
    case RC_PROTOCOL_PWM:  rc_pwm_init();  break;
    }
}

const char *rc_input_protocol_name(void) {
    switch (RC_PROTOCOL) {
    case RC_PROTOCOL_SBUS: return "SBUS (PIO, inverted)";
    case RC_PROTOCOL_CRSF: return "CRSF (UART0 420k)";
    case RC_PROTOCOL_PWM:  return "PWM (CH1 RC_RX, CH2 RC_TX)";
    }
    return "?";
}

// Called from interrupt context.
void rc_input_publish(const rc_frame_t *f) {
    latest = *f;
    latest_us = time_us_64();
    have_frame = true;
    frames++;
}

void rc_input_count_errors(uint32_t errors_total) {
    errors = errors_total;
}

bool rc_input_latest(rc_frame_t *out, uint32_t *age_ms) {
    uint32_t irq = save_and_disable_interrupts();
    bool have = have_frame;
    *out = latest;
    uint64_t t = latest_us;
    restore_interrupts(irq);

    uint64_t age = (time_us_64() - t) / 1000u;
    *age_ms = !have || age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
    return have;
}

void rc_input_stats(rc_stats_t *out) {
    uint32_t irq = save_and_disable_interrupts();
    out->frames = frames;
    out->errors = errors;
    restore_interrupts(irq);
}
