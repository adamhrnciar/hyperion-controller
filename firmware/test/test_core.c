// SPDX-License-Identifier: GPL-3.0-or-later
// Host unit tests for the portable code in src/core (no Pico SDK needed).
// Run: make -C firmware/test

#include <stdio.h>
#include <string.h>

#include "arming.h"
#include "crsf.h"
#include "mixer.h"
#include "rc.h"
#include "sbus.h"

static int failures;
static int checks;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

// Pack 16 x 11-bit values little-endian into 22 bytes.
static void pack11(const uint16_t *ch, uint8_t *dst) {
    memset(dst, 0, 22);
    for (int i = 0; i < 16; i++) {
        for (int b = 0; b < 11; b++) {
            if (ch[i] & (1u << b)) {
                int bit = i * 11 + b;
                dst[bit / 8] |= (uint8_t)(1u << (bit % 8));
            }
        }
    }
}

static void make_sbus(uint8_t *f, const uint16_t *raw, uint8_t flags, uint8_t end) {
    f[0] = SBUS_HEADER;
    pack11(raw, &f[1]);
    f[23] = flags;
    f[24] = end;
}

static bool feed_sbus(sbus_parser_t *p, const uint8_t *f, uint32_t *t, rc_frame_t *out) {
    bool got = false;
    for (int i = 0; i < SBUS_FRAME_LEN; i++) {
        got = sbus_parser_feed(p, f[i], true, *t, out);
        *t += 120;
    }
    *t += 4000;   // inter-frame gap
    return got;
}

static void test_unpack_and_scale(void) {
    uint16_t raw[16], out[16];
    uint8_t packed[22];
    for (int i = 0; i < 16; i++) {
        raw[i] = (uint16_t)(172 + i * 100);
    }
    pack11(raw, packed);
    rc_unpack_11bit(packed, out, 16);
    CHECK(memcmp(raw, out, sizeof(raw)) == 0);

    CHECK(rc_raw11_to_us(992) == 1500);
    CHECK(rc_raw11_to_us(172) == 987);
    CHECK(rc_raw11_to_us(1811) == 2011);
}

static void test_sbus(void) {
    sbus_parser_t p;
    rc_frame_t f;
    uint8_t frame[SBUS_FRAME_LEN];
    uint16_t raw[16];
    uint32_t t = 1000;
    for (int i = 0; i < 16; i++) {
        raw[i] = 992;
    }
    raw[0] = 1811;
    raw[1] = 172;

    sbus_parser_init(&p);
    make_sbus(frame, raw, 0x00, 0x00);
    CHECK(feed_sbus(&p, frame, &t, &f));
    CHECK(f.us[0] == 2011 && f.us[1] == 987 && f.us[2] == 1500);
    CHECK(!f.failsafe && !f.frame_lost);
    CHECK(p.frames == 1 && p.errors == 0);

    // Failsafe and frame-lost flags, SBUS2 end byte.
    make_sbus(frame, raw, 0x0C, 0x14);
    CHECK(feed_sbus(&p, frame, &t, &f));
    CHECK(f.failsafe && f.frame_lost);

    // Parity error in the middle: frame dropped, next frame decodes.
    make_sbus(frame, raw, 0x00, 0x00);
    bool got = false;
    for (int i = 0; i < SBUS_FRAME_LEN; i++) {
        got |= sbus_parser_feed(&p, frame[i], i != 10, t, &f);
        t += 120;
    }
    t += 4000;
    CHECK(!got);
    CHECK(feed_sbus(&p, frame, &t, &f));

    // Join mid-frame (no gap before first byte seen): bytes are ignored until a gap.
    sbus_parser_init(&p);
    p.started = true;
    p.last_byte_us = t - 100;
    got = false;
    for (int i = 12; i < SBUS_FRAME_LEN; i++) {
        got |= sbus_parser_feed(&p, frame[i], true, t, &f);
        t += 120;
    }
    t += 4000;
    CHECK(!got);
    CHECK(feed_sbus(&p, frame, &t, &f));

    // A 0x0F inside channel data is not taken as a header without a gap.
    sbus_parser_init(&p);
    raw[0] = 0x0F;
    make_sbus(frame, raw, 0x00, 0x00);
    CHECK(feed_sbus(&p, frame, &t, &f));
    CHECK(p.errors == 0);

    // Truncated frame (gap after 10 bytes) counts as an error and decodes nothing.
    got = false;
    for (int i = 0; i < 10; i++) {
        got |= sbus_parser_feed(&p, frame[i], true, t, &f);
        t += 120;
    }
    t += 4000;
    CHECK(!got);
    CHECK(feed_sbus(&p, frame, &t, &f));
    CHECK(p.errors == 1);

    // Bad end byte.
    make_sbus(frame, raw, 0x00, 0x55);
    CHECK(!feed_sbus(&p, frame, &t, &f));

    CHECK(sbus_parity_ok(0x00, 0));
    CHECK(!sbus_parity_ok(0x00, 1));
    CHECK(sbus_parity_ok(0x01, 1));
    CHECK(sbus_parity_ok(0x0F, 0));
    CHECK(!sbus_parity_ok(0x07, 0));
}

