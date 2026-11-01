// SPDX-License-Identifier: GPL-3.0-or-later
#include "arming.h"

void arming_init(arming_t *a, uint32_t hold_ms, uint32_t resume_ms) {
    a->state = ARM_NO_LINK;
    a->hold_ms = hold_ms;
    a->resume_ms = resume_ms;
    a->neutral_since_ms = 0;
    a->lost_at_ms = 0;
    a->neutral_timing = false;
    a->resume_pending = false;
}

arm_state_t arming_update(arming_t *a, const arming_input_t *in, uint32_t now_ms) {
    if (!in->link_ok) {
        if (a->state == ARM_ARMED) {
            a->resume_pending = true;
            a->lost_at_ms = now_ms;
        }
        a->state = ARM_NO_LINK;
        a->neutral_timing = false;
        return a->state;
    }
    if (!in->arm_switch) {
        a->state = ARM_SWITCH_OFF;
        a->neutral_timing = false;
        a->resume_pending = false;
        return a->state;
    }
    if (a->state == ARM_ARMED) {
        return a->state;
    }
    if (a->resume_pending) {
        a->resume_pending = false;
        if ((uint32_t)(now_ms - a->lost_at_ms) < a->resume_ms) {
            a->state = ARM_ARMED;
            return a->state;
        }
    }

    if (!in->sticks_neutral) {
        a->state = ARM_WAIT_NEUTRAL;
        a->neutral_timing = false;
        return a->state;
    }
    if (!a->neutral_timing) {
        a->neutral_timing = true;
        a->neutral_since_ms = now_ms;
    }
    a->state = (uint32_t)(now_ms - a->neutral_since_ms) >= a->hold_ms ? ARM_ARMED : ARM_WAIT_NEUTRAL;
    return a->state;
}

const char *arming_state_name(arm_state_t s) {
    switch (s) {
    case ARM_NO_LINK:      return "NO_LINK";
    case ARM_SWITCH_OFF:   return "SWITCH_OFF";
    case ARM_WAIT_NEUTRAL: return "WAIT_NEUTRAL";
    case ARM_ARMED:        return "ARMED";
    }
    return "?";
}
