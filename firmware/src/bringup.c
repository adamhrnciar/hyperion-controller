// SPDX-License-Identifier: GPL-3.0-or-later
#include "bringup.h"

#include <math.h>
#include <stdio.h>

#include "config.h"
#include "imu.h"
#include "led.h"
#include "motor.h"
#include "pins.h"
#include "rc_input.h"
#include "sense.h"

#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static int read_key(void) {
    int c = getchar_timeout_us(0);
    return c == PICO_ERROR_TIMEOUT ? -1 : c;
}

// Wait for one key, keeping the watchdog fed. Returns the key.
static int wait_key(void) {
    int c;
    while ((c = read_key()) < 0) {
        watchdog_update();
        sleep_ms(1);
    }
    return c;
}

static uint16_t sense_avg(unsigned channel, int n) {
    uint32_t sum = 0;
    for (int i = 0; i < n; i++) {
        sum += sense_raw(channel);
    }
    return (uint16_t)(sum / (uint32_t)n);
}

// --- blink -------------------------------------------------------------------

void mode_blink(void) {
    static const uint pads[] = {PIN_PWM1, PIN_PWM2, PIN_PWM3, PIN_PWM4};
    for (int i = 0; i < 4; i++) {
        gpio_init(pads[i]);
        gpio_set_dir(pads[i], GPIO_OUT);
    }
    printf("blink: green LED 1 Hz. PWM1..PWM4 pads toggle at 1, 2, 4, 8 Hz (3.3 V square wave).\n");

    uint32_t last_print = 0;
    while (true) {
        watchdog_update();
        uint32_t t = now_ms();
        led_set((t / 500) % 2);
        for (int i = 0; i < 4; i++) {
            gpio_put(pads[i], (t / (500u >> i)) % 2);
        }
        if (t - last_print >= 1000) {
            last_print = t;
            printf("%8lu ms alive  VBAT %lu mV  MCU %.1f C\n", (unsigned long)t,
                   (unsigned long)sense_vbat_mv(), (double)sense_mcu_temp_c());
        }
        sleep_ms(1);
    }
}

// --- adc ---------------------------------------------------------------------

void mode_adc(void) {
    printf("adc: 5 Hz, 64-sample averages. Compare VBAT with a DMM on the BAT+ pad.\n");
    printf("     IPROPI reads 0 mA with motors idle; 3300 mV on IPROPI = 1467 mA = current limit.\n");
    while (true) {
        watchdog_update();
        uint16_t vb = sense_avg(ADC_CH_VBAT, 64);
        uint16_t ia = sense_avg(ADC_CH_MA_IPROPI, 64);
        uint16_t ib = sense_avg(ADC_CH_MB_IPROPI, 64);
        printf("VBAT raw %4u = %5lu mV (pin %4lu mV) | IPROPI A raw %4u = %4lu mA | "
               "IPROPI B raw %4u = %4lu mA | MCU %.1f C\n",
               vb, (unsigned long)sense_raw_to_vbat_mv(vb), (unsigned long)sense_raw_to_mv(vb),
               ia, (unsigned long)sense_raw_to_motor_ma(ia),
               ib, (unsigned long)sense_raw_to_motor_ma(ib), (double)sense_mcu_temp_c());
        for (int i = 0; i < 200; i++) {
            watchdog_update();
            sleep_ms(1);
        }
    }
}

// --- rc ----------------------------------------------------------------------

void mode_rc(void) {
    rc_input_init();
    printf("rc: receiver %s. Motors stay off. 5 Hz output.\n", rc_input_protocol_name());

    rc_stats_t prev = {0};
    uint32_t last = now_ms();
    while (true) {
        watchdog_update();
        uint32_t t = now_ms();
        rc_frame_t f;
        uint32_t age;
        bool have = rc_input_latest(&f, &age);
        bool ok = have && !f.failsafe && age <= RC_TIMEOUT_MS;
        led_set(ok ? (t / 500) % 2 : (t / 100) % 2);

        if (t - last >= 200) {
            rc_stats_t st;
            rc_input_stats(&st);
            uint32_t fps = (st.frames - prev.frames) * 1000u / (t - last);
            printf("%-4s %3lu fr/s err+%-3lu age %4ld ms fs %d lost %d | ", ok ? "OK" : "LOST",
                   (unsigned long)fps, (unsigned long)(st.errors - prev.errors),
                   have ? (long)age : -1L, have && f.failsafe, have && f.frame_lost);
            for (int i = 0; i < 8; i++) {
                printf("CH%d %4u ", i + 1, have ? f.us[i] : 0);
            }
            printf("\n");
            prev = st;
            last = t;
        }
        sleep_ms(1);
    }
}

