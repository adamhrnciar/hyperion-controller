// SPDX-License-Identifier: GPL-3.0-or-later
// Test-side interface of the simulator: run the firmware, drive the pads,
// read back what the board would do.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Virtual time is counted in system-clock ticks (150 MHz).
#define TK_PER_US   150ull
#define US(x)       ((uint64_t)(x) * TK_PER_US)
#define MS(x)       ((uint64_t)(x) * 1000ull * TK_PER_US)

// --- running ----------------------------------------------------------------
void sim_reset(void);
void sim_boot(void (*entry)(void));     // start the firmware; it runs inside sim_run_*
void sim_run_until(uint64_t tk);
void sim_run_ms(uint32_t ms);
uint64_t sim_now(void);
double sim_now_ms(void);
void sim_set_time(uint64_t tk);         // before sim_boot: boot at a later timer value
void sim_set_quantum(uint64_t tk);      // time one busy-wait iteration takes (default 1 us)
bool sim_halted(void);                  // firmware stopped by a watchdog reset
uint64_t sim_watchdog_fired_at(void);   // 0 = never
int sim_faults(void);                   // peripheral misuse seen by the models
void sim_fault(const char *fmt, ...) __attribute__((format(__printf__, 1, 2)));

// Callback at a given time (receiver models use this).
typedef void (*sim_event_fn)(void *arg);
void sim_at(uint64_t tk, sim_event_fn fn, void *arg);

// --- console ----------------------------------------------------------------
extern bool sim_echo;                   // print the firmware log while it runs
void sim_key(int c);                    // key typed on the USB console
// Time (ticks) of the first log line at or after `after` that contains `needle`, 0 if none.
uint64_t sim_log_find(const char *needle, uint64_t after, const char **line);
int sim_log_count(const char *needle);

// --- external signals on pads -----------------------------------------------
enum { LINE_LOW = 0, LINE_HIGH = 1, LINE_HIZ = 2 };
void sim_line(uint32_t pin, uint64_t tk, int level);    // times must not go backwards
// UART waveform on a pad (SBUS: 100000 baud, 8E2, inverted).
uint64_t sim_line_uart(uint32_t pin, uint64_t tk, const uint8_t *data, size_t len, double baud,
                       bool even_parity, int stop_bits, bool inverted);
// Bytes delivered to the UART0 receiver (CRSF); the model checks pin mux and baud rate.
uint64_t sim_uart_rx(uint64_t tk, const uint8_t *data, size_t len, double baud);

// --- observation ------------------------------------------------------------
// H-bridge state over one PWM period, in permille of the period.
typedef struct {
    uint16_t fwd, rev, brake, coast;    // IN1/IN2 = 10, 01, 11, 00
} bridge_t;
bridge_t sim_bridge(int motor);         // right now
typedef struct {
    uint64_t tk;
    int motor;
    bridge_t b;
} bridge_ev_t;
size_t sim_bridge_trace(const bridge_ev_t **ev);

// Servo pad output: pulse width and period in microseconds (0 = no pulses).
typedef struct {
    uint32_t pulse_us, period_us;
} servo_t;
servo_t sim_servo(int idx);
typedef struct {
    uint64_t tk;
    int idx;
    servo_t s;
} servo_ev_t;
size_t sim_servo_trace(const servo_ev_t **ev);

int sim_pad_out(uint32_t pin);          // LINE_LOW / LINE_HIGH / LINE_HIZ as driven by the MCU
bool sim_led(void);

// --- analog and IMU ---------------------------------------------------------
extern uint32_t sim_vbat_mv;            // battery voltage at the BAT+ pad
extern uint32_t sim_motor_ma[2];        // low-side current reported on IPROPI
extern double sim_mcu_temp_c;

typedef struct {
    bool fitted;                        // false = no answer on SPI (MISO floats high)
    double acc_mg[3], gyro_dps[3], temp_c;
    uint32_t reset_us;                  // how long SW_RESET stays set
} sim_imu_t;
extern sim_imu_t sim_imu;