static int make_crsf(uint8_t *f, uint8_t type, const uint8_t *payload, int n) {
    f[0] = CRSF_SYNC_FC;
    f[1] = (uint8_t)(n + 2);
    f[2] = type;
    memcpy(&f[3], payload, (size_t)n);
    f[3 + n] = crsf_crc8(&f[2], n + 1);
    return n + 4;
}

static crsf_result_t feed_crsf(crsf_parser_t *p, const uint8_t *f, int n, uint32_t *t, rc_frame_t *out) {
    crsf_result_t r = CRSF_NONE;
    for (int i = 0; i < n; i++) {
        crsf_result_t ri = crsf_parser_feed(p, f[i], *t, out);
        if (ri != CRSF_NONE) {
            r = ri;
        }
        *t += 24;
    }
    *t += 2000;
    return r;
}

static void test_crsf(void) {
    crsf_parser_t p;
    rc_frame_t f;
    uint8_t frame[CRSF_MAX_FRAME];
    uint8_t payload[22];
    uint16_t raw[16];
    uint32_t t = 0;
    for (int i = 0; i < 16; i++) {
        raw[i] = 992;
    }
    raw[3] = 1811;
    pack11(raw, payload);

    // Known CRC-8/DVB-S2 check value for "123456789".
    CHECK(crsf_crc8((const uint8_t *)"123456789", 9) == 0xBC);

    crsf_parser_init(&p);
    int n = make_crsf(frame, CRSF_TYPE_RC, payload, 22);
    CHECK(feed_crsf(&p, frame, n, &t, &f) == CRSF_GOT_RC);
    CHECK(f.us[3] == 2011 && f.us[0] == 1500 && !f.failsafe);

    // Corrupt CRC.
    frame[10] ^= 0x01;
    CHECK(feed_crsf(&p, frame, n, &t, &f) == CRSF_NONE);
    CHECK(p.errors == 1);
    frame[10] ^= 0x01;

    // Link statistics with LQ 0 marks following RC frames as failsafe.
    uint8_t link[10] = {50, 50, 0, 5, 0, 4, 1, 60, 100, 8};
    int ln = make_crsf(frame, CRSF_TYPE_LINK, link, 10);
    CHECK(feed_crsf(&p, frame, ln, &t, &f) == CRSF_GOT_LINK);
    CHECK(p.uplink_lq == 0);
    n = make_crsf(frame, CRSF_TYPE_RC, payload, 22);
    CHECK(feed_crsf(&p, frame, n, &t, &f) == CRSF_GOT_RC);
    CHECK(f.failsafe);

    link[2] = 100;
    ln = make_crsf(frame, CRSF_TYPE_LINK, link, 10);
    feed_crsf(&p, frame, ln, &t, &f);
    n = make_crsf(frame, CRSF_TYPE_RC, payload, 22);
    CHECK(feed_crsf(&p, frame, n, &t, &f) == CRSF_GOT_RC);
    CHECK(!f.failsafe);

    // Garbage before the frame, and a bad length byte, are skipped.
    uint8_t junk[] = {0x00, 0x55, CRSF_SYNC_FC, 0xFF};
    feed_crsf(&p, junk, sizeof(junk), &t, &f);
    CHECK(feed_crsf(&p, frame, n, &t, &f) == CRSF_GOT_RC);
}