// --- imu ---------------------------------------------------------------------

void mode_imu(void) {
    printf("imu: LSM6DSV32X on SPI0 at %d Hz\n", IMU_SPI_HZ);
    while (!imu_init()) {
        printf("imu: init FAILED, WHO_AM_I = 0x%02X (expect 0x%02X). Retrying.\n",
               imu_who_am_i(), LSM6DSV32X_WHO_AM_I_VALUE);
        for (int i = 0; i < 1000; i++) {
            watchdog_update();
            sleep_ms(1);
        }
    }
    printf("imu: WHO_AM_I 0x%02X OK. Keep the board still for 1 s (gyro bias).\n", imu_who_am_i());

    imu_raw_t d = {0};
    int32_t bias_sum[3] = {0};
    int n = 0;
    uint32_t t0 = now_ms();
    while (now_ms() - t0 < 1000) {
        watchdog_update();
        if (imu_data_ready()) {
            imu_read(&d);
            for (int i = 0; i < 3; i++) {
                bias_sum[i] += d.gyro[i];
            }
            n++;
        }
    }
    if (n == 0) {
        printf("imu: no data-ready on INT1 (GPIO20) in 1 s. Check INT1 routing.\n");
        n = 1;
    }
    float bias[3];
    for (int i = 0; i < 3; i++) {
        bias[i] = imu_gyro_dps((int16_t)(bias_sum[i] / n));
    }
    printf("imu: %d samples in 1 s (expect ~960). Gyro bias %.2f %.2f %.2f dps\n",
           n, (double)bias[0], (double)bias[1], (double)bias[2]);

    uint32_t last = now_ms();
    uint32_t samples = 0;
    while (true) {
        watchdog_update();
        if (imu_data_ready()) {
            imu_read(&d);
            samples++;
        }
        uint32_t t = now_ms();
        if (t - last >= 100) {
            float ax = imu_accel_mg(d.accel[0]), ay = imu_accel_mg(d.accel[1]),
                  az = imu_accel_mg(d.accel[2]);
            float mag = sqrtf(ax * ax + ay * ay + az * az);
            printf("acc %7.0f %7.0f %7.0f mg |a| %5.0f mg  gyro %7.2f %7.2f %7.2f dps  "
                   "T %.1f C  %lu Hz\n",
                   (double)ax, (double)ay, (double)az, (double)mag,
                   (double)imu_gyro_dps(d.gyro[0]), (double)imu_gyro_dps(d.gyro[1]),
                   (double)imu_gyro_dps(d.gyro[2]), (double)imu_temp_c(d.temp),
                   (unsigned long)(samples * 1000u / (t - last)));
            samples = 0;
            last = t;
        }
    }
}

// --- motor -------------------------------------------------------------------

static void motor_help(void) {
    printf("\nmotor test. Keys:\n"
           "  a / b   select motor A (U2, M1A/M1B) or B (U3, M2A/M2B)\n"
           "  f / r   run forward / reverse for %d ms (press again to extend)\n"
           "  + / -   duty +/- 5 %% (max %d permille)\n"
           "  d       toggle decay: slow (drive/brake) / fast (drive/coast)\n"
           "  x       brake    space  coast (stop)    h  help\n"
           "Forward = IN1 high = current OUT1 -> OUT2.\n\n",
           MOTOR_TEST_RUN_MS, MOTOR_TEST_MAX_PERMILLE);
}

