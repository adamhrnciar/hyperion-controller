// SPDX-License-Identifier: GPL-3.0-or-later
// Hyperion v1.2 firmware entry point. HYPERION_MODE (set per executable by
// CMake) picks the normal drive firmware or one of the bring-up test modes.

#include <stdio.h>

#include "bringup.h"
#include "config.h"
#include "drive.h"
#include "led.h"
#include "motor.h"
#include "sense.h"
#include "servo_out.h"

#include "hardware/watchdog.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

static const char *mode_name(void) {
    switch (HYPERION_MODE) {
    case HYPERION_MODE_DRIVE:    return "drive";
    case HYPERION_MODE_BLINK:    return "blink";
    case HYPERION_MODE_ADC:      return "adc";
    case HYPERION_MODE_RC:       return "rc";
    case HYPERION_MODE_IMU:      return "imu";
    case HYPERION_MODE_MOTOR:    return "motor";
    case HYPERION_MODE_FAILSAFE: return "failsafe";
    case HYPERION_MODE_THERMAL:  return "thermal";
    }
    return "?";
}

int main(void) {
    // Drive both H-bridge inputs low before anything else can fail.
    motor_init();
    led_init();
    stdio_init_all();
    sense_init();

    bool watchdog_reset = watchdog_enable_caused_reboot();
    watchdog_enable(WATCHDOG_MS, true);

    // Bring-up modes wait up to 2 s for a USB terminal so the banner is not lost.
    if (HYPERION_MODE != HYPERION_MODE_DRIVE) {
        for (int i = 0; i < 2000 && !stdio_usb_connected(); i++) {
            watchdog_update();
            sleep_ms(1);
        }
    }
    printf("\nHyperion v1.2 firmware, mode %s, built " __DATE__ " " __TIME__ "\n", mode_name());
    if (watchdog_reset) {
        printf("*** reset by WATCHDOG ***\n");
    }

    switch (HYPERION_MODE) {
    case HYPERION_MODE_DRIVE:
        servo_out_init();
        drive_run(false);
        break;
    case HYPERION_MODE_FAILSAFE:
        servo_out_init();
        drive_run(true);
        break;
    case HYPERION_MODE_BLINK:   mode_blink();   break;
    case HYPERION_MODE_ADC:     mode_adc();     break;
    case HYPERION_MODE_RC:      mode_rc();      break;
    case HYPERION_MODE_IMU:     mode_imu();     break;
    case HYPERION_MODE_MOTOR:   mode_motor();   break;
    case HYPERION_MODE_THERMAL: mode_thermal(); break;
    }
    return 0;
}