static void test_mixer(void) {
    CHECK(mixer_us_to_cmd(1500, 25) == 0);
    CHECK(mixer_us_to_cmd(1525, 25) == 0);
    CHECK(mixer_us_to_cmd(1475, 25) == 0);
    CHECK(mixer_us_to_cmd(2000, 25) == 1000);
    CHECK(mixer_us_to_cmd(1000, 25) == -1000);
    CHECK(mixer_us_to_cmd(2012, 25) == 1000);
    CHECK(mixer_us_to_cmd(900, 25) == -1000);
    CHECK(mixer_us_to_cmd(1762, 25) == 498);
    CHECK(mixer_us_to_cmd(1527, 25) > 0 && mixer_us_to_cmd(1527, 25) < 10);

    int16_t l, r;
    mixer_tank(500, 0, &l, &r);
    CHECK(l == 500 && r == 500);
    mixer_tank(0, 400, &l, &r);
    CHECK(l == 400 && r == -400);
    mixer_tank(1000, 1000, &l, &r);
    CHECK(l == 1000 && r == 0);
    mixer_tank(1000, 500, &l, &r);
    CHECK(l == 1000 && r == 333);
    mixer_tank(-1000, -1000, &l, &r);
    CHECK(l == -1000 && r == 0);
}

static void test_arming(void) {
    arming_t a;
    arming_input_t in = {.link_ok = false, .sticks_neutral = true, .arm_switch = true};
    arming_init(&a, 500, 0);    // resume disabled: every reconnect needs neutral

    CHECK(arming_update(&a, &in, 0) == ARM_NO_LINK);
    CHECK(!arming_motors_enabled(&a));

    // Link up with sticks neutral: arms only after the hold time.
    in.link_ok = true;
    CHECK(arming_update(&a, &in, 1000) == ARM_WAIT_NEUTRAL);
    CHECK(arming_update(&a, &in, 1499) == ARM_WAIT_NEUTRAL);
    CHECK(arming_update(&a, &in, 1500) == ARM_ARMED);
    CHECK(arming_motors_enabled(&a));

    // Sticks moved while armed: stays armed.
    in.sticks_neutral = false;
    CHECK(arming_update(&a, &in, 1600) == ARM_ARMED);

    // Link lost while driving: disarm immediately.
    in.link_ok = false;
    CHECK(arming_update(&a, &in, 1700) == ARM_NO_LINK);

    // Link back with stick still held: must not arm.
    in.link_ok = true;
    CHECK(arming_update(&a, &in, 1800) == ARM_WAIT_NEUTRAL);
    CHECK(arming_update(&a, &in, 5000) == ARM_WAIT_NEUTRAL);

    // Stick released, but a brief deflection restarts the hold timer.
    in.sticks_neutral = true;
    CHECK(arming_update(&a, &in, 5100) == ARM_WAIT_NEUTRAL);
    in.sticks_neutral = false;
    CHECK(arming_update(&a, &in, 5400) == ARM_WAIT_NEUTRAL);
    in.sticks_neutral = true;
    CHECK(arming_update(&a, &in, 5500) == ARM_WAIT_NEUTRAL);
    CHECK(arming_update(&a, &in, 5999) == ARM_WAIT_NEUTRAL);
    CHECK(arming_update(&a, &in, 6000) == ARM_ARMED);

    // Arm switch off disarms; switching back on needs neutral again.
    in.arm_switch = false;
    CHECK(arming_update(&a, &in, 6100) == ARM_SWITCH_OFF);
    in.arm_switch = true;
    in.sticks_neutral = false;
    CHECK(arming_update(&a, &in, 6200) == ARM_WAIT_NEUTRAL);
    in.sticks_neutral = true;
    CHECK(arming_update(&a, &in, 6300) == ARM_WAIT_NEUTRAL);
    CHECK(arming_update(&a, &in, 6800) == ARM_ARMED);

    // millis() wrap-around during the hold.
    arming_init(&a, 500, 0);
    CHECK(arming_update(&a, &in, 0xFFFFFF00u) == ARM_WAIT_NEUTRAL);
    CHECK(arming_update(&a, &in, 0x000000F0u) == ARM_WAIT_NEUTRAL);
    CHECK(arming_update(&a, &in, 0x00000100u) == ARM_ARMED);
}

