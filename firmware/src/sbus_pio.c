// SPDX-License-Identifier: GPL-3.0-or-later
// SBUS on the RC_RX pad through PIO0. The RX FIFO is drained in the PIO
// interrupt so bytes get accurate timestamps for gap-based frame sync and
// the main loop cannot overflow the 8-entry FIFO.

#include "rc_input.h"

#include "core/sbus.h"
#include "pins.h"

#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/time.h"

#include "sbus_rx.pio.h"

#define SBUS_BAUD 100000

#define SBUS_PIO pio0
static uint sbus_sm;
static sbus_parser_t parser;

static void sbus_pio_irq(void) {
    while (!pio_sm_is_rx_fifo_empty(SBUS_PIO, sbus_sm)) {
        uint32_t w = pio_sm_get(SBUS_PIO, sbus_sm) >> 23;
        uint8_t data = (uint8_t)(w & 0xFF);
        bool parity_ok = sbus_parity_ok(data, (uint8_t)(w >> 8));
        rc_frame_t f;
        if (sbus_parser_feed(&parser, data, parity_ok, time_us_32(), &f)) {
            rc_input_publish(&f);
        }
    }
    rc_input_count_errors(parser.errors);
}

void sbus_rx_init(void) {
    sbus_parser_init(&parser);
    uint offset = pio_add_program(SBUS_PIO, &sbus_rx_program);
    sbus_sm = (uint)pio_claim_unused_sm(SBUS_PIO, true);
    sbus_rx_program_init(SBUS_PIO, sbus_sm, offset, PIN_RC_RX, SBUS_BAUD);

    pio_set_irq0_source_enabled(SBUS_PIO, pio_get_rx_fifo_not_empty_interrupt_source(sbus_sm), true);
    irq_set_exclusive_handler(PIO0_IRQ_0, sbus_pio_irq);
    irq_set_enabled(PIO0_IRQ_0, true);
}
