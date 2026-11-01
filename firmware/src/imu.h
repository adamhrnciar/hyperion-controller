// SPDX-License-Identifier: GPL-3.0-or-later
// LSM6DSV32X 6-axis IMU on SPI0 (U6). Register map checked against ST's
// lsm6dsv32x-pid driver (lsm6dsv32x_reg.h).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LSM6DSV32X_WHO_AM_I_VALUE 0x70

typedef struct {
    int16_t gyro[3];    // x, y, z raw
    int16_t accel[3];   // x, y, z raw
    int16_t temp;       // raw
} imu_raw_t;

// SPI mode 3, soft reset, disable I2C/I3C, BDU on,
// accel 960 Hz +-32 g, gyro 960 Hz +-2000 dps, gyro data-ready on INT1.
// Returns false if WHO_AM_I does not read 0x70 or the reset does not complete.
bool imu_init(void);

uint8_t imu_who_am_i(void);
bool imu_data_ready(void);      // INT1 pin high (gyro data ready)
bool imu_read(imu_raw_t *out);  // burst read of temperature, gyro, accel

uint8_t imu_read_reg(uint8_t reg);
void imu_write_reg(uint8_t reg, uint8_t val);

// Conversions for the configured full scales.
static inline float imu_accel_mg(int16_t raw) { return (float)raw * 0.976f; }   // +-32 g
static inline float imu_gyro_dps(int16_t raw) { return (float)raw * 0.070f; }   // +-2000 dps
static inline float imu_temp_c(int16_t raw) { return (float)raw / 256.0f + 25.0f; }
