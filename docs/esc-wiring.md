# ESC wiring — ESP32 to the RC-car chassis (40 A ESC, steering servo, camera pan/tilt)

**Rev A, 2026-11-07. Wired, and implemented in firmware 0.3.0 the same day — not yet run.**
This replaces the old L293D + DRV8833 drive with the hobby-grade hardware from the RC car: a
**TFTH 40 A brushed ESC** on the drive motor and the car's **steering servo**. The camera
pan/tilt (two D56MG) stays on **GPIO18/19** exactly as in [servo-wiring.md](servo-wiring.md).
The pin numbers are the ones in [config.h](../firmware/include/config.h); if the two ever
disagree, `config.h` is what gets flashed. **Firmware 0.2.3 or older must never run with the
ESC signal connected** (§5).

> **2026-11-08: the ESC signal moved GPIO32 → GPIO23, with a 1 kΩ series resistor in place of
> R7's 330 Ω.** GPIO32 measured **4.8 V with the ESC unplugged** — 5 V reaching it from the
> board wiring, not from the ESC — and is out of service with GPIO26 and GPIO27. **The ESC
> was confirmed working on GPIO23 the same day, and GPIO32 is recorded as broken.** Its pin must
> be disconnected from that wiring, not merely left unused. The ESC's own signal input pulls
> up to ~3.8 V when unplugged, which is why R7 went up to 1 kΩ. The diagrams below still show
> GPIO32: read **GPIO23** wherever they say GPIO32.

Assumptions to check before wiring:

- **The drive motor is brushed — two wires.** The car was sold as *brushless* and this ESC is
  *brushed*. If the motor has three wires, this ESC cannot drive it. Stop and re-plan.
- **The pack is 2S LiPo (7.4 V nominal, 8.4 V full).** That is what WPL/MN brushed ESCs are
  built for. The battery divider below (220 k / 22 k, ratio 11) does not depend on it, though:
  even a 3S pack only puts 1.15 V on GPIO34. Check the ESC's own input rating before trying
  anything above 2S.
- **The steering servo is a standard 3-wire servo** (signal, +, GND). If it has five wires,
  it is a bare motor plus a pot with no electronics, and it needs a different driver.
- "Rear servo" in the request is read as **the car's steering servo**, wherever it sits on
  the chassis.

## 1. Schematic

### 1.1 Power

```
                 SW1 main         F1 30 A (blade)
 2S LiPo + ─────o/ o──────────────[=====]───┬──────────────────────────► ESC BAT+  (thick red, 14–16 AWG)
 7.4 V nominal                              │
 8.4 V full                                 │  F2 3 A
                                            └──[==]──┬──► U1 buck 5.0 V / 3 A ──┬── CAM SERVO RAIL 5.0 V
                                                     │     (camera servos only) C1 100 µF + C2 100 nF
                                                     │                          (at the servo headers)
                                                     │
                                                     ├──► U2 buck 5.0 V / 2 A ──► ESP32 VIN
                                                     │     (logic only)
                                                     │
                                                     └──[R5 220 k]──┬──[R6 22 k]──┐
                                                                    │             │
                                                                    ├──► GPIO34   GND
                                                                    │  (0.76 V at 8.4 V)
                                                                  [C3 100 nF]  (optional,
                                                                    │           across R6)
                                                                   GND

 2S LiPo − ────────────────────────────────────┬──────────────────────► ESC BAT−  (thick black)
                                               │
                                          ★ STAR GND ── U1 GND ── U2 GND ── ESP32 GND ── every servo GND
```

### 1.2 Signals

