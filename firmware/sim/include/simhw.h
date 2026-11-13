// SPDX-License-Identifier: GPL-3.0-or-later
// Host stand-in for the parts of the Pico SDK that the Hyperion firmware uses.
//
// The firmware sources in ../src are compiled unchanged against this header.
// Behind the API sits a model of the RP2354A peripherals (see simhw.c):
// virtual time in system-clock ticks, PWM slices, a PIO state machine that
// executes the real assembled program, the PL011 RX FIFO and its interrupts,
// GPIO pads with pulls / input override / edge interrupts, the watchdog, the
// ADC and an LSM6DSV32X on SPI0.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef unsigned int uint;

// --- stdio: firmware printf goes to the simulator log ------------------------
int sim_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define printf sim_printf
#define PICO_ERROR_TIMEOUT (-2)
int getchar_timeout_us(uint32_t timeout_us);
bool stdio_init_all(void);
bool stdio_usb_connected(void);

// --- time -------------------------------------------------------------------
typedef uint64_t absolute_time_t;
uint32_t time_us_32(void);
uint64_t time_us_64(void);
absolute_time_t get_absolute_time(void);
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }
void sleep_ms(uint32_t ms);
void tight_loop_contents(void);

// --- clocks -----------------------------------------------------------------
#define SIM_SYS_HZ 150000000u
enum { clk_sys = 5 };
static inline uint32_t clock_get_hz(int clk) { (void)clk; return SIM_SYS_HZ; }

// --- interrupts -------------------------------------------------------------
enum { PIO0_IRQ_0 = 15, UART0_IRQ = 33, IO_IRQ_BANK0 = 21 };
typedef void (*irq_handler_t)(void);
void irq_set_exclusive_handler(uint num, irq_handler_t handler);
void irq_set_enabled(uint num, bool enabled);
uint32_t save_and_disable_interrupts(void);
void restore_interrupts(uint32_t status);

// --- gpio -------------------------------------------------------------------
typedef enum {
    GPIO_FUNC_SPI = 1, GPIO_FUNC_UART = 2, GPIO_FUNC_PWM = 4, GPIO_FUNC_SIO = 5,
    GPIO_FUNC_PIO0 = 6, GPIO_FUNC_NULL = 0x1f,
} gpio_function_t;
enum { GPIO_IN = 0, GPIO_OUT = 1 };
enum { GPIO_OVERRIDE_NORMAL = 0, GPIO_OVERRIDE_INVERT = 1 };
enum { GPIO_IRQ_EDGE_FALL = 0x4, GPIO_IRQ_EDGE_RISE = 0x8 };
typedef void (*gpio_irq_callback_t)(uint gpio, uint32_t events);

void gpio_init(uint gpio);
void gpio_set_function(uint gpio, gpio_function_t fn);
void gpio_set_dir(uint gpio, bool out);
void gpio_put(uint gpio, bool value);
bool gpio_get(uint gpio);
void gpio_pull_up(uint gpio);
void gpio_pull_down(uint gpio);
void gpio_set_inover(uint gpio, uint value);
void gpio_set_irq_enabled(uint gpio, uint32_t events, bool enabled);
void gpio_set_irq_enabled_with_callback(uint gpio, uint32_t events, bool enabled,
                                        gpio_irq_callback_t cb);

// --- pwm --------------------------------------------------------------------
typedef struct { uint32_t csr, div, top; } pwm_config;      // div: 8.4 fixed point
typedef struct { uint32_t en; } sim_pwm_hw_t;
extern sim_pwm_hw_t *const pwm_hw;

static inline pwm_config pwm_get_default_config(void) {
    pwm_config c = {0, 1u << 4, 0xffff};
    return c;
}
static inline void pwm_config_set_wrap(pwm_config *c, uint16_t wrap) { c->top = wrap; }
static inline void pwm_config_set_clkdiv_int(pwm_config *c, uint div) { c->div = div << 4; }
static inline void pwm_config_set_clkdiv(pwm_config *c, float div) {
    c->div = (uint32_t)(div * 16.0f);
}
static inline uint pwm_gpio_to_slice_num(uint gpio) { return (gpio >> 1) & 7u; }
void pwm_init(uint slice, pwm_config *c, bool start);
void pwm_set_gpio_level(uint gpio, uint16_t level);
void pwm_set_mask_enabled(uint32_t mask);

// --- adc --------------------------------------------------------------------
#define ADC_TEMPERATURE_CHANNEL_NUM 4
void adc_init(void);
void adc_gpio_init(uint gpio);
void adc_set_temp_sensor_enabled(bool enable);
void adc_select_input(uint input);
uint16_t adc_read(void);

