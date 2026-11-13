// SPDX-License-Identifier: GPL-3.0-or-later
// 50 Hz servo/ESC pulses on the PWM1..PWM4 pads (GPIO10..13, PWM slices 5 and 6).

#pragma once

#include <stdint.h>

#define SERVO_OUT_COUNT 4

void servo_out_init(void);              // all outputs low, no pulses
void servo_out_set_us(int idx, uint16_t us);    // 0 = no pulses, otherwise clamped 800..2200 us
void servo_out_off_all(void);           // SERVO_FAILSAFE_US on every pad
