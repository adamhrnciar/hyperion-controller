# Firmware simulator

Runs the firmware from `../src`, unchanged, on a model of the Hyperion v1.2 board. It exists
because no board has been powered yet: it checks the parts that the host unit tests in `../test`
cannot reach (interrupts, timing, pin setup, the test modes) before hardware arrives.

```
make -C firmware/sim                        # all variants, about 1 minute
make -C firmware/sim PIOASM=/path/to/pioasm # if the firmware was never built here
build/failsafe_sbus -v dropout              # one scenario, with the firmware console
```

`pioasm` comes from the normal firmware build (`firmware/build/pioasm/pioasm`). The simulator
executes the assembled `sbus_rx.pio`, so a change to the PIO program is tested as well.

## How it works

`include/` replaces the Pico SDK headers. Behind them `simhw.c` models the RP2354A peripherals in
virtual time, counted in 150 MHz system-clock ticks:

| Part | Model |
|---|---|
| Time | `time_us_32/64`, `sleep_ms`; a busy-wait iteration takes 1 µs. The firmware runs as a coroutine. |
| GPIO pads | Function select, output enable, pulls, input inversion, edge interrupts. `gpio_set_function` clears the inversion, as the SDK does. |
| PWM | Slices with wrap, divider and double-buffered compare values (taken at the next wrap). |
| PIO | One instruction per PIO cycle with delays, fractional clock divider, wrap, RX FIFO (joined) and its interrupt. Opcodes: JMP, WAIT, IN, PUSH, SET. |
| UART0 | PL011 receive FIFO, interrupt at 4 bytes or after 32 idle bit times, baud and pin-mux check. |
| Watchdog | Resets when not fed in time; all pads return to inputs. |
| ADC | VBAT divider, IPROPI (1500 µA/A into 1.5 kΩ), temperature sensor. |
| IMU | LSM6DSV32X registers used by the driver: WHO_AM_I, software reset, IF_INC, ODR, full scales, data-ready on INT1, output data in the configured scale. |
| DRV8231A | From the two input pads: share of each PWM period spent driving forward, reverse, braking and coasting. A floating input reads low (100 kΩ pull-down). |

Receiver models in `scenarios.c` produce the signals: SBUS as a waveform on the RC_RX pad
(100000 baud, 8E2, inverted), CRSF as bytes at 420000 baud with link statistics, PWM as servo
pulses on both pads. Each can lose the link in three ways: output stops, output continues with a
failsafe indication, output holds the last values with no indication.

Each scenario runs in its own process. `make` builds one binary per mode and protocol:

| Binary | Firmware | Scenarios |
|---|---|---|
| `failsafe_sbus`, `failsafe_crsf`, `failsafe_pwm` | `failsafe` mode | BRINGUP.md step 11: power-up without transmitter, arming, stick held, directions and mixing, weapon output, transmitter off, failsafe flag, wire pulled (0.5 s and 2 s), watchdog, timer wrap, protocol-specific cases |
| `drive_sbus` | `drive` mode | the same without the watchdog key |
| `armswitch_sbus` | `failsafe` with `RC_CH_ARM=4` | plus the arm switch |
| `rc_*`, `blink`, `adc`, `imu`, `motor`, `thermal` | test modes | steps 6 to 10 and 12: console output and pad behaviour |

Measured on the model with the default configuration: the bridges brake 61.2 to 61.7 ms after the
last frame when the receiver stops sending (`RC_TIMEOUT_MS` + 2 ms is the limit), within one frame
period when it raises its failsafe flag, and the watchdog resets a hung loop after 250 ms.

## What it does not prove

- The models come from the datasheets and the SDK sources, not from a board. A wrong assumption
  there passes here and fails on hardware. BRINGUP.md stays the real test.
- Not modelled: analog behaviour (ringing, brown-out, motor current, current chopping), USB,
  flash, clock start-up, the second core, interrupt latency and execution time (handlers take no
  time), the one-PWM-period glitch when two compare values are written across a wrap.
- `s_receiver_hold_without_flag` documents a case the firmware cannot detect: a receiver that
  keeps sending the last stick positions with no failsafe flag. Set the receiver to "No pulses".

## Checking the simulator itself

The original simulator author reported running the scenarios against 34 deliberately broken
copies of the firmware (missing input
inversion, wrong PIO timing, swapped directions, missing timeout, ignored failsafe flag, wrong
IMU scales, and so on). 33 were caught. The one left, a pull-up instead of the pull-down on the
SBUS pad, changes nothing the firmware can see: with no receiver the PIO program waits forever
either way.

Those mutation runs are historical evidence; their scripts and result logs are not included.
The 2026-11-13 review reran the 77 scenarios and the receiver-age regressions, not all 34 mutants.
