// SPDX-License-Identifier: GPL-3.0-or-later
// Scenarios run against the unmodified firmware (one binary per mode and
// receiver protocol, see the Makefile). Each scenario runs in its own process.
// They follow BRINGUP.md: what is checked here on the model is checked again
// on the real board there.

#define _GNU_SOURCE
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sim.h"

// The firmware sources are built with printf and main redirected; not this file.
#undef printf
#undef main

#include "../src/config.h"
#include "../src/pins.h"

int firmware_main(void);
static void fw(void) { firmware_main(); }

#pragma GCC diagnostic ignored "-Wunused-function"   // each build uses a subset of the helpers

// --- checks -------------------------------------------------------------------------
static int failures;
static void check(bool ok, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
static void check(bool ok, const char *file, int line, const char *fmt, ...) {
    if (ok) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "  FAIL %s:%d at %.3f ms: ", file, line, sim_now_ms());
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    failures++;
}
#define EXPECT(cond, ...) check((cond), __FILE__, __LINE__, __VA_ARGS__)

static double ms_of(uint64_t tk) { return (double)tk / (double)MS(1); }

static bool near(int a, int b, int tol) { return abs(a - b) <= tol; }

static void expect_bridge(int m, int fwd, int rev, int brake, int coast, const char *what,
                          int line) {
    bridge_t b = sim_bridge(m);
    check(near(b.fwd, fwd, 2) && near(b.rev, rev, 2) && near(b.brake, brake, 2) &&
              near(b.coast, coast, 2),
          __FILE__, line, "%s: motor %c is fwd %u rev %u brake %u coast %u, expected %d %d %d %d",
          what, 'A' + m, b.fwd, b.rev, b.brake, b.coast, fwd, rev, brake, coast);
}
#define BRIDGE(m, f, r, b, c, what) expect_bridge(m, f, r, b, c, what, __LINE__)

// First time at or after `from` when both bridges are fully in the given state. 0 = never.
static uint64_t both_bridges(uint64_t from, bool brake) {
    const bridge_ev_t *ev;
    size_t n = sim_bridge_trace(&ev);
    bridge_t cur[2] = {{0, 0, 0, 1000}, {0, 0, 0, 1000}};
    for (size_t i = 0; i <= n; i++) {
        uint64_t until = i < n ? ev[i].tk : UINT64_MAX;
        bool ok = brake ? cur[0].brake == 1000 && cur[1].brake == 1000
                        : cur[0].coast == 1000 && cur[1].coast == 1000;
        if (ok && until > from) {
            uint64_t since = i ? ev[i - 1].tk : 0;
            return since > from ? since : from;
        }
        if (i < n) {
            cur[ev[i].motor] = ev[i].b;
        }
    }
    return 0;
}

// True if either motor was driven (any forward or reverse time) in [from, to).
static bool driven_between(uint64_t from, uint64_t to) {
    const bridge_ev_t *ev;
    size_t n = sim_bridge_trace(&ev);
    bridge_t cur[2] = {{0, 0, 0, 1000}, {0, 0, 0, 1000}};
    for (size_t i = 0; i <= n; i++) {
        uint64_t until = i < n ? ev[i].tk : UINT64_MAX;
        uint64_t since = i ? ev[i - 1].tk : 0;
        bool on = cur[0].fwd || cur[0].rev || cur[1].fwd || cur[1].rev;
        if (on && until > from && since < to) {
            return true;
        }
        if (i < n) {
            cur[ev[i].motor] = ev[i].b;
        }
    }
    return false;
}

static bool armed_now(void) {
    // ARMED with neutral sticks coasts; disarmed brakes (MOTOR_DISARMED_BRAKE = 1).
    bridge_t a = sim_bridge(0), b = sim_bridge(1);
    return a.brake != 1000 || b.brake != 1000;
}

// --- receiver model -------------------------------------------------------------------
typedef enum {
    FS_NO_PULSES,   // output stops when the radio link is lost
    FS_FLAG,        // output continues: SBUS failsafe flag / CRSF link quality 0
    FS_HOLD,        // output continues with the last values and no flag (worst case)
} rx_fs_t;

static struct {
    bool powered, tx_on;
    bool cut[2];            // PWM: channel wire broken
    rx_fs_t fs;
    uint16_t ch[16];
    uint32_t period_us;
    double baud_scale;
    uint64_t last_end;      // end of the last frame sent with the link up
    uint64_t first_end;
    uint32_t frames;
} rx = {.period_us = 9000, .baud_scale = 1.0};

static uint16_t us_to_raw(uint16_t us) {
    return (uint16_t)lround(((double)us - 880.0) * 8.0 / 5.0);
}

static void pack11(const uint16_t *us, uint8_t *dst) {
    uint32_t bits = 0;
    int nbits = 0;
    for (int i = 0; i < 16; i++) {
        bits |= (uint32_t)(us_to_raw(us[i]) & 0x7FF) << nbits;
        nbits += 11;
        while (nbits >= 8) {
            *dst++ = (uint8_t)bits;
            bits >>= 8;
            nbits -= 8;
        }
    }
}

__attribute__((unused)) static uint8_t crc8_dvb_s2(const uint8_t *d, size_t n) {
    uint8_t crc = 0;
    while (n--) {
        crc ^= *d++;
        for (int i = 0; i < 8; i++) {
            crc = (uint8_t)((crc << 1) ^ ((crc & 0x80) ? 0xD5 : 0));
        }
    }
    return crc;
}

