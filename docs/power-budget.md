# Power budget and rail plan — step 0.3

Companion to [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) step 0.3, and the estimate
that step **1.7 replaces with measurements**. It closes nothing on its own: it exists so 0.4
has something to buy against and so 1.7 has a prediction to be wrong about.

> ⚠️ **Every current in this document is a datasheet figure or a class-typical guess. Nothing
> here has been measured, because nothing has been bought.** Each row says which it is. The
> two rows that matter most — the drive and steering motors — are the *least* known, because
> the chosen kit does not publish motor specifications.

**Decisions this step asks you for** are collected in §7. The rest is arithmetic.

---

## 1. What is fitted, and when

The budget has to cover the finished rover, not today's. Phase 1 draws a fraction of this.

| Load | Phase | Rail |
|---|---|---|
| ESP32 | 1 | 5 V (its own board regulator makes 3.3 V) |
| Drive motor | 1 | motor |
| Steering motor | 1 | motor |
| Mast pan/tilt servos ×2 | 3 | servo |
| Mast IP camera | 3 | 5 V |
| Sensing payload | 2 | 5 V — **allowance only, see §6** |

---

## 2. The load table

| Load | Rail | Idle | Working | Worst case | Where the number came from |
|---|---|---|---|---|---|
| ESP32-WROOM-32 | 5 V | 40 mA | 120 mA | **280 mA** TX peak | Espressif datasheet. The 120 mA assumes **Wi-Fi power save off**, which step 1.2 turned off deliberately — see below |
| Drive motor | motor | 0 | 350 mA on sand | **1.2 A** stall | class-typical 6 V kit gearmotor — **unconfirmed, and the biggest unknown here** |
| Steering motor | motor | 0 | **560 mA held at lock** | **800 mA** stall | ditto; the "working" figure is 0.7 duty (`kSteerHoldDuty`) into a stalled motor |
| Mast servos ×2 | servo | 10 mA | 300 mA moving | **1.4 A** both stalled | SG90 class |
| Mast IP camera | 5 V | — | 250 mA | 350 mA | ESP32-CAM class — unconfirmed until 0.4 picks one |
| Sensing allowance | 5 V | — | 300 mA | **800 mA** burst | UHF reader, candidate A (§6) |

**A steered chassis has no idle steering current and no idle drive current** — both motors are
unpowered whenever the rover is not being driven, and the steering spring holds centre for
free. That is worth stating because it is why the average draw in §5 is so much lower than the
peaks here.

**The ESP32's 120 mA is a latency decision, not just a datasheet row.** Step 1.2 measured
control RTT with Wi-Fi modem sleep at its default (on) and disabled: p95 went from **99.3 ms
to 19.8 ms**, against a 100 ms R1 target. `main.cpp` therefore calls `WiFi.setSleep(false)`,
which is why the radio draws its working current continuously rather than dozing between
beacons. Roughly 40 mA of the budget buys a fivefold latency improvement on the risk this
project is most exposed to — and that trade should not be quietly reversed at 1.7 to win back
runtime, because §5 shows the ESP32 is only 10% of the session average anyway.

### 2.1 Concurrent worst case

All-stalls-at-once (≈4.8 A of load) cannot physically happen: the rover is not surveying with
the mast while stalled against the glass with the reader transmitting. Two figures are worth
sizing against instead.

| Case | Motor rail | 5 V rail | Servo rail | Pack draw @ 11.1 V |
|---|---|---|---|---|
| **Realistic worst** — driving on sand, steering held, camera live, reader transmitting | 910 mA | 1.05 A | 0 | **≈ 1.4 A** |
| **Wiring/fuse sizing** — everything stalled together | 2.0 A | 1.43 A | 1.4 A | **≈ 3.0 A** |

Pack draw assumes 85% converter efficiency. Fuse at **5 A**, wiring for 3 A continuous.

---

## 3. The L293D voltage drop, and the pack decision it forces

Step 0.2 chose an L293D. It is a bipolar Darlington part and **drops roughly 1.8–2.0 V** across
the bridge at working current (TI datasheet: ~1.4 V high-side plus ~1.2 V low-side at 600 mA,
so 2.6 V is the pessimistic figure), against ~0.5 V for a TB6612FNG
([chassis-envelope §8.3](chassis-envelope.md)).

