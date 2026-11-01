// SPDX-License-Identifier: GPL-3.0-or-later
// DRV8231A H-bridge driver (U2 = motor A, U3 = motor B).
//
// DRV8231A truth table (datasheet SLVSFZ8A, table 7-2):
//   IN1 IN2  OUT1 OUT2
//    0   0   Z    Z     coast, sleep after tSLEEP 0.8..1.5 ms (tWAKE <= 250 us)
//    0   1   L    H     reverse
//    1   0   H    L     forward
//    1   1   L    L     brake (low-side slow decay)
// Inputs have 100 k pull-downs, so a reset or unprogrammed MCU leaves both bridges coasting.
// Current limit: VREF = 3.3 V, R_IPROPI = 1.5 k -> I_TRIP = 1.47 A, 25 us brake off-time.

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MOTOR_A = 0,
    MOTOR_B = 1,
    MOTOR_COUNT
} motor_id_t;

// Configure the four IN pins as PWM outputs, all low (coast, driver asleep).
// Call first in main(). Disarmed braking starts with the first motor_arm(false).
void motor_init(void);

// While disarmed every command is ignored and both bridges brake
// (MOTOR_DISARMED_BRAKE = 1, default) or coast (0).
void motor_arm(bool armed);
bool motor_is_armed(void);

// cmd: -1000 (full reverse) .. +1000 (full forward), permille duty.
// Uses drive/brake PWM when MOTOR_SLOW_DECAY = 1, drive/coast otherwise.
void motor_set(motor_id_t m, int16_t cmd);

// Same as motor_set but chooses the decay mode per call (bring-up tests).
void motor_set_decay(motor_id_t m, int16_t cmd, bool slow_decay);

void motor_coast(motor_id_t m);
void motor_brake(motor_id_t m);
void motor_coast_all(void);