static uint64_t rx_send(uint64_t t, bool link) {
#if RC_PROTOCOL == RC_PROTOCOL_SBUS
    uint8_t f[25] = {0x0F};
    pack11(rx.ch, &f[1]);
    f[23] = link ? 0x00 : (rx.fs == FS_FLAG ? 0x0C : 0x00);
    f[24] = 0x00;
    return sim_line_uart(PIN_RC_RX, t, f, sizeof(f), 100000.0 * rx.baud_scale, true, 2, true);
#elif RC_PROTOCOL == RC_PROTOCOL_CRSF
    // ELRS sends link statistics next to the channel frames.
    uint8_t ls[14] = {0xC8, 12, 0x14, 60, 60, (uint8_t)(link || rx.fs == FS_HOLD ? 100 : 0),
                     10, 0, 4, 0, 60, 100, 10};
    ls[13] = crc8_dvb_s2(&ls[2], 11);
    uint8_t f[26] = {0xC8, 24, 0x16};
    pack11(rx.ch, &f[3]);
    f[25] = crc8_dvb_s2(&f[2], 23);
    double baud = 420000.0 * rx.baud_scale;
    if (rx.frames % 10 == 0 || !link) {
        t = sim_uart_rx(t, ls, sizeof(ls), baud);
        if (!link && rx.fs == FS_NO_PULSES) {
            return t;       // ELRS: channel frames stop, link statistics show LQ 0
        }
    }
    return sim_uart_rx(t, f, sizeof(f), baud);
#else
    // Two servo outputs, one pulse after the other, like most PWM receivers.
    (void)link;
    static const uint32_t pin[2] = {PIN_RC_RX, PIN_RC_TX};
    uint64_t end = t;
    for (int i = 0; i < 2; i++) {
        if (!rx.cut[i]) {
            sim_line(pin[i], t, LINE_HIGH);
            sim_line(pin[i], t + US(rx.ch[i]), LINE_LOW);
            if (i == 0) {
                end = t + US(rx.ch[i]);
            }
        }
        t += US(rx.ch[i]) + US(50);
    }
    return end;
#endif
}

static void rx_tick(void *arg) {
    (void)arg;
    if (!rx.powered) {
        return;
    }
    uint64_t t = sim_now();
    bool silent = !rx.tx_on && rx.fs == FS_NO_PULSES && RC_PROTOCOL != RC_PROTOCOL_CRSF;
    if (!silent) {
        uint64_t end = rx_send(t, rx.tx_on);
        if (rx.tx_on) {
            rx.last_end = end;
            if (!rx.first_end) {
                rx.first_end = end;
            }
        }
        rx.frames++;
    }
    sim_at(t + US(rx.period_us), rx_tick, NULL);
}

static void rx_lines(int level) {
    if (RC_PROTOCOL == RC_PROTOCOL_CRSF) {
        return;     // CRSF bytes go straight to the UART model
    }
    sim_line(PIN_RC_RX, sim_now(), level);
    if (RC_PROTOCOL == RC_PROTOCOL_PWM) {
        sim_line(PIN_RC_TX, sim_now(), level);
    }
}

// Plug the receiver in (idle line driven) or pull its wires (pads float).
static void rx_power(bool on) {
    bool was = rx.powered;
    rx.powered = on;
    rx_lines(on ? LINE_LOW : LINE_HIZ);
    if (on && !was) {
        sim_at(sim_now() + US(200), rx_tick, NULL);
    }
}

static void sticks(uint16_t steer, uint16_t throttle, uint16_t weapon) {
    for (int i = 0; i < 16; i++) {
        rx.ch[i] = 1500;
    }
    rx.ch[RC_CH_STEER] = steer;
    rx.ch[RC_CH_THROTTLE] = throttle;
#if RC_PROTOCOL != RC_PROTOCOL_PWM
    rx.ch[RC_CH_WEAPON] = weapon;
#if RC_CH_ARM >= 0
    rx.ch[RC_CH_ARM] = 2000;
#endif
#else
    (void)weapon;
#endif
}

static void start(bool tx_on) {
    rx.period_us = RC_PROTOCOL == RC_PROTOCOL_SBUS ? 9000 : RC_PROTOCOL == RC_PROTOCOL_CRSF ? 4000 : 20000;
    sticks(1500, 1500, 1000);
    rx.tx_on = tx_on;
    sim_boot(fw);
    rx_power(true);
}

// Boot with the transmitter on and neutral sticks, and wait until the robot has armed.
static void boot_armed(void) {
    start(true);
    sim_run_ms(700);
    EXPECT(armed_now(), "not armed 700 ms after power-up with neutral sticks");
}

#define HAVE_WEAPON (RC_PROTOCOL != RC_PROTOCOL_PWM)
#define IS_DRIVE (HYPERION_MODE == HYPERION_MODE_DRIVE || HYPERION_MODE == HYPERION_MODE_FAILSAFE)
#define FRAME_MS ((double)rx.period_us / 1000.0)

#if IS_DRIVE
// --- BRINGUP 11.1: transmitter off at power-up ------------------------------------------
static void s_boot_no_transmitter(void) {
    start(false);
    sim_run_ms(2000);
    EXPECT(!driven_between(0, sim_now()), "a motor was driven with no transmitter");
    BRIDGE(0, 0, 0, 1000, 0, "disarmed");
    BRIDGE(1, 0, 0, 1000, 0, "disarmed");
    for (int i = 0; i < 4; i++) {
        EXPECT(sim_servo(i).pulse_us == 0, "PWM%d sends pulses while disarmed", i + 1);
    }
    uint64_t t_brake = both_bridges(0, true);
    EXPECT(t_brake && ms_of(t_brake) < 5.0, "bridges brake %.2f ms after reset, expected < 5 ms",
           ms_of(t_brake));
    // NO_LINK: the green LED blinks at 5 Hz.
    int toggles = 0;
    bool led = sim_led();
    for (int i = 0; i < 1000; i++) {
        sim_run_ms(1);
        toggles += sim_led() != led;
        led = sim_led();
    }
    EXPECT(toggles >= 9 && toggles <= 11, "LED toggled %d times in 1 s, expected 10 (5 Hz)", toggles);
}

