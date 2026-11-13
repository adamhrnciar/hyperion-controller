// SPDX-License-Identifier: GPL-3.0-or-later
// Arming / failsafe state machine. Portable, no SDK dependency.
//
// Motors may only run in ARM_ARMED. Rules:
//   - no valid link (timeout or receiver failsafe flag)  -> ARM_NO_LINK, motors off at once
//   - arm switch off                                     -> ARM_SWITCH_OFF, motors off at once
//   - a link that drops while ARMED and comes back within resume_ms re-arms at once, so a
//     short dropout in a match costs only the dropout itself (the motors brake meanwhile)
//   - every other way out of NO_LINK or SWITCH_OFF goes through ARM_WAIT_NEUTRAL: sticks
//     must be neutral for hold_ms before ARM_ARMED. Power-up, a transmitter switched off
//     and on again, or a long dropout with the stick held does not make the robot jump.

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ARM_NO_LINK = 0,
    ARM_SWITCH_OFF,
    ARM_WAIT_NEUTRAL,
    ARM_ARMED,
} arm_state_t;

typedef struct {
    bool link_ok;           // fresh frame within timeout and no receiver failsafe flag
    bool sticks_neutral;    // drive sticks inside the deadband
    bool arm_switch;        // arm switch on (true when no switch is configured)
} arming_input_t;

typedef struct {
    arm_state_t state;
    uint32_t hold_ms;
    uint32_t resume_ms;
    uint32_t neutral_since_ms;
    uint32_t lost_at_ms;
    bool neutral_timing;
    bool resume_pending;    // link was lost while ARMED
} arming_t;

// resume_ms = 0 disables resuming: every reconnect needs neutral sticks.
void arming_init(arming_t *a, uint32_t hold_ms, uint32_t resume_ms);
arm_state_t arming_update(arming_t *a, const arming_input_t *in, uint32_t now_ms);
const char *arming_state_name(arm_state_t s);

static inline bool arming_motors_enabled(const arming_t *a) {
    return a->state == ARM_ARMED;
}
