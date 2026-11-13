// SPDX-License-Identifier: GPL-3.0-or-later
// Peripheral models behind include/simhw.h. See README.md in this directory
// for what is modelled and what is not.

#define _GNU_SOURCE
#include "simhw.h"
#undef printf

#include "sim.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#include "../src/pins.h"

#define NPINS       30
#define NSLICES     12
#define COST_TIME   30u     // ticks one timer read takes (0.2 us)

// --- core state -----------------------------------------------------------------
static uint64_t now_tk;
static uint64_t quantum_tk = 150;
static uint64_t run_target;
static bool in_irq, irq_masked, halted, in_fw;
static ucontext_t main_ctx, fw_ctx;
static void (*fw_entry)(void);
static int faults;
bool sim_echo;

uint32_t sim_vbat_mv = 7400;
uint32_t sim_motor_ma[2];
double sim_mcu_temp_c = 30.0;
sim_imu_t sim_imu = {.fitted = true, .acc_mg = {0, 0, 1000}, .temp_c = 25.0, .reset_us = 50};

static void advance(uint64_t target);
static void trace_mark_all(void);

void sim_fault(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "  SIM FAULT at %.3f ms: ", (double)now_tk / (double)MS(1));
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    faults++;
}

int sim_faults(void) { return faults; }
uint64_t sim_now(void) { return now_tk; }
double sim_now_ms(void) { return (double)now_tk / (double)MS(1); }
void sim_set_time(uint64_t tk) { now_tk = tk; }
void sim_set_quantum(uint64_t tk) { quantum_tk = tk ? tk : 1; }
bool sim_halted(void) { return halted; }

// --- console ----------------------------------------------------------------------
typedef struct {
    uint64_t tk;
    char *text;
} log_line_t;
static log_line_t *logv;
static size_t logn, logcap;
static char partial[1024];
static size_t partial_n;
static int keys[64];
static int key_head, key_tail;

int sim_printf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (const char *p = buf; *p; p++) {
        if (*p != '\n' && partial_n < sizeof(partial) - 1) {
            partial[partial_n++] = *p;
            continue;
        }
        partial[partial_n] = 0;
        if (logn == logcap) {
            logcap = logcap ? logcap * 2 : 256;
            logv = realloc(logv, logcap * sizeof(*logv));
        }
        logv[logn].tk = now_tk;
        logv[logn].text = strdup(partial);
        if (sim_echo) {
            fprintf(stderr, "    [%10.3f] %s\n", sim_now_ms(), partial);
        }
        logn++;
        partial_n = 0;
    }
    return n;
}

uint64_t sim_log_find(const char *needle, uint64_t after, const char **line) {
    for (size_t i = 0; i < logn; i++) {
        if (logv[i].tk >= after && strstr(logv[i].text, needle)) {
            if (line) {
                *line = logv[i].text;
            }
            return logv[i].tk ? logv[i].tk : 1;
        }
    }
    return 0;
}

int sim_log_count(const char *needle) {
    int n = 0;
    for (size_t i = 0; i < logn; i++) {
        n += strstr(logv[i].text, needle) != NULL;
    }
    return n;
}

void sim_key(int c) {
    keys[key_tail] = c;
    key_tail = (key_tail + 1) % 64;
}

int getchar_timeout_us(uint32_t timeout_us) {
    if (key_head == key_tail) {
        if (timeout_us) {
            advance(now_tk + US(timeout_us));
        }
        return PICO_ERROR_TIMEOUT;
    }
    int c = keys[key_head];
    key_head = (key_head + 1) % 64;
    return c;
}

bool stdio_init_all(void) { return true; }
bool stdio_usb_connected(void) { return true; }

// --- pads and external lines ----------------------------------------------------------
typedef struct {
    uint8_t func;
    bool oe, out, pu, pd, inover, adc;
    uint32_t irq_events;
} pad_t;
static pad_t pads[NPINS];

typedef struct {
    uint64_t t;
    uint8_t level;
} edge_t;
typedef struct {
    edge_t *e;
    size_t n, cap, irq_cur;
} line_t;
static line_t lines[NPINS];

static gpio_irq_callback_t gpio_cb;
static struct {
    uint gpio;
    uint32_t events;
} gpio_pending[64];
static int gpio_pending_n;

static bool imu_int1(void);

void sim_line(uint32_t pin, uint64_t tk, int level) {
    line_t *l = &lines[pin];
    if (tk < now_tk) {
        sim_fault("line %u driven in the past", pin);
        return;
    }
    // A change before edges that are already queued replaces them (a wire pulled mid-frame).
    while (l->n && l->e[l->n - 1].t >= tk) {
        l->n--;
    }
    if (l->irq_cur > l->n) {
        l->irq_cur = l->n;
    }
    if (l->n && l->e[l->n - 1].level == level) {
        return;
    }
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 1024;
        l->e = realloc(l->e, l->cap * sizeof(*l->e));
    }
    l->e[l->n].t = tk;
    l->e[l->n].level = (uint8_t)level;
    l->n++;
}