// --- no receiver connected at all: the pad pulls must give an idle line -----------------
static void s_no_receiver(void) {
    sim_boot(fw);
    sim_run_ms(1500);
    EXPECT(!driven_between(0, sim_now()), "a motor was driven with no receiver");
    BRIDGE(0, 0, 0, 1000, 0, "no receiver");
    const char *line = NULL;
    EXPECT(sim_log_find("frames 0 err 0", MS(1000), &line),
           "floating receiver pad produced frames or errors");
}

// --- BRINGUP 11.2 and 11.12: arming, directions, mixing ----------------------------------
static void s_arm_and_drive(void) {
    start(true);
    sim_run_ms(400);
    EXPECT(!armed_now(), "armed after only 400 ms of neutral sticks");
    sim_run_ms(300);
    EXPECT(armed_now(), "not armed after 700 ms of neutral sticks");
    BRIDGE(0, 0, 0, 0, 1000, "armed, neutral");
    BRIDGE(1, 0, 0, 0, 1000, "armed, neutral");
    EXPECT(sim_led(), "LED not solid while armed");

    const int L = MOTOR_LEFT == 0 ? 0 : 1, R = 1 - L;
    // 1750 us with a 25 us deadband: (250 - 25) / 475 = 47.3 %. Slow decay: the rest brakes.
    sticks(1500, 1750, 1000);
    sim_run_ms(60);
    BRIDGE(L, 473, 0, 527, 0, "forward 47 %");
    BRIDGE(R, 473, 0, 527, 0, "forward 47 %");
    sticks(1500, 2000, 1000);
    sim_run_ms(60);
    BRIDGE(L, 1000, 0, 0, 0, "full forward");
    BRIDGE(R, 1000, 0, 0, 0, "full forward");
    sticks(1500, 1000, 1000);
    sim_run_ms(60);
    BRIDGE(L, 0, 1000, 0, 0, "full reverse");
    BRIDGE(R, 0, 1000, 0, 0, "full reverse");
    // Stick right: the left side runs forward, the right side backward.
    sticks(1750, 1500, 1000);
    sim_run_ms(60);
    BRIDGE(L, 473, 0, 527, 0, "turn right");
    BRIDGE(R, 0, 473, 527, 0, "turn right");
    // Full forward and full right: the mixer scales, the left side keeps 100 %.
    sticks(2000, 2000, 1000);
    sim_run_ms(60);
    BRIDGE(L, 1000, 0, 0, 0, "forward + right");
    BRIDGE(R, 0, 0, 0, 1000, "forward + right");
    // Outside the nominal range the command is clamped.
    sticks(1500, 2100, 1000);
    sim_run_ms(60);
    BRIDGE(L, 1000, 0, 0, 0, "over-range forward");
    sticks(1500, 1510, 1000);
    sim_run_ms(60);
    BRIDGE(L, 0, 0, 0, 1000, "inside the deadband");
    EXPECT(sim_log_count("err 0") > 0 && !sim_log_find("WATCHDOG", 0, NULL), "receiver errors logged");
}

// --- BRINGUP 11.3: stick held at power-up -------------------------------------------------
static void s_stick_held_at_power_up(void) {
    start(true);
    sticks(1500, 1800, 1000);
    sim_run_ms(2500);
    EXPECT(!driven_between(0, sim_now()), "robot moved with the stick held at power-up");
    EXPECT(!armed_now(), "armed with the stick held");
    sticks(1500, 1500, 1000);
    uint64_t t0 = sim_now();
    sim_run_ms(450);
    EXPECT(!armed_now(), "armed %.0f ms after the stick was released, hold is %d ms",
           ms_of(sim_now() - t0), ARM_NEUTRAL_HOLD_MS);
    sim_run_ms(100 + 2 * (uint32_t)FRAME_MS);
    EXPECT(armed_now(), "not armed after the stick was released");
#if HAVE_WEAPON
    // Same for the weapon stick.
    rx.tx_on = false;
    sim_run_ms(3000);
    sticks(1500, 1500, 1600);
    rx.tx_on = true;
    sim_run_ms(2500);
    EXPECT(!armed_now(), "armed with the weapon stick up");
    EXPECT(sim_servo(0).pulse_us == 0, "weapon pulses before arming");
    sticks(1500, 1500, 1000);
    sim_run_ms(600);
    EXPECT(armed_now(), "not armed after the weapon stick went down");
#endif
}

// --- BRINGUP 11.9: weapon output only while armed -------------------------------------------
#if HAVE_WEAPON
static void s_weapon_output(void) {
    boot_armed();
    servo_t s = sim_servo(0);
    EXPECT(s.pulse_us == 1000 && s.period_us == 20000, "PWM1 %u us / %u us, expected 1000 / 20000",
           s.pulse_us, s.period_us);
    sticks(1500, 1500, 1800);
    sim_run_ms(60);
    EXPECT(sim_servo(0).pulse_us == 1800, "PWM1 %u us, expected 1800", sim_servo(0).pulse_us);
    for (int i = 1; i < 4; i++) {
        EXPECT(sim_servo(i).pulse_us == 0, "PWM%d active", i + 1);
    }
    rx.tx_on = false;
    sim_run_ms(RC_TIMEOUT_MS + (uint32_t)FRAME_MS + 5);
    EXPECT(sim_servo(0).pulse_us == 0, "weapon still gets %u us pulses after link loss",
           sim_servo(0).pulse_us);
    // Back within the resume window with the weapon stick still up: the robot re-arms
    // (short dropout) and the weapon follows the stick again.
    rx.tx_on = true;
    sim_run_ms(100);
    EXPECT(sim_servo(0).pulse_us == 1800, "weapon did not resume after a short dropout");
}
#endif

