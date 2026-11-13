// SPDX-License-Identifier: GPL-3.0-or-later
#include "motor.h"

#include "config.h"
#include "pins.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

typedef struct {
    uint in1;
    uint in2;
    bool reversed;
} motor_pins_t;

static const motor_pins_t motors[MOTOR_COUNT] = {
    [MOTOR_A] = {PIN_MA_IN1, PIN_MA_IN2, MOTOR_A_REVERSED},
    [MOTOR_B] = {PIN_MB_IN1, PIN_MB_IN2, MOTOR_B_REVERSED},
};

static uint16_t pwm_top;
static volatile bool armed;

// level 0 = always low, pwm_top + 1 = always high.
static void set_levels(motor_id_t m, uint32_t in1, uint32_t in2) {
    pwm_set_gpio_level(motors[m].in1, (uint16_t)in1);
    pwm_set_gpio_level(motors[m].in2, (uint16_t)in2);
}

void motor_init(void) {
    uint32_t slice_mask = 0;
    uint32_t wrap = clock_get_hz(clk_sys) / MOTOR_PWM_HZ - 1;
    // 16-bit counter: 150 MHz / 20 kHz = 7500 counts, fits without a divider.
    pwm_top = (uint16_t)(wrap > 0xFFFE ? 0xFFFE : wrap);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, pwm_top);
    pwm_config_set_clkdiv_int(&cfg, 1);

    for (int m = 0; m < MOTOR_COUNT; m++) {
        uint pins[2] = {motors[m].in1, motors[m].in2};
        for (int i = 0; i < 2; i++) {
            uint slice = pwm_gpio_to_slice_num(pins[i]);
            if (!(slice_mask & (1u << slice))) {
                pwm_init(slice, &cfg, false);
                slice_mask |= 1u << slice;
            }
            pwm_set_gpio_level(pins[i], 0);
            gpio_set_function(pins[i], GPIO_FUNC_PWM);
        }
    }
    // Motor A uses slices 1 and 3, motor B slice 4. Start them in phase.
    pwm_set_mask_enabled(pwm_hw->en | slice_mask);
    armed = false;
}

static void disarmed_output(motor_id_t m) {
    if (MOTOR_DISARMED_BRAKE) {
        motor_brake(m);
    } else {
        motor_coast(m);
    }
}

void motor_arm(bool on) {
    armed = on;
    if (!on) {
        for (int m = 0; m < MOTOR_COUNT; m++) {
            disarmed_output((motor_id_t)m);
        }
    }
}

bool motor_is_armed(void) {
    return armed;
}

void motor_set_decay(motor_id_t m, int16_t cmd, bool slow_decay) {
    if (!armed) {
        disarmed_output(m);
        return;
    }
    if (cmd > 1000) {
        cmd = 1000;
    } else if (cmd < -1000) {
        cmd = -1000;
    }
    if (motors[m].reversed) {
        cmd = (int16_t)-cmd;
    }
    if (cmd == 0) {
        if (MOTOR_NEUTRAL_BRAKE) {
            motor_brake(m);
        } else {
            motor_coast(m);
        }
        return;
    }

    uint32_t full = (uint32_t)pwm_top + 1;
    uint32_t mag = (uint32_t)(cmd < 0 ? -cmd : cmd);
    uint32_t on = full * mag / 1000;
    if (slow_decay) {
        // Forward: IN1 high, IN2 high only during the off (brake) phase.
        uint32_t off = full - on;
        if (cmd > 0) {
            set_levels(m, full, off);
        } else {
            set_levels(m, off, full);
        }
    } else {
        // Forward: IN1 PWM, IN2 low; the off phase coasts.
        if (cmd > 0) {
            set_levels(m, on, 0);
        } else {
            set_levels(m, 0, on);
        }
    }
}

void motor_set(motor_id_t m, int16_t cmd) {
    motor_set_decay(m, cmd, MOTOR_SLOW_DECAY);
}

void motor_coast(motor_id_t m) {
    set_levels(m, 0, 0);
}

void motor_brake(motor_id_t m) {
    uint32_t full = (uint32_t)pwm_top + 1;
    set_levels(m, full, full);
}

void motor_coast_all(void) {
    for (int m = 0; m < MOTOR_COUNT; m++) {
        motor_coast((motor_id_t)m);
    }
}
