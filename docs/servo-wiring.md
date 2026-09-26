# Servo wiring — ESP32 to the camera pan/tilt (2 × D56MG)

**Rev B, 2026-09-19. Wired, and actuated by firmware 0.2.0 the same day.** Rev A had four
SG90s; the mast no longer moves, so its pan/tilt pair is gone and only the **camera's pan and
tilt** remain, on **JX PDI-D56MG** servos. The pin numbers are the ones in
[config.h](../firmware/include/config.h); if the two ever disagree, `config.h` is what gets
flashed and this file is wrong. §5 is the firmware and §6 the free-run test.

The power side is the flight plan from [power-budget.md §4](power-budget.md) — 2S pack, 7.5 A
fuse, a 5 V buck for the servos alone and a 5 V / 2 A buck for the logic. That plan is still
waiting on the Phase 0 decisions; the bench version is in §4 below.

## 0. The part

JX PDI-D56MG: digital, metal gear, coreless, 5.6 g, **3.7–5.5 V**, 120° travel, centre
1520 µs, accepts up to 333 Hz. 0.89 kg·cm at 5.5 V. JR lead: brown GND, red +, orange signal.

- **5.5 V is the maximum.** The rail must be a regulated 5.0 V — never the 6 V motor rail,
  never the pack, and not a fresh 4×AA pack (5.6 V NiMH, 6.4 V alkaline). Check the label says
  D56MG and not DHV56MG, the high-voltage variant with a different range.
- **Stall current is not published.** Budgeted at 1 A per servo until measured (§4 step 6);
  U1 is sized at 3 A on that estimate.

## 1. Schematic

```
 2S LiPo +──[F1 7.5 A]──┬──► U1 buck 5.0 V / 3 A ──┬───────────┬──────────┬─────── SERVO 5.0 V (red)
                        │     (servo rail)          │           │          │         │
                        │                         C1 100µF    C2 100nF     │         │
                        │                           │           │  (at the │         │
                        │                          GND         GND servos) │         │
                        │                                                  │         │
                        └──► U2 buck 5.0 V / 2 A ──► ESP32 VIN             │         │
                              (logic rail)                                 │         │
                                                                           │         │
   ESP32 GPIO18 ──┬──[R1 330 Ω]──────────────── SIG  S1 camera pan   +5V ──┘         │
                [R3 10k]                                                             │
                 GND                                                                 │
   ESP32 GPIO19 ──┬──[R2 330 Ω]──────────────── SIG  S2 camera tilt  +5V ────────────┘
                [R4 10k]
                 GND

   ESP32 GND ─────┬───── both servo GNDs (brown) ───── U1/U2 GND ───── ★ pack −   (one net, star point)
```

## 2. Pin and channel map

| Servo | Axis | GPIO | LEDC channel | LEDC timer | `config.h` |
|---|---|---|---|---|---|
| S1 | Camera pan | 18 | 2 | 1 · 50 Hz | `PIN_SERVO_CAM_PAN` |
| S2 | Camera tilt | 19 | 3 | 1 · 50 Hz | `PIN_SERVO_CAM_TILT` |
| — | Drive (existing) | 32 · 27 | 0 · 1 | 0 · 1.5 kHz | `LEDC_CHANNEL_DRIVE_IN*` |

LEDC channels share a timer in pairs on arduino-esp32 2.x, and a timer has one frequency, so
the servos take 2+3 (timer 1) and can never retune the drive's timer. At 16 bits and 50 Hz one
count is 0.31 µs: 920 / 1520 / 2120 µs is a duty of 3014 / 4981 / 6947. That pulse range is
the servo's whole 120°; what stops the head wrapping its own cable is the per-axis angle
limits in §5, not the pulse range. 50 Hz is the starting rate; the D56MG would take up to 333 Hz if faster
settling is wanted.

**Why these GPIOs:** neither is a strapping pin and neither emits anything during boot.
Excluded: 0, 2, 5, 12, 15 (strapping), 26 (damaged, 2026-08-30), 34–39 (input only), 16/17
(kept for UART2 to the detection board), 21/22 (kept for I²C). GPIO13 and GPIO23, which Rev A
gave the mast pair, are free again.

## 3. Rules

- **Hold the servo rail at 5.0 V** — see §0.
- **Never power the servos from the ESP32 board** — not its 5V pin, not 3V3. A servo stall
  dips the rail, the ESP32 resets and the link drops (power-budget §4.1).
- **Join the grounds.** The signal has no reference otherwise; the servos jitter or ignore it.
- **3.3 V signal, driven directly.** If one jitters, add a 74AHCT125 powered from the servo
  rail. Never pull the signal up to 5 V — that puts 5 V on a 3.6 V-max pin.
- **330 Ω series** on each signal: a flexing pan/tilt lead that shorts SIG to +5 V then puts
  ~5 mA into the GPIO clamp rather than amps.
- **10 kΩ pull-downs** hold the lines low while the GPIOs float through reset and flashing, so
  the head does not twitch.
- **C1/C2 at the servo connectors**, not at the buck.

## 4. Bench wiring now

1. ESP32 on USB (COM3), as today. VIN open.
2. Servos from a **separate, regulated 5.0 V** supply of at least 2 A — a bench PSU with a 2 A
   limit, or a 5 V USB charger through a breakout.
3. **Not** the drive bench pack and **not** an AA holder — both can exceed 5.5 V.
4. Servo supply − to an ESP32 GND pin.
5. One servo first: 1520 µs to centre, then sweep slowly toward 1000 and 2000 µs; if it buzzes
   or strains at an end, pull that limit in.
6. Stall it gently against a stop and read the supply's current — that is the figure §0 is
   missing.

## 5. Firmware (0.2.0)

