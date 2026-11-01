# Hyperion firmware

Firmware skeleton and bring-up test modes for the Hyperion v1.2 board (RP2354A, 2× DRV8231A,
LSM6DSV32X). The bring-up procedure is in [BRINGUP.md](BRINGUP.md).

**Status: builds, not yet run on hardware.** No v1.2 board exists yet. What has been checked:

- All 8 modes build without warnings with Pico SDK 2.3.1, using either Arch's
  `arm-none-eabi-gcc` 16.2 or Arm GNU Toolchain 15.2.rel1, for SBUS, CRSF and PWM receiver
  input. Each image is about 35–38 KB of the 2 MB flash.
- `test/`: host unit tests for the portable code (SBUS and CRSF parsers, arming/failsafe state
  machine, tank mixer). 88 checks pass, built with `-Wall -Wextra -Werror -Wconversion` and
  AddressSanitizer/UBSan.
- The SBUS PIO program was checked in a cycle-level simulation against 8E2 waveforms with
  0 and ±3 % baud error: every byte and parity bit decoded correctly.

## Why the Pico SDK (C)

The two realistic choices for the RP2350 are the Raspberry Pi Pico SDK (C) and Rust with
embassy-rp. This firmware uses the Pico SDK:

- It is Raspberry Pi's own SDK and has supported the RP2350 since version 2.0. New silicon
  steppings, errata workarounds and RP2350 features (boot ROM, partition tables, OTP, the
  RISC-V cores) land there first.
- The RP2354A (RP2350A die plus 2 MB of in-package flash) needs only a board header
  ([`boards/hyperion_v1_2.h`](boards/hyperion_v1_2.h)); the rest is the standard RP2350A setup.
- PIO programs are assembled by `pioasm` as part of the build, and the SBUS receiver is based on
  the official `uart_rx` PIO example.
- The supporting tools come from the same place: picotool, Raspberry Pi's OpenOCD, the Debug
  Probe, and the VS Code extension that installs the toolchain.
- arduino-pico is built on the Pico SDK, so the drivers here can move to an Arduino build later.

embassy-rp also supports the RP2350 and its async model suits this kind of firmware, but its
RP2350 support is younger and its API still changes between releases. For a board that has never
been powered, the vendor SDK removes one unknown from bring-up. The portable logic in `src/core`
is plain C with no SDK dependency and would port easily.

## Building

Requirements:

- Arm GNU toolchain (`arm-none-eabi-gcc` with newlib). Either the system package
  (Arch/CachyOS: `sudo pacman -S arm-none-eabi-gcc arm-none-eabi-newlib`) or Arm's tarball
  unpacked anywhere, with `PICO_TOOLCHAIN_PATH` pointing at it.
- CMake 3.13+ and Ninja or Make
- Pico SDK 2.1.0 or later (2.3.1 tested). Only the TinyUSB submodule is needed (USB stdio):

  ```
  git clone --depth 1 -b master https://github.com/raspberrypi/pico-sdk ~/pico-sdk
  git -C ~/pico-sdk submodule update --init --depth 1 lib/tinyusb
  ```

  Alternatively pass `-DPICO_SDK_FETCH_FROM_GIT=ON` to let CMake download it.
- picotool (Arch: AUR `picotool`). If it is not installed, the SDK downloads and builds it inside
  the build directory (needs libusb).

Build all modes:

```
export PICO_SDK_PATH=~/pico-sdk
cmake -S firmware -B firmware/build -G Ninja
cmake --build firmware/build
```

Add `export PICO_TOOLCHAIN_PATH=<toolchain dir>` only when using Arm's tarball instead of the
system compiler.

This produces `firmware/build/hyperion_<mode>.uf2` (and `.elf` for SWD) for each mode.

Host unit tests (no SDK needed):

```
make -C firmware/test
```

### Build options

| CMake option | Default | Meaning |
|---|---|---|
| `HYPERION_MODES` | all | Modes to build, e.g. `-DHYPERION_MODES="blink;motor"` |
| `HYPERION_RC_PROTOCOL` | `SBUS` | `SBUS`, `CRSF` or `PWM` |
| `HYPERION_EXTRA_DEFINES` | empty | Overrides for [`src/config.h`](src/config.h), e.g. `-DHYPERION_EXTRA_DEFINES="RC_CH_ARM=4;MOTOR_TEST_MAX_PERMILLE=300"` |
| `PICO_PLATFORM` | `rp2350` (from the board header) | `rp2350-riscv` to build for the Hazard3 cores |

