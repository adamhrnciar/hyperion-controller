// SPDX-License-Identifier: GPL-3.0-or-later
// Normal firmware loop: receiver -> arming/failsafe -> tank mix -> motors and PWM pads.
// Never returns. verbose = log every arming transition (failsafe test mode).

#pragma once

#include <stdbool.h>

void drive_run(bool verbose);
