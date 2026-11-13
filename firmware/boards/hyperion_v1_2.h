// SPDX-License-Identifier: GPL-3.0-or-later
// Pico SDK board header for Hyperion v1.2.
// RP2354A = RP2350A die (QFN-60, 30 GPIO) + 2 MB flash in the package.
// Crystal, flash and LED wiring follow the Raspberry Pi Pico 2 reference design
// (12 MHz ABM8-272-T3 with 1k series resistor and 15 pF loads, BOOT on QSPI_SS via 1k).

// pico_cmake_set PICO_PLATFORM=rp2350

#ifndef _BOARDS_HYPERION_V1_2_H
#define _BOARDS_HYPERION_V1_2_H

#define HYPERION_V1_2

#define PICO_RP2350A 1

// UART0 (GPIO0/1) is the receiver port, not a console. stdio goes over USB.

#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
#endif

#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif

// In-package flash: 2 MB, W25Q-compatible.
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (2 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
