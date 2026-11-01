// SPDX-License-Identifier: GPL-3.0-or-later
#include "servo_out.h"

#include "config.h"
#include "pins.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

static const uint servo_pins[SERVO_OUT_COUNT] = {PIN_PWM1, PIN_PWM2, PIN_PWM3, PIN_PWM4};

void servo_out_init(void) {
    // 1 us per count, 20000 counts = 20 ms period.
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, (float)clock_get_hz(clk_sys) / 1000000.0f);
    pwm_config_set_wrap(&cfg, 19999);

    uint32_t slice_mask = 0;
    for (int i = 0; i < SERVO_OUT_COUNT; i++) {
        uint slice = pwm_gpio_to_slice_num(servo_pins[i]);
        if (!(slice_mask & (1u << slice))) {
            pwm_init(slice, &cfg, false);
            slice_mask |= 1u << slice;
        }
        pwm_set_gpio_level(servo_pins[i], 0);
        gpio_set_function(servo_pins[i], GPIO_FUNC_PWM);
    }
    pwm_set_mask_enabled(pwm_hw->en | slice_mask);
}

void servo_out_set_us(int idx, uint16_t us) {
    if (idx < 0 || idx >= SERVO_OUT_COUNT) {
        return;
    }
    if (us != 0) {
        if (us < 800) {
            us = 800;
        } else if (us > 2200) {
            us = 2200;
        }
    }
    pwm_set_gpio_level(servo_pins[idx], us);
}

void servo_out_off_all(void) {
    for (int i = 0; i < SERVO_OUT_COUNT; i++) {
        servo_out_set_us(i, SERVO_FAILSAFE_US);
    }
}