// --- BRINGUP 11.4: transmitter switched off, receiver stops sending -------------------------
static void s_tx_off_no_pulses(void) {
    boot_armed();
    sticks(1500, 1650, 1000);
    sim_run_ms(200);
    BRIDGE(0, 263, 0, 737, 0, "driving 26 %");
    rx.fs = FS_NO_PULSES;
    rx.tx_on = false;
    uint64_t t_off = sim_now();
    sim_run_ms(300);
    uint64_t t_stop = both_bridges(t_off, true);
    double dt = t_stop ? ms_of(t_stop - rx.last_end) : INFINITY;
    printf("    last frame -> both bridges braking: %.2f ms\n", dt);
    EXPECT(t_stop != 0, "motors never stopped");
    EXPECT(dt <= RC_TIMEOUT_MS + 2.1, "stop took %.2f ms after the last frame, limit %d ms", dt,
           RC_TIMEOUT_MS + 2);
    EXPECT(dt >= RC_TIMEOUT_MS, "stopped %.2f ms after the last frame, before the timeout", dt);
    EXPECT(!driven_between(t_stop, sim_now()), "motor driven again after failsafe");
#if HYPERION_MODE == HYPERION_MODE_FAILSAFE
    EXPECT(sim_log_find("ARMED        -> NO_LINK", t_off, NULL), "transition not logged");
#endif
    // Transmitter back after more than the resume window with the stick still held: no motion.
    sim_run_ms(1500);
    rx.tx_on = true;
    sim_run_ms(1500);
    EXPECT(!driven_between(t_stop, sim_now()), "robot jumped on reconnect with the stick held");
    sticks(1500, 1500, 1000);
    sim_run_ms(700);
    EXPECT(armed_now(), "did not re-arm after the stick was centred");
}

// --- BRINGUP 11.4: receiver keeps sending and raises its failsafe flag ------------------------
#if RC_PROTOCOL != RC_PROTOCOL_PWM
static void s_tx_off_failsafe_flag(void) {
    boot_armed();
    sticks(1500, 1650, 1000);
    sim_run_ms(200);
    rx.fs = FS_FLAG;
    rx.tx_on = false;
    uint64_t t_off = sim_now();
    sim_run_ms(200);
    uint64_t t_stop = both_bridges(t_off, true);
    double dt = t_stop ? ms_of(t_stop - rx.last_end) : INFINITY;
    printf("    last good frame -> both bridges braking: %.2f ms\n", dt);
    // The first frame carrying the flag ends one frame period after the last good one.
    EXPECT(t_stop && dt <= FRAME_MS + 2.0, "stop took %.2f ms, expected within %.1f ms", dt,
           FRAME_MS + 2.0);
    sim_run_ms(3000);
    EXPECT(!driven_between(t_stop, sim_now()), "motor driven while the failsafe flag is set");
}
#endif

// --- BRINGUP 11.6 and 11.13: signal wire pulled, short and long --------------------------------
static void s_dropout_resume(void) {
    boot_armed();
    sticks(1500, 1650, 1000);
    sim_run_ms(200);
    rx_power(false);
    uint64_t t_cut = sim_now();
    sim_run_ms(500);
    uint64_t t_stop = both_bridges(t_cut, true);
    EXPECT(t_stop && ms_of(t_stop - t_cut) <= RC_TIMEOUT_MS + FRAME_MS + 2.1,
           "stop took %.2f ms after the wire was pulled", ms_of(t_stop - t_cut));
    rx_power(true);
    uint64_t t_back = sim_now();
    sim_run_ms(2 * (uint32_t)FRAME_MS + 10);
    BRIDGE(0, 263, 0, 737, 0, "resumed after a 0.5 s dropout");
    printf("    wire back -> driving again within %.0f ms\n", ms_of(sim_now() - t_back));

    // 2 s outage: longer than ARM_RESUME_MS, so neutral sticks are needed again.
    rx_power(false);
    sim_run_ms(2000);
    rx_power(true);
    t_back = sim_now();
    sim_run_ms(1500);
    EXPECT(!driven_between(t_back, sim_now()), "robot drove after a 2 s outage with the stick held");
    sticks(1500, 1500, 1000);
    sim_run_ms(700);
    EXPECT(armed_now(), "did not re-arm after the stick was centred");
}

// --- worst case: a receiver that holds the last sticks without any flag ------------------------
// The firmware cannot detect this. The scenario documents it, and passes when the motors keep running.
static void s_receiver_hold_without_flag(void) {
    boot_armed();
    sticks(1500, 1650, 1000);
    sim_run_ms(200);
    rx.fs = FS_HOLD;
    rx.tx_on = false;
    sim_run_ms(1000);
    BRIDGE(0, 263, 0, 737, 0, "receiver in hold mode (undetectable)");
}

// --- BRINGUP 11.8: main loop hangs ------------------------------------------------------------
#if HYPERION_MODE == HYPERION_MODE_FAILSAFE
static void s_watchdog(void) {
    boot_armed();
    sticks(1500, 1650, 1000);
    sim_run_ms(200);
    sim_key('w');
    uint64_t t_key = sim_now();
    sim_run_ms(400);
    uint64_t t_wd = sim_watchdog_fired_at();
    EXPECT(t_wd != 0, "watchdog did not reset a hung loop");
    double dt = ms_of(t_wd - t_key);
    printf("    hang -> watchdog reset: %.1f ms\n", dt);
    EXPECT(dt <= WATCHDOG_MS + 2.0, "watchdog took %.1f ms, configured %d ms", dt, WATCHDOG_MS);
    BRIDGE(0, 0, 0, 0, 1000, "after watchdog reset");
    BRIDGE(1, 0, 0, 0, 1000, "after watchdog reset");
    EXPECT(sim_pad_out(PIN_MA_IN1) == LINE_HIZ, "motor pad still driven after reset");
}
#endif

