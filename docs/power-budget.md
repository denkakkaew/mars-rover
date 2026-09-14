# Power budget and rail plan — step 0.3

Companion to [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) step 0.3, and the estimate
that step **1.7 replaces with measurements**. It closes nothing on its own: it exists so 0.4
has something to buy against and so 1.7 has a prediction to be wrong about.

> ⚠️ **Every current in this document is a datasheet figure or a class-typical guess. Nothing
> here has been measured, because almost nothing has been bought.** Each row says which it is.
> The three rows that matter most — the two drive motors and the steering motor — are the
> *least* known, and §2.2 gives a thirty-second bench measurement that resolves them.

**Decisions this step asks you for** are collected in §7. The rest is arithmetic.

---

## 0. Re-estimated 2026-08-18 — what changed and why

The first draft of this document was written against the parts believed to be fitted on
2026-08-13. Three of those beliefs were wrong, and the corrections do not cancel out.

| Was | Is | Where it came from |
|---|---|---|
| L293D, ~2 V bridge drop, 600 mA/channel | **DRV8833**, ~0.36 V drop, 1.5 A RMS/bridge, **V_M max 10.8 V** | step 1.3 on the bench |
| Steering PWM'd at `kSteerHoldDuty = 0.7` | **Steering switched flat on** — no PWM, no duty parameter | step 1.3: 70% duty could not shift the axle against its return spring |
| **One** drive motor | **Two** drive motors, both 130-class | your correction, 2026-08-18 |
| Two SG90 servos (mast pan/tilt) | **Four** SG90 servos — mast pan/tilt ×2 and camera pan/tilt ×2 | your correction, 2026-08-18 |

The three headline results all reverse:

- **§3's recommendation flips from "3S + 8 V buck" to "2S + 6 V buck".** 3S is no longer a
  preference, it is destructive: 12.6 V exceeds the DRV8833's 10.8 V `V_M` maximum. And the
  8 V setpoint existed only to overcome a 2 V drop that no longer exists.
- **F6 is not dissolved. It moved.** Step 1.3 recorded F6 as "largely dissolved by the part
  change", which was true of the *part* and false of the *load*: steering went from 70% duty
  to 100% duty at the same moment. §7 restates it against the DRV8833.
- **Two new findings, F8 and F9**, both consequences of the third motor: a DRV8833 has two
  bridges and there are now three motors, and pack voltage now lands on the motor undivided.

The session average moves less than any of this suggests — from 6.0 W to **7.1 W** — because
every added load is duty-limited. The *peaks* are what moved.

---

## 1. What is fitted, and when

The budget has to cover the finished rover, not today's. Phase 1 draws a fraction of this.

| Load | Qty | Part | Phase | Rail |
|---|---|---|---|---|
| ESP32 | 1 | ESP32-D0WD-V3 devkit | 1 | 5 V (its own board regulator makes 3.3 V) |
| Drive motor | **2** | 130-class DC, geared | 1 | motor — **both on bridge A, see F8** |
| Steering motor | 1 | 130-class DC, geared | 1 | motor — bridge B |
| Mast pan/tilt servos | 2 | SG90 | 3 | servo |
| Camera pan/tilt servos | **2** | SG90 | 3 | servo |
| Mast IP camera | 1 | not chosen until 0.4 | 3 | 5 V |
| Sensing payload | 1 | not chosen until 2.0 | 2 | 5 V — **allowance only, see §6** |

**Three motors and four servos.** The motor count is what forces F8; the servo count roughly
doubles the servo rail, which §4.1 already gave its own regulator for reasons that now apply
twice as hard.

---

## 2. The load table