void mode_motor(void) {
    motor_id_t ch = MOTOR_A;
    int16_t duty = 100;
    bool slow = MOTOR_SLOW_DECAY;
    int dir = 0;
    uint32_t run_until = 0;
    uint32_t last_print = 0;

    motor_help();
    motor_arm(true);
    while (true) {
        watchdog_update();
        uint32_t t = now_ms();
        int c = read_key();
        switch (c) {
        case 'a': case 'b':
            motor_coast_all();
            dir = 0;
            ch = c == 'a' ? MOTOR_A : MOTOR_B;
            printf("motor %c selected\n", c == 'a' ? 'A' : 'B');
            break;
        case 'f': case 'r':
            dir = c == 'f' ? 1 : -1;
            run_until = t + MOTOR_TEST_RUN_MS;
            break;
        case '+':
            duty = (int16_t)(duty + 50 > MOTOR_TEST_MAX_PERMILLE ? MOTOR_TEST_MAX_PERMILLE : duty + 50);
            printf("duty %d permille\n", duty);
            break;
        case '-':
            duty = (int16_t)(duty - 50 < 50 ? 50 : duty - 50);
            printf("duty %d permille\n", duty);
            break;
        case 'd':
            slow = !slow;
            printf("decay %s\n", slow ? "slow (drive/brake)" : "fast (drive/coast)");
            break;
        case 'x':
            dir = 0;
            motor_brake(ch);
            printf("brake\n");
            break;
        case ' ':
            dir = 0;
            motor_coast_all();
            printf("coast\n");
            break;
        case 'h': case '?':
            motor_help();
            break;
        default:
            break;
        }

        if (dir != 0 && (int32_t)(t - run_until) >= 0) {
            dir = 0;
            motor_coast_all();
            printf("auto stop\n");
        }
        if (dir != 0) {
            motor_set_decay(ch, (int16_t)(dir * duty), slow);
        }
        led_set(dir != 0);

        if (t - last_print >= (dir != 0 ? 100u : 1000u)) {
            last_print = t;
            printf("%c %-4s duty %4d %s  IA %4lu mA  IB %4lu mA  VBAT %5lu mV\n",
                   ch == MOTOR_A ? 'A' : 'B', dir > 0 ? "FWD" : dir < 0 ? "REV" : "OFF",
                   duty, slow ? "slow" : "fast",
                   (unsigned long)sense_motor_ma(MOTOR_A), (unsigned long)sense_motor_ma(MOTOR_B),
                   (unsigned long)sense_vbat_mv());
        }
        sleep_ms(1);
    }
}

// --- thermal -----------------------------------------------------------------
//
// Drives the selected channel(s) at 100 % forward into a locked rotor and
// watches IPROPI. The DRV8231A has no fault pin: thermal shutdown shows up as
// the low-side current collapsing to ~0 while the inputs still command drive.
// A drop below max(100 mA, stall/4) for 20 ms counts as a TSD event; the 3 ms
// OCP retry and the 25 us current-chopping off time are far shorter than that.

#define TSD_LOW_MS      20
#define STALL_AVG_FROM  100     // ms after start: skip inrush
#define STALL_AVG_TO    600

typedef struct {
    bool active;
    bool tripped;           // currently in TSD
    float filt_ma;
    uint32_t stall_sum, stall_n, stall_ma;
    uint32_t low_ms;
    uint32_t tsd_count;
    uint32_t first_tsd_ms;
    uint32_t trip_start_ms;
    uint32_t on_ms;
} thermal_ch_t;