// --- the 32-bit microsecond timer wraps every 71.6 minutes --------------------------------------
static void s_timer_wrap_while_driving(void) {
    // Power up 3 s before the wrap and drive across it.
    sim_set_time(US(0x100000000ull) - MS(3000));
    uint64_t t0 = sim_now();
    boot_armed();
    sticks(1500, 1650, 1000);
    sim_run_ms(1000);
    uint64_t t1 = sim_now();
    sim_run_ms(3000);
    EXPECT(sim_now() > US(0x100000000ull), "test did not reach the wrap");
    BRIDGE(0, 263, 0, 737, 0, "driving across the timer wrap");
    uint64_t t_stop = both_bridges(t1, true);
    EXPECT(t_stop == 0, "motors stopped at %.1f ms after boot while crossing the wrap",
           ms_of(t_stop - t0));
}

static void s_timer_wrap_after_link_loss(void) {
    // Link lost while driving. 71.6 minutes later the 32-bit frame age wraps back to
    // zero: the stale frame must not look fresh again.
    boot_armed();
    sticks(1500, 1650, 1000);
    sim_run_ms(300);
    rx_power(false);
    sim_run_ms(1000);
    uint64_t t_quiet = sim_now();
    sim_set_quantum(US(250));
    sim_run_until(rx.last_end + US(0x100000000ull) - MS(200));
    sim_set_quantum(US(1));
    sim_run_ms(600);
    EXPECT(!driven_between(t_quiet, sim_now()), "motor driven by a stale frame after the timer wrap");
    EXPECT(!armed_now(), "armed from a stale frame");
    const char *line = NULL;
    uint64_t up = sim_log_find("link UP", t_quiet, &line);
    if (!up) {
        up = sim_log_find("WAIT_NEUTRAL", t_quiet, &line);
    }
    EXPECT(up == 0, "firmware saw a link %.1f min after the last frame: \"%s\"",
           ms_of(up - rx.last_end) / 60000.0, line ? line : "");
}

// --- SBUS: baud rate tolerance of the PIO decoder -------------------------------------------------
#if RC_PROTOCOL == RC_PROTOCOL_SBUS
static void s_sbus_baud_tolerance(void) {
    static const double scale[] = {0.97, 0.985, 1.0, 1.015, 1.03};
    start(true);
    sim_run_ms(700);
    for (size_t i = 0; i < sizeof(scale) / sizeof(scale[0]); i++) {
        rx.baud_scale = scale[i];
        sticks(1500, (uint16_t)(1600 + 50 * i), 1000);
        sim_run_ms(300);
        int mag = (100 + 50 * (int)i - RC_DEADBAND_US) * 1000 / (500 - RC_DEADBAND_US);
        mag = mag * 7500 / 1000 * 1000 / 7500;
        bridge_t b = sim_bridge(0);
        EXPECT(near(b.fwd, mag, 2), "baud %+.1f %%: duty %u, expected %d", (scale[i] - 1) * 100,
               b.fwd, mag);
    }
    EXPECT(sim_log_count("err 0") > 0 && sim_log_find("err 0", sim_now() - MS(250), NULL),
           "decode errors within +-3 %% baud error");
}

// A frame cut short, a bad header and a parity error must each cost only that frame.
static void s_sbus_corrupt_frames(void) {
    boot_armed();
    sticks(1500, 1650, 1000);
    sim_run_ms(100);
    rx_power(false);                    // stop the regular frames, keep the line idle
    sim_line(PIN_RC_RX, sim_now(), LINE_LOW);
    sim_run_ms(5);
    uint64_t t = sim_now();
    uint8_t f[25] = {0x0F};
    uint16_t full[16];
    for (int i = 0; i < 16; i++) {
        full[i] = 2000;                 // would be full throttle if it were accepted
    }
    pack11(full, &f[1]);
    // 1: truncated after 12 bytes. 2: wrong end byte. 3: wrong header.
    sim_line_uart(PIN_RC_RX, t, f, 12, 100000, true, 2, true);
    f[24] = 0x55;
    sim_line_uart(PIN_RC_RX, t + MS(9), f, 25, 100000, true, 2, true);
    f[24] = 0x00;
    f[0] = 0x0E;
    sim_line_uart(PIN_RC_RX, t + MS(18), f, 25, 100000, true, 2, true);
    // 4: odd parity instead of even on every byte.
    f[0] = 0x0F;
    uint64_t tb = t + MS(27);
    for (int i = 0; i < 25; i++) {
        uint8_t b = f[i];
        int ones = __builtin_popcount(b);
        sim_line(PIN_RC_RX, tb, LINE_HIGH);
        for (int k = 0; k < 8; k++) {
            sim_line(PIN_RC_RX, tb + US(10) * (uint64_t)(k + 1), ((b >> k) & 1) ? LINE_LOW : LINE_HIGH);
        }
        sim_line(PIN_RC_RX, tb + US(90), (ones & 1) ? LINE_HIGH : LINE_LOW);
        sim_line(PIN_RC_RX, tb + US(100), LINE_LOW);
        tb += US(120);
    }
    sim_run_ms(50);
    bridge_t b = sim_bridge(0);
    EXPECT(b.fwd < 300, "a corrupt frame was accepted: duty %u", b.fwd);
    sim_run_ms(40);
    EXPECT(!armed_now(), "no failsafe after 90 ms of corrupt frames only");
}
#endif

// --- PWM input: one channel wire breaks ------------------------------------------------------------
#if RC_PROTOCOL == RC_PROTOCOL_PWM
static void pwm_wire_break(int ch) {
    boot_armed();
    sticks(1500, 1650, 1000);
    sim_run_ms(200);
    rx.cut[ch] = true;
    uint64_t t_cut = sim_now();
    sim_run_ms(300);
    uint64_t t_stop = both_bridges(t_cut, true);
    double dt = t_stop ? ms_of(t_stop - t_cut) : INFINITY;
    printf("    CH%d wire cut -> braking after %.1f ms\n", ch + 1, dt);
    EXPECT(dt <= RC_TIMEOUT_MS + 2 * FRAME_MS + 2.0, "CH%d broken: stop took %.1f ms", ch + 1, dt);
    sim_run_ms(2000);
    EXPECT(!driven_between(t_stop, sim_now()), "motor driven with CH%d broken", ch + 1);
}