// Index of the last edge at or before t, -1 if none.
static long line_index(const line_t *l, uint64_t t) {
    long lo = 0, hi = (long)l->n - 1, r = -1;
    while (lo <= hi) {
        long mid = (lo + hi) / 2;
        if (l->e[mid].t <= t) {
            r = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return r;
}

static bool pin_in_at(uint pin, uint64_t t) {
    const pad_t *p = &pads[pin];
    long i = line_index(&lines[pin], t);
    int lv = i < 0 ? LINE_HIZ : lines[pin].e[i].level;
    if (pin == PIN_IMU_INT1 && sim_imu.fitted) {
        lv = imu_int1();
    }
    if (lv == LINE_HIZ) {
        lv = (p->func == GPIO_FUNC_SIO && p->oe) ? p->out : p->pu;
    }
    return (lv != 0) != p->inover;
}

static uint64_t line_next_edge(uint pin, uint64_t t) {
    const line_t *l = &lines[pin];
    long i = line_index(l, t) + 1;
    return i < (long)l->n ? l->e[i].t : UINT64_MAX;
}

static void pad_changed(uint gpio);

void gpio_set_function(uint gpio, gpio_function_t fn) {
    pads[gpio].func = (uint8_t)fn;
    pads[gpio].inover = false;      // the SDK rewrites the whole CTRL register
    pad_changed(gpio);
}

void gpio_init(uint gpio) {
    pads[gpio].oe = false;
    pads[gpio].out = false;
    gpio_set_function(gpio, GPIO_FUNC_SIO);
}

static void imu_cs(bool level);

void gpio_set_dir(uint gpio, bool out) {
    bool was = pads[gpio].oe;
    pads[gpio].oe = out;
    if (gpio == PIN_IMU_CS && was != out) {
        imu_cs(out ? pads[gpio].out : true);
    }
    pad_changed(gpio);
}

void gpio_put(uint gpio, bool value) {
    bool was = pads[gpio].out;
    pads[gpio].out = value;
    if (gpio == PIN_IMU_CS && pads[gpio].oe && was != value) {
        imu_cs(value);
    }
    pad_changed(gpio);
}

bool gpio_get(uint gpio) { return pin_in_at(gpio, now_tk); }

void gpio_pull_up(uint gpio) {
    pads[gpio].pu = true;
    pads[gpio].pd = false;
}

void gpio_pull_down(uint gpio) {
    pads[gpio].pu = false;
    pads[gpio].pd = true;
}

void gpio_set_inover(uint gpio, uint value) { pads[gpio].inover = value == GPIO_OVERRIDE_INVERT; }

void gpio_set_irq_enabled(uint gpio, uint32_t events, bool enabled) {
    if (enabled) {
        pads[gpio].irq_events |= events;
        lines[gpio].irq_cur = (size_t)(line_index(&lines[gpio], now_tk) + 1);
    } else {
        pads[gpio].irq_events &= ~events;
    }
}

void gpio_set_irq_enabled_with_callback(uint gpio, uint32_t events, bool enabled,
                                        gpio_irq_callback_t cb) {
    gpio_set_irq_enabled(gpio, events, enabled);
    gpio_cb = cb;
}

int sim_pad_out(uint32_t pin);
bool sim_led(void) { return sim_pad_out(PIN_LED_STATUS) == LINE_HIGH; }

// --- PWM ------------------------------------------------------------------------------
typedef struct {
    bool en;
    uint32_t top, div;
    uint16_t cc[2], pend[2];
    uint64_t pend_from[2];
    uint64_t t0;
} slice_t;
static slice_t sl[NSLICES];
static sim_pwm_hw_t pwm_regs;
sim_pwm_hw_t *const pwm_hw = &pwm_regs;

static uint64_t sl_period(const slice_t *s) { return (uint64_t)(s->top + 1) * s->div / 16; }

static uint64_t sl_next_wrap(const slice_t *s, uint64_t t) {
    uint64_t p = sl_period(s);
    return s->t0 + ((t - s->t0) / p + 1) * p;
}

static uint16_t sl_cc(const slice_t *s, int ch, uint64_t t) {
    return t >= s->pend_from[ch] ? s->pend[ch] : s->cc[ch];
}

static bool sl_level(const slice_t *s, int ch, uint64_t t) {
    uint32_t ctr = s->en ? (uint32_t)(((t - s->t0) * 16 / s->div) % (s->top + 1)) : 0;
    return ctr < sl_cc(s, ch, t);
}

void pwm_init(uint slice, pwm_config *c, bool start) {
    slice_t *s = &sl[slice];
    memset(s, 0, sizeof(*s));
    s->top = c->top;
    s->div = c->div ? c->div : 16;
    s->en = start;
    s->t0 = now_tk;
    if (start) {
        pwm_regs.en |= 1u << slice;
    } else {
        pwm_regs.en &= ~(1u << slice);
    }
    trace_mark_all();
}

void pwm_set_gpio_level(uint gpio, uint16_t level) {
    slice_t *s = &sl[pwm_gpio_to_slice_num(gpio)];
    int ch = gpio & 1;
    if (now_tk >= s->pend_from[ch]) {
        s->cc[ch] = s->pend[ch];
    }
    if (s->pend[ch] == level) {
        return;
    }
    s->pend[ch] = level;
    // The compare register is double-buffered: a running slice takes it at the next wrap.
    s->pend_from[ch] = s->en ? sl_next_wrap(s, now_tk) : 0;
    if (!s->en) {
        s->cc[ch] = level;
    }
    pad_changed(gpio);
}

void pwm_set_mask_enabled(uint32_t mask) {
    for (uint i = 0; i < NSLICES; i++) {
        bool en = (mask >> i) & 1u;
        if (en && !sl[i].en) {
            sl[i].t0 = now_tk;
        }
        sl[i].en = en;
    }
    pwm_regs.en = mask;
    trace_mark_all();
}

static int pad_out_at(uint pin, uint64_t t) {
    const pad_t *p = &pads[pin];
    if (p->func == GPIO_FUNC_PWM) {
        return sl_level(&sl[pwm_gpio_to_slice_num(pin)], pin & 1, t);
    }
    if (p->func == GPIO_FUNC_SIO && p->oe) {
        return p->out;
    }
    return LINE_HIZ;
}

int sim_pad_out(uint32_t pin) { return pad_out_at(pin, now_tk); }

// --- H-bridge and servo observation -----------------------------------------------------
static const uint motor_pins[2][2] = {{PIN_MA_IN1, PIN_MA_IN2}, {PIN_MB_IN1, PIN_MB_IN2}};
static const uint servo_pins[4] = {PIN_PWM1, PIN_PWM2, PIN_PWM3, PIN_PWM4};
static bridge_ev_t *btrace;
static size_t btrace_n, btrace_cap;
static servo_ev_t *strace;
static size_t strace_n, strace_cap;
static bool motor_dirty[2], servo_dirty[4];
static bridge_t motor_last[2] = {{0, 0, 0, 1000}, {0, 0, 0, 1000}};
static servo_t servo_last[4];

bridge_t sim_bridge(int m) {
    // Sample one PWM period, starting at the next wrap so that pending compare values count.
    const slice_t *ref = NULL;
    for (int i = 0; i < 2; i++) {
        const slice_t *s = &sl[pwm_gpio_to_slice_num(motor_pins[m][i])];
        if (pads[motor_pins[m][i]].func == GPIO_FUNC_PWM && s->en) {
            ref = s;
        }
    }
    uint32_t n = ref ? ref->top + 1 : 1;
    uint64_t base = ref ? sl_next_wrap(ref, now_tk) : now_tk;
    uint32_t count[4] = {0};
    for (uint32_t k = 0; k < n; k++) {
        uint64_t t = ref ? base + (uint64_t)k * ref->div / 16 : base;
        // The DRV8231A inputs have 100 k pull-downs: a floating pad reads low.
        int in1 = pad_out_at(motor_pins[m][0], t) == LINE_HIGH;
        int in2 = pad_out_at(motor_pins[m][1], t) == LINE_HIGH;
        count[in1 | (in2 << 1)]++;
    }
    bridge_t b;
    b.coast = (uint16_t)((count[0] * 1000ull + n / 2) / n);
    b.fwd = (uint16_t)((count[1] * 1000ull + n / 2) / n);
    b.rev = (uint16_t)((count[2] * 1000ull + n / 2) / n);
    b.brake = (uint16_t)((count[3] * 1000ull + n / 2) / n);
    return b;
}

servo_t sim_servo(int idx) {
    servo_t r = {0, 0};
    uint pin = servo_pins[idx];
    const slice_t *s = &sl[pwm_gpio_to_slice_num(pin)];
    if (pads[pin].func == GPIO_FUNC_PWM && s->en) {
        uint64_t t = sl_next_wrap(s, now_tk);
        uint32_t cc = sl_cc(s, pin & 1, t);
        if (cc > s->top + 1) {
            cc = s->top + 1;
        }
        r.period_us = (uint32_t)(sl_period(s) / TK_PER_US);
        r.pulse_us = (uint32_t)((uint64_t)cc * s->div / 16 / TK_PER_US);
    } else if (pad_out_at(pin, now_tk) == LINE_HIGH) {
        r.pulse_us = r.period_us = UINT32_MAX;      // held high
    }
    return r;
}

size_t sim_bridge_trace(const bridge_ev_t **ev) {
    *ev = btrace;
    return btrace_n;
}

size_t sim_servo_trace(const servo_ev_t **ev) {
    *ev = strace;
    return strace_n;
}

static void pad_changed(uint gpio) {
    for (int m = 0; m < 2; m++) {
        if (gpio == motor_pins[m][0] || gpio == motor_pins[m][1]) {
            motor_dirty[m] = true;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (gpio == servo_pins[i]) {
            servo_dirty[i] = true;
        }
    }
}

static void trace_mark_all(void) {
    motor_dirty[0] = motor_dirty[1] = true;
    for (int i = 0; i < 4; i++) {
        servo_dirty[i] = true;
    }
}

static void trace_flush(void) {
    for (int m = 0; m < 2; m++) {
        if (!motor_dirty[m]) {
            continue;
        }
        motor_dirty[m] = false;
        bridge_t b = sim_bridge(m);
        if (memcmp(&b, &motor_last[m], sizeof(b)) == 0) {
            continue;
        }
        motor_last[m] = b;
        if (btrace_n == btrace_cap) {
            btrace_cap = btrace_cap ? btrace_cap * 2 : 1024;
            btrace = realloc(btrace, btrace_cap * sizeof(*btrace));
        }
        btrace[btrace_n++] = (bridge_ev_t){now_tk, m, b};
    }
    for (int i = 0; i < 4; i++) {
        if (!servo_dirty[i]) {
            continue;
        }
        servo_dirty[i] = false;
        servo_t s = sim_servo(i);
        if (memcmp(&s, &servo_last[i], sizeof(s)) == 0) {
            continue;
        }
        servo_last[i] = s;
        if (strace_n == strace_cap) {
            strace_cap = strace_cap ? strace_cap * 2 : 1024;
            strace = realloc(strace, strace_cap * sizeof(*strace));
        }
        strace[strace_n++] = (servo_ev_t){now_tk, i, s};
    }
}

// --- ADC ------------------------------------------------------------------------------
static bool adc_on, adc_temp_on;
static uint adc_ch;

void adc_init(void) { adc_on = true; }
void adc_gpio_init(uint gpio) { pads[gpio].adc = true; }
void adc_set_temp_sensor_enabled(bool enable) { adc_temp_on = enable; }
void adc_select_input(uint input) { adc_ch = input; }

uint16_t adc_read(void) {
    static const uint ch_pin[3] = {PIN_MA_IPROPI, PIN_MB_IPROPI, PIN_VBAT_SENSE};
    double v = 0;
    if (!adc_on) {
        sim_fault("adc_read before adc_init");
    }
    if (adc_ch < 3 && !pads[ch_pin[adc_ch]].adc) {
        sim_fault("ADC channel %u read without adc_gpio_init", adc_ch);
    }
    switch (adc_ch) {
    case 0:
    case 1:     // IPROPI: 1500 uA/A into 1.5 k, clamped by VREF regulation near 3.3 V
        v = sim_motor_ma[adc_ch] * 1e-3 * DRV_AIPROPI_UA_PER_A * 1e-6 * DRV_RIPROPI_OHM;
        break;
    case 2:
        v = sim_vbat_mv * 1e-3 * VBAT_DIV_BOT_OHM / (VBAT_DIV_TOP_OHM + VBAT_DIV_BOT_OHM);
        break;
    case ADC_TEMPERATURE_CHANNEL_NUM:
        if (!adc_temp_on) {
            sim_fault("temperature sensor read while disabled");
        }
        v = 0.706 - (sim_mcu_temp_c - 27.0) * 0.001721;
        break;
    default:
        sim_fault("ADC channel %u is not connected", adc_ch);
    }
    advance(now_tk + US(2));        // 96 cycles of the 48 MHz ADC clock
    double raw = v / 3.3 * 4096.0;
    return (uint16_t)(raw < 0 ? 0 : raw > 4095 ? 4095 : raw);
}

// --- SPI0 and the LSM6DSV32X ------------------------------------------------------------
struct sim_spi {
    bool inited;
    uint baud;
    int cpol, cpha, order, bits;
};
spi_inst_t sim_spi0;

static struct {
    uint8_t r[0x80];
    bool selected, rd;
    int idx;
    uint8_t addr;
    uint64_t reset_done, g_t0, g_read_idx;
} imu;

static void imu_reset_regs(void) {
    memset(imu.r, 0, sizeof(imu.r));
    imu.r[0x0F] = 0x70;     // WHO_AM_I
    imu.r[0x12] = 0x44;     // CTRL3: BDU, IF_INC
}

static double odr_hz(uint8_t code) {
    static const double hz[16] = {0, 1.875, 7.5, 15, 30, 60, 120, 240, 480, 960, 1920, 3840, 7680};
    return hz[code & 0x0F];
}

static uint64_t imu_sample_idx(void) {
    double hz = odr_hz(imu.r[0x11]);
    if (hz == 0) {
        return 0;
    }
    return (uint64_t)((double)(now_tk - imu.g_t0) * hz / SIM_SYS_HZ);
}

static bool imu_int1(void) {
    return (imu.r[0x0D] & 0x02) && imu_sample_idx() > imu.g_read_idx;
}

static int16_t sat16(double v) {
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : lround(v));
}

static uint8_t imu_rd(uint8_t a) {
    if (a == 0x12 && (imu.r[a] & 1) && now_tk >= imu.reset_done) {
        imu.r[a] &= (uint8_t)~1u;
    }
    if (a < 0x20 || a > 0x2D) {
        return imu.r[a];
    }
    static const double g_mdps[16] = {4.375, 8.75, 17.5, 35, 70, [0x0C] = 140};
    static const double xl_mg[4] = {0.122, 0.244, 0.488, 0.976};
    int16_t v = 0;
    int i = (a - 0x20) / 2;
    if (i == 0) {
        v = sat16((sim_imu.temp_c - 25.0) * 256.0);
    } else if (i <= 3) {
        double sens = g_mdps[imu.r[0x15] & 0x0F];
        if (odr_hz(imu.r[0x11]) > 0 && sens > 0) {
            v = sat16(sim_imu.gyro_dps[i - 1] * 1000.0 / sens);
        }
        if (a & 1) {
            imu.g_read_idx = imu_sample_idx();      // reading an output high byte clears DRDY
        }
    } else if (odr_hz(imu.r[0x10]) > 0) {
        v = sat16(sim_imu.acc_mg[i - 4] / xl_mg[imu.r[0x17] & 3]);
    }
    return (uint8_t)((a & 1) ? (uint16_t)v >> 8 : (uint16_t)v & 0xFF);
}

static void imu_wr(uint8_t a, uint8_t v) {
    if (a == 0x0F || (a >= 0x20 && a <= 0x2D)) {
        return;
    }
    if (a == 0x12 && (v & 1)) {
        imu_reset_regs();
        imu.r[0x12] |= 1;
        imu.reset_done = now_tk + US(sim_imu.reset_us);
        return;
    }
    if (a == 0x11 && (imu.r[a] & 0x0F) != (v & 0x0F)) {
        imu.g_t0 = now_tk;
        imu.g_read_idx = 0;
    }
    imu.r[a] = v;
}

static void imu_cs(bool level) {
    imu.selected = !level;
    imu.idx = 0;
}

static uint8_t imu_xfer(uint8_t tx) {
    const struct sim_spi *s = &sim_spi0;
    if (!sim_imu.fitted || !imu.selected) {
        return 0x00;        // MISO floats; the pad's reset pull-down reads it as 0
    }
    if (!s->inited || pads[PIN_IMU_MISO].func != GPIO_FUNC_SPI ||
        pads[PIN_IMU_SCK].func != GPIO_FUNC_SPI || pads[PIN_IMU_MOSI].func != GPIO_FUNC_SPI) {
        sim_fault("IMU transfer with SPI0 or its pads not set up");
        return 0x00;
    }
    if (s->cpol != s->cpha || s->order != SPI_MSB_FIRST || s->bits != 8) {
        sim_fault("IMU needs SPI mode 0 or 3, MSB first, 8 bits");
        return 0xFF;
    }
    if (s->baud > 10000000u) {
        sim_fault("IMU SPI clock %u Hz above 10 MHz", s->baud);
    }
    if (imu.idx++ == 0) {
        imu.rd = tx & 0x80;
        imu.addr = tx & 0x7F;
        return 0;
    }
    uint8_t v = 0;
    if (imu.rd) {
        v = imu_rd(imu.addr);
    } else {
        imu_wr(imu.addr, tx);
    }
    if (imu.r[0x12] & 0x04) {
        imu.addr = (imu.addr + 1) & 0x7F;
    }
    return v;
}

uint spi_init(spi_inst_t *spi, uint baudrate) {
    spi->inited = true;
    spi->baud = baudrate;
    spi->bits = 8;
    spi->order = SPI_MSB_FIRST;
    return baudrate;
}

void spi_set_format(spi_inst_t *spi, uint data_bits, int cpol, int cpha, int order) {
    spi->bits = (int)data_bits;
    spi->cpol = cpol;
    spi->cpha = cpha;
    spi->order = order;
}

static void spi_time(const spi_inst_t *spi, size_t len) {
    advance(now_tk + (uint64_t)len * 8 * SIM_SYS_HZ / (spi->baud ? spi->baud : 1000000u));
}

int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len) {
    for (size_t i = 0; i < len; i++) {
        imu_xfer(src[i]);
    }
    spi_time(spi, len);
    return (int)len;
}

int spi_read_blocking(spi_inst_t *spi, uint8_t repeated_tx_data, uint8_t *dst, size_t len) {
    for (size_t i = 0; i < len; i++) {
        dst[i] = imu_xfer(repeated_tx_data);
    }
    spi_time(spi, len);
    return (int)len;
}

// --- UART0 receiver (PL011) ---------------------------------------------------------------
struct sim_uart {
    bool inited, fifo_en, rx_irq;
    uint baud, data_bits, stop_bits;
    int parity;
    uint8_t fifo[32];
    int n, head;
    uint64_t last_rx;
    uint32_t overruns, dropped;
};
uart_inst_t sim_uart0;

typedef struct {
    uint64_t t;
    uint8_t byte;
    double baud;
} uart_byte_t;
static uart_byte_t *urx;
static size_t urx_n, urx_cap, urx_cur;

uint64_t sim_uart_rx(uint64_t tk, const uint8_t *data, size_t len, double baud) {
    double bit = (double)SIM_SYS_HZ / baud;
    for (size_t i = 0; i < len; i++) {
        if (urx_n == urx_cap) {
            urx_cap = urx_cap ? urx_cap * 2 : 4096;
            urx = realloc(urx, urx_cap * sizeof(*urx));
        }
        // A byte reaches the FIFO in the middle of its stop bit.
        urx[urx_n++] = (uart_byte_t){tk + (uint64_t)(bit * (10.0 * (double)i + 9.5)), data[i], baud};
    }
    return tk + (uint64_t)(bit * 10.0 * (double)len);
}

static uint64_t uart_rt_time(void) {
    const struct sim_uart *u = &sim_uart0;
    return u->last_rx + 32ull * SIM_SYS_HZ / (u->baud ? u->baud : 115200u);
}

static bool uart_irq_asserted(void) {
    const struct sim_uart *u = &sim_uart0;
    if (!u->rx_irq || u->n == 0) {
        return false;
    }
    return u->n >= (u->fifo_en ? 4 : 1) || now_tk >= uart_rt_time();
}

static void uart_deliver(const uart_byte_t *b) {
    struct sim_uart *u = &sim_uart0;
    bool ok = u->inited && pads[PIN_RC_RX].func == GPIO_FUNC_UART && u->data_bits == 8 &&
              u->parity == UART_PARITY_NONE && fabs(b->baud - u->baud) / u->baud < 0.03;
    if (!ok) {
        u->dropped++;
        return;
    }
    if (u->n == (u->fifo_en ? 32 : 1)) {
        u->overruns++;
        return;
    }
    u->fifo[(u->head + u->n) % 32] = b->byte;
    u->n++;
    u->last_rx = b->t;
}

uint uart_init(uart_inst_t *uart, uint baudrate) {
    memset(uart, 0, sizeof(*uart));
    uart->inited = true;
    uart->baud = baudrate;
    uart->data_bits = 8;
    uart->stop_bits = 1;
    uart->fifo_en = true;
    return baudrate;
}

void uart_set_format(uart_inst_t *uart, uint data_bits, uint stop_bits, int parity) {
    uart->data_bits = data_bits;
    uart->stop_bits = stop_bits;
    uart->parity = parity;
}

void uart_set_hw_flow(uart_inst_t *uart, bool cts, bool rts) {
    if (cts || rts) {
        sim_fault("UART0 has no flow control pins on this board");
    }
    (void)uart;
}

void uart_set_fifo_enabled(uart_inst_t *uart, bool enabled) { uart->fifo_en = enabled; }
bool uart_is_readable(uart_inst_t *uart) { return uart->n > 0; }

char uart_getc(uart_inst_t *uart) {
    if (uart->n == 0) {
        sim_fault("uart_getc on an empty FIFO would block");
        return 0;
    }
    uint8_t b = uart->fifo[uart->head];
    uart->head = (uart->head + 1) % 32;
    uart->n--;
    return (char)b;
}

void uart_set_irq_enables(uart_inst_t *uart, bool rx_has_data, bool tx_needs_data) {
    uart->rx_irq = rx_has_data;
    (void)tx_needs_data;
}

// --- PIO0 -----------------------------------------------------------------------------------
typedef struct {
    bool enabled, claimed, irq_rxne;
    pio_sm_config c;
    uint8_t pc, isr_cnt, delay;
    uint32_t x, y, isr;
    uint64_t next_tk;
    uint32_t frac;
    uint32_t fifo[8];
    uint8_t fifo_n, fifo_head, fifo_cap;
} sm_t;
struct sim_pio {
    uint16_t mem[32];
    uint8_t used;
    sm_t sm[4];
};
struct sim_pio sim_pio0;

static irq_handler_t irq_handlers[64];
static bool irq_enabled[64];

uint pio_add_program(PIO pio, const pio_program_t *program) {
    uint offset = 32u - pio->used - program->length;
    for (uint i = 0; i < program->length; i++) {
        uint16_t ins = program->instructions[i];
        if ((ins >> 13) == 0) {
            ins = (uint16_t)(ins + offset);     // relocate JMP targets
        }
        pio->mem[offset + i] = ins;
    }
    pio->used = (uint8_t)(pio->used + program->length);
    return offset;
}

int pio_claim_unused_sm(PIO pio, bool required) {
    for (int i = 0; i < 4; i++) {
        if (!pio->sm[i].claimed) {
            pio->sm[i].claimed = true;
            return i;
        }
    }
    if (required) {
        sim_fault("no free PIO state machine");
    }
    return -1;
}

void pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint pin, uint count, bool is_out) {
    if (is_out) {
        sim_fault("PIO pin %u set as output; the receiver pad must stay an input", pin);
    }
    (void)pio; (void)sm; (void)count;
}