static void arm_now(arming_t *a, arming_input_t *in, uint32_t t) {
    in->link_ok = true;
    in->arm_switch = true;
    in->sticks_neutral = true;
    arming_update(a, in, t);
    arming_update(a, in, t + 500);
}

static void test_arming_resume(void) {
    arming_t a;
    arming_input_t in = {0};
    arming_init(&a, 500, 1000);

    // Never armed: a link that comes up with the stick held must not arm.
    in.link_ok = true;
    in.arm_switch = true;
    CHECK(arming_update(&a, &in, 0) == ARM_WAIT_NEUTRAL);
    in.link_ok = false;
    arming_update(&a, &in, 100);
    in.link_ok = true;
    CHECK(arming_update(&a, &in, 200) == ARM_WAIT_NEUTRAL);

    // Short dropout while driving: brake during the dropout, re-arm at once with the stick held.
    arm_now(&a, &in, 1000);
    CHECK(a.state == ARM_ARMED);
    in.sticks_neutral = false;
    in.link_ok = false;
    CHECK(arming_update(&a, &in, 2000) == ARM_NO_LINK);
    CHECK(arming_update(&a, &in, 2300) == ARM_NO_LINK);
    in.link_ok = true;
    CHECK(arming_update(&a, &in, 2999) == ARM_ARMED);

    // Several dropouts in a row each resume.
    in.link_ok = false;
    arming_update(&a, &in, 3000);
    in.link_ok = true;
    CHECK(arming_update(&a, &in, 3100) == ARM_ARMED);
    in.link_ok = false;
    arming_update(&a, &in, 3200);
    in.link_ok = true;
    CHECK(arming_update(&a, &in, 3300) == ARM_ARMED);

    // Long outage (transmitter off and on): needs neutral again.
    in.link_ok = false;
    arming_update(&a, &in, 4000);
    in.link_ok = true;
    CHECK(arming_update(&a, &in, 5000) == ARM_WAIT_NEUTRAL);
    // ... and the expired resume does not fire later either.
    in.link_ok = false;
    arming_update(&a, &in, 5100);
    in.link_ok = true;
    CHECK(arming_update(&a, &in, 5200) == ARM_WAIT_NEUTRAL);

    // Arm switch turned off during a dropout: no resume.
    arm_now(&a, &in, 6000);
    CHECK(a.state == ARM_ARMED);
    in.sticks_neutral = false;
    in.link_ok = false;
    arming_update(&a, &in, 7000);
    in.link_ok = true;
    in.arm_switch = false;
    CHECK(arming_update(&a, &in, 7100) == ARM_SWITCH_OFF);
    in.arm_switch = true;
    CHECK(arming_update(&a, &in, 7200) == ARM_WAIT_NEUTRAL);

    // Resume window across millis() wrap-around.
    arm_now(&a, &in, 0xFFFFF000u);
    CHECK(a.state == ARM_ARMED);
    in.sticks_neutral = false;
    in.link_ok = false;
    arming_update(&a, &in, 0xFFFFFF00u);
    in.link_ok = true;
    CHECK(arming_update(&a, &in, 0x00000100u) == ARM_ARMED);
}

int main(void) {
    test_unpack_and_scale();
    test_sbus();
    test_crsf();
    test_mixer();
    test_arming();
    test_arming_resume();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