// CH2 dead for 71.6 minutes while CH1 keeps pulsing: its last pulse must not count again.
static void s_pwm_timer_wrap_dead_channel(void) {
    boot_armed();
    sticks(1500, 1650, 1000);
    sim_run_ms(300);
    rx.cut[1] = true;
    uint64_t t_cut = sim_now();
    sim_run_ms(1000);
    sim_set_quantum(US(250));
    sim_run_until(t_cut + US(0x100000000ull) - MS(300));
    sim_set_quantum(US(1));
    sim_run_ms(700);
    EXPECT(!driven_between(t_cut + MS(200), sim_now()), "motor driven by a stale CH2 pulse");
    const char *line = NULL;
    uint64_t up = sim_log_find("link UP", t_cut + MS(200), &line);
    EXPECT(up == 0, "stale CH2 pulse accepted %.1f min after the wire broke: \"%s\"",
           ms_of(up - t_cut) / 60000.0, line ? line : "");
}

static void s_pwm_ch1_wire_break(void) { pwm_wire_break(0); }
static void s_pwm_ch2_wire_break(void) { pwm_wire_break(1); }
#endif

// --- arm switch (built with RC_CH_ARM set) ----------------------------------------------------------
#if RC_CH_ARM >= 0
static void s_arm_switch(void) {
    start(true);
    rx.ch[RC_CH_ARM] = 1000;
    sim_run_ms(1500);
    EXPECT(!armed_now(), "armed with the arm switch off");
    rx.ch[RC_CH_ARM] = 2000;
    sim_run_ms(450);
    EXPECT(!armed_now(), "armed before the neutral hold time");
    sim_run_ms(100);
    EXPECT(armed_now(), "not armed with the switch on");
    sticks(1500, 1650, 1000);
    sim_run_ms(100);
    rx.ch[RC_CH_ARM] = 1000;
    uint64_t t = sim_now();
    sim_run_ms(2 * (uint32_t)FRAME_MS + 3);
    EXPECT(both_bridges(t, true), "switch off did not stop the motors within two frames");
    rx.ch[RC_CH_ARM] = 2000;
    sim_run_ms(1500);
    EXPECT(!driven_between(t + MS(30), sim_now()), "switch back on with the stick held drove the robot");
}
#endif
#endif // IS_DRIVE

// --- bring-up modes ------------------------------------------------------------------------------------
#if HYPERION_MODE == HYPERION_MODE_RC
static void s_rc_mode(void) {
    start(true);
    sticks(1235, 1766, 1000);     // values the 11-bit channel format represents exactly
    sim_run_ms(1500);
    const char *line = NULL;
    EXPECT(sim_log_find("OK ", MS(1000), &line), "no OK line in the rc mode output");
    if (line) {
        printf("    %s\n", line);
        EXPECT(strstr(line, "err+0 "), "decode errors: %s", line);
        char want[64];
        snprintf(want, sizeof(want), "CH%d 1235", RC_CH_STEER + 1);
        EXPECT(strstr(line, want), "steer channel wrong: %s", line);
        snprintf(want, sizeof(want), "CH%d 1766", RC_CH_THROTTLE + 1);
        EXPECT(strstr(line, want), "throttle channel wrong: %s", line);
        int fps = 0;
        sscanf(line, "OK %d", &fps);
        int expect = (int)(1000.0 / FRAME_MS);
        EXPECT(abs(fps - expect) <= expect / 10 + 5, "frame rate %d, expected about %d", fps, expect);
    }
    EXPECT(!driven_between(0, sim_now()), "rc mode drove a motor");
    rx.tx_on = false;
    sim_run_ms(500);
    EXPECT(sim_log_find("LOST", sim_now() - MS(250), NULL), "rc mode does not show LOST");
}
#endif

#if HYPERION_MODE == HYPERION_MODE_IMU
static void s_imu_mode(void) {
    sim_imu.acc_mg[0] = 30;
    sim_imu.acc_mg[1] = -20;
    sim_imu.acc_mg[2] = 1000;
    sim_imu.gyro_dps[0] = 1.4;
    sim_imu.gyro_dps[1] = -0.7;
    sim_imu.gyro_dps[2] = 0.35;
    sim_imu.temp_c = 31.5;
    sim_boot(fw);
    sim_run_ms(1500);
    const char *line = NULL;
    EXPECT(sim_log_find("WHO_AM_I 0x70 OK", 0, NULL), "WHO_AM_I check failed");
    EXPECT(sim_log_find("samples in 1 s", 0, &line), "no bias line");
    int n = 0;
    float bx = 0, by = 0, bz = 0;
    if (line) {
        printf("    %s\n", line);
        sscanf(line, "imu: %d samples in 1 s (expect ~960). Gyro bias %f %f %f", &n, &bx, &by, &bz);
    }
    EXPECT(n >= 950 && n <= 970, "%d samples in 1 s, expected about 960", n);
    EXPECT(fabsf(bx - 1.4f) < 0.08f && fabsf(by + 0.7f) < 0.08f && fabsf(bz - 0.35f) < 0.08f,
           "gyro bias %.2f %.2f %.2f, expected 1.40 -0.70 0.35", bx, by, bz);
    sim_imu.gyro_dps[2] = -720;     // a fast spin about the vertical axis
    sim_run_ms(400);
    EXPECT(sim_log_find("acc ", sim_now() - MS(150), &line), "no data line");
    if (line) {
        printf("    %s\n", line);
        float ax, ay, az, mag, gx, gy, gz, temp;
        int hz = 0;
        int got = sscanf(line, "acc %f %f %f mg |a| %f mg gyro %f %f %f dps T %f C %d Hz", &ax, &ay,
                         &az, &mag, &gx, &gy, &gz, &temp, &hz);
        EXPECT(got == 9, "data line not parsed (%d fields)", got);
        EXPECT(fabsf(ax - 30) < 2 && fabsf(ay + 20) < 2 && fabsf(az - 1000) < 2 && fabsf(mag - 1000) < 3,
               "acceleration %.0f %.0f %.0f |%.0f|, expected 30 -20 1000", ax, ay, az, mag);
        EXPECT(fabsf(gz + 720) < 0.2f && fabsf(gx - 1.4f) < 0.08f, "gyro %.2f %.2f %.2f", gx, gy, gz);
        EXPECT(fabsf(temp - 31.5f) < 0.1f, "temperature %.1f, expected 31.5", temp);
        EXPECT(hz >= 940 && hz <= 980, "sample rate %d Hz", hz);
    }
}

