// SPDX-License-Identifier: GPL-3.0-or-later
#include "sense.h"

#include "pins.h"

#include "hardware/adc.h"

// ADC_AVDD is +3V3 (ME6211); readings scale with the actual rail.
#define ADC_VREF_MV 3300u
#define ADC_FULL    4095u

void sense_init(void) {
    adc_init();
    adc_gpio_init(PIN_MA_IPROPI);
    adc_gpio_init(PIN_MB_IPROPI);
    adc_gpio_init(PIN_VBAT_SENSE);
    adc_set_temp_sensor_enabled(true);
}

uint16_t sense_raw(unsigned channel) {
    adc_select_input(channel);
    return adc_read();
}

uint32_t sense_raw_to_mv(uint16_t raw) {
    return (uint32_t)raw * ADC_VREF_MV / ADC_FULL;
}

uint32_t sense_raw_to_vbat_mv(uint16_t raw) {
    return (uint32_t)((uint64_t)raw * ADC_VREF_MV * (VBAT_DIV_TOP_OHM + VBAT_DIV_BOT_OHM) /
                      ((uint64_t)ADC_FULL * VBAT_DIV_BOT_OHM));
}

uint32_t sense_raw_to_motor_ma(uint16_t raw) {
    // I = V / (A_IPROPI * R_IPROPI) = V / 2.25 V/A; full scale 3.3 V = 1467 mA
    uint64_t uv = (uint64_t)raw * ADC_VREF_MV * 1000u / ADC_FULL;
    return (uint32_t)(uv * 1000u / ((uint64_t)DRV_AIPROPI_UA_PER_A * DRV_RIPROPI_OHM));
}

uint32_t sense_vbat_mv(void) {
    return sense_raw_to_vbat_mv(sense_raw(ADC_CH_VBAT));
}

uint32_t sense_motor_ma(motor_id_t m) {
    return sense_raw_to_motor_ma(sense_raw(m == MOTOR_A ? ADC_CH_MA_IPROPI : ADC_CH_MB_IPROPI));
}

float sense_mcu_temp_c(void) {
    // RP2350 datasheet: T = 27 - (V - 0.706) / 0.001721
    float v = (float)sense_raw(ADC_TEMPERATURE_CHANNEL_NUM) * 3.3f / 4096.0f;
    return 27.0f - (v - 0.706f) / 0.001721f;
}
