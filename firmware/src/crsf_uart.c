// SPDX-License-Identifier: GPL-3.0-or-later
// CRSF on UART0 (RC_TX = GPIO0, RC_RX = GPIO1), 420000 baud 8N1, decoded in the UART IRQ.
// TX is configured for later telemetry but nothing is sent yet.

#include "rc_input.h"

#include "core/crsf.h"
#include "pins.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/time.h"

#define CRSF_UART uart0
#define CRSF_BAUD 420000

static crsf_parser_t parser;

static void crsf_uart_irq(void) {
    while (uart_is_readable(CRSF_UART)) {
        uint8_t b = (uint8_t)uart_getc(CRSF_UART);
        rc_frame_t f;
        if (crsf_parser_feed(&parser, b, time_us_32(), &f) == CRSF_GOT_RC) {
            rc_input_publish(&f);
        }
    }
    rc_input_count_errors(parser.errors);
}

void crsf_rx_init(void) {
    crsf_parser_init(&parser);
    uart_init(CRSF_UART, CRSF_BAUD);
    uart_set_format(CRSF_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(CRSF_UART, false, false);
    uart_set_fifo_enabled(CRSF_UART, true);
    gpio_set_function(PIN_RC_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_RC_RX, GPIO_FUNC_UART);
    gpio_pull_up(PIN_RC_RX);    // idle high; no receiver = no start bits

    irq_set_exclusive_handler(UART0_IRQ, crsf_uart_irq);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(CRSF_UART, true, false);
}
