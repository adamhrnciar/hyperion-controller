// SPDX-License-Identifier: GPL-3.0-or-later
// Build-time configuration. Every value can be overridden from CMake with
// -D<NAME>=<value> (see firmware/README.md).

#pragma once

// --- Firmware mode -------------------------------------------------------------
// CMake builds one executable per mode (hyperion_<mode>.uf2) and sets HYPERION_MODE.
#define HYPERION_MODE_DRIVE     0   // normal firmware: RC -> arming -> tank mix -> motors
#define HYPERION_MODE_BLINK     1   // LED + PWM pad toggle, console heartbeat
#define HYPERION_MODE_ADC       2   // VBAT, IPROPI and MCU temperature readout
#define HYPERION_MODE_RC        3   // receiver decode, no motors
#define HYPERION_MODE_IMU       4   // LSM6DSV32X WHO_AM_I and data
#define HYPERION_MODE_MOTOR     5   // interactive single-channel motor test over USB
#define HYPERION_MODE_FAILSAFE  6   // drive firmware with verbose arming/failsafe log
#define HYPERION_MODE_THERMAL   7   // DRV8231A stall test, logs time to thermal shutdown

#ifndef HYPERION_MODE
#define HYPERION_MODE HYPERION_MODE_BLINK
#endif

// --- Receiver --------------------------------------------------------------------
#define RC_PROTOCOL_SBUS        0   // PIO, 100000 baud 8E2, pad input inverted
#define RC_PROTOCOL_CRSF        1   // UART0, 420000 baud 8N1
#define RC_PROTOCOL_PWM         2   // servo pulses: CH1 on RC_RX pad, CH2 on RC_TX pad

#ifndef RC_PROTOCOL
#define RC_PROTOCOL RC_PROTOCOL_SBUS
#endif

// No valid frame for this long -> link lost -> motors brake. 60 ms is 3 frames at
// 50 Hz (CRSF/PWM) and 4 at the slowest SBUS rate (14 ms). With ELRS at 50 Hz
// packet rate and telemetry on, raise it to about 100 ms to avoid false trips.
#ifndef RC_TIMEOUT_MS
#define RC_TIMEOUT_MS           60
#endif

// Channel assignment, 0-based (0 = CH1). Defaults follow the AETR order used by
// EdgeTX/OpenTX, FrSky, FlySky and ELRS on a Mode 2 transmitter: the right stick
// drives (CH1 left/right, CH2 up/down), the left stick's throttle axis (CH3, not
// self-centring) runs the weapon. Spektrum uses TAER (CH1 throttle).
#ifndef RC_CH_STEER
#define RC_CH_STEER             0   // CH1 aileron
#endif
#ifndef RC_CH_THROTTLE
#define RC_CH_THROTTLE          1   // CH2 elevator
#endif
#ifndef RC_CH_WEAPON
#define RC_CH_WEAPON            2   // CH3, sent to PWM1
#endif
// Arming also needs the weapon channel at or below this (weapon stick low).
#ifndef RC_WEAPON_ARM_MAX_US
#define RC_WEAPON_ARM_MAX_US    1100
#endif
// Arm switch channel, -1 = no switch (arm on link + neutral sticks only).
#ifndef RC_CH_ARM
#define RC_CH_ARM               (-1)
#endif
#ifndef RC_ARM_THRESHOLD_US
#define RC_ARM_THRESHOLD_US     1700
#endif

// Stick deadband around 1500 us, in us.
#ifndef RC_DEADBAND_US
#define RC_DEADBAND_US          25
#endif

// Sticks must stay neutral this long after link-up / switch-on before arming.
#ifndef ARM_NEUTRAL_HOLD_MS
#define ARM_NEUTRAL_HOLD_MS     500
#endif

// A link lost while armed that returns within this time re-arms at once, without
// the neutral-stick check (match dropouts). Longer outages need neutral sticks.
// 0 = always require neutral.
#ifndef ARM_RESUME_MS
#define ARM_RESUME_MS           1000
#endif

// --- Motors ----------------------------------------------------------------------
#ifndef MOTOR_PWM_HZ
#define MOTOR_PWM_HZ            20000   // DRV8231A accepts 0..200 kHz
#endif

// 1 = drive/brake PWM (slow decay). TI recommends it, and IPROPI senses the
//     current in both phases. 0 = drive/coast PWM (fast decay); IPROPI reads
//     zero during the coast phase.
#ifndef MOTOR_SLOW_DECAY
#define MOTOR_SLOW_DECAY        1
#endif

// Inside the deadband while armed: 1 = brake (both low-sides on), 0 = coast
// (driver sleeps after ~1 ms).
#ifndef MOTOR_NEUTRAL_BRAKE
#define MOTOR_NEUTRAL_BRAKE     0
#endif

// Disarmed and failsafe: 1 = brake both bridges (robot stops fastest), 0 = coast.
#ifndef MOTOR_DISARMED_BRAKE
#define MOTOR_DISARMED_BRAKE    1
#endif

#ifndef MOTOR_A_REVERSED
#define MOTOR_A_REVERSED        0
#endif
#ifndef MOTOR_B_REVERSED
#define MOTOR_B_REVERSED        0
#endif

// Forward = IN1 high = current from the "A" pad to the "B" pad (M1A -> M1B, M2A -> M2B).
// Set *_REVERSED instead of re-soldering a motor that turns the wrong way.

// Which driver is the left side of the robot (0 = motor A / U2, 1 = motor B / U3).
#ifndef MOTOR_LEFT
#define MOTOR_LEFT              0
#endif

// --- PWM output pads -------------------------------------------------------------
// Pulse width sent while disarmed or in failsafe. 0 = no pulses (most ESCs disarm).
#ifndef SERVO_FAILSAFE_US
#define SERVO_FAILSAFE_US       0
#endif

// --- Watchdog --------------------------------------------------------------------
#ifndef WATCHDOG_MS
#define WATCHDOG_MS             250
#endif

// --- Bring-up limits -------------------------------------------------------------
// Motor test mode: highest duty (permille) and how long one key press runs the motor.
#ifndef MOTOR_TEST_MAX_PERMILLE
#define MOTOR_TEST_MAX_PERMILLE 1000
#endif
#ifndef MOTOR_TEST_RUN_MS
#define MOTOR_TEST_RUN_MS       3000
#endif

// Thermal test: hard stop after this long, or after this many thermal shutdowns.
#ifndef THERMAL_TEST_MAX_S
#define THERMAL_TEST_MAX_S      120
#endif
#ifndef THERMAL_TEST_MAX_TSD
#define THERMAL_TEST_MAX_TSD    3
#endif
// Abort if the supply sags below this (bench supply in current limit, flat pack).
#ifndef THERMAL_TEST_MIN_VBAT_MV
#define THERMAL_TEST_MIN_VBAT_MV 5500
#endif

// IMU SPI clock (LSM6DSV32X max 10 MHz).
#ifndef IMU_SPI_HZ
#define IMU_SPI_HZ              8000000
#endif
