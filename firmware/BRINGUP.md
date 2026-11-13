# Hyperion v1.2 bring-up plan

Step-by-step plan for the first assembled v1.2 boards. Do the steps in order. Stop at the first
failed check, find the cause, and only then continue. Record every result in the
[results table](#results-table-template) at the end, one copy per board.

Firmware for each step is one of the `hyperion_<mode>.uf2` files built from `firmware/`
(see [README.md](README.md)). All test modes log over USB CDC; open the port with
`picocom /dev/ttyACM0` (or any terminal; the baud rate is ignored).

## Reference values

Taken from the v1.2 schematic, the BOM and the datasheets. The pin map is in
[`src/pins.h`](src/pins.h).

| Quantity | Expected | Source |
|---|---|---|
| +5V | 4.98 V (0.8 V × (1 + 115k/22k)) | U4 LMR51610, R2/R3 |
| +5V from USB only | about 4.6–4.8 V | USB VBUS through D3 B5819W |
| +3V3 | 3.30 V ±2 % | U5 ME6211C33 |
| +1V1 (DVDD) | 1.10 V | RP2354A internal regulator, L1 |
| VBAT_SENSE | VBAT × 22/122 = 0.180 × VBAT (8.4 V → 1.52 V, 12.6 V → 2.27 V, 16.8 V → 3.03 V) | R4 100k / R5 22k |
| VBAT measurable range | 0–18.3 V | 3.3 V ADC reference |
| DRV8231A VREF | +3V3 | U2/U3 pin 4 |
| IPROPI scale | 2.25 V per A (1500 µA/A × 1.5 kΩ) | R13/R14 |
| Current limit I_TRIP | 3.3 V / 2.25 V/A = 1.47 A, ±6 % mirror error → 1.38–1.56 A | DRV8231A SLVSFZ8A |
| Current-chopping off time | 25 µs, brake (both low-sides on) | DRV8231A |
| DRV8231A UVLO | 4.3 V rising, 4.2 V falling | DRV8231A |
| DRV8231A OCP | 3.7 A min, auto-retry after 3 ms | DRV8231A |
| DRV8231A thermal shutdown | 150 °C min, 175 °C typ, 40 °C hysteresis | DRV8231A |
| Idle battery current, no USB, blank flash | about 10–40 mA (MCU, LDO, buck, red LED) | estimate |
| Status LEDs | red D4 = +3V3 power, always on; green D5 = GPIO25 | schematic |

## Equipment

- Bench supply 0–20 V, at least 3 A, with an adjustable current limit. The early steps use it
  instead of a LiPo.
- 2S, 3S and 4S LiPo packs with an inline fuse (3 A) or a smoke stopper for the later steps.
- DMM, oscilloscope (2 channels, 50 MHz or more), K-type thermocouple or an IR camera.
- Raspberry Pi Debug Probe (or any CMSIS-DAP probe) for SWD.
- USB cable with the far end soldered or pogo-pinned to TP3–TP6 (VBUS, D+, D−, GND).
- A USB isolator for the motor and thermal tests, or a laptop running on battery. Stalled motors
  share ground with the PC through the USB cable.
- Motors: N20 and N10 gearmotors of the types the robots use, and a clamp to lock the output shaft.
- Receivers: one SBUS receiver (FrSky, RadioMaster, ...), one ELRS receiver (CRSF output), and
  one PWM receiver, plus a matching transmitter.
- Microscope or loupe, 100 µF 35 V electrolytic for C3.

## 1. Visual inspection

For each board, under magnification:

1. U1 RP2354A (QFN-60, 0.4 mm pitch): pin-1 marker matches the silkscreen, no bridges, all side
   fillets wetted.
2. U2/U3 DRV8231A (WSON-8, 2 × 2 mm): pin 1 orientation, no bridges between pads 1–4 and 5–8.
3. U6 LSM6DSV32X (LGA-14): orientation dot, part sits flat (LGA joints can't be inspected; rely on
   the IMU test in step 10).
4. L1 polarity mark on pad 1 (VREG_LX side, next to U1). This orientation is required by the
   RP2350 datasheet for the core regulator.
5. D1 SMF18A (unidirectional TVS): cathode band on pad 1 = VBAT side. The schematic symbol has
   A1/A2 pin names like a bidirectional TVS, so check the physical part. Reversed, it shorts VBAT
   to GND at power-on.
6. D2 BZT52C12, D3 B5819W, D4/D5 LEDs: cathode on pad 1.
7. Q1 AON7403 orientation (gate pin 1).
8. Y1 crystal orientation, C28/C29 present.
9. Edge pads TP1/TP2 (battery) and TP13–TP16 (motors) clean, no solder balls.
10. Bottom-side test pads TP3–TP12 and TP17–TP26: no bridges between neighbours, especially RC_5V
    next to RC_TX/RC_RX and PWM_5V next to the PWM pads.

Pass: no defects found, or all defects reworked and re-inspected.

## 2. Unpowered resistance and diode checks

DMM on a board that has never been powered. C3 not fitted yet. Readings marked "settles" climb
while the capacitors charge from the meter.

| # | Measure | Expected | Fail if |
|---|---|---|---|
| 2.1 | TP1 BAT+ to TP2 BAT−, ohms | settles above 100 kΩ | below 1 kΩ |
| 2.2 | Diode test, red on BAT+, black on VBAT (C1 pad) | 0.4–0.7 V (Q1 body diode) | short or OL |
| 2.3 | VBAT (C1 pad) to GND, ohms | settles near 122 kΩ (R4 + R5) | below 10 kΩ |
| 2.4 | +5V (C7 pad) to GND | settles above 10 kΩ | below 100 Ω |
| 2.5 | +3V3 (C11 pad) to GND | above 1 kΩ | below 100 Ω |
| 2.6 | +1V1 (C24 pad) to GND | above 100 Ω; note the value and compare boards | below 10 Ω |
| 2.7 | Each of M1A, M1B, M2A, M2B to GND and to VBAT, ohms | high, no short | below 1 kΩ |
| 2.8 | Diode test, red on M1A, black on VBAT (high-side body diode); repeat for each output | 0.4–0.8 V | short |
| 2.9 | Diode test, red on GND, black on M1A (low-side body diode); repeat for each output | 0.4–0.8 V | short |
| 2.10 | Neighbouring bottom pads (RC_5V/RC_TX, RC_TX/RC_RX, PWMx/PWMx+1, PWM_5V/PWM1) | open | short |

## 3. First power-on, current limited

Bench supply with its output off, connected to TP1 BAT+ (red) and TP2 BAT− (black). No USB, no
motors, no receiver.

C3 rule: fit C3 (100 µF 35 V electrolytic, + lead on pad 1 = VBAT) before any 4S connection, and
before the first time a LiPo is plugged in at any voltage. A LiPo hot-plug rings the supply well
above the pack voltage with only ceramics on VBAT, and the DRV8231A VM limits are 35 V absolute
and a 2 V/µs ramp rate. A bench supply ramping up slowly is fine without C3 at 2S and 3S.

| # | Step | Pass |
|---|---|---|
| 3.1 | Set 7.4 V, current limit 50 mA. Turn on. | Red LED on. Supply not in current limit. Current 10–40 mA. |
| 3.2 | Raise limit to 150 mA. Wait 60 s, touch-test U1, U4, U5, Q1. | Current stable. No part noticeably warm. |
| 3.3 | Measure rails (step 4) at 7.4 V and 8.4 V (2S full). | All in range. |
| 3.4 | Set 11.1 V, then 12.6 V (3S). Measure rails. | All in range, current lower than at 2S. |
| 3.5 | Turn off, fit C3. Set 14.8 V, then 16.8 V (4S). Measure rails. | All in range. TVS D1 cold (it starts to conduct only around 20 V). |
| 3.6 | Slowly lower the supply from 7.4 V to 4.0 V, watching +5V and +3V3. Note where +3V3 falls out of regulation and the board resets. | Information only; record the voltages. |
| 3.7 | USB only (no battery): connect TP3–TP6 to a PC. | +5V 4.6–4.8 V, +3V3 3.30 V, red LED on. PC sees an RP2350 boot device (blank flash). |
| 3.8 | Battery and USB together (7.4 V). | +5V 4.98 V from the buck (D3 blocks USB). No change in USB enumeration. |

## 4. Rail measurements

At every voltage in step 3. DC values with the DMM; ripple with the scope (20 MHz bandwidth limit,
short ground spring on the probe).

| Rail | Measure at | DC pass range | Ripple pass |
|---|---|---|---|
| VBAT | C1 pad | supply − 0.05 V (Q1 drop) | — |
| +5V | C7 or TP17 RC_5V | 4.85–5.15 V | < 50 mVpp |
| +3V3 | C11 | 3.23–3.37 V | < 30 mVpp |
| +1V1 | C24 | 1.05–1.15 V (only once the MCU runs) | < 30 mVpp |
| VBAT_SENSE | C8 | 0.180 × VBAT ±2 % | — |
| VREF U2, U3 | pin 4 | equal to +3V3 | — |
| RUN | TP7 | 3.3 V | — |
| Buck SW node | L2 pad 1 | clean switching, no ringing above VBAT + 5 V | — |

## 5. SWD and USB boot

1. Blank board, USB connected: the board enumerates as an RP2350 BOOTSEL drive.
   `picotool info -a` must report an RP2350, package QFN-60 (RP2350A), and 2 MB flash.
2. SWD: Debug Probe to TP8 SWCLK, TP9 SWDIO, TP10 GND. Either probe-rs
   (`cargo install probe-rs-tools --locked`):

   ```
   probe-rs info --chip RP235x
   ```

   or OpenOCD with RP2350 support (Raspberry Pi's fork, as shipped with the VS Code extension; the
   Arch `openocd` 0.12.0 package does not support the RP2350):

   ```
   openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000"
   ```

   Pass: both Cortex-M33 cores found, no SWD errors.
3. Flash `hyperion_blink.elf` over SWD (`probe-rs download --chip RP235x hyperion_blink.elf`, or
   `program hyperion_blink.elf verify reset exit` in OpenOCD) and once by copying
   `hyperion_blink.uf2` to the BOOTSEL drive. Both must work.
4. BOOT pad: with firmware in flash, short TP11 BOOT to TP12 GND, then reset (short TP7 RUN to
   GND briefly, or power-cycle). Pass: the BOOTSEL drive appears again.

## 6. LED and GPIO blink (`hyperion_blink`)

| # | Check | Pass |
|---|---|---|
| 6.1 | Green LED D5 | Blinks at 1 Hz. |
| 6.2 | USB console | "alive" line every second with a plausible VBAT and MCU temperature. |
| 6.3 | Scope on TP21–TP24 (PWM1–PWM4) | 3.3 V square waves at 1, 2, 4, 8 Hz respectively. Confirms each pad reaches the right GPIO (10–13). |
| 6.4 | Crystal | Board runs at the right speed: the 1 Hz LED matches a stopwatch over 60 s (a failed XOSC would stop the firmware). |

## 7. ADC check (`hyperion_adc`)

| # | Check | Pass |
|---|---|---|
| 7.1 | VBAT reading vs DMM on the BAT+ pad at 7.4, 12.6 and 16.8 V | Within ±3 %. Record the error; it sets a calibration factor later. |
| 7.2 | IPROPI A and B with motors idle | 0–10 mA. |
| 7.3 | MCU temperature | Room temperature ±10 °C (the RP2350 sensor is not accurate). |

## 8. DRV8231A channel tests (`hyperion_motor`)

Interactive over USB: `a`/`b` selects the channel, `f`/`r` runs it for 3 s, `+`/`-` changes duty
in 5 % steps, `d` toggles slow/fast decay, `x` brakes, space stops. Start with 10 % duty every
time. Bench supply at 7.4 V unless stated.

Forward means IN1 high: current flows OUT1 → OUT2 (M1A → M1B, M2A → M2B).

| # | Setup | Check | Pass |
|---|---|---|---|
| 8.1 | No motor, limit 200 mA. Scope on M1A and M1B. | `a`, `f`, 10 %, slow decay | M1A switches 0 ↔ VBAT at 20 kHz, high 10 % ±1 % of the period; M1B low. `r`: mirrored. |
| 8.2 | Same, `d` for fast decay | | M1A driven high 10 % of the time, floating otherwise (no load); M1B floating when off. |
| 8.3 | Same for channel B (`b`) on M2A/M2B | | As 8.1 and 8.2. |
| 8.4 | Space key, then wait 2 s | | Both outputs high-impedance; supply current back to idle (driver asleep). |
| 8.5 | N20 motor on M1A/M1B, limit 1 A | duty 10 → 25 → 50 → 100 %, both directions | Smooth speed increase. Forward and reverse spin opposite ways. No-load current about 50–150 mA at 100 %. Record which way "forward" turns (forward = M1A positive). |
| 8.6 | Same | At 100 %, compare IPROPI A with a DMM in series with the motor | Within ±10 % + 20 mA above 200 mA. (IPROPI below about 100 mA is not accurate.) |
| 8.7 | Same, channel B | | As 8.5 and 8.6. |
| 8.8 | Locked rotor (or a 2–3 Ω 10 W resistor), supply at a voltage where V / R_motor > 1.6 A, limit 2.5 A | 100 % for at most 2 s | Motor current 1.38–1.56 A (DMM or current probe). IPROPI reads about 1400–1470 mA. Scope on an output shows 25 µs off-time chopping. |
| 8.9 | Both channels locked at once, 4S | 100 % on both for 2 s each, then together (run `2` in the thermal mode and abort after 2 s) | +5V and +3V3 stay in range on the scope. No MCU reset (watch the console). |

The motor resistance decides whether the limit is reached: measure each motor's winding resistance
with the DMM and record it. Stall current without the limit is VBAT / R_motor.

## 9. Receiver input (`hyperion_rc`)

The receiver gets 5 V from TP17 RC_5V and TP18 RC_GND. Motors are never driven in this mode.

### 9.1 SBUS (default build)

Signal to TP20 RC_RX/SBUS. The pad input is inverted in the RP2354A (GPIO INOVER) and decoded by
PIO at 100000 baud 8E2.

| # | Check | Pass |
|---|---|---|
| 9.1.1 | Frame rate | Matches the receiver: about 111 frames/s (9 ms), 143 (7 ms fast mode) or 71 (14 ms). |
| 9.1.2 | Errors over 60 s with the transmitter on | 0 (some receivers add a few; < 0.1 % of frames). |
| 9.1.3 | Channels 1–8 | Follow the sticks; centre 1500 ±10 µs; ends about 988 / 2012 µs at 100 % travel. |
| 9.1.4 | Transmitter off | Either `fs 1` (receiver failsafe flag) or frames stop and the line shows LOST. Note which one and the delay. |

Set the receiver's failsafe to "No pulses" where possible. With "Hold" or "Custom" the receiver
keeps sending frames, and the firmware relies on the SBUS failsafe flag.

### 9.2 CRSF

Rebuild with `-DHYPERION_RC_PROTOCOL=CRSF`. ELRS receiver TX to TP20 RC_RX, receiver RX to
TP19 RC_TX. UART0 at 420000 baud.

| # | Check | Pass |
|---|---|---|
| 9.2.1 | Frame rate | Matches the ELRS packet rate (50–500 Hz). |
| 9.2.2 | Errors over 60 s | 0 CRC errors. |
| 9.2.3 | Channels | As 9.1.3. |
| 9.2.4 | Transmitter off | Frames stop (ELRS "No pulses") or link quality 0 sets `fs 1`. LOST within 60 ms of the last frame. |

### 9.3 PWM

Rebuild with `-DHYPERION_RC_PROTOCOL=PWM`. Receiver CH1 to TP20 RC_RX, CH2 to TP19 RC_TX.

| # | Check | Pass |
|---|---|---|
| 9.3.1 | CH1, CH2 values | Match the receiver pulse widths on the scope within ±5 µs. |
| 9.3.2 | Unplug CH2 | `fs 1` within 60 ms. |
| 9.3.3 | Unplug CH1 | LOST within 60 ms. |

## 10. IMU (`hyperion_imu`)

| # | Check | Pass |
|---|---|---|
| 10.1 | WHO_AM_I | 0x70. If it reads 0x00 or 0xFF: check U6 soldering, IMU_CS (GPIO17) and SPI pins. |
| 10.2 | Data rate | About 960 samples/s (gyro data-ready on INT1, GPIO20). 0 means INT1 is not reaching the MCU. |
| 10.3 | Board flat, top up, still | \|a\| = 1000 ±50 mg, one axis about ±1000 mg, the other two within ±50 mg. Record which axis and sign points up. |
| 10.4 | Gyro bias, still | Each axis within ±3 dps. |
| 10.5 | Rotate the board 90° by hand about each edge | The matching gyro axis responds; record the axis and sign for each board edge (needed for auto-invert and heading hold). |
| 10.6 | Turn the board upside down | The vertical axis reverses sign. |
| 10.7 | Temperature | Plausible (20–40 °C at room temperature). |

## 11. Failsafe (`hyperion_failsafe`)

Full drive firmware with every arming change logged with a timestamp, the frame age and the time
since the last good frame. While disarmed or in failsafe both bridges brake and the PWM pads send
no pulses. Motors on both channels, wheels off the ground, bench supply 7.4 V
limit 1 A, receiver bound and set to "No pulses" failsafe if it supports it.

| # | Test | Pass |
|---|---|---|
| 11.1 | Power the board with the transmitter off | Motors never move; the wheels are hard to turn by hand (braked). Green LED blinks fast (NO_LINK). |
| 11.2 | Transmitter on, sticks centred | WAIT_NEUTRAL, then ARMED after 0.5 s; green LED solid. |
| 11.3 | Power-cycle the board with throttle held forward; repeat with the weapon stick (CH3) up | Stays WAIT_NEUTRAL while either stick is held; arms 0.5 s after it is released. |
| 11.4 | Drive at about 30 %, switch the transmitter off | Motors brake to a stop. Log: ARMED → NO_LINK at most RC_TIMEOUT_MS + 2 ms (62 ms) after the last good frame when frames stop, or at the next frame when the receiver raises its failsafe flag. Measure the total time from transmitter off to motors stopped (scope on a motor output, or a 240 fps video) and record it; it includes the receiver's own failsafe delay. |
| 11.5 | Transmitter off for more than 2 s, then back on with the stick held forward | Stays WAIT_NEUTRAL until the stick returns to centre. |
| 11.6 | Pull the receiver signal wire while driving | Motors stop within 62 ms. |
| 11.7 | Pull the receiver 5 V while driving | Motors stop within 62 ms. |
| 11.8 | Press `w` in the console while driving | MCU resets within 250 ms (watchdog). Motors coast during the reset, then brake. Banner shows `reset by WATCHDOG`; the robot needs neutral sticks again before it moves. |
| 11.9 | Scope on TP21 PWM1 (weapon) | Pulses only while ARMED; no pulses in every non-armed state. |
| 11.10 | Remove and re-apply power while the transmitter is on and the stick is held | No twitch on the motor outputs during the power ramp (scope). |
| 11.11 | Only if an arm switch is configured (`RC_CH_ARM`): switch it off while driving | Motors stop at once; re-arming needs neutral sticks. |
| 11.12 | Direction (see "Driving conventions" in README.md) | Right stick up: both sides drive the robot forward. Right stick right: left side forward, right side backward. Left stick (CH3) up: PWM1 pulse width rises from about 1000 to 2000 µs. Fix a wrong side by swapping its motor wires or with `MOTOR_x_REVERSED`. |
| 11.13 | Driving at about 30 %, pull the receiver signal wire and plug it back in within about 0.5 s, stick held | Motors brake during the dropout, then drive again at once without centring the stick; log shows NO_LINK → ARMED. Repeat with the wire out for 2 s: needs neutral sticks before it drives again. |

The goal is the shortest possible stop time. Most of it is the receiver's own failsafe delay, so
set the receiver to "No pulses" where possible and compare receivers using the 11.4 total.

## 12. DRV8231A thermal test at the 1.47 A limit (`hyperion_thermal`)

### Why

Stall is the normal state of a drive motor in a pushing match. At the current limit each DRV8231A
dissipates

- package: DSG, 2 × 2 mm WSON-8 with exposed pad
- R_DS(on) high side + low side: 0.6 Ω typical at 25 °C (300 mΩ + 300 mΩ, datasheet SLVSFZ8A)
- P = 1.47² × 0.6 ≈ 1.3 W
- R_θJA = 66.5 °C/W (DSG, JEDEC board) → ΔT ≈ 1.3 × 66.5 ≈ +87 °C
- thermal shutdown at 150 °C minimum (175 °C typical), auto-restart after cooling by 40 °C

At 25 °C ambient that gives T_J ≈ 112 °C, about 38 °C under the minimum TSD threshold. The margin
is smaller than it looks:

- R_DS(on) rises with junction temperature (datasheet figures 6-6 and 6-8), so the real
  dissipation at T_J ≈ 110 °C is above 1.3 W.
- The 66.5 °C/W figure is for the JEDEC test board. This board is 2 layers, 34 × 28 mm, with both
  drivers close together, so the real value is probably higher, and the other channel adds heat.
- Inside a robot after a match the ambient can be 40–50 °C.

TSD protects the driver, but the side that trips loses drive until it has cooled by 40 °C. The test
measures how long a stall can last before that happens.

The limit is only reached when VBAT / R_motor > 1.47 A. N10 and many N20 motors have enough
winding resistance that, at 2S, stall current stays below the limit and the driver dissipates only
I_stall² × 0.6 Ω. Measure and record each motor's resistance first.

### How the firmware detects TSD

The DRV8231A has no fault pin. In thermal shutdown the bridge goes high-impedance, so with a locked
rotor the IPROPI current drops to zero while the inputs still command 100 % drive. The thermal mode
samples IPROPI at 1 kHz (8 ms filter). It counts a TSD event when the current stays below
max(100 mA, stall current / 4) for 20 ms, and logs a recovery when the current comes back. The
3 ms OCP retry and the 25 µs current-chopping off time are much shorter, so neither is mistaken for
TSD. Output is CSV (`t_ms,motor,state,i_ma,vbat_mv,mcu_c,tsd_count`) every 100 ms, plus `#` lines
for events and a summary.

### Procedure

1. Board on a non-conductive stand in still air. Record the ambient temperature. Optional second
   series: board mounted inside a robot chassis.
2. Thermocouple bead on top of U2 with Kapton tape, or an IR camera (emissivity about 0.95 for the
   package). Ψ_JT = 2.2 °C/W, so T_J ≈ T_top + 3 °C at 1.3 W.
3. Lock the motor output shaft with a clamp. Stalling a geared N20 at 4S stresses the gearbox; lock
   at the output, as in a pushing match.
4. Supply from the bench supply at the full-charge voltage (8.4, 12.6, 16.8 V), current limit 2.5 A
   per active channel. C3 fitted.
5. USB through an isolator. Log to a file, for example:
   `picocom -q /dev/ttyACM0 | tee thermal_N20_4S_A_1.csv`
6. Flash `hyperion_thermal.uf2`, select `a`, `b` or `2` (both), confirm with `y`. The run stops
   after 120 s, after 3 TSD events per channel, if VBAT falls below 5.5 V, or on space.
7. Let the board cool to ambient + 2 °C (at least 5 minutes) between runs.
8. After the whole series, repeat step 8.8 (current limit check) on both channels.

Test matrix: {N10, N20} × {2S, 3S, 4S} × {channel A alone, both channels}. Run each point at least
twice.

### Pass criteria (proposed, to be confirmed)

| # | Criterion | Pass |
|---|---|---|
| 12.1 | Current regulation, when VBAT / R_motor > 1.6 A | Stall current 1.38–1.56 A for the whole run until TSD. |
| 12.2 | Single channel, 25 °C ambient, 4S, locked rotor | No TSD within 10 s. Good: first TSD after 30 s or more. Fail: TSD before 10 s. |
| 12.3 | Both channels locked together, same conditions | No TSD within 10 s. |
| 12.4 | Behaviour at TSD | Channel recovers on its own; MCU does not reset; the other channel keeps running (single-channel runs: no effect on channel B). |
| 12.5 | Other parts (IR camera) | Q1, L2, U4 and the board copper stay below 100 °C. |
| 12.6 | After the series | Step 8.8 still passes on both channels; no change in idle current. |

A fail on 12.2 or 12.3 is a v1.3 input: more copper and vias under U2/U3, a lower I_TRIP
(larger R_IPROPI), or firmware current limiting from IPROPI.

### Thermal results template

| Board | Motor | R_motor (Ω) | Cells / V | Channel(s) | Ambient (°C) | Stall current (mA) | First TSD (s) | TSD events | Max U2 top (°C) | On-time (%) | Recovery OK | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| | N20 | | 2S / 8.4 | A | | | | | | | | |
| | N20 | | 3S / 12.6 | A | | | | | | | | |
| | N20 | | 4S / 16.8 | A | | | | | | | | |
| | N20 | | 4S / 16.8 | A + B | | | | | | | | |
| | N10 | | 2S / 8.4 | A | | | | | | | | |
| | N10 | | 3S / 12.6 | A | | | | | | | | |
| | N10 | | 4S / 16.8 | A | | | | | | | | |
| | N10 | | 4S / 16.8 | A + B | | | | | | | | |

## 13. Reverse polarity (optional, last)

On one board that has passed everything else: bench supply at 7.4 V, limit 100 mA, connected
reversed (+ to BAT−). Pass: current below 1 mA, no LED, board passes step 6 afterwards. Q1 blocks
both through its body diode and its gate (V_GS positive when reversed).

## Results table template

Copy once per board. Board ID: `picotool info -a` serial number.

Board ID: __________ Date: __________ Tester: __________ Firmware commit: __________

| Step | Description | Criterion | Measured | Pass / Fail | Notes |
|---|---|---|---|---|---|
| 1 | Visual inspection | no defects | | | |
| 2.1–2.10 | Unpowered checks | see step 2 | | | |
| 3.1 | Power-on 7.4 V, idle current | 10–40 mA | | | |
| 3.3 | Rails at 2S | step 4 ranges | +5V: +3V3: +1V1: | | |
| 3.4 | Rails at 3S | step 4 ranges | +5V: +3V3: | | |
| 3.5 | Rails at 4S, C3 fitted | step 4 ranges | +5V: +3V3: | | |
| 3.6 | Brown-out voltages | info | +3V3 drop at: reset at: | | |
| 3.7 | USB-only power | +5V 4.6–4.8 V, enumerates | | | |
| 4 | Ripple +5V / +3V3 | < 50 / < 30 mVpp | | | |
| 5.1 | picotool info | RP2350A, 2 MB | | | |
| 5.2 | SWD | both cores found | | | |
| 5.4 | BOOT pad | BOOTSEL drive | | | |
| 6 | Blink, PWM pads | 1 Hz; 1/2/4/8 Hz | | | |
| 7.1 | VBAT accuracy | ±3 % | 7.4 V: 12.6 V: 16.8 V: | | |
| 7.2 | IPROPI idle | 0–10 mA | A: B: | | |
| 8.1–8.4 | Outputs, no load | 20 kHz, duty ±1 % | | | |
| 8.5–8.7 | N20 run, IPROPI accuracy | ±10 % + 20 mA | A: B: | | |
| 8.8 | Current limit | 1.38–1.56 A | A: B: | | |
| 8.9 | Both stalled, rails | no reset | | | |
| 9.1 | SBUS | rate, 0 errors, failsafe | | | |
| 9.2 | CRSF | rate, 0 errors, failsafe | | | |
| 9.3 | PWM | ±5 µs, failsafe | | | |
| 10.1–10.2 | IMU ID, rate | 0x70, ~960 Hz | | | |
| 10.3–10.4 | IMU still | 1 g ±50 mg, bias ±3 dps | | | |
| 10.5 | Axis map | record | | | |
| 11.1–11.3 | Arming interlocks | no motion | | | |
| 11.4 | TX off | ≤ 62 ms after last frame; total ___ ms | | | |
| 11.5–11.7 | Reconnect, signal/power loss | as specified | | | |
| 11.8 | Watchdog | reset ≤ 250 ms, then brake | | | |
| 11.9 | Weapon pad in failsafe | no pulses | | | |
| 11.10 | Power ramp | no twitch | | | |
| 11.12 | Direction and steering | as specified | | | |
| 11.13 | Short dropout resume | resumes < 1 s, neutral after 2 s | | | |
| 12 | Thermal series | see thermal table | | | |
| 13 | Reverse polarity (optional) | < 1 mA, survives | | | |