static void thermal_run(bool use_a, bool use_b) {
    thermal_ch_t ch[MOTOR_COUNT] = {{.active = use_a}, {.active = use_b}};
    printf("t_ms,motor,state,i_ma,vbat_mv,mcu_c,tsd_count\n");

    motor_arm(true);
    for (int m = 0; m < MOTOR_COUNT; m++) {
        if (ch[m].active) {
            motor_set_decay((motor_id_t)m, 1000, true);
        }
    }

    uint32_t start = now_ms();
    uint32_t last_ms = 0, last_log = 0;
    const char *stop_reason = "time limit";
    while (true) {
        watchdog_update();
        uint32_t t = now_ms() - start;
        if (read_key() == ' ') {
            stop_reason = "aborted by user";
            break;
        }
        if (t >= THERMAL_TEST_MAX_S * 1000u) {
            break;
        }
        if (t == last_ms) {
            continue;
        }
        uint32_t dt = t - last_ms;    // normally 1; more if a log line blocked
        last_ms = t;

        uint32_t vbat = sense_vbat_mv();
        if (t > 50 && vbat < THERMAL_TEST_MIN_VBAT_MV) {
            stop_reason = "VBAT below limit (supply in current limit?)";
            break;
        }

        bool all_done = true;
        for (int m = 0; m < MOTOR_COUNT; m++) {
            thermal_ch_t *c = &ch[m];
            if (!c->active) {
                continue;
            }
            float i = (float)sense_motor_ma((motor_id_t)m);
            c->filt_ma += (i - c->filt_ma) / 8.0f;      // 1 kHz IIR, ~8 ms
            if (t >= STALL_AVG_FROM && t < STALL_AVG_TO) {
                c->stall_sum += (uint32_t)c->filt_ma;
                c->stall_n++;
                c->stall_ma = c->stall_sum / c->stall_n;
            }
            uint32_t thresh = c->stall_ma / 4 > 100 ? c->stall_ma / 4 : 100;
            if (!c->tripped) {
                c->on_ms += dt;
                c->low_ms = c->filt_ma < (float)thresh ? c->low_ms + dt : 0;
                if (c->low_ms >= TSD_LOW_MS && t >= STALL_AVG_FROM) {
                    c->tripped = true;
                    c->tsd_count++;
                    c->trip_start_ms = t - c->low_ms;
                    if (c->tsd_count == 1) {
                        c->first_tsd_ms = c->trip_start_ms;
                    }
                    printf("# motor %c TSD #%lu at %lu ms (stall avg %lu mA)\n", 'A' + m,
                           (unsigned long)c->tsd_count, (unsigned long)c->trip_start_ms,
                           (unsigned long)c->stall_ma);
                }
            } else if (c->filt_ma > (float)(2 * thresh)) {
                c->tripped = false;
                c->low_ms = 0;
                printf("# motor %c recovered after %lu ms off\n", 'A' + m,
                       (unsigned long)(t - c->trip_start_ms));
            }
            if (c->tsd_count < THERMAL_TEST_MAX_TSD) {
                all_done = false;
            }
        }
        if (all_done) {
            stop_reason = "TSD count limit";
            break;
        }

        if (t - last_log >= 100) {
            last_log = t;
            float mcu = sense_mcu_temp_c();
            for (int m = 0; m < MOTOR_COUNT; m++) {
                if (ch[m].active) {
                    printf("%lu,%c,%s,%.0f,%lu,%.1f,%lu\n", (unsigned long)t, 'A' + m,
                           ch[m].tripped ? "TSD" : "ON", (double)ch[m].filt_ma,
                           (unsigned long)vbat, (double)mcu, (unsigned long)ch[m].tsd_count);
                }
            }
        }
    }

    motor_arm(false);
    motor_coast_all();
    uint32_t total = now_ms() - start;
    printf("# stopped: %s after %lu ms\n", stop_reason, (unsigned long)total);
    for (int m = 0; m < MOTOR_COUNT; m++) {
        thermal_ch_t *c = &ch[m];
        if (!c->active) {
            continue;
        }
        printf("# motor %c: stall %lu mA, first TSD ", 'A' + m, (unsigned long)c->stall_ma);
        if (c->tsd_count) {
            printf("%lu ms", (unsigned long)c->first_tsd_ms);
        } else {
            printf("none");
        }
        printf(", TSD events %lu, on-time %lu %%\n", (unsigned long)c->tsd_count,
               (unsigned long)(total ? c->on_ms * 100u / total : 0));
        if (c->stall_ma < 300) {
            printf("# motor %c: stall current < 300 mA. Rotor not locked, or motor resistance "
                   "too high to reach the 1.47 A limit at this voltage.\n", 'A' + m);
        }
    }
}

void mode_thermal(void) {
    while (true) {
        printf("\nthermal test: 100 %% forward into a LOCKED rotor, max %d s or %d TSD events.\n"
               "Clamp the motor shaft. Thermocouple or IR camera on U2/U3. Space aborts.\n"
               "Select: a = motor A, b = motor B, 2 = both. ", THERMAL_TEST_MAX_S,
               THERMAL_TEST_MAX_TSD);
        int sel = wait_key();
        if (sel != 'a' && sel != 'b' && sel != '2') {
            printf("\n");
            continue;
        }
        printf("%c\nVBAT %lu mV. Start? (y/n) ", sel, (unsigned long)sense_vbat_mv());
        if (wait_key() != 'y') {
            printf("cancelled\n");
            continue;
        }
        printf("y\n");
        thermal_run(sel == 'a' || sel == '2', sel == 'b' || sel == '2');
    }
}