So the motor sees **`V_pack − 2 V`**, and a 6 V motor wants **8 V at the driver input**.

### 3.1 What each pack does, unregulated

| Pack | Full | Nominal | Cutoff | Motor sees, full → cutoff |
|---|---|---|---|---|
| **2S** (7.4 V nom) | 8.4 V | 7.4 V | 6.6 V | 6.4 V → **4.6 V** |
| **3S** (11.1 V nom) | 12.6 V | 11.1 V | 9.9 V | **10.6 V** → 7.9 V |

3S direct-drive **overvolts a 6 V motor by 77%** at full charge. That is not a candidate.

2S direct-drive starts correct and sags: the motor loses **28% of its voltage** over the
discharge.

### 3.2 Why that sag is a real problem, and not just an inefficiency

This is the argument for regulating, and it is a software argument rather than an electrical
one.

Step S.15 set **`MIN_EFFECTIVE_THROTTLE = 0.45`** in
[rover_link.gd](../console/scripts/rover_link.gd) — the floor below which the rover will not
break out of its own ruts on sand. It is a **fixed constant**. Breakaway needs roughly a fixed
torque, so the duty required to reach it scales inversely with rail voltage: a 28% sag pushes
the real floor from 0.45 to about **0.63** by end of session.

**The rover would begin a session driveable and end it unable to start from rest at the
operator's usual throttle** — and the console, holding a constant, would have no way to know.
The operator would report it as "the rover stopped responding to small inputs", which is a
miserable thing to debug in an arena.

A regulated motor rail removes the whole failure mode. The console's constant stays true from
full to cutoff.

### 3.3 Recommendation

**3S pack with an 8.0 V buck converter feeding the L293D.** The motor sees a constant
6.0–6.2 V from full charge to cutoff.

Keeping 3S also costs nothing in code: `BATTERY_FULL_V = 12.6` / `BATTERY_EMPTY_V = 10.5` in
[fake_rover.py](../tools/fake_rover.py) and `BATTERY_LOW_V = 11.1` /
`BATTERY_CRITICAL_V = 10.5` in [console.gd](../console/scripts/console.gd) are already written
for a 3S pack. **Choosing 2S instead means changing all four**, plus the low-battery colour
thresholds on the telemetry strip.

---

## 4. Rail plan

Four rails, one ground.

```
  3S LiPo  11.1 V nominal   (12.6 V charged, 9.9 V cutoff)
    │
    ├──[ fuse 5 A ]──┬── buck  8.0 V / 3 A ──► L293D Vs (pin 8)
    │                │                          │  1000 µF bulk AT the driver
    │                │                          ├─► drive motor
    │                │                          └─► steering motor
    │                │
    │                ├── buck  5.0 V / 3 A ──┬─► ESP32 board VIN  (onboard LDO → 3.3 V)
    │                │      "logic rail"     ├─► L293D Vss (pin 16, logic supply)
    │                │                       ├─► mast IP camera
    │                │                       └─► sensing payload   470 µF bulk, local
    │                │
    │                └── buck  5.0 V / 3 A ──── mast pan/tilt servos ×2
    │                       "servo rail"
    │
    └──[ 100 kΩ / 27 kΩ divider ]──► ESP32 GPIO34  (ADC, battery sense)

  COMMON GROUND — star point at the pack negative terminal
```

### 4.1 Why the servos get their own regulator rather than sharing the logic rail

Two servos stalling is a **1.4 A step**, and servo current transients are the classic cause of
ESP32 brownouts. On a shared rail that step resets the ESP32 mid-drive; the link drops, the
failsafe trips, the motors cut, and the operator sees an unexplained dropout with no bad
telemetry preceding it — the hardest class of fault to diagnose from the console.

Separating them costs one converter and makes that failure impossible rather than unlikely.
This is the same reasoning as §1.1 of [protocol.md](protocol.md), applied to amps instead of
bits: keep the bursty thing off the channel the safety behaviour depends on.

### 4.2 Logic levels

L293D `Vss` (pin 16, logic supply) comes from the 5 V rail. Its input threshold is
**V_IH = 2.3 V at Vss = 5 V**, so the ESP32's 3.3 V GPIO drives it directly with ~1 V of
margin — **no level shifter needed**.

