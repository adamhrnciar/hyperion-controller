// SPDX-License-Identifier: GPL-3.0-or-later
// Green status LED (D5, GPIO25). The red LED is the hard-wired +3V3 power LED.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/arming.h"
#include "pins.h"

#include "hardware/gpio.h"

static inline void led_init(void) {
    gpio_init(PIN_LED_STATUS);
    gpio_set_dir(PIN_LED_STATUS, GPIO_OUT);
    gpio_put(PIN_LED_STATUS, 0);
}

static inline void led_set(bool on) {
    gpio_put(PIN_LED_STATUS, on);
}

// NO_LINK: fast blink 5 Hz. SWITCH_OFF / WAIT_NEUTRAL: short flash every second. ARMED: on.
static inline void led_show_arm_state(arm_state_t s, uint32_t now_ms) {
    switch (s) {
    case ARM_NO_LINK:
        led_set((now_ms / 100) % 2);
        break;
    case ARM_SWITCH_OFF:
    case ARM_WAIT_NEUTRAL:
        led_set(now_ms % 1000 < 100);
        break;
    case ARM_ARMED:
        led_set(true);
        break;
    }
}
