# Chassis envelope vs. the 1.4 m arena — step 0.2

> ## ⛔ Decision, 2026-08-13: all four candidates below were rejected
>
> The chassis chosen instead is an AliExpress kit (item `1005008274445888`) with a
> **fundamentally different drive architecture**: **two DC motors — one for
> forward/backward, one for steering left/right** — driven through an **L293D**, with speed
> set by PWM.
>
> **That is not skid steer.** Everything from §1 down assumes a differential platform that
> pivots about its own centre, and a steered chassis cannot do that at all. The swept-circle
> method below is the right method for the wrong vehicle, so **§1–§5 are superseded** and
> kept only as the record of how the decision was reached. §6's non-geometric criteria
> (sand, payload, mast stability) still apply to any chassis.
>
> The operator was also explicit that fine positioning is **not** a design goal for this
> build: rocks are scattered at random and the operator drives to them by hand, free-drive,
> with no precision-placement requirement. Steps that existed to serve millimetre alignment
> should be read in that light.
>
> **What the new architecture actually constrains is [§8](#8-what-the-steered-chassis-changes).**

Written at step 0.2 of [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md). Its job was to
catch risk **R5** — "a 4-wheel chassis cannot turn in place inside a 1.4 m-wide arena" —
**on paper, before anything is bought.** Nothing here commits to a purchase; step 0.4 quotes
whatever this step settles on.

**Nothing has been measured.** Every dimension below is a vendor figure from a product page,
and vendor figures are inconsistent about whether width includes the wheels. Where that is
ambiguous the table says so and carries a conservative estimate. All of it is re-measured
against the actual unit at step 1.5.

---

## 1. The geometry

A skid-steer platform turns in place by spinning one side forward and the other back. It
pivots about the centroid of its wheel contact patches — the chassis centre, for a
fore-aft-symmetric 4WD — and every point of the vehicle sweeps a circle about that centre.

**What has to fit the arena is the diagonal, not the width.** This is the whole point of the
step: comparing a 300 mm-wide chassis against a 1400 mm arena is the wrong comparison.

```
                     <----------- L ----------->
                     +-------------------------+        swept radius r is measured
                     |                         |  ^     from the pivot to the FURTHEST
                     |            o pivot      |  |     point of the vehicle, which is
                     |             ` .         |  W     normally a rear corner:
                     |                 ` . r   |  |
                     +---------------------`---+  v         r = sqrt(L² + W²) / 2
                                            `
                                          corner       swept diameter D = 2r = sqrt(L² + W²)

   ...unless something is bolted to the front. Then the far point may be out there instead:

                     +-------------------------+====[]   <- front mount, projecting P
                     |            o             |  arm/antenna, width Wf
                     +-------------------------+

              r_front = sqrt( (L/2 + P)² + (Wf/2)² )        D = 2 · max(r_corner, r_front)
```

The drivable width is **not** the glass width. Terrain dressing mounds against the walls and
the chassis must never touch glass, so a keep-out is subtracted at each side:

```
    drivable width  =  1400 mm  -  2 × keep-out
    clearance/side  =  (drivable width - D) / 2        negative means it cannot turn
```

**Keep-out is provisional at 50 mm per wall** (1300 mm drivable) until step 0.5 settles the
sand depth and dressing. §4 shows how little the answer depends on it.

---

## 2. Candidates

Four representative classes, from a hobby PCB platform to an all-terrain chassis. These are
starting points for discussion, not a shortlist anyone has committed to.

| | Chassis | L × W quoted (mm) | Wheel Ø | Clearance | Payload | Width includes wheels? |
|---|---|---|---|---|---|---|
| **C1** | [XiaoR Geek 4WD](https://www.xiaorgeek.net/products/4wd-robot-car-chassis-kits) (PCB deck) | 256 × 150 | 65 mm | ~15 mm | **500 g** | **No** — PCB only; est. 205 mm over wheels |
| **C2** | [LewanSoul 4WD](https://www.amazon.com/LewanSoul-Chassis-Intelligent-Aluminum-Unassembled/dp/B08LK1RDXM) (alu) | 180 × 140 | 66 mm | ~20 mm | 1500 g | Unclear; est. 195 mm over wheels |
| **C3** | [Hobby King alu 4WD](https://hobbyking.com/aluminum-4wd-robot-chassis-black-kit.html) | 210 × 202 | ~65 mm | ~20 mm | not stated | Assumed yes |
| **C4** | [Dagu Wild Thumper 4WD](https://www.pololu.com/product/1565) | 280 × 300 | **120 mm** | **60 mm** | several kg (1.9 kg bare) | Yes |

---

## 3. The arithmetic

Swept diameter `D = sqrt(L² + W²)`, using the **over-wheels** width where the quoted figure
excludes wheels. Clearance is per side against a 1300 mm drivable width.

### 3.1 Chassis alone

| | L × W used (mm) | Swept Ø (mm) | Clearance/side (mm) | Turns in place? |
|---|---|---|---|---|
| C1 | 256 × 205 | **328** | +486 | yes |
| C2 | 180 × 195 | **265** | +517 | yes |
| C3 | 210 × 202 | **291** | +504 | yes |
| C4 | 280 × 300 | **410** | +445 | yes |

### 3.2 With the front mount — reserving the worst case across the 2.0 candidates

Step 2.0 has not chosen a sensing method, so this reserves the **largest** envelope any
candidate would need, per the plan's instruction to carry the margin rather than re-decide
later:

- **UHF RFID panel antenna** (2.0 candidate A) — a flat panel, projecting perhaps **60 mm**
  beyond the nose, ~100 mm wide.
- **Probe arm + IR/colour sensor** (2.0 candidate B) — must reach the ground *ahead* of the
  rover, so at full extension it projects far further. **Reserve 200 mm, 60 mm wide.**
- **Camera-only** (candidates C and D) — projects nothing.

Reserving the probe arm at P = 200 mm, Wf = 60 mm:

| | r_corner (mm) | r_front (mm) | Governing | Swept Ø (mm) | Clearance/side (mm) |
|---|---|---|---|---|---|
| C1 | 164 | **329** | front mount | **659** | +321 |
| C2 | 133 | **292** | front mount | **583** | +358 |
| C3 | 146 | **306** | front mount | **613** | +344 |
| C4 | 205 | **341** | front mount | **683** | +309 |

**Every candidate turns in place with at least 300 mm to spare per side, in the worst case
that step 2.0 can produce.**

### 3.3 To scale, against the drivable corridor

Swept circles drawn against the 1300 mm drivable width. `#` is the chassis-alone circle,
`=` the extra reserved for a probe arm, `.` is spare room.

```
                |<---------------- 1300 mm drivable --------------------->|
                |                                                          |
  C1  659 mm    |..............=======###############=======...............|
  C2  583 mm    |................=======############=======................|
  C3  613 mm    |...............=======#############=======................|
  C4  683 mm    |..............======##################======..............|
                |                                                          |
  first size    |##########################################################|
  that fails    |       920 mm square - nothing here is remotely close     |
```

---

## 4. How sensitive is this to the keep-out?

Not at all. C4 is the worst case at 683 mm swept:

| Keep-out per wall | Drivable width | C4 clearance/side |
|---|---|---|
| 0 mm | 1400 mm | +358 mm |
| 50 mm *(assumed)* | 1300 mm | +309 mm |
| 80 mm | 1240 mm | +279 mm |
| 120 mm | 1160 mm | +239 mm |
| 200 mm | 1000 mm | +159 mm |

The step 0.5 dressing decision cannot make R5 bite.

---

## 5. Finding: R5 does not discriminate between chassis

**A chassis would have to be roughly 920 mm square before it could not turn in a 1300 mm
corridor.** No desk-sized 4WD platform is remotely close — the largest candidate here uses
**half** the available width even with a 200 mm arm reserved in front of it.

This matches what the S.12 simulator showed independently: a 1.10 × 0.85 m chassis was the
first size that fouled the walls.

So R5, *as written*, is settled and should not drive the chassis decision. What it was
protecting against is real, but it is a **driveability** concern rather than a geometric one —
whether the operator can hold a line down a 1.3 m corridor when the rover curves under its own
steering drift (S.12 measured ~6° of heading error over 0.6 m on sand). That is judged at
steps 1.5 and 3.6, and is already noted in the plan's risk register as the unnumbered
driveability risk.

**Recommendation: retire R5 to "settled on paper, confirm at 1.5" and choose the chassis on
the criteria in §6 instead.** That is a decision for the review gate, not one this document
takes.

---

## 6. What should decide it instead

Ranked by how much they actually constrain the build:

1. **Sand capability.** The arena floor is sand, and step 1.5 explicitly drives on it. Wheel
   diameter and ground clearance dominate here. A 65 mm wheel with ~15–20 mm of clearance
   (C1–C3) will plough and bog; 120 mm spiked wheels with 60 mm clearance and suspension (C4)
   will not. **This is the single biggest discriminator and it is not close.**
2. **Payload.** Rough estimate of what has to ride: ESP32 + H-bridge ~70 g, 3S LiPo ~180 g,
   camera ~30 g, mast pole + pan/tilt head ~200 g, sensing payload 150–300 g, wiring and
   mounts ~150 g — call it **800–1000 g**, before any margin. That **disqualifies C1 outright**
   at its stated 500 g limit and makes C2 marginal at 1500 g.
3. **Mast stability.** Step 3.4 requires the pole not to oscillate visibly after a fast pan,
   and R8 makes the mast the rover's only eye. A tall mast on a small, light chassis rings;
   a wider, heavier base damps it. Favours C4, and argues against C2 being the smallest.
4. **A usable front mounting face.** Whatever 2.0 picks bolts on here. Needs a rigid, flat
   front with mounting holes at a sensible height — C4's 2 mm anodised plate with a 10 mm hole
   grid is the easiest of the four to work with.
5. **Power draw (R4).** The one place C4 loses: four larger motors on sand pull far more
   current than four TT motors, which pushes up the step 0.3 budget and the pack size. This is
   a real cost and should be priced at 0.4, not waved away.

---

## 7. Open items this step does not close

| # | Item | Where it closes |
|---|---|---|
| 1 | Whether each quoted width includes the wheels — C1 and C2 are estimates | 0.4 on the datasheet, 1.5 with calipers |
| 2 | Actual keep-out once the terrain is dressed | 0.5 |
| 3 | Real front-mount projection, once 2.0 picks a method | 2.0, then re-check this table |
| 4 | Whether the chassis is fore-aft symmetric about the wheels — an overhanging tail moves the pivot and grows the swept radius | 1.5, measured |
| 5 | Measured turning circle vs. these figures | **1.5** |

---

## 8. What the steered chassis changes

Recorded 2026-08-13, when the candidates above were rejected. This is the live section; §1–§7
are history.

### 8.1 R5 is reopened, and it now means something different

A skid-steer rover pivots about its centre, so "can it turn round" was a question about the
swept diagonal — and §5 showed no plausible chassis fails it. **A steered chassis cannot pivot
at all.** It has a *minimum turning circle* set by its wheelbase and steering lock, and that
circle is typically much larger than the vehicle.

If the minimum turning circle exceeds the drivable width, the rover **cannot turn round in one
move** and needs a three-point turn against the glass. That may well be acceptable — the
mission is free-drive and the operator can reverse — but it is now a genuine open question
where it previously was not.

**Not calculated here, by instruction.** It is a tape-measure job on the real chassis at step
1.5: drive it at full lock and measure the circle. One number, one afternoon, no arithmetic
needed in advance.

### 8.2 The control protocol no longer describes this vehicle

[docs/protocol.md](protocol.md) v1 §3.2 defines driving as **per-side throttle**:

```
{"cmd":"drive","l":0.6,"r":-0.6}
```

There are no "sides" on this chassis. `l` and `r` have no meaning, and the command has to
become throttle-plus-steering. Every implementation carries that: `lib/Protocol`, `lib/Drive`,
`tools/fake_rover.py`, and `rover_link.gd`.

**This is a breaking change and needs a protocol version bump to v2** — and it is breaking in
the worst way, silently. A v1 console sending `l`/`r` at a v2 rover would have both fields
ignored and default to zero (v1 §3.2 makes them optional), so the console would look like it
was driving while the rover sat still. The handshake exists precisely to catch this.

### 8.3 The L293D is the weakest of the three drivers, in two specific ways

Not a veto — it is a legitimate choice for two small motors, and it is what has been decided.
But two things need checking rather than assuming:

- **Current.** L293D is rated **600 mA continuous per channel** (1.2 A peak). A geared drive
  motor pushing through sand can exceed that, and stall current certainly will. `CLAUDE.md`'s
  target architecture named TB6612FNG (1.2 A continuous, 3.2 A peak) or L298N. **Measure stall
  current at step 1.7** and compare against 600 mA before committing the arena to it.
- **Voltage drop.** L293D is a bipolar Darlington part and drops roughly **1.8–2 V** across the
  bridge, against ~0.5 V for a TB6612FNG. On a 6 V rail the motor sees about 4 V, so it will
  be noticeably slower and weaker than its rating suggests. Size the motor rail at 0.3 with
  that drop included.

### 8.4 The PWM frequency in `lib/Drive` is wrong for an L293D

`Drive.cpp` sets `kPwmFrequencyHz = 20000`, chosen to keep motor whine above the audible band.
**An L293D cannot switch cleanly at 20 kHz** — it is a slow Darlington device, and the usual
guidance is to stay at or below a few kHz. At 20 kHz most of the duty cycle is spent in the
transition, wasting power as heat and giving poor low-speed torque, which is exactly the
region the S.13 throttle floor cares about.

**Drop it to ~1–2 kHz**, and accept that the motors will whine audibly. That is a real
trade-off to make deliberately rather than discover on the bench.

### 8.5 The steering is three-position, not proportional

Confirmed 2026-08-13: the steering motor drives to a **mechanical end stop**, with a spring
recentring it when unpowered. There is no intermediate angle — the rover goes straight, or it
turns at full lock.

Three consequences, and the first is the one that matters:

- **You cannot make a small heading correction.** S.12 measured the rover curving several
  degrees per metre under its own drive asymmetry, and the only correction available is a
  *full-lock* turn the other way. Driving straight becomes a series of taps rather than a
  held input. This makes the S.13 nudge concept more useful, not less — a timed steering tap
  is exactly the primitive this chassis needs — and it is the thing to watch at 1.5.
- **The failsafe centres the steering for free.** Cutting power lets the spring recentre, so a
  rover that fails safe mid-turn coasts straight rather than continuing to arc into the glass.
  That is a genuine safety property worth writing into the contract — and worth confirming at
  1.3, because a weak or missing spring quietly removes it.
- **The console maps onto it cleanly.** Hold-to-drive already means "press for as long as you
  want this"; hold-LEFT / release-to-centre is the same gesture, and needs no new control.

### 8.6 Still true, and worth keeping

- The **payload estimate of 800–1000 g** from §6 is independent of steering type, and this
  chassis class is much lighter-duty than C4. Check it against the kit's stated load limit.
- **Sand capability** remains the thing most likely to disappoint (§6.1). Small wheels and low
  clearance plough. Now compounded: a steered front axle digs in more readily than a driven
  one, and there is only *one* driven axle instead of four.
- **Mast stability** (§6.3) — a tall pan/tilt mast on a light chassis rings after a fast pan,
  and R8 makes the mast the only eye.