Main `config.h` settings: `RC_TIMEOUT_MS` (60), channel map (`RC_CH_STEER` = CH1, `RC_CH_THROTTLE`
= CH2, `RC_CH_WEAPON` = CH3, `RC_CH_ARM` = none), `RC_DEADBAND_US` (25), `ARM_NEUTRAL_HOLD_MS`
(500), `ARM_RESUME_MS` (1000), `RC_WEAPON_ARM_MAX_US` (1100), `MOTOR_PWM_HZ` (20 kHz),
`MOTOR_SLOW_DECAY` (1), `MOTOR_NEUTRAL_BRAKE` (0), `MOTOR_DISARMED_BRAKE` (1), `MOTOR_A_REVERSED` /
`MOTOR_B_REVERSED`, `MOTOR_LEFT` (motor A), `SERVO_FAILSAFE_US` (0 = no pulses), `WATCHDOG_MS`
(250).

## Modes

| Mode | File | What it does |
|---|---|---|
| `drive` | `hyperion_drive.uf2` | Normal firmware: receiver → arming/failsafe → tank mix → motors; RC_CH_WEAPON to PWM1 (SBUS/CRSF only; with PWM input PWM1 stays off). Status line once per second. |
| `blink` | `hyperion_blink.uf2` | Green LED at 1 Hz, PWM1–4 pads toggle at 1/2/4/8 Hz, heartbeat on USB. |
| `adc` | `hyperion_adc.uf2` | VBAT, IPROPI A/B and MCU temperature, raw and converted. |
| `rc` | `hyperion_rc.uf2` | Receiver decode: frame rate, errors, failsafe flags, CH1–8. Motors off. |
| `imu` | `hyperion_imu.uf2` | LSM6DSV32X WHO_AM_I, gyro bias, accel/gyro/temperature at 10 Hz, sample rate. |
| `motor` | `hyperion_motor.uf2` | Interactive single-channel test over USB: direction, duty, decay mode, brake, with current and VBAT readout. Each key press runs for 3 s. |
| `failsafe` | `hyperion_failsafe.uf2` | `drive` with every arming transition logged (frame age, time since last good frame); `w` hangs the loop to test the watchdog. |
| `thermal` | `hyperion_thermal.uf2` | 100 % stall on one or both channels; logs CSV and the time to DRV8231A thermal shutdown. |

All modes print over USB CDC (`/dev/ttyACM0`). UART0 is the receiver port, so there is no UART
console.

## Driving conventions

- **Sides:** motor A (U2, pads M1A/M1B) is the left side, motor B (U3, pads M2A/M2B) the right
  (`MOTOR_LEFT`).
- **Forward:** a positive command sets IN1 high, so current flows from the A pad to the B pad
  (M1A → M1B, M2A → M2B). Wire each motor so that this drives the robot forward. In a normal
  chassis the two motors face opposite ways, so the right motor usually ends up with its leads
  swapped compared to the left one. If a side runs backwards, swap its two wires or set
  `MOTOR_A_REVERSED=1` / `MOTOR_B_REVERSED=1`.
- **Sticks (Mode 2, AETR):** right stick drives. Up = forward, right = turn right (left side
  faster). Left stick up/down (CH3, the throttle stick, which stays where you leave it) runs the
  weapon on PWM1; it must be at the bottom (≤ 1100 µs) before the robot arms. AETR is the
  default channel order on EdgeTX/OpenTX, FrSky, FlySky and ELRS. Spektrum uses TAER: either
  remap on the transmitter or change `RC_CH_*`. If forward or right comes out reversed, reverse
  that channel on the transmitter.

## Safety behaviour

- `motor_init()` is the first call in `main()` and drives all four H-bridge inputs low. The
  DRV8231A inputs also have 100 kΩ pull-downs, so a reset, an empty flash or BOOTSEL mode leaves
  both bridges coasting. The bring-up test modes keep the bridges coasting when idle; only the
  `drive` and `failsafe` modes brake while disarmed.
- Motors run only in the ARMED state. Arming needs a valid link, both drive sticks inside the
  deadband and the weapon channel at or below 1100 µs (`RC_WEAPON_ARM_MAX_US`) for 500 ms, and
  the arm switch on if `RC_CH_ARM` is set. A lost link, a receiver failsafe flag or the arm
  switch going off disarms at once.