⚠️ **Checkpoint at 1.3:** some L293D breakout modules tie `Vss` to `Vs`. If this board does,
`Vss` becomes 8 V, V_IH rises with it, and 3.3 V logic may no longer reliably switch. Check the
module's schematic or continuity between pins 8 and 16 *before* wiring.

### 4.3 Bulk capacitance

- **1000 µF at the L293D `Vs`**, physically at the driver, not at the converter. Motor inrush
  and the steering motor's stall are step loads, and the buck's control loop is too slow.
- **470 µF local to the sensing payload** if 2.0 picks the UHF reader, which draws in bursts
  while transmitting.

---

## 5. Battery sizing for a full session

Revision 2's loop repeats across rocks, so runtime is sized against a **whole session**, not a
single run (revised R4).

**Session assumed: 20 minutes** — five rocks at roughly three minutes each, plus setup and
margin. If a session is meaningfully longer than that, this is the number to tell me.

| Load | Power when on | Duty | Average |
|---|---|---|---|
| ESP32 | 0.60 W | 100% | 0.60 W |
| Mast camera | 1.25 W | 100% | 1.25 W |
| Drive motor | 2.80 W | 40% | 1.12 W |
| Steering motor | 4.40 W | 10% | 0.44 W |
| Mast servos | 1.50 W | 10% | 0.15 W |
| Sensing (candidate A) | 1.50 W | 100% | 1.50 W |
| | | **subtotal** | **5.06 W** |
| Converter losses (÷ 0.85) | | | **≈ 6.0 W** |

- 20 minutes at 6 W = **2.0 Wh**
- ×2 for session overrun and not running the pack flat = **4.0 Wh**
- LiPo usable fraction ≈ 80% (4.2 V down to 3.5 V per cell) → **5.0 Wh of rated capacity**
- At 11.1 V nominal → **450 mAh minimum**

**Recommendation: a 3S 1500–2200 mAh pack** — 16–24 Wh, so 3–5× the requirement. That buys
several sessions per charge rather than one, keeps the C-rate trivial (3.0 A peak from
1500 mAh is 2 C against a 25 C part), and weighs about 120–170 g against the 800–1000 g payload
allowance in [chassis-envelope §6](chassis-envelope.md).

Oversizing here is cheap and the alternative is a rover that dies mid-demonstration.

---

## 6. The sensing allowance — carried, not costed

Step 2.0 has not run, so there is no sensing part to budget. The allowance is sized against the
hungriest candidate, and **which candidate is hungriest depends on which question you ask**:

| Candidate | Average | Peak | Rail |
|---|---|---|---|
| **A** — UHF RFID reader | 300 mA | **800 mA** burst while transmitting | 5 V logic |
| **B** — probe arm + IR/colour sensor | ~150 mA | **2.1 A** (3 servos stalled) | 5 V servo |
| **C/D** — camera CV | **0 mA** | **0 mA** | none — the compute is console-side |

**The line item carried into 0.4 is candidate A's 800 mA / 1.5 W**, because A is what
[storyboard-rev2.md](../plan/storyboard-rev2.md) currently specifies.

The useful finding is that **the rail plan in §4 survives every candidate without change**:

- **A** lands on the logic rail, which has headroom for it.
- **B**'s 2.1 A of stall lands on the **servo** rail, which is already specified at 3 A because
  the mast servos needed it. B would put five servos on that rail rather than two — tight, but
  within it.
- **C and D add nothing to the rover at all**, and would reduce the budget in §5 by 1.5 W,
  about a quarter of the average draw.

So 2.0 changes a BOM line and the §5 average. It does not reopen this step.

---

## 7. Findings and decisions

### 🔴 F5 — `BATTERY_DIVIDER_RATIO = 2.0` would destroy the ADC pin

[config.h](../firmware/include/config.h) carries `BATTERY_DIVIDER_RATIO = 2.0f`. Against the
3S pack this repo assumes everywhere else, that puts **6.3 V on GPIO34 at full charge**. The
ESP32's absolute maximum on any GPIO is **3.6 V**. This is not an uncalibrated placeholder —
it is a value that damages the board the first time it is powered.

