// SPDX-License-Identifier: GPL-3.0-or-later
// Servo-pulse (PWM) receiver input: CH1 on the RC_RX pad, CH2 on the RC_TX pad.
// Pulse widths are timed with GPIO edge interrupts. A frame is published on
// every CH1 pulse; a channel with no valid pulse for RC_TIMEOUT_MS is
// reported as failsafe, so a dead CH2 wire also stops the robot.
// Unused channels read 1500 us.

#include "rc_input.h"

#include "config.h"
#include "pins.h"

#include "hardware/gpio.h"
#include "pico/time.h"

#define PWM_CHANNELS        2
#define PWM_MIN_US          800
#define PWM_MAX_US          2200
#define PWM_CH_TIMEOUT_US   (RC_TIMEOUT_MS * 1000u)

static const uint pwm_pins[PWM_CHANNELS] = {PIN_RC_RX, PIN_RC_TX};
static uint32_t rise_us[PWM_CHANNELS];
static uint32_t good_us[PWM_CHANNELS];
static uint16_t width_us[PWM_CHANNELS];
static bool seen[PWM_CHANNELS];
static uint32_t errors;

static void rc_pwm_edge(uint gpio, uint32_t events) {
    uint32_t now = time_us_32();
    int ch = gpio == PIN_RC_RX ? 0 : 1;

    if (events & GPIO_IRQ_EDGE_RISE) {
        rise_us[ch] = now;
    }
    if (!(events & GPIO_IRQ_EDGE_FALL)) {
        return;
    }
    uint32_t w = now - rise_us[ch];
    if (w < PWM_MIN_US || w > PWM_MAX_US) {
        errors++;
        rc_input_count_errors(errors);
        return;
    }
    width_us[ch] = (uint16_t)w;
    good_us[ch] = now;
    seen[ch] = true;

    if (ch != 0) {
        return;
    }
    rc_frame_t f = {.count = PWM_CHANNELS};
    for (int i = 0; i < RC_MAX_CHANNELS; i++) {
        f.us[i] = RC_CENTER_US;
    }
    for (int i = 0; i < PWM_CHANNELS; i++) {
        f.us[i] = width_us[i];
        if (!seen[i] || now - good_us[i] > PWM_CH_TIMEOUT_US) {
            f.failsafe = true;
        }
    }
    rc_input_publish(&f);
}

void rc_pwm_init(void) {
    for (int i = 0; i < PWM_CHANNELS; i++) {
        gpio_init(pwm_pins[i]);
        gpio_set_dir(pwm_pins[i], GPIO_IN);
        gpio_pull_down(pwm_pins[i]);
    }
    gpio_set_irq_enabled_with_callback(pwm_pins[0], GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true, rc_pwm_edge);
    gpio_set_irq_enabled(pwm_pins[1], GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
}