```
  ESP32 DevKit                                                     ESC TFTH 40 A (brushed)
                                                                  ┌──────────────────────────────┐
  GPIO32 ──┬──[R7 330 Ω]─────────────────────────────── SIG  ──── │ white / orange               │
         [R8 10 k]                                                │                    M+ ───────┼──► drive motor +
          GND                                                     │                    M− ───────┼──► drive motor −
                                                                  │                              │
  GND ──────────────────────────────────────────────── GND  ──── │ black / brown                │
                                                                  │                              │
                                          ┌───────────── BEC+ ─── │ red  (≈5–6 V out — measure)  │
                                          │                       └──────────────────────────────┘
                                          │
                                          │                        STEERING SERVO (car)
  GPIO33 ──┬──[R9 330 Ω]──────────────────┼──────────── SIG  ──── orange / white / yellow
         [R10 10 k]                       └──────────── +    ──── red
          GND                                                     
  GND ───────────────────────────────────────────────── GND  ──── brown / black

                                                                   CAMERA PAN  (D56MG, unchanged)
  GPIO18 ──┬──[R1 330 Ω]─────────────────────────────── SIG  ──── orange
         [R3 10 k]                        CAM 5.0 V ─── +    ──── red
          GND                             GND ───────── GND  ──── brown

                                                                   CAMERA TILT (D56MG, unchanged)
  GPIO19 ──┬──[R2 330 Ω]─────────────────────────────── SIG  ──── orange
         [R4 10 k]                        CAM 5.0 V ─── +    ──── red
          GND                             GND ───────── GND  ──── brown

  GPIO34 ◄── battery divider (§1.1)            GPIO2 ── on-board status LED (unchanged)

  NOT USED: GPIO26 (damaged 2026-08-30) · GPIO27 (reported broken 2026-11-07)
```

**The ESC's red BEC wire goes to the steering servo and nowhere else.** That is how the car was
wired originally, with the receiver passing it through. Never connect it to the ESP32 (3V3 or
VIN) or to the camera rail. Two regulators tied together fight each other, and the D56MGs
max out at 5.5 V.

## 2. Pin and channel map

| Device | GPIO | LEDC channel | LEDC timer | Pulse | Proposed `config.h` name |
|---|---|---|---|---|---|
| ESC (drive) | **23** (was 32 until 2026-11-08) | 0 | 0 · **50 Hz** | 1000 / **1500** / 2000 µs (rev / neutral / fwd) | `PIN_ESC` |
| Steering servo | **33** | 1 | 0 · **50 Hz** | 1000 / **1500** / 2000 µs (left / centre / right, to be measured) | `PIN_SERVO_STEER` |
| Camera pan | 18 | 2 | 1 · 50 Hz | 920 … 2120 µs | `PIN_SERVO_CAM_PAN` (unchanged) |
| Camera tilt | 19 | 3 | 1 · 50 Hz | 920 … 2120 µs | `PIN_SERVO_CAM_TILT` (unchanged) |
| Battery sense | 34 | — | — | ADC at **6 dB**, ratio **11.0** | `PIN_BATTERY_SENSE` |
| Status LED | 2 | — | — | — | `PIN_STATUS_LED` (unchanged) |

Timer 0 drops from 1.5 kHz to 50 Hz. The 1.5 kHz rate was only there for the L293D, and an
ESC and a servo both want 50 Hz frames. The cameras keep timer 1, so nothing about them changes.

**Why these GPIOs.** *(GPIO23 since 2026-11-08 — see the note at the top; the reasoning
below is why GPIO32 was chosen, and GPIO23 passes the same tests.)* GPIO32 already carried the drive. It is not a strapping pin, and it is
silent during boot, which matters even more now: a pin that pulses at reset is a pin that can
twitch an ESC. GPIO33 was the steering input before and passes the same test.

**Excluded:**

- 26 and 27 (damaged)
- 0, 2, 5, 12, 15 (strapping)
- 34–39 (input-only)
- 16/17 (kept for UART2)
- 21/22 (kept for I²C)

**GPIO25 is now spare.** So are 13 and 23.

## 3. Rules

- **Neutral before power.** Most car ESCs arm only if they see a neutral pulse (≈1500 µs) when
  power comes on. The firmware has to output 1500 µs from the moment it starts. The 10 kΩ
  pull-down (R8) keeps the line quiet before then, so the ESC sees *no* signal rather than a
  random one.
- **The failsafe has to send neutral, not stop the pulses.** Some cheap ESCs keep running the
  last throttle when the pulses stop, and nothing published says how this one behaves. Holding
  1500 µs is the only safe-mode output that works on every ESC. Confirm the ESC's own
  signal-loss behaviour on the bench too (§4 step 7).
- **The steering servo now has a centre, and it no longer springs back.** It holds whatever
  angle it was last given. The failsafe must actively send 1500 µs to keep a cut-out rover
  running straight. The old spring-return axle did that unpowered, and this one does not.
- **Keep the thick wires short and separate.** Run pack → F1 → ESC → motor in heavy wire, away
  from the signal leads. The ESC− goes straight to pack−. The logic grounds join at the star
  point, not partway along the motor current path.