void pio_gpio_init(PIO pio, uint pin) {
    (void)pio;
    gpio_set_function(pin, GPIO_FUNC_PIO0);
}

void pio_sm_init(PIO pio, uint sm, uint initial_pc, const pio_sm_config *config) {
    sm_t *s = &pio->sm[sm];
    bool claimed = s->claimed, irq = s->irq_rxne;
    memset(s, 0, sizeof(*s));
    s->claimed = claimed;
    s->irq_rxne = irq;
    s->c = *config;
    s->pc = (uint8_t)initial_pc;
    s->fifo_cap = config->fifo_join == PIO_FIFO_JOIN_RX ? 8 : config->fifo_join ? 0 : 4;
}

void pio_sm_set_enabled(PIO pio, uint sm, bool enabled) {
    sm_t *s = &pio->sm[sm];
    if (enabled && !s->enabled) {
        s->next_tk = now_tk;
    }
    s->enabled = enabled;
}

bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm) { return pio->sm[sm].fifo_n == 0; }

uint32_t pio_sm_get(PIO pio, uint sm) {
    sm_t *s = &pio->sm[sm];
    if (s->fifo_n == 0) {
        sim_fault("pio_sm_get on an empty RX FIFO");
        return 0;
    }
    uint32_t v = s->fifo[s->fifo_head];
    s->fifo_head = (uint8_t)((s->fifo_head + 1) % 8);
    s->fifo_n--;
    return v;
}