static void s_imu_missing(void) {
    sim_imu.fitted = false;
    sim_boot(fw);
    sim_run_ms(2500);
    EXPECT(sim_log_count("init FAILED, WHO_AM_I = 0x00") >= 2, "missing IMU not reported");
    EXPECT(sim_watchdog_fired_at() == 0, "watchdog reset while retrying the IMU");
}

static void s_imu_slow_reset(void) {
    sim_imu.reset_us = 4000;        // far slower than the real part, still inside the 10 ms poll
    sim_boot(fw);
    sim_run_ms(1500);
    EXPECT(sim_log_find("WHO_AM_I 0x70 OK", 0, NULL), "init failed with a 4 ms software reset");
}
#endif

#if HYPERION_MODE == HYPERION_MODE_ADC
static void s_adc_mode(void) {
    sim_vbat_mv = 12600;
    sim_motor_ma[0] = 1000;
    sim_motor_ma[1] = 250;
    sim_mcu_temp_c = 41.0;
    sim_boot(fw);
    sim_run_ms(600);
    const char *line = NULL;
    EXPECT(sim_log_find("VBAT raw", MS(100), &line), "no adc output");
    if (line) {
        printf("    %s\n", line);
        int raw, vbat, pin, ra, ia, rb, ib;
        float temp;
        int got = sscanf(line, "VBAT raw %d = %d mV (pin %d mV) | IPROPI A raw %d = %d mA | "
                               "IPROPI B raw %d = %d mA | MCU %f C", &raw, &vbat, &pin, &ra, &ia, &rb,
                         &ib, &temp);
        EXPECT(got == 8, "adc line not parsed");
        EXPECT(abs(vbat - 12600) <= 40, "VBAT %d mV, expected 12600", vbat);
        EXPECT(abs(ia - 1000) <= 5 && abs(ib - 250) <= 5, "currents %d / %d mA, expected 1000 / 250", ia, ib);
        EXPECT(fabsf(temp - 41.0f) < 1.0f, "MCU temperature %.1f, expected 41", temp);
    }
    BRIDGE(0, 0, 0, 0, 1000, "adc mode");
}
#endif

#if HYPERION_MODE == HYPERION_MODE_BLINK
static void s_blink_mode(void) {
    static const uint32_t pads[4] = {PIN_PWM1, PIN_PWM2, PIN_PWM3, PIN_PWM4};
    int toggles[5] = {0};
    int last[5] = {0};
    sim_boot(fw);
    sim_run_ms(50);
    for (int t = 0; t < 4000; t++) {
        sim_run_ms(1);
        for (int i = 0; i < 5; i++) {
            int v = i < 4 ? sim_pad_out(pads[i]) == LINE_HIGH : sim_led();
            toggles[i] += v != last[i] && t > 0;
            last[i] = v;
        }
    }
    for (int i = 0; i < 4; i++) {
        int expect = 8 << i;    // 1, 2, 4, 8 Hz for 4 s
        EXPECT(abs(toggles[i] - expect) <= 1, "PWM%d toggled %d times in 4 s, expected %d", i + 1,
               toggles[i], expect);
    }
    EXPECT(abs(toggles[4] - 8) <= 1, "LED toggled %d times in 4 s, expected 8", toggles[4]);
    EXPECT(sim_log_count("alive") >= 3, "no heartbeat on the console");
}
#endif

#if HYPERION_MODE == HYPERION_MODE_MOTOR
static void s_motor_mode(void) {
    sim_boot(fw);
    sim_run_ms(2100);
    BRIDGE(0, 0, 0, 0, 1000, "idle");
    sim_key('f');
    sim_run_ms(100);
    BRIDGE(0, 100, 0, 900, 0, "A forward 10 % slow decay");
    BRIDGE(1, 0, 0, 0, 1000, "B untouched");
    sim_key('d');
    sim_run_ms(50);
    BRIDGE(0, 100, 0, 0, 900, "A forward 10 % fast decay");
    sim_key('+');
    sim_key('+');
    sim_run_ms(50);
    BRIDGE(0, 200, 0, 0, 800, "A forward 20 %");
    sim_run_ms(MOTOR_TEST_RUN_MS);
    BRIDGE(0, 0, 0, 0, 1000, "auto stop");
    EXPECT(sim_log_find("auto stop", 0, NULL), "auto stop not logged");
    sim_key('b');
    sim_key('d');
    sim_key('r');
    sim_run_ms(100);
    BRIDGE(1, 0, 200, 800, 0, "B reverse 20 % slow decay");
    BRIDGE(0, 0, 0, 0, 1000, "A off while B runs");
    sim_key('x');
    sim_run_ms(20);
    BRIDGE(1, 0, 0, 1000, 0, "B brake");
    sim_key(' ');
    sim_run_ms(20);
    BRIDGE(1, 0, 0, 0, 1000, "B coast");
    for (int i = 0; i < 20; i++) {
        sim_key('+');
    }
    sim_key('f');
    sim_run_ms(100);
    BRIDGE(1, MOTOR_TEST_MAX_PERMILLE, 0, 1000 - MOTOR_TEST_MAX_PERMILLE, 0, "B at the duty limit");
}
#endif