- **3.3 V signal, driven directly.** ESCs and servos take a 3.3 V pulse almost universally. If
  one doesn't, add a 74AHCT125 powered from 5 V. **Never pull a signal up to 5 V or to the BEC.**
- **330 Ω series, 10 kΩ pull-down on every signal.** These are the same reasons as
  [servo-wiring.md §3](servo-wiring.md).
- **Battery divider: 220 k / 22 k, ratio 11.0.** It replaces the 100 k / 47 k that
  power-budget F5 recommended. GPIO34 sees:

  | Pack | Voltage | GPIO34 |
  |---|---|---|
  | 2S full | 8.4 V | 0.76 V |
  | 2S empty | 6.0 V | 0.55 V |
  | 3S full | 12.6 V | 1.15 V |

  Every case is far under the pin's 3.6 V maximum, so the wrong pack cannot damage the pin.
  The cost is resolution: at the default 11 dB attenuation, one ADC step is about 9 mV of
  battery voltage. Setting **GPIO34 to 6 dB** attenuation (≈0.15–1.75 V range) roughly
  halves that and keeps the reading in the ADC's linear region. 5% resistors can put the
  ratio off by up to ≈±9%, which the meter calibration (risk R4) takes out either way.
  C3 smooths motor noise out of the reading.
- **Fuse sizing.** F1 protects the ESC branch: 30 A covers this ESC and motor on 14–16 AWG
  wire. F2 (3 A) protects the thin electronics branch, which would never blow F1. SW1 has to
  be rated for the ESC current. Otherwise, use the ESC's own switch for the drive and give SW1
  only the electronics branch.

## 4. Bench bring-up order

1. **Wheels off the ground** — chassis on a box — for every step until 8.
2. Check the motor wire count (two = brushed, carry on) and the pack (2S).
3. ESC powered from the pack, **signal lead unplugged**, steering servo unplugged. Measure red
   to black on the ESC's 3-pin lead: that is the BEC voltage. **If it is above the steering
   servo's rating** — or there is no red wire at all (an "opto" ESC) — power the servo from a
   separate buck instead (U3, set to the servo's rated voltage, ≥ 3 A). Do not use U1.
4. ESP32 on USB (COM3) with **VIN open**. Never feed USB and U2 at the same time unless the
   board has a VIN diode.
5. Flash the **bench sketch** (`python -m platformio run -e motortest -t upload -t monitor`).
   It outputs **1500 µs on GPIO23 (ESC) and GPIO33 from boot**. Then plug in the ESC signal and
   power the ESC. It should beep its arming tones and stay still.
6. Steering servo: plug it in, then use `t 1400`, `t 1300` … and `t 1600`, `t 1700` …, in
   steps of 50 µs. Stop at the pulse where the steering links reach their end stops; if the
   servo buzzes, back off 20 µs. Those two figures go into `STEER_LEFT_US` /
   `STEER_RIGHT_US`. Nudge `STEER_CENTRE_US` until the wheels point straight. Then check
   that `l` steers **left** and `r` right; if not, swap the two values.
7. ESC: `f 0.2`, `f 0.5`, `b 0.2` … through the cap. Then `e 1550`, `e 1560` … to find where
   the motor first turns. Check that `f` drives **forward**; if not, swap `ESC_FORWARD_US` /
   `ESC_REVERSE_US`. Note any **brake-then-reverse** behaviour: many car ESCs only reverse
   after neutral → reverse → neutral → reverse. Then flash the rover firmware
   (`python -m platformio run -t upload`). Drive slowly from the console and pull its link:
   the wheels must stop.
8. Only then on the floor, at a heavily capped throttle — this car is sold as 75 km/h, and the
   arena is 3 m long.

## 5. Firmware (0.3.0)

Firmware 0.2.3 and older treat GPIO32/27 as L293D inputs and put **1.5 kHz PWM** on GPIO32.
An ESC reads that as garbage, and garbage can mean throttle. **Never flash an older build
onto this board with the ESC connected.**

What 0.3.0 changed. Everything below is in the code; none of it has run on the car yet:

- **`lib/RcMap`** makes every decision: deadband, speed cap, direction, steering trim. It has
  no Arduino headers and is tested on the PC in `test/test_rcmap`. **`lib/Drive`** is now
  just the two 50 Hz LEDC channels (ESC on 0, steering on 1, timer 0).
- **The speed cap**: `THROTTLE_LIMIT_FORWARD` / `_REVERSE` = **0.30** since 2026-11-08
  (0.75 on 2026-11-07, then 0.40 and 0.20), because the 370 motor is too powerful at full.
  Full stick is 1650 µs forward and 1350 µs reverse. It scales rather than clips, so half stick
  is half the cap and the console's speed modes keep their meaning.
- **The minimum speed**: `ESC_FORWARD_START_US` / `ESC_REVERSE_START_US`, where the motor
  starts to turn. Any demand past the 0.05 deadband jumps straight to it, so the slowest
  stick moves the car rather than sitting in the ESC's own neutral band. Full stick is still
  the cap. Added 2026-11-07, when 0.10 of stick reached only ~1538 µs and did not move the
  car. **Forward measured the same day: the motor first turns at 1540 µs** (wheels up), so
  the start is set at 1560. **Reverse first turns at 1440 µs**, so it is set at 1420. Measure
  with `e` (step 7) and add ~20 µs for the car's weight. **Now 1550 / 1450** (2026-11-08, set
  by the user after trying 1560 / 1420, 1500 — off, too much dead stick — and 1570 / 1440).
  At the 0.30 cap the whole stick spans 1550–1650 µs forward and 1450–1350 µs reverse;
  reverse's start is 10 µs inside the measured 1440, so the lightest reverse may not move. 1500 switches
  either one off.
