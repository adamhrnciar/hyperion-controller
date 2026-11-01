// SPDX-License-Identifier: GPL-3.0-or-later
// ADC readings: battery voltage, DRV8231A IPROPI currents, RP2354A die temperature.

#pragma once

#include <stdint.h>

#include "motor.h"

void sense_init(void);

uint16_t sense_raw(unsigned channel);       // one 12-bit conversion
uint32_t sense_vbat_mv(void);
uint32_t sense_motor_ma(motor_id_t m);  // low-side current; 0 while coasting
float sense_mcu_temp_c(void);

// Conversions, exposed for logging raw counts.
uint32_t sense_raw_to_mv(uint16_t raw);
uint32_t sense_raw_to_vbat_mv(uint16_t raw);
uint32_t sense_raw_to_motor_ma(uint16_t raw);