void pio_set_irq0_source_enabled(PIO pio, pio_interrupt_source_t source, bool enabled) {
    pio->sm[source].irq_rxne = enabled;
}

static void sm_skip(sm_t *s, uint64_t cycles) {
    uint64_t total = cycles * s->c.clkdiv_fp + s->frac;
    s->next_tk += total >> 8;
    s->frac = (uint32_t)(total & 0xFF);
}

static bool sm_push(sm_t *s) {
    if (s->fifo_n == s->fifo_cap) {
        return false;
    }
    s->fifo[(s->fifo_head + s->fifo_n) % 8] = s->isr;
    s->fifo_n++;
    s->isr = 0;
    s->isr_cnt = 0;
    return true;
}

static bool pio_irq_asserted(void) {
    for (int i = 0; i < 4; i++) {
        if (sim_pio0.sm[i].irq_rxne && sim_pio0.sm[i].fifo_n) {
            return true;
        }
    }
    return false;
}

// Run one state machine up to `limit`. Returns the time of the cycle that raised an
// interrupt the CPU can take now, or `limit`.
static uint64_t sm_run(struct sim_pio *pio, sm_t *s, uint64_t limit) {
    bool can_irq = irq_enabled[PIO0_IRQ_0] && !irq_masked && !in_irq && !halted;
    while (s->enabled && s->next_tk <= limit) {
        uint64_t t = s->next_tk;
        if (s->delay) {
            s->delay--;
            sm_skip(s, 1);
            continue;
        }
        uint16_t ins = pio->mem[s->pc];
        uint op = ins >> 13, arg = ins & 0x1F, mid = (ins >> 5) & 7;
        int jump = -1;
        bool stall = false, pushed = false;
        switch (op) {
        case 0: {   // JMP
            bool take = false;
            switch (mid) {
            case 0: take = true; break;
            case 1: take = s->x == 0; break;
            case 2: take = s->x != 0; s->x--; break;
            case 3: take = s->y == 0; break;
            case 4: take = s->y != 0; s->y--; break;
            case 5: take = s->x != s->y; break;
            case 6: take = pin_in_at(s->c.jmp_pin, t); break;
            default: sim_fault("PIO: JMP !OSRE not modelled"); break;
            }
            if (take) {
                jump = (int)arg;
            }
            break;
        }
        case 1: {   // WAIT
            bool pol = (ins >> 7) & 1;
            uint src = mid & 3;
            uint pin = src == 0 ? arg : (s->c.in_base + arg) % 32;
            if (src > 1 || pin >= NPINS) {
                sim_fault("PIO: WAIT source not modelled");
                break;
            }
            if (pin_in_at(pin, t) != pol) {
                // Nothing changes until the next edge on that pad: skip the idle cycles.
                uint64_t te = line_next_edge(pin, t);
                uint64_t until = te < limit ? te : limit + 1;
                uint64_t n = (until - t) * 256 / s->c.clkdiv_fp;
                sm_skip(s, n ? n : 1);
                continue;
            }
            break;
        }
        case 2: {   // IN
            uint cnt = arg ? arg : 32;
            uint32_t data = 0;
            switch (mid) {
            case 0:
                for (uint i = 0; i < cnt; i++) {
                    uint pin = (s->c.in_base + i) % 32;
                    data |= (uint32_t)(pin < NPINS && pin_in_at(pin, t)) << i;
                }
                break;
            case 1: data = s->x; break;
            case 2: data = s->y; break;
            case 3: data = 0; break;
            case 6: data = s->isr; break;
            default: sim_fault("PIO: IN source not modelled"); break;
            }
            if (cnt < 32) {
                data &= (1u << cnt) - 1;
                s->isr = s->c.shift_right ? (s->isr >> cnt) | (data << (32 - cnt))
                                          : (s->isr << cnt) | data;
            } else {
                s->isr = data;
            }
            s->isr_cnt = (uint8_t)(s->isr_cnt + cnt > 32 ? 32 : s->isr_cnt + cnt);
            if (s->c.autopush && s->isr_cnt >= s->c.push_threshold) {
                pushed = sm_push(s);
                stall = !pushed;
            }
            break;
        }
        case 4:     // PUSH
            if (ins & 0x80) {
                sim_fault("PIO: PULL not modelled");
            } else if (sm_push(s)) {
                pushed = true;
            } else if (ins & 0x20) {
                stall = true;       // blocking push on a full FIFO
            }
            break;
        case 7:     // SET
            if (mid == 1) {
                s->x = arg;
            } else if (mid == 2) {
                s->y = arg;
            }
            break;
        default:
            sim_fault("PIO: opcode %u not modelled", op);
            break;
        }
        sm_skip(s, 1);
        if (stall) {
            continue;
        }
        s->delay = (ins >> 8) & 0x1F;
        s->pc = jump >= 0 ? (uint8_t)jump : s->pc == s->c.wrap ? s->c.wrap_target : (uint8_t)(s->pc + 1);
        if (pushed && s->irq_rxne && can_irq) {
            return t;
        }
    }
    return limit;
}