All motor figures are at a **6 V motor rail** (§3's recommendation). Motor current scales with
rail voltage, so these are not portable to a different pack decision — §3.1 does that
arithmetic separately.

| Load | Rail | Idle | Working | Worst case | Where the number came from |
|---|---|---|---|---|---|
| ESP32-WROOM-32 | 5 V | 40 mA | 120 mA | **280 mA** TX peak | Espressif datasheet. The 120 mA assumes **Wi-Fi power save off**, which step 1.2 turned off deliberately — see below |
| Drive motors ×2 | motor | 0 | **500 mA total** on sand | **4.0 A** both stalled | class-typical 130 can motor — **unconfirmed, and the biggest unknown here**, §2.2 |
| Steering motor | motor | 0 | **2.0 A held at lock** | 2.0 A — *the working figure is already the stall* | ditto. No longer duty-limited: 1.3 removed the PWM |
| SG90 servos ×4 | servo | 20 mA | 600 mA moving | **2.8 A** all stalled | SG90 class, ~700 mA stall each |
| Mast IP camera | 5 V | — | 250 mA | 350 mA | ESP32-CAM class — unconfirmed until 0.4 picks one |
| Sensing allowance | 5 V | — | 300 mA | **800 mA** burst | UHF reader, candidate A (§6) |

**A steered chassis has no idle steering current and no idle drive current** — all three motors
are unpowered whenever the rover is not being driven, and the steering spring holds centre for
free. That is still why the average in §5 is so much lower than the peaks here.

**Why two drive motors are not twice one drive motor.** The tractive load is a property of the
rover and the sand, not of the motor count; two motors split the same torque. The first draft's
350 mA for one motor decomposes roughly into 150 mA of no-load losses plus 200 mA of torque
current, so two motors sharing that torque draw about `2 × 150 + 2 × 100 = 500 mA`. Two driven
wheels also dig in less than one, so the true figure may be *lower* still. **This is the row
most likely to be wrong in the optimistic direction, and 1.5 measures it on actual sand.**

**The steering row no longer has a "working" number distinct from its stall.** Three-position
steering drives the axle into a mechanical end stop and holds it there for as long as the
operator holds the button, at full rail voltage since 1.3. Held steering *is* a continuous
stall. This is F6, and it is now the single largest instantaneous load on the rover.

**The ESP32's 120 mA is a latency decision, not just a datasheet row.** Step 1.2 measured
control RTT with Wi-Fi modem sleep at its default (on) and disabled: p95 went from **99.3 ms
to 19.8 ms**, against a 100 ms R1 target. `main.cpp` therefore calls `WiFi.setSleep(false)`,
which is why the radio draws its working current continuously rather than dozing between
beacons. Roughly 40 mA of the budget buys a fivefold latency improvement on the risk this
project is most exposed to — and that trade should not be quietly reversed at 1.7 to win back
runtime, because §5 shows the ESP32 is only 8% of the session average anyway.

### 2.1 The one measurement that would retire most of this uncertainty

Every motor figure above descends from one unknown: the **armature resistance** of a 130-class
motor, which is not published for kit parts and varies by winding across a range wide enough to
change the answer.

Stall current is just `V_rail / R_armature`. So:

> **Put a multimeter on ohms across one motor's terminals, held stationary. Rotate the shaft a
> little and take the highest reading you see** (brush position changes it). That number, and
> the rail voltage, give every stall figure in this document exactly.

| If R measures | Stall at 6 V, per motor | Two drive motors paralleled | Verdict against the DRV8833 |
|---|---|---|---|
| 1.0 Ω | 6.0 A | 12 A | hopeless — OCP trips instantly, needs a lower rail or a bigger driver |
| 2.0 Ω | 3.0 A | 6.0 A | drive stalls trip OCP; steering alone survivable but hot |
| **3.0 Ω** *(assumed here)* | **2.0 A** | **4.0 A** | drive stalls trip OCP; steering is over the 1.5 A RMS rating |
| 5.0 Ω | 1.2 A | 2.4 A | comfortable — F6 and F8 both soften a great deal |

**Everything in §2, §2.2, §3 and §5 is computed at the 3.0 Ω row.** If the meter says otherwise,
this document is wrong by that ratio and should be re-run before 0.4 buys anything. It is the
cheapest de-risking available and it needs no purchase.

### 2.2 Concurrent worst case

Four cases are worth sizing against. The important change from the first draft is that
**driving-and-steering-together is now the ordinary case, not an exotic one**: the bicycle
model makes a stationary rover unable to turn at all, so every turn the rover ever makes is a
drive current and a steering stall at the same time.

| Case | Motor rail | 5 V rail | Servo rail | Pack draw @ 7.4 V |
|---|---|---|---|---|
| Straight-line driving, camera live, reader on | 500 mA | 670 mA | 0 | **≈ 1.0 A** |
| Surveying — stopped, mast and camera panning | 0 | 670 mA | 600 mA | **≈ 1.0 A** |
| **Normal turn** — driving on sand, steering held at lock | **2.5 A** | 670 mA | 0 | **≈ 2.9 A** |
| **Realistic worst** — stalled against the glass, still holding lock | 6.0 A | 670 mA | 0 | **≈ 6.2 A** |
| **Wiring/fuse sizing** — everything stalled together | 6.0 A | 1.43 A | 2.8 A | **≈ 9 A** |

Pack draw assumes 85% converter efficiency. **Fuse at 7.5 A slow-blow; wire for 6 A
continuous.** Both are up from the first draft's 5 A / 3 A, and the driver of that increase is
the steering stall — a load the first draft had no row for at full voltage.

The last row cannot physically happen: the rover is not surveying with both pan/tilt heads
while stalled against the glass. **The fourth can, and it is what a novice operator produces by
holding LEFT into a wall for three seconds.**

---

## 3. Pack voltage, and what the DRV8833 changes about it

Step 1.3 replaced the L293D with a **DRV8833** after the L293D's ~2 V bridge drop left only
~2.9 V at the motor from the bench's 4.8 V pack, and nothing moved usefully. The DRV8833 drops
about **0.36 V** at 1 A (R_DS(on) ≈ 360 mΩ, high side plus low side), so the motor now sees
essentially the rail.

That is good news that arrives holding two problems.

- **`V_M` maximum is 10.8 V.** 3S LiPo is 12.6 V charged. The first draft's recommendation
  would now destroy the driver on first power-up.
- **The 2 V of drop was doing hidden work.** It absorbed pack-voltage excess and it limited
  stall current. Both of those protections are gone, and the pack now lands on a 130-class
  motor undivided. This is **F9**.

### 3.1 What each pack does, unregulated

A 130-class motor in a kit of this sort is a **3–6 V part**; the bench pack today is 4.8 V.

| Pack | Full | Nominal | Cutoff | Motor sees, full → cutoff | Verdict |
|---|---|---|---|---|---|
| 4×AA NiMH (4.8 V) | 5.2 V | 4.8 V | 4.0 V | 4.8 → 3.6 V | in spec, but 25% sag and weak on sand |
| 5×AA NiMH (6.0 V) | 6.5 V | 6.0 V | 5.0 V | 6.1 → 4.6 V | in spec throughout; 25% sag |
| **2S LiPo (7.4 V)** | 8.4 V | 7.4 V | 6.6 V | **8.0 → 6.2 V** | **over-volts a 6 V motor by 33% at full charge** |
| 3S LiPo (11.1 V) | 12.6 V | — | — | — | ⛔ **exceeds `V_M` 10.8 V — destroys the driver** |

2S direct-drive is the mirror image of the problem the first draft found with 3S on an L293D,
arrived at from the opposite direction: **it starts over the motor's rating and sags into it.**
And because stall current scales with rail voltage, 8.0 V on the assumed 3.0 Ω winding is
**2.7 A per motor / 5.3 A across the paralleled pair** — comfortably into the DRV8833's
overcurrent trip on any stall, at exactly the moment the rover most needs to push.

### 3.2 Why the sag is still a real problem, and this argument did survive

This was the first draft's argument for regulating, and **it is untouched by the driver
change** — it is a software argument rather than an electrical one, and it applies to any
unregulated pack.

Step S.15 set **`MIN_EFFECTIVE_THROTTLE = 0.45`** in
[rover_link.gd](../console/scripts/rover_link.gd) — the floor below which the rover will not
break out of its own ruts on sand. It is a **fixed constant**. Breakaway needs roughly a fixed
torque, so the duty required to reach it scales inversely with rail voltage: a 25% sag pushes
the real floor from 0.45 to about **0.60** by end of session.

**The rover would begin a session driveable and end it unable to start from rest at the
operator's usual throttle** — and the console, holding a constant, would have no way to know.
The operator would report it as "the rover stopped responding to small inputs", which is a
miserable thing to debug in an arena.

A regulated motor rail removes the whole failure mode. The console's constant stays true from
full to cutoff.

> ⚠️ Separately: **0.45 itself is now suspect.** S.15 raised it from 0.32 purely on the
> L293D's voltage-drop argument, and that argument is void. The number has still never met a
> motor. Measure breakaway at 1.5 and expect it to come *down*.

### 3.3 Recommendation

**2S LiPo with a 6.0 V buck converter feeding the DRV8833's `V_M`.** The motor sees a constant
~5.6 V at the terminals (6.0 V rail less the bridge drop) from full charge to cutoff, inside
its rating at every state of charge, with stall current capped by the rail rather than by the
pack.

This costs one converter and buys four things at once: it keeps `V_M` clear of its 10.8 V
maximum with a wide margin, it stops over-volting the motor, it caps stall current at the
figures §2 is computed against, and it keeps `MIN_EFFECTIVE_THROTTLE` true for a whole session.

**Size the motor buck at 6 V / 5 A**, not 3 A — the normal-turn case is 2.5 A continuous and
stalls step well above it. A 3 A part would current-limit during turns, which presents as the
rover slowing mid-corner for no reason the telemetry explains.

Choosing 2S **does** cost four constants, all currently written for 3S:

| File | Constant | Now | 2S |
|---|---|---|---|
| [fake_rover.py](../tools/fake_rover.py) | `BATTERY_FULL_V` | 12.6 | **8.4** |
| [fake_rover.py](../tools/fake_rover.py) | `BATTERY_EMPTY_V` | 10.5 | **6.6** |
| [console.gd](../console/scripts/console.gd) | `BATTERY_LOW_V` | 11.1 | **7.2** |
| [console.gd](../console/scripts/console.gd) | `BATTERY_CRITICAL_V` | 10.5 | **6.8** |

Plus `BATTERY_DIVIDER_RATIO` in [config.h](../firmware/include/config.h), which F5 was going to
change anyway.

**The alternative worth considering is 5×AA NiMH at 6.0 V, direct, no motor buck at all** —
in spec at both ends, no converter, cheap, and it makes the pack a commodity the arena can keep
spares of. It costs the §3.2 property (the throttle floor moves during a session), it is
heavier per watt-hour, and the same four constants still change. If the buck is the part you
would rather not add, this is the honest second choice, not a bad one.

---

## 4. Rail plan

Four rails, one ground.

```
  2S LiPo  7.4 V nominal   (8.4 V charged, 6.6 V cutoff)
    │
    ├──[ fuse 7.5 A ]─┬── buck  6.0 V / 5 A ──► DRV8833 V_M
    │   slow-blow     │                          │  1000 µF bulk AT the driver
    │                 │                          ├─► bridge A ─┬─► drive motor 1  } paralleled
    │                 │                          │             └─► drive motor 2  } — see F8
    │                 │                          └─► bridge B ──► steering motor
    │                 │
    │                 ├── buck  5.0 V / 2 A ──┬─► ESP32 board VIN  (onboard LDO → 3.3 V)
    │                 │      "logic rail"     ├─► mast IP camera
    │                 │                       └─► sensing payload   470 µF bulk, local
    │                 │
    │                 └── buck  5.0 V / 4 A ──── SG90 servos ×4  (mast ×2, camera ×2)
    │                        "servo rail"        100 µF bulk, local
    │
    └──[ 100 kΩ / 47 kΩ divider ]──► ESP32 GPIO34  (ADC, battery sense)

  COMMON GROUND — star point at the pack negative terminal
```

**The DRV8833 needs no logic supply.** The L293D's `Vss` rail is gone: the DRV8833 takes 3.3 V
logic directly from the ESP32 with no level shifting and no second supply pin, which also
retires the first draft's checkpoint about breakout modules tying `Vss` to `Vs`. One fewer
thing to verify, and one fewer thing to get wrong.

**Do not feed the ESP32 from the pack directly.** 8.4 V into VIN puts 5.1 V across the devkit's
onboard linear regulator, which is 1.4 W at the 280 mA TX peak — enough to drive it into
thermal foldback, and a foldback on the 3.3 V rail during a transmit is a brownout reset. The
5 V logic rail exists partly for this.

### 4.1 Why the servos get their own regulator rather than sharing the logic rail

Four servos stalling is a **2.8 A step** — double the first draft's figure — and servo current
transients are the classic cause of ESP32 brownouts. On a shared rail that step resets the
ESP32 mid-drive; the link drops, the failsafe trips, the motors cut, and the operator sees an
unexplained dropout with no bad telemetry preceding it — the hardest class of fault to diagnose
from the console.

Separating them costs one converter and makes that failure impossible rather than unlikely.
This is the same reasoning as §1.1 of [protocol.md](protocol.md), applied to amps instead of
bits: keep the bursty thing off the channel the safety behaviour depends on.

The doubled servo count is why this rail is now specified at **4 A** rather than 3 A.

### 4.2 Logic levels

The DRV8833 is a 3.3 V-logic part driven directly by the ESP32 — `AIN1`/`AIN2`/`BIN1`/`BIN2`
and `STBY` all take GPIO levels with no shifter and no separate logic rail. Verified working on
the bench at step 1.3.

`STBY` must be HIGH for the driver to do anything, and it is driven from GPIO14 rather than
tied to 3V3 deliberately: that is what makes `stop()` a hardware disable of both bridges rather
than merely a zero duty cycle ([config.h](../firmware/include/config.h)).

The 20 kHz PWM restored at 1.3 is well inside the DRV8833's 250 kHz input limit.

### 4.3 Bulk capacitance

- **1000 µF at the DRV8833 `V_M`**, physically at the driver, not at the converter. Motor
  inrush and the steering stall are step loads, and the buck's control loop is too slow. This
  matters *more* than it did with the L293D, because the DRV8833 passes the step through
  instead of absorbing 2 V of it.
- **470 µF local to the sensing payload** if 2.0 picks the UHF reader, which draws in bursts
  while transmitting.
- **100 µF local to the servo rail**, which now carries four servos.

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
| Drive motors ×2 | 3.00 W | 40% | 1.20 W |
| **Steering motor** | **12.00 W** | 10% | **1.20 W** |
| SG90 servos ×4 | 3.00 W | 10% | 0.30 W |
| Sensing (candidate A) | 1.50 W | 100% | 1.50 W |
| | | **subtotal** | **6.05 W** |
| Converter losses (÷ 0.85) | | | **≈ 7.1 W** |

- 20 minutes at 7.1 W = **2.4 Wh**
- ×2 for session overrun and not running the pack flat = **4.8 Wh**
- LiPo usable fraction ≈ 80% (4.2 V down to 3.5 V per cell) → **6.0 Wh of rated capacity**
- At 7.4 V nominal → **810 mAh minimum**

**Recommendation: a 2S 1500–2200 mAh pack** — 11–16 Wh, so 2–3× the requirement. That buys
several sessions per charge rather than one, keeps the C-rate undemanding (the 6.2 A
realistic-worst peak is about 4 C against a 1500 mAh 25 C part), and weighs 80–120 g against
the 800–1000 g payload allowance in [chassis-envelope §6](chassis-envelope.md).

Oversizing here is cheap and the alternative is a rover that dies mid-demonstration.

### 5.1 The result worth noticing

**The steering motor is 20% of the session energy at 10% duty** — as much as both drive motors
combined, and more than the ESP32 and camera together. A 130 motor held against its end stop at
full rail voltage is a 12 W space heater, and the operator holds it every time they turn.

Two consequences. First, if session runtime ever disappoints, **steering is the first place to
look, not the radio**. Second, it makes F6 an energy finding as well as a thermal one: anything
that reduces the hold current pays twice.

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

The rail plan in §4 still survives every candidate, but **one of them no longer survives for
free**:

- **A** lands on the logic rail, which has headroom for it.
- **B**'s 2.1 A of stall lands on the **servo** rail — and that rail now carries four servos
  rather than two, so B would put **seven** servos on it and 4.9 A of theoretical stall. At
  4 A that is no longer merely tight: **B would need the servo rail resized to 6 A.** This is
  a change from the first draft, and it is caused by the camera-servo correction, not by B.
- **C and D add nothing to the rover at all**, and would reduce the budget in §5 by 1.5 W,
  about a fifth of the average draw.

So 2.0 changes a BOM line, the §5 average, and — if it picks B — one converter rating. It does
not reopen this step.

---

## 7. Findings and decisions

### 🔴 F5 — `BATTERY_DIVIDER_RATIO = 2.0` would destroy the ADC pin *(revised for 2S)*

[config.h](../firmware/include/config.h) carries `BATTERY_DIVIDER_RATIO = 2.0f`. Against a 2S
pack that puts **4.2 V on GPIO34 at full charge**; against 3S it was 6.3 V. The ESP32's
absolute maximum on any GPIO is **3.6 V**. Moving to a smaller pack reduces the overshoot but
**does not retire the finding** — it is still a value that damages the board the first time it
is powered.

It has never bitten because the divider has never been wired: bring-up at 1.1 ran on USB power
with no divider at all, deliberately. It bites **the moment a pack is connected to GPIO34.**

**Recommended divider for 2S: 100 kΩ (top) / 47 kΩ (bottom), ratio 3.13.**

| Pack | Volts at GPIO34 |
|---|---|
| 8.4 V charged | 2.69 V |
| 7.4 V nominal | 2.37 V |
| 6.6 V cutoff | 2.11 V |

That sits inside the ADC's usable range at 11 dB attenuation with headroom at the top, and
draws 57 µA — negligible against a 1500 mAh pack. It lands on almost exactly the same ADC
voltages as the first draft's 3S/4.70 recommendation, so nothing downstream of the ADC needs
rethinking.

If **100 kΩ / 27 kΩ has already been bought** for the 3S plan, it is *safe* on 2S — 1.79 V at
full charge — it merely wastes two thirds of the ADC range and costs resolution on the
low-battery thresholds. Not a hazard; use it if it is what is on the shelf, and set the ratio
to 4.70.

The *ratio constant* is fixed by the resistors chosen here; step 1.7 still calibrates it
against a meter, because 1% resistors and the ESP32's ADC non-linearity together are worth
several hundred millivolts.

### 🔴 F6 — held steering is a continuous stall at full rail voltage *(restated against the DRV8833)*

Step 1.3 recorded F6 as "largely dissolved by the part change". **That was half right, and the
half it got wrong is the load.** The same step that swapped in a better driver also deleted the
duty cycle that was limiting the steering current, for good reasons — 70% duty could not shift
the axle against its return spring — but the two changes were assessed separately and their
product was never worked out.

The arithmetic, at the assumed 3.0 Ω winding and a 6 V rail:

- Steering channel: **2.0 A continuous** for as long as the operator holds the button, against
  the DRV8833's **1.5 A RMS** per-bridge rating. 133% of rating.
  ⚠️ [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) step 1.3 records the part as
  "1.2 A/channel". TI's datasheet says 1.5 A RMS / 2 A peak; 1.2 A may be the breakout
  module's derating or a carry-over from the TB6612FNG line. **This document uses 1.5 A. If
  1.2 A is right, the overload is 167% rather than 133%** — worth resolving from the module's
  own listing at 0.4.
- Dissipation in that bridge: `I² × R_DS(on)` = `2.0² × 0.36` ≈ **1.44 W**.
- A DRV8833 breakout with a modest copper pour is θ_JA ≈ 50–70 °C/W → **70–100 °C rise** over
  ambient, from the steering bridge alone, before the drive bridge adds anything.

The part has both thermal shutdown and overcurrent protection, so the realistic failure is
still not smoke — it is **the driver cutting out mid-turn and the rover coasting on**,
intermittently, under exactly the sustained steering the arena demands. That is a
safety-adjacent failure, not a performance one, and it is the *same* failure the L293D
threatened, on the part that was supposed to have removed it.

The escape routes have changed, because the obvious one is closed:

1. ~~Lower the hold duty~~ — **tried and failed at 1.3.** A PWM-limited steering channel could
   not move the axle at all.
2. **Full voltage to move, reduced duty to hold.** Reaching lock and *staying* at lock are
   different mechanical problems, and 1.3 only tested the first. A ~300 ms full-voltage kick
   followed by a lower holding duty could cut the average current substantially. This
   reintroduces the parameter 1.3 deliberately deleted, so it must be **measured at 1.4, not
   guessed** — and if it works it wants a different name, so it cannot be mistaken for
   `kSteerHoldDuty` creeping back.
3. **Lower the motor rail for bridge B only.** Steering current scales with rail voltage and
   the axle only has to reach a stop. A separate 4.5 V feed would cut hold current by 25% and
   dissipation by 44%, at the cost of a fourth converter.
4. **Heatsink the DRV8833**, or pick a module with a proper thermal pad. Cheapest of all.
5. **Replace the steering motor with a servo.** An SG90 or MG90S holds a position without
   stalling, which removes the finding entirely rather than mitigating it — and the BOM already
   has four of them. This is a chassis change so it belongs to 0.2 rather than here, but it is
   the only option on this list that makes the problem go away.

**Measure the steering stall at 1.4, one step earlier than the first draft asked** — 1.7 is too
late, because 1.4 is the first step that holds steering under power for any length of time.

### 🟠 F8 — three motors, two bridges

A DRV8833 has **two** H-bridges. The rover now has **three** motors. Bridge B is the steering,
so **the two drive motors must share bridge A**, and the two ways to share it are not
equivalent.

| | Parallel | Series |
|---|---|---|
| Volts per motor at a 6 V rail | 5.6 V each | 2.8 V each |
| Total current, working on sand | 500 mA | 250 mA |
| Total current, both stalled | **4.0 A** — trips OCP | **1.0 A** — comfortable |
| Speed | full | roughly half |
| One motor jams | the other keeps driving; the rover yaws | both stop |
| Unequal grip | each motor gets full volts regardless | the *unloaded* motor takes most of the voltage, so the wheel with less grip spins faster — backwards |

**Recommendation: parallel**, which is how these kits ship wired and what the 6 V rail is sized
for. Series halves the current draw and would make F8 and much of F6's rail sizing evaporate,
but it also halves the speed and gives the wrong response to unequal grip on sand — the terrain
this build is most worried about.

Two things follow from parallel that are worth writing down:

- **Bridge A carries double the current of a single-motor design**, so it is the drive bridge,
  not just the steering bridge, that sits near the DRV8833's rating. The 1000 µF bulk in §4.3
  is doing real work.
- **There is no independent control of the two drive wheels, and there should not be.** A
  steered chassis wants both rear wheels at the same speed; the differential is mechanical or
  absent. Nothing in [protocol.md](protocol.md), `lib/Drive`, or `fake_rover.py` changes —
  `fwd` still maps to one bridge. **F8 is a wiring and rail-sizing finding, not a software
  one.**

⚠️ **Confirm how the kit actually wires the pair before assuming.** Some kits ship them already
soldered in series inside the gearbox housing. Check with a meter at 1.4 — the two arrangements
read very differently on ohms.

### 🟠 F9 — the bridge drop was doing hidden work, and it is gone

The L293D's ~2 V drop was a defect with two accidental benefits: it absorbed excess pack
voltage before it reached the motor, and it limited stall current. The DRV8833's ~0.36 V drop
removes the defect and both benefits together.

Consequences, all of which §3 acts on:

- **3S is now destructive rather than merely wasteful** — 12.6 V against a 10.8 V `V_M` maximum.
- **2S direct over-volts a 3–6 V motor by 33% at full charge**, where the same pack behind an
  L293D would have been roughly correct.
- **Stall currents scale with the pack** rather than being clamped by the bridge, which is what
  makes F6's and F8's numbers as large as they are.
- **The motor rail wants regulating for a different reason than the first draft gave.** The
  first draft regulated to *raise* motor voltage above a lossy bridge; §3.3 regulates to *cap*
  it below the pack. Same part, opposite job, and a setpoint of 6.0 V instead of 8.0 V.

### 📋 Decisions for you

1. **Pack: 2S LiPo with a 6 V / 5 A motor buck** (recommended, §3.3), or 5×AA NiMH at 6 V
   direct with no buck and a throttle floor that moves during a session? Either way, **3S is
   off the table** — F9.
2. **Session length** — is 20 minutes still the right figure to size against (§5)?
3. **Drive motors in parallel or series** (F8) — parallel for speed and correct behaviour on
   uneven grip; series for a quarter of the stall current. My recommendation is parallel, but
   it deserves a deliberate answer, because series would soften F6's rail sizing too.
4. **What to do about F6**, which is the finding with real teeth now. Options 2 (kick-then-hold,
   measured at 1.4), 4 (heatsink) and 5 (servo steering) are the ones worth costing at 0.4. My
   recommendation is **buy a heatsink and an MG90S alongside** — a few dollars total, and
   between them they cover both the cheap mitigation and the real fix, so an unpleasant
   measurement at 1.4 becomes an afternoon rather than a re-order.
5. **Meter the 130 motors' resistance before 0.4 buys anything** (§2.1). Not really a decision
   — thirty seconds of work that determines whether F6 and F8 are serious or merely tidy.

---

## 8. What steps 1.4, 1.5 and 1.7 measure against this

| This document predicts | Measured at | What is measured |
|---|---|---|
| 130 motor armature resistance ≈ 3.0 Ω | **now**, §2.1 | ohms across the terminals, shaft held |
| Kit wires the drive pair in parallel — F8 | **1.4** | ohms across the pair, before power |
| **Steering 2.0 A continuous held at lock — F6** | **1.4** | actual, against a 1.5 A RMS rating |
| **DRV8833 package temperature under sustained steering — F6** | **1.4** | a thermometer on the module after 30 s of held lock |
| A reduced holding duty can keep the axle at lock — F6 option 2 | **1.4** | the lowest duty that *holds* what full voltage *reached* |
| Drive motors 500 mA total on sand, 4.0 A both stalled | 1.5 / 1.7 | actual, on the actual terrain |
| `MIN_EFFECTIVE_THROTTLE` 0.45 is too high (§3.2) | 1.5 | breakaway throttle on sand |
| 7.1 W session average | 1.7 | pack voltage logged over a real session |
| Divider ratio 3.13 | 1.7 | calibrated against a meter → `BATTERY_DIVIDER_RATIO` |
| 20 min session inside a 1500 mAh 2S pack | 1.7 | runtime to cutoff, with margin stated |

---

*Step 0.3 · re-estimated 2026-08-18 against the DRV8833, three motors and four servos ·
estimate only, nothing measured, no pack procured · superseded in part by steps 1.4 and 1.7*
