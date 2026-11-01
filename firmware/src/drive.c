// SPDX-License-Identifier: GPL-3.0-or-later
#include "drive.h"

#include <stdio.h>

#include "config.h"
#include "core/arming.h"
#include "core/mixer.h"
#include "led.h"
#include "motor.h"
#include "rc_input.h"
#include "sense.h"
#include "servo_out.h"

#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#define LOOP_PERIOD_US 1000

// A configured arm switch that the receiver does not send counts as off.
static bool arm_switch_on(const rc_frame_t *f) {
#if RC_CH_ARM < 0
    (void)f;
    return true;
#else
    return RC_CH_ARM < f->count && f->us[RC_CH_ARM] >= RC_ARM_THRESHOLD_US;
#endif
}

// PWM input carries only CH1/CH2; the other channels are filler, never sent to the weapon.
static bool weapon_present(const rc_frame_t *f) {
    return RC_CH_WEAPON < f->count;
}

static bool weapon_low(const rc_frame_t *f) {
    return !weapon_present(f) || f->us[RC_CH_WEAPON] <= RC_WEAPON_ARM_MAX_US;
}

void drive_run(bool verbose) {
    arming_t arm;
    arming_init(&arm, ARM_NEUTRAL_HOLD_MS, ARM_RESUME_MS);
    arm_state_t prev = arm.state;
    uint32_t last_status_ms = 0;
    uint32_t last_good_frame_ms = 0;
    bool link_was_ok = false;

    rc_input_init();
    printf("drive: receiver %s, timeout %d ms, neutral hold %d ms, resume window %d ms\n",
           rc_input_protocol_name(), RC_TIMEOUT_MS, ARM_NEUTRAL_HOLD_MS, ARM_RESUME_MS);

    while (true) {
        uint32_t loop_start = time_us_32();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        watchdog_update();

        rc_frame_t f;
        uint32_t age_ms;
        bool have = rc_input_latest(&f, &age_ms);
        bool link_ok = have && !f.failsafe && age_ms <= RC_TIMEOUT_MS;
        if (link_ok) {
            last_good_frame_ms = now - age_ms;
        }

        int16_t thr = 0, steer = 0;
        if (link_ok) {
            thr = mixer_us_to_cmd(f.us[RC_CH_THROTTLE], RC_DEADBAND_US);
            steer = mixer_us_to_cmd(f.us[RC_CH_STEER], RC_DEADBAND_US);
        }
        arming_input_t in = {
            .link_ok = link_ok,
            .sticks_neutral = thr == 0 && steer == 0 && weapon_low(&f),
            .arm_switch = link_ok && arm_switch_on(&f),
        };
        arming_update(&arm, &in, now);
        motor_arm(arming_motors_enabled(&arm));

        if (arming_motors_enabled(&arm)) {
            int16_t left, right;
            mixer_tank(thr, steer, &left, &right);
            motor_set(MOTOR_LEFT == 0 ? MOTOR_A : MOTOR_B, left);
            motor_set(MOTOR_LEFT == 0 ? MOTOR_B : MOTOR_A, right);
            servo_out_set_us(0, weapon_present(&f) ? f.us[RC_CH_WEAPON] : SERVO_FAILSAFE_US);
        } else {
            servo_out_off_all();    // motors already braking via motor_arm(false)
        }

        led_show_arm_state(arm.state, now);

        if (verbose && arm.state != prev) {
            // Time from the last good frame to this transition = failsafe reaction time.
            printf("%10lu ms  %-12s -> %-12s  frame age %lu ms, since last good frame %lu ms, %s\n",
                   (unsigned long)now, arming_state_name(prev), arming_state_name(arm.state),
                   (unsigned long)(have ? age_ms : 0),
                   (unsigned long)(now - last_good_frame_ms),
                   !have ? "no frame yet" : f.failsafe ? "receiver failsafe flag" :
                   age_ms > RC_TIMEOUT_MS ? "timeout" : "link ok");
        }
        if (verbose && link_ok != link_was_ok) {
            printf("%10lu ms  link %s\n", (unsigned long)now, link_ok ? "UP" : "DOWN");
        }
        prev = arm.state;
        link_was_ok = link_ok;

        // Failsafe test: 'w' hangs the loop with the motors running; the watchdog
        // must reset the MCU within WATCHDOG_MS and the bridges must coast.
        if (verbose && getchar_timeout_us(0) == 'w') {
            printf("%10lu ms  hanging main loop, expect watchdog reset in %d ms\n",
                   (unsigned long)now, WATCHDOG_MS);
            while (true) {
                tight_loop_contents();
            }
        }

        if (now - last_status_ms >= (verbose ? 200u : 1000u)) {
            last_status_ms = now;
            rc_stats_t st;
            rc_input_stats(&st);
            printf("%10lu ms  %-12s thr %5d steer %5d  A %4lu mA  B %4lu mA  VBAT %5lu mV  "
                   "frames %lu err %lu\n",
                   (unsigned long)now, arming_state_name(arm.state), thr, steer,
                   (unsigned long)sense_motor_ma(MOTOR_A), (unsigned long)sense_motor_ma(MOTOR_B),
                   (unsigned long)sense_vbat_mv(), (unsigned long)st.frames,
                   (unsigned long)st.errors);
        }

        while (time_us_32() - loop_start < LOOP_PERIOD_US) {
            tight_loop_contents();
        }
    }
}