// --- spi --------------------------------------------------------------------
typedef struct sim_spi spi_inst_t;
extern spi_inst_t sim_spi0;
#define spi0 (&sim_spi0)
enum { SPI_CPOL_0 = 0, SPI_CPOL_1 = 1 };
enum { SPI_CPHA_0 = 0, SPI_CPHA_1 = 1 };
enum { SPI_LSB_FIRST = 0, SPI_MSB_FIRST = 1 };
uint spi_init(spi_inst_t *spi, uint baudrate);
void spi_set_format(spi_inst_t *spi, uint data_bits, int cpol, int cpha, int order);
int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len);
int spi_read_blocking(spi_inst_t *spi, uint8_t repeated_tx_data, uint8_t *dst, size_t len);

// --- uart -------------------------------------------------------------------
typedef struct sim_uart uart_inst_t;
extern uart_inst_t sim_uart0;
#define uart0 (&sim_uart0)
enum { UART_PARITY_NONE = 0, UART_PARITY_EVEN = 1, UART_PARITY_ODD = 2 };
uint uart_init(uart_inst_t *uart, uint baudrate);
void uart_set_format(uart_inst_t *uart, uint data_bits, uint stop_bits, int parity);
void uart_set_hw_flow(uart_inst_t *uart, bool cts, bool rts);
void uart_set_fifo_enabled(uart_inst_t *uart, bool enabled);
bool uart_is_readable(uart_inst_t *uart);
char uart_getc(uart_inst_t *uart);
void uart_set_irq_enables(uart_inst_t *uart, bool rx_has_data, bool tx_needs_data);

// --- pio --------------------------------------------------------------------
#define PICO_PIO_VERSION 1
typedef struct sim_pio *PIO;
extern struct sim_pio sim_pio0;
#define pio0 (&sim_pio0)

typedef struct pio_program {
    const uint16_t *instructions;
    uint8_t length;
    int8_t origin;
    uint8_t pio_version;
    uint8_t used_gpio_ranges;
} pio_program_t;

typedef struct {
    uint32_t clkdiv_fp;         // 16.8 fixed point
    uint8_t wrap_target, wrap;
    uint8_t in_base, jmp_pin;
    bool shift_right, autopush;
    uint8_t push_threshold;
    uint8_t fifo_join;
} pio_sm_config;
enum { PIO_FIFO_JOIN_NONE = 0, PIO_FIFO_JOIN_TX = 1, PIO_FIFO_JOIN_RX = 2 };
typedef uint pio_interrupt_source_t;

static inline pio_sm_config pio_get_default_sm_config(void) {
    pio_sm_config c = {.clkdiv_fp = 1u << 8, .wrap_target = 0, .wrap = 31, .shift_right = true,
                       .push_threshold = 32};
    return c;
}
static inline void sm_config_set_wrap(pio_sm_config *c, uint target, uint wrap) {
    c->wrap_target = (uint8_t)target;
    c->wrap = (uint8_t)wrap;
}
static inline void sm_config_set_in_pins(pio_sm_config *c, uint base) { c->in_base = (uint8_t)base; }
static inline void sm_config_set_jmp_pin(pio_sm_config *c, uint pin) { c->jmp_pin = (uint8_t)pin; }
static inline void sm_config_set_in_shift(pio_sm_config *c, bool right, bool autopush, uint thr) {
    c->shift_right = right;
    c->autopush = autopush;
    c->push_threshold = (uint8_t)thr;
}
static inline void sm_config_set_fifo_join(pio_sm_config *c, int join) { c->fifo_join = (uint8_t)join; }
static inline void sm_config_set_clkdiv(pio_sm_config *c, float div) {
    c->clkdiv_fp = (uint32_t)(div * 256.0f);
}
static inline pio_interrupt_source_t pio_get_rx_fifo_not_empty_interrupt_source(uint sm) { return sm; }

uint pio_add_program(PIO pio, const pio_program_t *program);
int pio_claim_unused_sm(PIO pio, bool required);
void pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint pin, uint count, bool is_out);
void pio_gpio_init(PIO pio, uint pin);
void pio_sm_init(PIO pio, uint sm, uint initial_pc, const pio_sm_config *config);
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled);
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm);
uint32_t pio_sm_get(PIO pio, uint sm);
void pio_set_irq0_source_enabled(PIO pio, pio_interrupt_source_t source, bool enabled);

// --- watchdog ---------------------------------------------------------------
void watchdog_enable(uint32_t delay_ms, bool pause_on_debug);
void watchdog_update(void);
bool watchdog_enable_caused_reboot(void);
