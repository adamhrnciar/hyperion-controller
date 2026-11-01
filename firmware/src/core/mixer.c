// SPDX-License-Identifier: GPL-3.0-or-later
#include "mixer.h"

#include <stdlib.h>

#define HALF_RANGE_US 500

int16_t mixer_us_to_cmd(uint16_t us, uint16_t deadband_us) {
    int32_t d = (int32_t)us - 1500;
    int32_t mag = labs(d);
    if (mag <= deadband_us) {
        return 0;
    }
    int32_t cmd = (mag - deadband_us) * CMD_MAX / (HALF_RANGE_US - deadband_us);
    if (cmd > CMD_MAX) {
        cmd = CMD_MAX;
    }
    return (int16_t)(d < 0 ? -cmd : cmd);
}

void mixer_tank(int16_t throttle, int16_t steer, int16_t *left, int16_t *right) {
    int32_t l = (int32_t)throttle + steer;
    int32_t r = (int32_t)throttle - steer;
    int32_t peak = labs(l) > labs(r) ? labs(l) : labs(r);
    if (peak > CMD_MAX) {
        l = l * CMD_MAX / peak;
        r = r * CMD_MAX / peak;
    }
    *left = (int16_t)l;
    *right = (int16_t)r;
}