- Match dropouts: if the link drops while armed and comes back within `ARM_RESUME_MS` (1 s), the
  robot re-arms at once and follows the sticks again; it only brakes for the dropout itself.
  After a longer outage (transmitter switched off and on), after power-up, or after the arm switch
  was off, the sticks must be neutral for 500 ms first, so the robot cannot jump.
- Link loss: no valid frame for `RC_TIMEOUT_MS` (60 ms), the SBUS failsafe bit, CRSF link
  quality 0, or (PWM input) any channel without a valid pulse for 60 ms. With ELRS at a 50 Hz
  packet rate raise the timeout to about 100 ms; at 150 Hz and above 60 ms has plenty of margin.
- Disarmed and failsafe action: both bridges brake (both low-side FETs on), which stops the robot
  fastest, and the PWM pads stop sending pulses so the weapon ESC turns off
  (`MOTOR_DISARMED_BRAKE = 1`, `SERVO_FAILSAFE_US = 0`). Between reset and the first pass of the
  control loop the bridges coast, because the DRV8231A inputs are pulled low.
- Most of the stop time after the transmitter is switched off is the receiver's own failsafe
  delay, not the firmware's. Set the receiver failsafe to "No pulses" where it offers that, so it
  stops sending frames as soon as it loses the link.
- Hardware watchdog, 250 ms. USB stdout gives up after 5 ms so a stalled terminal cannot hold up
  the control loop.

## Layout

```
boards/hyperion_v1_2.h   Pico SDK board header (RP2354A, 12 MHz crystal, 2 MB flash, LED on GPIO25)
src/pins.h               Pin map from the schematic netlist, with net names and U1 pad numbers
src/config.h             Build-time configuration and mode/protocol constants
src/main.c               Init (bridges off first), watchdog, mode dispatch
src/drive.c              Normal control loop, shared by the failsafe test mode
src/bringup.c            blink / adc / rc / imu / motor / thermal test modes
src/motor.c              DRV8231A PWM driver with arming gate, slow/fast decay
src/servo_out.c          50 Hz pulses on PWM1–4
src/sense.c              VBAT, IPROPI current, MCU temperature
src/imu.c                LSM6DSV32X SPI driver
src/rc_input.c           Receiver backend selection and latest-frame store
src/sbus_rx.pio          PIO UART receiver (8 data + parity bits), input inverted in the pad
src/sbus_pio.c           SBUS backend: PIO RX FIFO drained in the PIO interrupt
src/crsf_uart.c          CRSF backend on UART0 at 420 kbaud
src/rc_pwm.c             Servo-pulse backend (CH1 RC_RX, CH2 RC_TX) with GPIO edge interrupts
src/led.h                Status LED patterns
src/core/                Portable, host-tested: SBUS/CRSF parsers, arming, mixer, 11-bit unpacking
test/                    Host unit tests for src/core
```

## Hardware notes for firmware

- Motor A uses PWM slices 1 (IN1, GPIO2) and 3 (IN2, GPIO7); motor B uses slice 4 (GPIO8/9).
  All three slices share one frequency and start in phase.
- Slow decay (drive/brake) is the default. TI recommends it, and the DRV8231A IPROPI output senses
  the low-side current in both the drive and the brake phase, so the current reading stays
  continuous. In fast decay (drive/coast) IPROPI reads zero during the coast phase.
- SBUS: the RC_RX pad's input inverter (`gpio_set_inover`) undoes the SBUS inversion, then PIO
  decodes plain UART. `gpio_set_function` clears that override, so the inversion is applied after
  the pin is handed to PIO. The pad has a pull-down so a missing receiver reads as an idle line.
- RP2350 GPIOs are 5 V tolerant when IOVDD is present; the ADC pins (VBAT, IPROPI) are not, but
  they only see divided or IPROPI-limited voltages.
- The red LED is a hard-wired power LED; only the green LED is under firmware control.

## Not done yet

- IMU auto-invert and heading hold (the driver reads the data; no fusion yet)
- CRSF telemetry (battery, current) back to the transmitter; the UART TX line is set up
- iBUS and PPM input
- Firmware current limiting and battery undervoltage cutoff using IPROPI and VBAT
- VBAT and IPROPI calibration stored in flash

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). Every source file carries an
`SPDX-License-Identifier` line. `pico_sdk_import.cmake` follows the Pico SDK file it mirrors and is
BSD-3-Clause. The Pico SDK (BSD-3-Clause) and TinyUSB (MIT) that the firmware links against keep
their own licences; both can be combined with GPL code.
