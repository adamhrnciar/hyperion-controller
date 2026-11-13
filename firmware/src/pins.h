// SPDX-License-Identifier: GPL-3.0-or-later
// Hyperion v1.2 pin map.
//
// Extracted from the KiCad netlist (hardware/hyperion.kicad_sch, exported with
// `kicad-cli sch export netlist`). Net names match the schematic; U1 pad
// numbers are for the RP2354A QFN-60.

#pragma once

// --- Receiver port (TP19 RC_TX, TP20 RC_RX/SBUS) ------------------------------
// UART0 pins. CRSF uses the hardware UART; SBUS uses PIO with the pad input
// inverted (GPIO INOVER); PWM/PPM input uses edge capture on the same pads.
#define PIN_RC_TX           0   // net CRSF_TX,   U1 pad 2,  TP19
#define PIN_RC_RX           1   // net CRSF_RX,   U1 pad 3,  TP20 (SBUS input)

// --- Motor A: U2 DRV8231A, outputs M1A/M1B (TP13/TP14) -----------------------
#define PIN_MA_IN1          2   // net MA_IN1, U1 pad 4,  PWM slice 1 A
#define PIN_MA_IN2          7   // net MA_IN2, U1 pad 10, PWM slice 3 B
#define PIN_MA_IPROPI       26  // net MA_IPROPI, U1 pad 40, ADC0, R13 1.5k

// --- Motor B: U3 DRV8231A, outputs M2A/M2B (TP15/TP16) -----------------------
#define PIN_MB_IN1          8   // net MB_IN1, U1 pad 12, PWM slice 4 A
#define PIN_MB_IN2          9   // net MB_IN2, U1 pad 13, PWM slice 4 B
#define PIN_MB_IPROPI       27  // net MB_IPROPI, U1 pad 41, ADC1, R14 1.5k

// --- PWM output pads TP21..TP24 (weapon ESC, servo, spare) ------------------
#define PIN_PWM1            10  // net PWM1, U1 pad 14, PWM slice 5 A
#define PIN_PWM2            11  // net PWM2, U1 pad 15, PWM slice 5 B
#define PIN_PWM3            12  // net PWM3, U1 pad 16, PWM slice 6 A
#define PIN_PWM4            13  // net PWM4, U1 pad 17, PWM slice 6 B

// --- IMU: U6 LSM6DSV32X on SPI0 ----------------------------------------------
#define IMU_SPI             spi0
#define PIN_IMU_MISO        16  // net IMU_MISO, U1 pad 27, U6 pin 1 SDO/SA0
#define PIN_IMU_CS          17  // net IMU_CS,   U1 pad 28, U6 pin 12 CS
#define PIN_IMU_SCK         18  // net IMU_SCK,  U1 pad 29, U6 pin 13 SCL/SPC
#define PIN_IMU_MOSI        19  // net IMU_MOSI, U1 pad 31, U6 pin 14 SDA/SDI
#define PIN_IMU_INT1        20  // net IMU_INT1, U1 pad 32, U6 pin 4
// U6 INT2 (pin 9) is not connected. SDx/SCx (OIS aux SPI) are tied to GND.

// --- Status LED ---------------------------------------------------------------
// Green D5 via R12 1k. The red LED D4 is a power LED on +3V3 via R6, not an MCU pin.
#define PIN_LED_STATUS      25  // net STAT_LED, U1 pad 37

// --- Analog -------------------------------------------------------------------
#define PIN_VBAT_SENSE      28  // net VBAT_SENSE, U1 pad 42, ADC2
#define ADC_CH_MA_IPROPI    0
#define ADC_CH_MB_IPROPI    1
#define ADC_CH_VBAT         2

// VBAT divider R4 100k (top) / R5 22k (bottom), C8 100 nF.
#define VBAT_DIV_TOP_OHM    100000u
#define VBAT_DIV_BOT_OHM    22000u

// DRV8231A current sense: I_IPROPI = I_LS * 1500 uA/A into R_IPROPI = 1.5 kOhm,
// so V_IPROPI = 2.25 V per A. VREF is tied to +3V3, so I_TRIP = 3.3 / 2.25 = 1.47 A.
#define DRV_AIPROPI_UA_PER_A 1500u
#define DRV_RIPROPI_OHM      1500u

// --- Unused -------------------------------------------------------------------
// GPIO3, 4, 5, 6, 14, 15, 21, 22, 23, 24, 29 (ADC3) are not connected.
// Debug: SWCLK TP8, SWDIO TP9, RUN TP7. BOOT TP11 (QSPI_SS via R11 1k).
// USB: D+ TP4, D- TP5 (27R series R9/R10), VBUS TP3 feeds +5V through D3.