- **`lib/Aim`** — every decision: travel limits, direction, angle → pulse, what a missing axis
  means. No Arduino headers; tested on the PC in `test/test_aim`.
- **`lib/PanTilt`** — the two LEDC channels, and nothing else. Pins and limits come in through
  the constructor, like `lib/Drive`.
- **`main.cpp`** — actuates `mast` (docs/protocol.md §3.4) **only while armed**, advertises the
  `mast` capability, and reports `pan`/`tilt` in telemetry.

| Setting | Value | `config.h` |
|---|---|---|
| Pan travel | −45 … +45° | `CAM_PAN_MIN_DEG` / `CAM_PAN_MAX_DEG` |
| Tilt travel | **−60** … +45° | `CAM_TILT_MIN_DEG` / `CAM_TILT_MAX_DEG` |
| Scale | pan **−10**, tilt **+10** µs/° (magnitude an estimate) | `CAM_PAN_US_PER_DEG` / `CAM_TILT_US_PER_DEG` |
| Centre | 1520 µs | `SERVO_PULSE_CENTRE_US` |
| Pulse range | 920 … 2120 µs | `SERVO_PULSE_MIN_US` / `SERVO_PULSE_MAX_US` |

**Tilt down was opened from −30° to −60° on 2026-09-20** (firmware 0.2.2), because −30° did
not look far enough down to drive on. That needed the pulse range widened too: at 10 µs/° a
60° droop is 2120 µs, and the old 2000 µs ceiling stopped the camera at −48°. 920–2120 µs is
the D56MG's full 120°, so **tilt now has no reserve left** — if the bracket fouls before −60°,
pull `CAM_TILT_MIN_DEG` back rather than widening the pulse limits past the part's rating.

**Direction is the sign of the scale.** Pan positive = right, tilt positive = up. If a button
moves the camera the wrong way, the servo is mounted the other way round: negate that axis's
`*_US_PER_DEG`, reflash, done. Nothing else moves — the travel limits, the console and the
wire are all stated in protocol terms, so "down is down" survives any re-mount.

**Expect to redo this after any bracket change**, and budget a minute for it rather than
treating it as a fault. It has already happened twice: 0.2.1 reversed tilt (TILT UP pointed
down); then the servos were **re-positioned** and 0.2.3 reversed **both** — pan to −10, tilt
back to +10. The `test_head_axes_are_both_reversed` host test records which way they currently
face, so a stale sign fails on the PC rather than on the bench.

The console mirrors the travel limits as `HEAD_PAN_LIMITS` / `HEAD_TILT_LIMITS` in
`console/scripts/console.gd` — change one, change both. The **signs** are firmware-only: the
console never knows how a servo is mounted.

Behaviour worth knowing before the first run:

- **The head centres at every boot**, when `PanTilt::begin()` starts the pulses. Before that
  the lines sit low on the pull-downs and the servos are limp.
- **Safe mode leaves the head where it is** (docs/protocol.md §6.1): the failsafe cuts the
  motors, never the servos, and pan/tilt commands are ignored until the rover re-arms.
- **A pan/tilt press arms the rover.** A `mast` frame is a fresh command like `drive`, so the
  pad works without touching the drive pad first — and the rover drops back to safe 500 ms
  after you let go, which the console shows as SAFE MODE. That is normal.
- **Telemetry reports the commanded angle.** The servos have no feedback, so a head that is
  stalled, unpowered or unplugged still reads as obeying.

## 6. Free-run test

On battery, no USB — the way the rover runs in the arena.

1. **Supply first.** Servos on their own regulated 5.0 V (§0, §3). If the status LED goes
   into a boot flicker when the head moves, the servo current is browning out the ESP32 —
   a shared supply, the same fault power-budget §4.1 predicts.
2. Power up. **The head should snap to centre** within a second of boot.
3. Start the console: `& $env:GODOT_BIN --path console`. The readiness row should read
   `FW 0.2.0 … [drive, steer3, mast]` and **PAN/TILT READY**.
4. **Direction check, one axis at a time.** Tap PAN R (or `D`): the camera must turn right.
   Tap TILT UP (or `W`): it must point up. Either wrong → negate that axis's scale (§5).
5. **Travel check.** Hold PAN R until the readout stops at +45°, then PAN L to −45°, then
   TILT to +45° and −30°. At each end, look and listen: **no buzzing, no strain, no cable
   pulled tight.** A servo that hums at an end is stalled against something — narrow that
   limit in `config.h` *and* `console.gd`.
6. CENTRE (or `C`) returns to 0/0.
7. **Failsafe check.** Pan to one side, then pull the console's network (or close it). The
   drive must stop as always; **the head must stay where it is**, not recentre.
8. **Drive and pan together** — hold FORWARD and pan at the same time. The drive must not
   stutter: the two share the link, and this is the first time they have.
9. Record the travel limits you ended with and any reversed axis. Those become step 3.4's
   measured limits (docs/protocol.md §9 item 1).

A scripted version of steps 4–6 without the console, useful when the console itself is in
doubt: connect to `ws://192.168.1.50:81/`, send `{"cmd":"hello","v":2}`, then
`{"cmd":"mast","pan":20,"tilt":10}` and watch the `pan`/`tilt` in telemetry.

## 7. Parts for the harness

2 × JX PDI-D56MG · 1 × buck 5.0 V ≥ 3 A (U1) · 2 × 330 Ω (R1, R2) · 2 × 10 kΩ (R3, R4) ·
1 × 100 µF ≥ 10 V electrolytic (C1) · 1 × 100 nF ceramic (C2) · 2 × 3-pin 2.54 mm header ·
optional 74AHCT125. U2, F1 and the pack are already in the power plan.