#if HYPERION_MODE == HYPERION_MODE_THERMAL
static void s_thermal_mode(void) {
    sim_vbat_mv = 16800;
    sim_boot(fw);
    sim_run_ms(2100);
    sim_key('a');
    sim_run_ms(20);
    BRIDGE(0, 0, 0, 0, 1000, "before confirmation");
    sim_key('y');
    sim_motor_ma[0] = 1450;
    sim_run_ms(5000);
    BRIDGE(0, 1000, 0, 0, 0, "stall test running");
    BRIDGE(1, 0, 0, 0, 1000, "channel B not selected");
    uint64_t t_run = sim_log_find("t_ms,motor", 0, NULL);
    sim_motor_ma[0] = 0;            // thermal shutdown: the bridge goes high-impedance
    uint64_t t_tsd = sim_now();
    sim_run_ms(2000);
    const char *line = NULL;
    EXPECT(sim_log_find("# motor A TSD #1", t_tsd, &line), "thermal shutdown not detected");
    if (line) {
        printf("    %s (injected at %.0f ms)\n", line, ms_of(t_tsd - t_run));
        int at = 0, stall = 0;
        sscanf(line, "# motor A TSD #1 at %d ms (stall avg %d mA)", &at, &stall);
        EXPECT(abs(at - (int)ms_of(t_tsd - t_run)) <= 25, "TSD time %d ms, injected at %.0f ms", at,
               ms_of(t_tsd - t_run));
        EXPECT(abs(stall - 1450) <= 15, "stall current %d mA, expected 1450", stall);
    }
    sim_motor_ma[0] = 1450;
    sim_run_ms(500);
    EXPECT(sim_log_find("# motor A recovered", t_tsd, NULL), "recovery not detected");
    sim_key(' ');
    sim_run_ms(50);
    EXPECT(sim_log_find("aborted by user", 0, NULL), "space did not abort");
    BRIDGE(0, 0, 0, 0, 1000, "after abort");

    // Supply collapses during a run: the test must stop on its own.
    sim_key('2');
    sim_run_ms(20);
    sim_key('y');
    sim_run_ms(1000);
    BRIDGE(1, 1000, 0, 0, 0, "both channels");
    sim_vbat_mv = 5000;
    sim_run_ms(50);
    EXPECT(sim_log_find("VBAT below limit", 0, NULL), "undervoltage abort missing");
    BRIDGE(0, 0, 0, 0, 1000, "after undervoltage abort");
    BRIDGE(1, 0, 0, 0, 1000, "after undervoltage abort");
    EXPECT(sim_watchdog_fired_at() == 0, "watchdog reset during the thermal test");
}
#endif

// --- runner -------------------------------------------------------------------------------------------------
typedef struct {
    const char *name;
    void (*fn)(void);
} scenario_t;

#define S(f) {#f, f}
static const scenario_t scenarios[] = {
#if IS_DRIVE
    S(s_boot_no_transmitter),
    S(s_no_receiver),
    S(s_arm_and_drive),
    S(s_stick_held_at_power_up),
#if HAVE_WEAPON
    S(s_weapon_output),
#endif
    S(s_tx_off_no_pulses),
#if RC_PROTOCOL != RC_PROTOCOL_PWM
    S(s_tx_off_failsafe_flag),
#endif
    S(s_dropout_resume),
    S(s_receiver_hold_without_flag),
#if HYPERION_MODE == HYPERION_MODE_FAILSAFE
    S(s_watchdog),
#endif
    S(s_timer_wrap_while_driving),
    S(s_timer_wrap_after_link_loss),
#if RC_PROTOCOL == RC_PROTOCOL_SBUS
    S(s_sbus_baud_tolerance),
    S(s_sbus_corrupt_frames),
#endif
#if RC_PROTOCOL == RC_PROTOCOL_PWM
    S(s_pwm_ch1_wire_break),
    S(s_pwm_ch2_wire_break),
    S(s_pwm_timer_wrap_dead_channel),
#endif
#if RC_CH_ARM >= 0
    S(s_arm_switch),
#endif
#endif
#if HYPERION_MODE == HYPERION_MODE_RC
    S(s_rc_mode),
#endif
#if HYPERION_MODE == HYPERION_MODE_IMU
    S(s_imu_mode),
    S(s_imu_missing),
    S(s_imu_slow_reset),
#endif
#if HYPERION_MODE == HYPERION_MODE_ADC
    S(s_adc_mode),
#endif
#if HYPERION_MODE == HYPERION_MODE_BLINK
    S(s_blink_mode),
#endif
#if HYPERION_MODE == HYPERION_MODE_MOTOR
    S(s_motor_mode),
#endif
#if HYPERION_MODE == HYPERION_MODE_THERMAL
    S(s_thermal_mode),
#endif
};

int main(int argc, char **argv) {
    const char *only = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            sim_echo = true;
        } else {
            only = argv[i];
        }
    }
    int failed = 0, ran = 0;
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
        if (only && !strstr(scenarios[i].name, only)) {
            continue;
        }
        printf("  %s\n", scenarios[i].name + 2);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            sim_reset();
            scenarios[i].fn();
            if (sim_faults()) {
                fprintf(stderr, "  FAIL: %d simulator fault(s)\n", sim_faults());
                failures++;
            }
            fflush(stdout);
            _exit(failures ? 1 : 0);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        ran++;
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            printf("  ^^^ FAILED%s\n", WIFEXITED(st) ? "" : " (crashed)");
            failed++;
        }
    }
    printf("%d scenario(s), %d failed\n", ran, failed);
    return failed ? 1 : 0;
}