static uint64_t pio_run_until(uint64_t limit) {
    for (int i = 0; i < 4; i++) {
        uint64_t t = sm_run(&sim_pio0, &sim_pio0.sm[i], limit);
        if (t < limit) {
            limit = t;
        }
    }
    return limit;
}

// --- interrupts ---------------------------------------------------------------------------
void irq_set_exclusive_handler(uint num, irq_handler_t handler) { irq_handlers[num] = handler; }
void irq_set_enabled(uint num, bool enabled) { irq_enabled[num] = enabled; }

static bool irq_any_pending(void) {
    return (irq_enabled[PIO0_IRQ_0] && pio_irq_asserted()) ||
           (irq_enabled[UART0_IRQ] && uart_irq_asserted()) || gpio_pending_n > 0;
}

static void dispatch_irqs(void) {
    if (in_irq || irq_masked || halted) {
        return;
    }
    in_irq = true;
    for (int guard = 0; guard < 100000; guard++) {
        if (irq_enabled[PIO0_IRQ_0] && pio_irq_asserted() && irq_handlers[PIO0_IRQ_0]) {
            irq_handlers[PIO0_IRQ_0]();
        } else if (irq_enabled[UART0_IRQ] && uart_irq_asserted() && irq_handlers[UART0_IRQ]) {
            irq_handlers[UART0_IRQ]();
        } else if (gpio_pending_n > 0) {
            uint gpio = gpio_pending[0].gpio;
            uint32_t events = gpio_pending[0].events;
            memmove(&gpio_pending[0], &gpio_pending[1], sizeof(gpio_pending[0]) * (size_t)--gpio_pending_n);
            if (gpio_cb) {
                gpio_cb(gpio, events);
            }
        } else {
            in_irq = false;
            return;
        }
    }
    sim_fault("interrupt handler does not clear its source");
    in_irq = false;
}