It has never bitten because no hardware exists. It bites **the moment a pack is connected to
GPIO34** — which is step 1.3 at the earliest, not 1.1: bring-up runs on USB power with no
divider wired at all, deliberately.

**Recommended divider: 100 kΩ (top) / 27 kΩ (bottom), ratio 4.70.**

| Pack | Volts at GPIO34 |
|---|---|
| 12.6 V charged | 2.68 V |
| 11.1 V nominal | 2.36 V |
| 9.9 V cutoff | 2.11 V |

That sits inside the ADC's usable range at 11 dB attenuation with headroom at the top, and
draws 99 µA — negligible against a 1500 mAh pack. The *ratio constant* is fixed by the
resistors chosen here; step 1.7 still calibrates it against a meter, because 1% resistors and
the ESP32's ADC non-linearity together are worth several hundred millivolts.

### 🔴 F6 — the steering channel is the binding constraint on the L293D, thermally

Three-position steering means a held turn parks the motor against its end stop for as long as
the operator holds it — a **continuous stall**, not a transient one.

At `kSteerHoldDuty = 0.7` into a motor stalling at 800 mA, the channel carries ~560 mA average
against the L293D's **600 mA continuous rating**: 93% of rating, no margin. If the real motor
stalls at 1.2 A, it is 840 mA and **over**.

Worse, the thermal arithmetic bites before the current rating does:

- Two channels near rating: ~2 V × 0.9 A ≈ **1.8 W** dissipated in the package
- DIP-16, no heatsink, no airflow: θ_JA ≈ **67 °C/W**
- Rise ≈ **120 °C** over ambient → junction at ~145 °C against a 150 °C limit

The part has thermal shutdown, so the realistic failure is not smoke — it is **the driver
cutting out mid-turn and the rover coasting on**, intermittently, under exactly the sustained
steering the arena demands. That is a safety-adjacent failure, not a performance one.

Three ways out, in increasing order of cost:

1. **Lower `kSteerHoldDuty`** — find the least duty that still reaches full lock (step 1.3).
   Free, and the first thing to try.
2. **Heatsink the L293D**, or pick a module with a copper pour.
3. **TB6612FNG instead** — ~0.5 V drop, so roughly a quarter the dissipation, and 1.2 A
   continuous. This is what `CLAUDE.md` originally specified, and it also removes most of §3's
   voltage-drop problem.

**Measure stall current at 1.7 before committing the arena to the L293D.** This is the same
warning [chassis-envelope §8.3](chassis-envelope.md) raised, now with the thermal number
behind it.

### 📋 Decisions for you

1. **Pack: 3S with an 8 V motor buck** (recommended, §3.3), or 2S direct-drive and accept a
   throttle floor that moves during a session plus four constants to change?
2. **Session length** — is 20 minutes the right figure to size against (§5)?
3. **Motor rail regulated or not** — this is really decision 1 restated, but it is the one that
   costs a part, so it is worth saying out loud.
4. **Do we pre-emptively swap the L293D for a TB6612FNG?** F6 says the L293D is marginal on
   the steering channel and §3 says its voltage drop is what forces the buck. A different
   driver improves both. Against that: the L293D is what the chosen kit ships with, and 1.7
   might measure a gentle little motor that is fine. My recommendation is **buy one TB6612FNG
   alongside** at 0.4 — it is a low-cost part and it is the fix for two separate findings, so
   having it on the shelf turns a possible re-order into an afternoon.

---

## 8. What step 1.7 measures against this

| This document predicts | 1.7 measures |
|---|---|
| Drive motor 350 mA on sand, 1.2 A stall | Actual, on the actual terrain |
| Steering motor 560 mA held at lock | Actual — **against the 600 mA rating, F6** |
| L293D package temperature under sustained steering | Actual, with a thermometer on the DIP |
| 6 W session average | Pack voltage logged over a real session |
| Divider ratio 4.70 | Calibrated against a meter → `BATTERY_DIVIDER_RATIO` |
| 20 min session inside a 1500 mAh pack | Runtime to cutoff, with margin stated |

---

*Step 0.3 · estimate only, nothing measured, no hardware procured · superseded in part by
step 1.7*
