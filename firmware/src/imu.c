// SPDX-License-Identifier: GPL-3.0-or-later
#include "imu.h"

#include "config.h"
#include "pins.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"

#define REG_IF_CFG      0x03    // bit0 I2C_I3C_disable
#define REG_INT1_CTRL   0x0D    // bit0 DRDY_XL, bit1 DRDY_G
#define REG_WHO_AM_I    0x0F
#define REG_CTRL1       0x10    // [6:4] OP_MODE_XL, [3:0] ODR_XL
#define REG_CTRL2       0x11    // [6:4] OP_MODE_G,  [3:0] ODR_G
#define REG_CTRL3       0x12    // bit7 BOOT, bit6 BDU, bit2 IF_INC, bit0 SW_RESET
#define REG_CTRL6       0x15    // [3:0] FS_G
#define REG_CTRL8       0x17    // [1:0] FS_XL
#define REG_OUT_TEMP_L  0x20    // temp (2), gyro xyz (6), accel xyz (6), consecutive

#define ODR_960HZ       0x09
#define FS_G_2000DPS    0x04
#define FS_XL_32G       0x03
#define CTRL3_SW_RESET  0x01
#define CTRL3_IF_INC    0x04
#define CTRL3_BDU       0x40

// spi_*_blocking return only after the bus is idle, so CS can follow directly.
static inline void cs(bool level) {
    gpio_put(PIN_IMU_CS, level);
}

void imu_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {(uint8_t)(reg & 0x7F), val};
    cs(0);
    spi_write_blocking(IMU_SPI, buf, 2);
    cs(1);
}

static void read_regs(uint8_t reg, uint8_t *dst, size_t len) {
    uint8_t addr = reg | 0x80;
    cs(0);
    spi_write_blocking(IMU_SPI, &addr, 1);
    spi_read_blocking(IMU_SPI, 0x00, dst, len);
    cs(1);
}

uint8_t imu_read_reg(uint8_t reg) {
    uint8_t v;
    read_regs(reg, &v, 1);
    return v;
}

uint8_t imu_who_am_i(void) {
    return imu_read_reg(REG_WHO_AM_I);
}

bool imu_init(void) {
    gpio_init(PIN_IMU_CS);
    gpio_put(PIN_IMU_CS, 1);
    gpio_set_dir(PIN_IMU_CS, GPIO_OUT);

    gpio_init(PIN_IMU_INT1);
    gpio_set_dir(PIN_IMU_INT1, GPIO_IN);
    gpio_pull_down(PIN_IMU_INT1);

    spi_init(IMU_SPI, IMU_SPI_HZ);
    spi_set_format(IMU_SPI, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(PIN_IMU_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_IMU_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_IMU_MOSI, GPIO_FUNC_SPI);

    sleep_ms(10);   // boot time after power-up

    if (imu_who_am_i() != LSM6DSV32X_WHO_AM_I_VALUE) {
        return false;
    }

    // Reset sequence from ST's driver: power down both sensors, set SW_RESET, poll.
    imu_write_reg(REG_CTRL1, 0x00);
    imu_write_reg(REG_CTRL2, 0x00);
    imu_write_reg(REG_CTRL3, CTRL3_SW_RESET);
    int tries = 0;
    while (imu_read_reg(REG_CTRL3) & CTRL3_SW_RESET) {
        if (++tries > 10) {
            return false;
        }
        sleep_ms(1);
    }

    imu_write_reg(REG_IF_CFG, 0x01);                    // SPI only
    imu_write_reg(REG_CTRL3, CTRL3_BDU | CTRL3_IF_INC);
    imu_write_reg(REG_CTRL6, FS_G_2000DPS);
    imu_write_reg(REG_CTRL8, FS_XL_32G);
    imu_write_reg(REG_INT1_CTRL, 0x02);                 // gyro data ready on INT1
    imu_write_reg(REG_CTRL1, ODR_960HZ);                // high-performance mode
    imu_write_reg(REG_CTRL2, ODR_960HZ);

    return imu_read_reg(REG_CTRL8) == FS_XL_32G;
}

bool imu_data_ready(void) {
    return gpio_get(PIN_IMU_INT1);
}

bool imu_read(imu_raw_t *out) {
    uint8_t b[14];
    read_regs(REG_OUT_TEMP_L, b, sizeof(b));
    out->temp = (int16_t)(b[0] | (b[1] << 8));
    for (int i = 0; i < 3; i++) {
        out->gyro[i] = (int16_t)(b[2 + 2 * i] | (b[3 + 2 * i] << 8));
        out->accel[i] = (int16_t)(b[8 + 2 * i] | (b[9 + 2 * i] << 8));
    }
    return true;
}