uint32_t save_and_disable_interrupts(void) {
    uint32_t was = irq_masked;
    irq_masked = true;
    return was;
}

void restore_interrupts(uint32_t status) {
    irq_masked = status;
    if (!irq_masked) {
        dispatch_irqs();
    }
}

// --- watchdog -----------------------------------------------------------------------------
static bool wd_on;
static uint64_t wd_period, wd_deadline, wd_fired;

void watchdog_enable(uint32_t delay_ms, bool pause_on_debug) {
    (void)pause_on_debug;
    wd_on = true;
    wd_period = MS(delay_ms);
    wd_deadline = now_tk + wd_period;
}

void watchdog_update(void) { wd_deadline = now_tk + wd_period; }
bool watchdog_enable_caused_reboot(void) { return false; }
uint64_t sim_watchdog_fired_at(void) { return wd_fired; }

// --- scheduler ------------------------------------------------------------------------------
typedef struct {
    uint64_t tk;
    sim_event_fn fn;
    void *arg;
} event_t;
static event_t events[64];
static int events_n;

void sim_at(uint64_t tk, sim_event_fn fn, void *arg) {
    if (events_n == 64) {
        sim_fault("too many scheduled events");
        return;
    }
    events[events_n++] = (event_t){tk, fn, arg};
}