- **Steering precision**: the console sends `steer` at 0.01 resolution, and every step
  reaches the servo as its own pulse (≈3 µs apart on the ±300 µs placeholder span). That is
  finer than a hobby servo's own deadband, so the servo, not the link, sets the limit.
  `test_steering_is_fine_grained` holds this on the PC.
- **Steering end stops**: measured 2026-11-07 at **1100 / 1900 µs** (full left / full right).
  `STEER_LEFT_US` / `STEER_RIGHT_US` are set **20 µs inside**, at **1120 / 1880**, so a held
  full lock never jams the servo against the linkage.
- Steering becomes proportional. The handshake `caps` changes from `steer3` to **`steerprop`**,
  which protocol v2 already allows for, so this is **not a protocol bump**
  ([protocol.md](protocol.md)). The ±0.5 threshold goes.
- Failsafe output becomes **1500 µs on both** (neutral throttle, centred steering), held
  rather than stopped.
- `BATTERY_DIVIDER_RATIO` = **11.0**, read with `analogReadMilliVolts()` at 6 dB. This
  closes power-budget F5 for this wiring.
- `PIN_DRIVE_IN*` / `PIN_STEER_IN*` are gone. Nothing drives GPIO26 or GPIO27.
- **The bench sketch** (`-e motortest`) is now the ESC + steering bench for §4 steps 5–7:
  `f`/`b`/`l`/`r`/`c` go through the real mapping and cap, `e <µs>` / `t <µs>` send raw
  pulses for finding the ESC's start point and the steering end stops, and throttle returns
  to neutral after 3 s without a command.

## 6. Parts for the harness

- 1 × TFTH 40 A brushed ESC
- 1 × car steering servo
- 2 × JX PDI-D56MG (unchanged)
- 1 × SW1 (rated for the ESC current, or electronics branch only, §3)
- 1 × F1 30 A blade fuse and holder
- 1 × F2 3 A fuse and holder
- 1 × U1 buck 5.0 V ≥ 3 A (unchanged)
- 1 × U2 buck 5.0 V ≥ 2 A
- Optional: 1 × U3 buck for the steering servo (§4 step 3)
- Resistors: R1, R2, R7, R9 330 Ω · R3, R4, R8, R10 10 kΩ · **R5 220 kΩ** (red-red-yellow-gold)
  · **R6 22 kΩ** (red-red-orange-gold)
- Capacitors: C1 100 µF ≥ 10 V · C2 100 nF · C3 100 nF (optional, across R6)
- 4 × 3-pin 2.54 mm headers
- Pack connector to match the car battery (T-plug / XT60 / Tamiya)
- 14–16 AWG silicone wire for the pack → ESC → motor run

**Removed from the board:** the L293D, the DRV8833, and the old steering motor.
