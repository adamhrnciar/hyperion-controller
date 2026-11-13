// SPDX-License-Identifier: GPL-3.0-or-later
// Stick scaling and tank mixing. Portable, no SDK dependency.
// Motor commands are permille: -1000 (full reverse) .. 0 .. +1000 (full forward).

#pragma once

#include <stdint.h>

#define CMD_MAX 1000

// 1000..2000 us -> -1000..1000 with a deadband around 1500 us. Out-of-range input is clamped.
int16_t mixer_us_to_cmd(uint16_t us, uint16_t deadband_us);

// left = throttle + steer, right = throttle - steer. If either side exceeds
// CMD_MAX, both are scaled down together so the turn ratio is kept.
void mixer_tank(int16_t throttle, int16_t steer, int16_t *left, int16_t *right);