static uint64_t next_due(void) {
    uint64_t t = UINT64_MAX;
    for (int i = 0; i < events_n; i++) {
        t = events[i].tk < t ? events[i].tk : t;
    }
    if (urx_cur < urx_n && urx[urx_cur].t < t) {
        t = urx[urx_cur].t;
    }
    if (sim_uart0.rx_irq && sim_uart0.n > 0 && uart_rt_time() > now_tk && uart_rt_time() < t) {
        t = uart_rt_time();
    }
    for (uint p = 0; p < NPINS; p++) {
        const line_t *l = &lines[p];
        if (pads[p].irq_events && l->irq_cur < l->n && l->e[l->irq_cur].t < t) {
            t = l->e[l->irq_cur].t;
        }
    }
    if (wd_on && wd_deadline < t) {
        t = wd_deadline;
    }
    return t;
}

static void process_due(void) {
    for (int i = 0; i < events_n;) {
        if (events[i].tk <= now_tk) {
            event_t e = events[i];
            events[i] = events[--events_n];
            e.fn(e.arg);
            i = 0;
        } else {
            i++;
        }
    }
    while (urx_cur < urx_n && urx[urx_cur].t <= now_tk) {
        uart_deliver(&urx[urx_cur++]);
    }
    for (uint p = 0; p < NPINS; p++) {
        line_t *l = &lines[p];
        while (pads[p].irq_events && l->irq_cur < l->n && l->e[l->irq_cur].t <= now_tk) {
            uint64_t t = l->e[l->irq_cur++].t;
            bool before = pin_in_at(p, t - 1), after = pin_in_at(p, t);
            uint32_t ev = (!before && after ? GPIO_IRQ_EDGE_RISE : 0) |
                          (before && !after ? GPIO_IRQ_EDGE_FALL : 0);
            ev &= pads[p].irq_events;
            if (ev && gpio_pending_n < 64) {
                gpio_pending[gpio_pending_n].gpio = p;
                gpio_pending[gpio_pending_n++].events = ev;
            }
        }
    }
}

static void watchdog_fire(void) {
    wd_fired = now_tk;
    wd_on = false;
    halted = true;
    // The reset returns every pad to its reset state: input, not driven.
    for (uint p = 0; p < NPINS; p++) {
        pads[p].func = GPIO_FUNC_NULL;
        pads[p].oe = false;
        pads[p].inover = false;
        pads[p].irq_events = 0;
    }
    memset(sl, 0, sizeof(sl));
    pwm_regs.en = 0;
    for (int i = 0; i < 4; i++) {
        sim_pio0.sm[i].enabled = false;
    }
    trace_mark_all();
    trace_flush();
    if (in_fw) {
        in_fw = false;
        setcontext(&main_ctx);
    }
}

static void advance(uint64_t target) {
    if (in_irq) {
        return;     // handlers are modelled as taking no time
    }
    for (;;) {
        trace_flush();
        dispatch_irqs();
        uint64_t t = target, due = next_due();
        if (due < t) {
            t = due;
        }
        if (in_fw && run_target < t) {
            t = run_target;
        }
        if (t < now_tk) {
            t = now_tk;
        }
        now_tk = pio_run_until(t);
        process_due();
        if (wd_on && now_tk >= wd_deadline) {
            watchdog_fire();
        }
        if (in_fw && now_tk >= run_target) {
            trace_flush();
            in_fw = false;
            swapcontext(&fw_ctx, &main_ctx);
        }
        if (now_tk >= target && (irq_masked || halted || !irq_any_pending())) {
            return;
        }
    }
}

// --- firmware-visible time ---------------------------------------------------------------------
uint32_t time_us_32(void) {
    advance(now_tk + COST_TIME);
    return (uint32_t)(now_tk / TK_PER_US);
}

uint64_t time_us_64(void) {
    advance(now_tk + COST_TIME);
    return now_tk / TK_PER_US;
}

absolute_time_t get_absolute_time(void) { return time_us_64(); }
void sleep_ms(uint32_t ms) { advance(now_tk + MS(ms)); }
void tight_loop_contents(void) { advance(now_tk + quantum_tk); }

// --- running the firmware ------------------------------------------------------------------------
static void fw_trampoline(void) {
    fw_entry();
    sim_fault("firmware main() returned");
    halted = true;
    in_fw = false;
    setcontext(&main_ctx);
}

void sim_reset(void) {
    for (uint p = 0; p < NPINS; p++) {
        pads[p] = (pad_t){.func = GPIO_FUNC_NULL, .pd = true};
    }
    imu_reset_regs();
}

void sim_boot(void (*entry)(void)) {
    static char stack[1 << 20];
    fw_entry = entry;
    getcontext(&fw_ctx);
    fw_ctx.uc_stack.ss_sp = stack;
    fw_ctx.uc_stack.ss_size = sizeof(stack);
    fw_ctx.uc_link = &main_ctx;
    makecontext(&fw_ctx, fw_trampoline, 0);
}

void sim_run_until(uint64_t tk) {
    if (tk <= now_tk) {
        return;
    }
    if (halted) {
        advance(tk);
        return;
    }
    run_target = tk;
    in_fw = true;
    swapcontext(&main_ctx, &fw_ctx);
}

void sim_run_ms(uint32_t ms) { sim_run_until(now_tk + MS(ms)); }

uint64_t sim_line_uart(uint32_t pin, uint64_t tk, const uint8_t *data, size_t len, double baud,
                       bool even_parity, int stop_bits, bool inverted) {
    double bit = (double)SIM_SYS_HZ / baud, t = (double)tk;
    int idle = inverted ? LINE_LOW : LINE_HIGH, active = inverted ? LINE_HIGH : LINE_LOW;
    for (size_t i = 0; i < len; i++) {
        int ones = 0;
        sim_line(pin, (uint64_t)t, active);     // start bit
        t += bit;
        for (int b = 0; b < 8; b++) {
            int v = (data[i] >> b) & 1;
            ones += v;
            sim_line(pin, (uint64_t)t, v ? idle : active);
            t += bit;
        }
        if (even_parity) {
            sim_line(pin, (uint64_t)t, (ones & 1) ? idle : active);
            t += bit;
        }
        sim_line(pin, (uint64_t)t, idle);
        t += bit * stop_bits;
    }
    return (uint64_t)t;
}
