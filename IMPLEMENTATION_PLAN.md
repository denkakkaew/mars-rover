# Implementation Plan — Mars Rover Exploration

Companion to the proposal. The proposal says *what* gets built and *why*; this file says
*in what order*, in pieces small enough to review one at a time.

**Tracking Revision 2** — [plan/storyboard-exploration.docx](plan/storyboard-exploration.docx)
(English) and [plan/storyboard-exploration-th.docx](plan/storyboard-exploration-th.docx)
(Thai), both dated 2026-07-27. Revision 2 supersedes the Sample-Return concept in
[plan/storyboard.md](plan/storyboard.md); that file is Revision 1 and is now historical
except for its §2 project rationale, which Revision 2 carries forward unchanged.

**Status: no hardware procured.** Phase S is software groundwork and is doable today.
Everything from Phase 1 onward is blocked until parts exist.

> ⏸ **Re-prioritised 2026-08-13 — driving first.** Rock identification is **on hold** and the
> sensing method is **reopened**: UHF RFID is no longer a settled decision, it is one candidate
> among several. Phase 2 is suspended behind a new decision gate (step 2.0). Phase 1 and the
> driving half of Phase 3 proceed without it. See the next section.

---

## Re-prioritised 2026-08-13 — driving first

The plan below was written assuming UHF RFID was decided. It no longer is. Nothing about the
*mission shape* changed — the rover still seeks, scans, and identifies rocks — but **how** it
identifies them is now an open question, and the driving platform is the thing being built
first regardless of how that question resolves.

**What is on hold:** all of Phase 2, step 0.1, the sensing half of the bill of materials, the
UHF band and tag-embedding items in 0.5, and Phase 4 (which rehearses Scenes 5–8).

**What proceeds:** Phase S, Phase 1 in full, and Phase 3 minus its sensing dependencies. That
is a rover that can be driven the length of the arena on one camera feed with no direct
sightline — a real, demonstrable milestone, and the exit criteria for the driving work is now
step 3.7 rather than 4.4.

**Why this ordering is safe:** the drive platform, the link, the failsafe, the latency budget,
the chassis, the power rails and the mast camera are all **identical** under every sensing
candidate. None of that work is speculative, and none of it has to be redone when the method
is chosen. The one thing that does depend on the choice is what hangs off the front of the
rover, which is why 0.2's clearance check carries a placeholder until 2.0 lands.

**What Phase S already built is not wasted.** The composition table, session log and analysis
panel from S.11 key off an **ID string** and a **signal strength number**. Every candidate
below produces both, so the console-side payoff survives whichever sensor wins — only the
detection source changes.

⚠️ **This plan now disagrees with the proposal.** [plan/storyboard-rev2.md](plan/storyboard-rev2.md)
states UHF RFID as decided design throughout (§3, §4.3, §5.2, §5.4, R2, R7). Per `CLAUDE.md`,
the storyboard is the authority on the mission, so **it needs a Revision 3 once 2.0 picks a
method** — not before, since writing it now would just record the same open question twice.
Until then, treat the storyboard's §4.3 as one candidate rather than the specification.

---

## What Revision 2 changed

The mission is no longer "seek → collect → return". It is **"seek → scan → identify"**, and
it repeats for as many rocks as a session calls for.

| | Revision 1 | Revision 2 |
|---|---|---|
| Manipulator | 3–4 servo arm + gripper | **Removed entirely** |
| Sensing | — | **UHF RFID reader + front antenna**, tags sealed in each rock |
| Cameras | 2 (front + mast) | **1** (mast pan/tilt, doubles as the driving view) |
| Monitors | 2 | **1** |
| Payoff | Rock delivered to base | **Simulated elemental composition** on the console |
| Loop | One collect-and-return | **Repeatable** across many rocks in a session |
| Phase 2 | Manipulator integration | **RFID sensing integration** |

Consequences that shape the work below:

- **R2 changed meaning.** It was servo torque and dropped samples; it is now RFID read
  reliability through an irregular rock at an unknown orientation. The gate moved from
  "20 lifts, no drops" to "clean repeatable reads across orientations".
- **R1 eased.** One video stream instead of two roughly halves the bandwidth competing with
  the control channel. The [S.7 latency target](docs/protocol.md) is unchanged and now has
  more headroom.
- **R7 and R8 are new.** Tag survivability during embedding, and the mast camera being the
  rover's *only* eyes.
- **Phase S is largely still good.** The link, failsafe, latency and layout work all stand.
  What needs revising is the `arm` message, the readiness row, and the reserved arm region on
  screen — steps S.9–S.11 below.

---

## How to use this plan

- **One step at a time.** I do a single step, then stop and show you the result. Nothing in
  the next step starts until you say go.
- **Every step has a review gate** listing exactly what I'll put in front of you. If a gate
  can't be satisfied, that's a finding, not a step to skip.
- **Owners:** 💻 = code (me) · 🔧 = bench/hardware (you) · 📋 = decision or approval (you).
- **Size** is relative effort (S/M/L) for sequencing only. Per the proposal's stance, this
  plan carries **no dates and no costs** — those follow Phase 0 approval.
- **If a step fails its verification,** we stop and revise the plan rather than carrying the
  problem into the next step. That is the same go/no-go discipline as §7 of the proposal.
- Phase exit criteria are quoted from the proposal §7 and are the real gates; the per-step
  gates below are finer-grained checkpoints inside them.

---

## Step index

| # | Step | Owner | Size | Risk |
|---|---|---|---|---|
| **S — Software groundwork** (parallel with Phase 0, no spend, no hardware) | | | |
| S.1 | ✅ Put the repo under version control | 💻 | S | — |
| S.2 | ✅ Freeze the control protocol as a written contract | 💻 | S | R1 |
| S.3 | ✅ Extract command parsing into a host-testable module | 💻 | M | — |
| S.4 | ✅ Native unit tests for the parser + failsafe logic | 💻 | M | R1 |
| S.5 | ✅ Desktop fake-rover simulator | 💻 | M | — |
| S.6 | ✅ Console: telemetry + link states against the simulator | 💻 | M | — |
| S.7 | ✅ Console: latency HUD and round-trip measurement | 💻 | M | R1 |
| S.8 | ✅ Console: touch-target and layout pass | 💻 | M | — |
| **S — Revision 2 rework** (brings Phase S onto the revised mission) | | | |
| S.9 | ✅ Revise the protocol: drop `arm`, add the tag-read path | 💻 | M | R2 |
| S.10 | ✅ Simulator: tagged rocks and distance-dependent RSSI | 💻 | M | R2 |
| S.11 | ✅ Console: signal meter, analysis panel, session log | 💻 | L | R2 |
| **S — Driving focus** (added 2026-08-13; still no hardware, no spend) | | | |
| S.12 | ✅ Simulator: skid-steer motion model and slip on sand | 💻 | M | R5 |
| S.13 | ✅ Console: fine-drive modes and nudge moves | 💻 | M | R5 |
| **S — Steered-chassis rework** (forced by the 0.2 decision, 2026-08-13) | | | |
| S.14 | ✅ Protocol v2 + firmware: throttle and steering, not per-side | 💻 | M | **R5** |
| S.15 | ✅ Simulator and console onto the steered model | 💻 | M | R5 |
| **0 — Approval & procurement** | | | |
| 0.1 | ⏸ Fix the sensing spec *(was: the RFID spec)* — **held behind 2.0** | 📋 | M | R2 |
| 0.2 | ✅ Chassis chosen — **2-motor steered kit, L293D**; candidates rejected | 📋 | M | R5 |
| 0.3 | Power budget estimate and rail plan | 📋 | M | R4 |
| 0.4 | Bill of materials — **drive, vision and console only** | 📋 | L | — |
| 0.5 | Arena and Wi-Fi infrastructure plan *(UHF items moved to 0.6)* | 📋 | M | R6 |
| 0.6 | ⏸ Bill of materials — sensing — **held behind 2.0** | 📋 | M | R2 |
| **1 — Drive platform bring-up** | | | |
| 1.1 | ✅ ESP32 board sanity — **passed on real hardware** | 🔧💻 | S | — |
| 1.2 | Wi-Fi join, WebSocket echo, RSSI survey through the glass | 🔧💻 | M | R6 |
| 1.3 | Bench-spin one motor, no chassis | 🔧💻 | M | — |
| 1.4 | Both sides, direction and polarity calibration | 🔧💻 | M | — |
| 1.5 | Chassis assembly and first driven run | 🔧 | L | R5 |
| 1.6 | Failsafe proving — cut the link mid-drive | 🔧💻 | S | R1 |
| 1.7 | Current draw measurement per subsystem | 🔧 | M | R4 |
| 1.8 | Control-latency measurement, idle link | 🔧💻 | S | R1 |
| **2 — Rock identification** ⏸ **ON HOLD — method not decided** | | | |
| 2.0 | **Decide the identification method** — the gate that unsuspends this phase | 📋 | M | **R2** |
| 2.1 | ⏸ Reader bring-up on the bench, off the rover | 🔧💻 | M | — |
| 2.2 | ⏸ Tag embedding trial and survivability | 🔧 | M | **R7** |
| 2.3 | ⏸ Read-range characterisation, bare tags | 🔧 | M | R2 |
| 2.4 | ⏸ Read through rock, sand, and tag orientation | 🔧 | L | **R2** |
| 2.5 | ⏸ Reader mounted and powered on the rover | 🔧 | M | R4 |
| 2.6 | ⏸ Firmware RFID module and tag-read frames | 💻 | M | — |
| 2.7 | ⏸ On-rover read reliability acceptance | 🔧💻 | M | **R2** |
| **3 — Vision & operator console** *(sensing dependencies removed)* | | | |
| 3.1 | The mast camera streaming to its monitor | 🔧 | M | R3 |
| 3.2 | Forward-and-down driving view on one camera | 🔧 | M | **R8** |
| 3.3 | Concurrent load test — video plus driving | 🔧💻 | M | **R1** |
| 3.4 | Mast pan/tilt under console control | 🔧💻 | M | R8 |
| 3.5 | Console final layout on the real touchscreen | 💻 | L | — |
| 3.6 | Blind-driving acceptance run | 🔧 | M | R8 |
| 3.7 | **Driving-only mission rehearsal (Scenes 1–4)** — the driving finish line | 🔧 | M | R8 |
| **4 — Full mission integration** ⏸ **blocked on Phase 2** | | | |
| 4.1a | Arena dressing and base zone — **terrain only, proceeds** | 🔧 | M | — |
| 4.1b | ⏸ Preparing the rock batch — **held behind 2.0** | 🔧 | M | R7 |
| 4.2 | ⏸ Session log and multi-rock run state | 💻 | M | — |
| 4.3 | ⏸ Scene-by-scene rehearsal, 1 through 8 | 🔧 | L | — |
| 4.4 | ⏸ Full repeatable mission run, two or more rocks | 🔧 | M | — |
| 4.5 | ⏸ Handover documentation | 💻 | M | — |

---

# Phase S — Software groundwork

Runs **in parallel with Phase 0**, not ahead of it. Nothing here commits spend or touches
hardware, so it bypasses no gate — it exists so that when parts arrive, the console and the
protocol are already proven against a simulator and Phase 1 is purely a hardware exercise.

## Completed under Revision 1 (S.1 – S.8)

All eight steps are done and reviewed. They remain valid under Revision 2 — the transport,
the failsafe, the latency measurement and the layout work are all mission-agnostic. What each
delivered:

| Step | Delivered | Still valid? |
|---|---|---|
| S.1 | Repo under git on `main`, remote at `denkakkaew/mars-rover` | yes |
| S.2 | [docs/protocol.md](docs/protocol.md) — the frozen v1 contract | **revised by S.9** |
| S.3 | `firmware/lib/Protocol`, `firmware/lib/Safety` — no Arduino headers | yes; `arm` parsing drops in S.9 |
| S.4 | 72 host unit tests, `pio test -e native` | yes; arm tests drop in S.9 |
| S.5 | `tools/fake_rover.py` — desktop rover simulating the arena | yes; gains tags in S.10 |
| S.6 | Link state machine, readiness row, telemetry health | yes; readiness row changes in S.9 |
| S.7 | Latency HUD, RTT CSV, **target agreed: p95 ≤ 100 ms, ceiling 250 ms** | yes, with more headroom |
| S.8 | Touch sizing (15 mm min), reserved regions, high-contrast theme | yes; the reserved *arm* region becomes the analysis panel in S.11 |

Three findings raised during Phase S (F1–F4) are closed. See
[docs/protocol.md §8](docs/protocol.md) for the record.

---

### Step S.9 — Revise the protocol for the RFID mission
**Goal:** one authoritative contract again. Right now the frozen protocol describes an arm
that no longer exists and has no way to report a tag read.
**Owner:** 💻 · **Size:** M · **Depends on:** S.8 · **Risk:** R2

**Do:**
1. Remove the `arm` command from `docs/protocol.md`, `lib/Protocol`, the simulator, and
   `rover_link.gd`. Drop the arm tests.
2. Add the **tag-read frame**, rover → console: tag ID (the EPC, as a hex string), RSSI in
   dBm, and a read timestamp. Decide whether it is unsolicited on read or carried in
   telemetry, and write down why.
3. Add **reader status** to the telemetry frame — ready / scanning / fault — so Scene 1's
   "RFID reader reports ready" has something behind it.
4. Change the handshake `caps` list from `drive, mast, arm` to `drive, mast, rfid`.
5. Confirm whether this needs a protocol version bump. It probably does **not**: §2.2 already
   says an unknown `cmd` is ignored rather than fatal, so a console that stops sending `arm`
   and a rover that stops understanding it interoperate cleanly. Record the reasoning either
   way.

**Verify:** `pio test -e native` green; the simulator conformance script passes; the console
still links, drives, and reports latency against the simulator.

**Review gate:** the protocol diff and the passing test run. This is the piece hardest to
change later, so it is worth reading closely a second time. ⛔ Stop for approval.

---

### Step S.10 — Simulator: tagged rocks and distance-dependent RSSI
**Goal:** develop the whole scanning experience with no reader and no tags in existence.
**Owner:** 💻 · **Size:** M · **Depends on:** S.9 · **Risk:** R2

**Do:**
1. Place simulated tagged rocks in the simulator's 1.4 × 3.0 m arena, each with a tag ID and
   a position.
2. Model read behaviour as a function of antenna-to-tag distance and bearing: RSSI rising as
   the rover closes, a read range that is configurable, and **no read at all** beyond it.
3. Make the nasty cases switchable, because they are what the console has to handle
   gracefully: intermittent reads at the margin, a tag that never reads (the R7 failure), and
   two tags in range at once.
4. Show the rocks and the current read state in the ASCII arena view.

**Verify:** driving the simulated rover toward a rock makes the reported RSSI climb, and a
tag read fires inside the configured range and not outside it.

**Review gate:** live demo — I drive the fake rover up to a fake rock and it reads.
⛔ Stop for approval.

---

### Step S.11 — Console: signal meter, analysis panel, session log
**Goal:** Scenes 5 through 8 working against the simulator — the whole payoff of the revised
mission, before any hardware exists.
**Owner:** 💻 · **Size:** L · **Depends on:** S.10 · **Risk:** R2

**Do:**
1. **Signal meter** in the region S.8 reserved for the arm: a live RSSI bar with enough
   resolution to be useful as a "getting warmer" cue while closing in (proposal §5.1).
2. **Analysis panel**: on a successful read, show the elemental composition as a report card
   with a mineral-class label.
3. **Composition lookup table**, console-side and editable without reflashing — tag ID to
   composition profile, in a plain data file, per arena setup (proposal §5.4).
4. **Session log**: every rock identified so far, with tag ID and time; the loop repeats, so
   this is the running record Scene 8 appends to.
5. Handle the unhappy paths deliberately: an unknown tag ID, a tag read whose signal then
   drops, and the same rock re-read twice.

**Verify:** a full simulated Scene 3 → 8 loop, twice over, against two different fake rocks.

**Review gate:** live demo of the loop, plus the lookup table format.
⛔ Stop for approval. **— S.1 – S.11 complete. —**

---

## Driving focus (S.12 – S.13) — added 2026-08-13

With Phase 2 suspended, these two steps are **the only unblocked work in the whole plan**, and
they attack the half of the problem that no sensing decision can fix: a skid-steer rover on
sand inside a 1.4 m corridor is genuinely twitchy to drive, and Scene 5's "drives in small
steps" currently has nothing behind it. Both are console/simulator work with no hardware and
no spend, and both hold at the frozen protocol v1 — the fine-drive behaviour is a **scaling of
the `l`/`r` values the console already sends**, not a new message, so the firmware, the
simulator and `docs/protocol.md` need no change.

The order matters: the simulator is the only thing to tune drive feel against, and right now
it is too forgiving to tell you anything. Make it honest first, then tune against it.

---

### Step S.12 — Simulator: skid-steer motion model and slip
**Goal:** a fake rover that is as hard to drive as the real one will be, so S.13 is tuned
against something meaningful rather than against ideal kinematics.
**Owner:** 💻 · **Size:** M · **Depends on:** S.11 · **Risk:** R5

**Do:**
1. Replace the current motion model in [tools/fake_rover.py](tools/fake_rover.py) with a
   skid-steer model that includes the things that make skid steer awkward: **wheel slip during
   a turn-in-place**, a **minimum throttle before the rover moves at all** (matching the
   `Drive` deadband), and **momentum** — it does not stop the instant the throttle does.
2. Make the surface a parameter: hard floor vs. sand, since 1.5 expects sand to load the
   motors much more heavily.
3. Model the **chassis footprint and swept turning circle** against the 1.4 m width, so the
   simulator can report a wall strike. This is R5 rehearsed in software before 0.2 does it on
   paper and 1.5 does it for real.
4. Expose the model's constants as CLI switches, so the numbers measured at 1.3–1.5 can be fed
   back in later and the simulator re-tuned to match the real rover.

**Verify:** turn-in-place drifts rather than pivoting perfectly; a hard stop coasts; the ASCII
view shows the swept circle touching the walls at a plausible chassis size.

**Review gate:** the model's constants with the reasoning for each, and a driven demo showing
the arena now being genuinely awkward to manoeuvre in. **These are guesses until Phase 1
measures them — the point is a pessimistic simulator, not an accurate one.**
⛔ Stop for approval.

---

### Step S.13 — Console: fine-drive modes and nudge moves
**Goal:** make the rover controllable at the precision Scenes 4–5 need, against the S.12
simulator.
**Owner:** 💻 · **Size:** M · **Depends on:** S.12 · **Risk:** R5

**Do:**
1. **Speed modes** — at minimum a coarse "transit" and a fine "precision" scale, applied in
   [console/scripts/rover_link.gd](console/scripts/rover_link.gd) to the throttle before it is
   sent. Current mode must be visible on screen at a glance; an operator who thinks they are
   in fine mode and is not will hit the glass.
2. **Nudge moves** — a discrete "move a short step and stop" action, so lining up does not
   require timing a button press. Implemented as a timed run of repeated `drive` commands
   followed by `stop`, **not** as a new protocol verb.
3. **Throttle curve** — non-linear response so the low end of the range has usable resolution.
4. Respect the existing safety contract exactly: nudges still repeat every 150 ms while
   running, still stop on link loss, and the pad stays disabled unless the link is live and
   telemetry fresh. **A nudge in flight must be cancellable instantly** — a moving rover with
   an uninterruptible command is a worse failure than a twitchy one.
5. Touch-audit the new controls against the 15 mm minimum from S.8.

**Verify:** `pio test -e native` still green (nothing in the firmware should have moved);
drive the S.12 simulator up to a wall and stop short of it repeatably; the RTT figures from
S.7 are unchanged by the extra command traffic.

**Review gate:** live demo of a fine approach and a nudge, plus the touch audit output.
⛔ Stop for approval.

---

## Steered-chassis rework (S.14 – S.15) — forced by the 0.2 decision, 2026-08-13

Step 0.2 chose a chassis with **one drive motor and one steering motor**, not a differential
platform. Phase S was built end to end on skid steer, so this is the same kind of retarget
S.9–S.11 were for Revision 2: not new scope, but the software catching up with a decision
that has already been taken.

**What survives untouched:** the transport, the failsafe contract, the handshake, the latency
measurement and HUD, the touch audit, the composition table, the analysis panel and the
session log. All of it is steering-agnostic.

**What has to change:** the meaning of `drive`, and everything that implements it.

Still no hardware and no spend. Both steps are doable today.

---

### Step S.14 — Protocol v2 and the firmware drive path
**Goal:** one authoritative contract again, describing the vehicle that was actually chosen.
**Owner:** 💻 · **Size:** M · **Depends on:** 0.2 · **Risk:** **R5**

**Do:**
1. **`docs/protocol.md` first**, per the repo's own rule — edit the document, bump the
   version, then change the implementations. Redefine `drive` as throttle plus steering:
   `{"cmd":"drive","fwd":0.6,"steer":-1.0}`, both −1.0 … 1.0, `steer` negative for left.
   Retire `l` and `r`.
   **The steering on this chassis is three-position** (confirmed 2026-08-13): the motor
   drives to a mechanical end stop and a spring recentres it unpowered. Keep `steer` a float
   anyway and **document the thresholding** — the firmware rounds to left/centre/right — then
   advertise which kind of steering this build has in the handshake `caps`. That is exactly
   what S.2's capability discovery was built for, and it means a later proportional-steering
   rover needs no version bump.
2. **Bump to v2 and write down why it is breaking**, in the same message-by-message style
   §5 already uses. The reasoning that let S.9 stay at v1 does not apply here: an unknown
   *verb* is safely ignored, but `drive` with unknown *fields* parses as a valid
   zero-throttle command. **A v1 console would silently fail to drive a v2 rover** — that is
   exactly the failure the handshake exists to catch, so it must be caught.
3. `lib/Protocol` — parse the new fields, reject the old ones; update `test_protocol`.
4. `lib/Drive` — one throttle channel and one steering channel instead of two sides, pins
   through the constructor as now. **Drop `kPwmFrequencyHz` from 20 kHz to ~1–2 kHz**: an
   L293D is a slow Darlington part that cannot switch cleanly at 20 kHz, and the low-speed
   torque that costs is exactly what the S.13 throttle floor depends on. Record that the
   motors will now whine audibly — a deliberate trade.
5. `config.h` — real pin names for the L293D, and LEDC channel assignment (0 = drive,
   1 = steer, mast servos still need their own).
6. Document **what `steer` means when the rover is stationary** — the wheels turn, but the
   rover does not move until throttle is applied. The pad has to be honest about that.
7. Record the one piece of **good news in the failsafe**: three-position steering springs
   back to centre when unpowered, so cutting the motors centres the steering for free. A
   rover that fails safe mid-turn coasts straight rather than continuing to arc into the
   glass. Write it into the contract as a property, and confirm it on the bench at 1.3 —
   if the spring is weak or absent, that assumption has to come back out.

**Verify:** `pio test -e native` green; the contract and the code agree field for field.

**Review gate:** the protocol diff, the version-bump reasoning, and the passing test run.
**This is the hardest thing in the repo to change later — worth reading twice.**
⛔ Stop for approval.

---

### Step S.15 — Simulator and console onto the steered model
**Goal:** the simulator stops lying about what the rover can do, and the pad stops sending
commands the rover cannot honour.
**Owner:** 💻 · **Size:** M · **Depends on:** S.14 · **Risk:** R5

**Do:**
1. `tools/fake_rover.py` — replace the S.12 skid-steer model with a **steered (bicycle)
   model**: a wheelbase, a steering lock, and therefore a **minimum turning circle**. The
   headline behaviour is that **turn-in-place is gone** — steering at zero throttle does
   nothing but move the wheels. Keep everything S.12 established that still applies:
   momentum, breakaway, drift, surface presets, corner-based wall collision.
2. Show the **minimum turning circle against the drivable width** in the ASCII view — this is
   R5's new form, and seeing whether a U-turn fits is the whole question.
3. `rover_link.gd` — `hold_drive(left, right)` becomes `hold_drive(throttle, steer)`. Bump
   `PROTOCOL_VERSION` to 2. The S.13 speed modes, throttle shaping and nudges carry over
   unchanged in spirit; **re-check the throttle floor**, since it was measured against a
   skid-steer model and an L293D's voltage drop makes the real floor higher, not lower.
4. `console.gd` and the scene — LEFT and RIGHT now **steer while driving** rather than pivot.
   Decide what they should do when the rover is stopped, and make the pad honest about it.
   Re-run the touch audit.
5. Keep the simulator a genuine second implementation of the v2 contract, per `CLAUDE.md` —
   strict parsing and failsafe mirrored, so a divergence still means one of them has a bug.

**Verify:** `pio test -e native` green; the console links, drives and steers against the
simulator; a U-turn attempt in the arena either fits or visibly does not.

**Review gate:** live demo of driving and steering, plus the measured minimum turning circle
in simulation — **the number step 1.5 will check with a tape measure.**
⛔ Stop for approval. **— End of Phase S —**

---

# Phase 0 — Approval & procurement

> **Proposal exit criteria:** *budget and part list approved.*

Mostly your decisions; I can draft and calculate. Nothing here is ordered until you approve.

### Step 0.1 — Fix the sensing spec ⏸ ON HOLD
**Goal:** the decisions that size the whole sensing chain, settled before anything is quoted.
**Owner:** 📋 · **Size:** M · **Depends on:** **2.0** · **Risk:** R2

> ⏸ **Held from 2026-08-13.** This step assumes UHF RFID, which is no longer decided. It
> cannot be written until **step 2.0** picks a method — and if 2.0 picks something other than
> RFID, most of what follows is replaced rather than edited. Kept below as the worked-out
> version for the RFID candidate.

*Replaces Revision 1's sample mass/size spec, which sized the manipulator. With no arm, rock
mass no longer matters to anything — what matters now is whether a tag can be read.*

**Do:** decide, and treat as fixed:
1. **Required read range** — the standoff at which the operator should get a solid read.
   This is the number everything else is derived from. Too short and Scene 5 becomes
   millimetre alignment again, which the revision exists to avoid; too long and the rover
   reads a rock it is not pointing at.
2. **Reader module and frequency plan** — UHF 860–960 MHz, with the regional band confirmed
   for where this will actually run.
3. **Tag type and form factor** — must survive being sealed inside a rock (R7) and be small
   enough to embed in the rock sizes the arena will use.
4. **Rock envelope** — no longer a mass question, but rocks still need to be large enough to
   take an embedded tag and clearly visible on the single camera feed from across the arena.
5. **How many tags** — the batch size for the arena, plus spares, since a tag that fails
   after embedding cannot be recovered.

**Review gate:** written spec, one short table, agreed by you before 0.4 quotes anything.
⛔ Stop for approval.

---

### Step 0.2 — Chassis footprint vs. the 1.4 m turn, on paper
**Goal:** catch R5 before anything is bought.
**Owner:** 📋 · **Size:** M · **Risk:** R5

**Do:**
1. For each candidate chassis, compute the swept circle of a skid-steer turn-in-place
   (diagonal across the wheelbase), not just the static footprint.
2. Compare against 1.4 m **minus** the terrain dressing depth at the walls — the usable width
   is narrower than the glass width.
3. Check what the **front mounting point** projects, using the worst case across the 2.0
   candidates rather than assuming one: an RFID antenna is a flat panel a few centimetres
   proud, a short IR/colour probe arm is a **swept envelope** that has to clear the glass at
   full extension. **Reserve the larger.** Chassis choice is the one Phase 0 decision that
   2.0 can invalidate, so carry the margin rather than re-deciding later.

**Verify:** a dimensioned sketch with the numbers, per candidate, with the front-mount
allowance called out separately so it can be re-checked once 2.0 lands.

**Delivered:** [docs/chassis-envelope.md](docs/chassis-envelope.md) — four candidates, the
swept-circle arithmetic, a to-scale corridor drawing, and a keep-out sensitivity table.

### ✅ 0.2 decided, 2026-08-13 — all four candidates rejected

**Chosen:** an AliExpress kit (item `1005008274445888`) with **two DC motors — one drive, one
steering** — through an **L293D**, PWM speed control. Plus a scope ruling: **free drive**.
Rocks are scattered at random, the operator drives to them by hand, and **precision
positioning is not a design goal for this build.**

**This changes the drive architecture, not just the part.** The repo is built end to end for
skid steer, and a steered chassis cannot pivot in place at all. Consequences, worked through
in [§8 of the envelope doc](docs/chassis-envelope.md):

- **The protocol no longer describes the vehicle.** `{"cmd":"drive","l":..,"r":..}` is
  per-side throttle; this chassis has no sides. Needs **v2** — and it breaks *silently*,
  since v1 makes `l`/`r` optional, so a v1 console would look like it was driving a rover
  that never moved. → **new steps S.14 and S.15.**
- **R5 is reopened and inverted.** It was settled for skid steer; a steered chassis has a
  *minimum turning circle* that may exceed the arena width. Measured at 1.5, not calculated.
- **`lib/Drive`'s 20 kHz PWM is wrong for an L293D** — a slow Darlington part that wants
  ≤ ~2 kHz. → folded into S.14.
- **L293D is 600 mA/channel and drops ~1.8–2 V.** → stall-current check added to 1.7, rail
  sizing note added to 0.3.

**Not re-run:** the swept-circle arithmetic. It is the right method for the wrong vehicle, and
the free-drive ruling means the millimetre-alignment case it served no longer applies.

---

### Step 0.3 — Power budget estimate and rail plan
**Goal:** an estimate to buy against, replaced by measurement in 1.7.
**Owner:** 📋 · **Size:** M · **Risk:** R4

**Do:**
1. Tabulate worst-case current per subsystem from datasheets. *Much smaller servo load than
   Revision 1 — two mast servos instead of an arm's four or five — but note that servo
   **stall** current still sizes that rail.* Two motors now, not four (0.2).
2. **Size the motor rail with the L293D's voltage drop included** — roughly 1.8–2 V across
   the bridge, against ~0.5 V for a MOSFET part. A 6 V motor needs about 8 V at the driver
   input to see its rated voltage, so the pack and regulator have to carry that overhead or
   the rover will be slower and weaker than the motor spec suggests.
3. Carry a **sensing allowance** rather than a specific part, since 2.0 has not run. Size it
   against the hungriest candidate — a UHF reader draws a meaningful, *bursty* current while
   transmitting; a probe arm's servos are dominated by **stall**, not by their running draw.
   Note which candidate the allowance came from so it can be replaced with a real figure.
4. Plan **separate rails**: motors at pack voltage, servos on their own regulator, ESP32,
   camera and the sensing payload on a clean 5 V/3.3 V rail, all sharing a common ground.
5. Size the LiPo for a **full multi-rock session** plus margin — Revision 2's loop repeats, so
   the runtime demand is higher than a single collect-and-return (revised R4).

**Delivered:** [docs/power-budget.md](docs/power-budget.md) — the load table, the four-rail
plan, session energy arithmetic, a candidate-independent sensing allowance, and two findings:

- **F5 🔴 `BATTERY_DIVIDER_RATIO = 2.0` would destroy the ADC pin** at step 1.1 — 6.3 V onto a
  3.6 V absolute maximum. Recommends 100k/27k, ratio 4.70.
- **F6 🔴 the steering channel is the binding constraint on the L293D, thermally.** A held turn
  is a *continuous* stall; ~1.8 W in a DIP-16 with θ_JA ≈ 67 °C/W puts the junction near its
  limit, and the realistic failure is thermal shutdown cutting out mid-turn.

**Review gate:** the rail diagram and the current table, plus the four decisions in its §7 —
pack choice, session length, whether the motor rail is regulated, and whether to buy a
TB6612FNG alongside the L293D. ⛔ Stop for approval.

---

### Step 0.4 — Bill of materials — drive, vision and console
**Goal:** the costed BOM the proposal defers until this point, **for everything the sensing
decision does not touch** — which is most of the rover.
**Owner:** 📋 · **Size:** L · **Depends on:** 0.2, 0.3

*Split on 2026-08-13. Sensing parts move to 0.6 so that ordering the drive platform is not
held hostage to a decision that has nothing to do with it.*

**Do:** every part with quantity, unit cost, supplier, and lead time — chassis and motors,
H-bridge, ESP32, **one** camera, **one** monitor, mast pan/tilt servos, touchscreen, power
system, arena materials, plus consumables and **spares for the parts most likely to fail**
(motor driver, servos). Leave the sensing payload as a **costed placeholder line** carrying
0.3's allowance, so the total is not misleadingly low. Flag long-lead items.

**Review gate:** the BOM spreadsheet, with the sensing placeholder shown as a placeholder.
**This satisfies the proposal's Phase 0 gate for everything except sensing — budget and part
list approved for the drive platform.** ⛔ Stop for approval.

---

### Step 0.5 — Arena and Wi-Fi infrastructure plan
**Goal:** decide the network setup before it is a problem.
**Owner:** 📋 · **Size:** M · **Risk:** R6

*The UHF band check and the tag embedding process moved to 0.6 on 2026-08-13 — both are
RFID-specific and only exist if 2.0 picks RFID.*

**Do:**
1. Decide the network topology: rover as access point, or a dedicated router with everything
   as clients. *Easier than Revision 1 — one camera stream instead of two.*
2. Pick 2.4 GHz channels and check what else is using them in the room.
3. Plan AP placement relative to the box, including the rover at the **far** end of the 3 m
   run, which is the worst case.
4. Plan the terrain dressing itself — sand depth and rock sizes — since 1.2 re-surveys RSSI
   with the dressing in place and 1.5 drives on it. This part is method-independent; only the
   *tagging* of the rocks waits on 2.0.

**Review gate:** the network diagram and the 2.4 GHz channel plan. ⛔ Stop for approval.

---

### Step 0.6 — Bill of materials — sensing ⏸ ON HOLD
**Goal:** cost the sensing payload once there is a decision to cost.
**Owner:** 📋 · **Size:** M · **Depends on:** **2.0**, 0.1 · **Risk:** R2

> ⏸ **Held behind 2.0.** Nothing here can be quoted until a method exists.

**Do:** the sensing payload with quantity, unit cost, supplier and lead time, plus spares —
and whatever infrastructure that method drags along with it. For the RFID candidate that
means the reader, antenna, tag batch, the **UHF band and legal power check** for the region
this will run in (860–960 MHz is regionally allocated), and the **tag embedding process**
(adhesive or epoxy type, cure temperature, tag placement in the rock) that R7 turns on. For a
probe-arm candidate it means the servos, the sensor, and a rock-finishing process instead.
Replace 0.4's placeholder line with the real figure and re-check the total against 0.3.

**Review gate:** the sensing BOM and the revised total. ⛔ Stop for approval.

---

# Phase 1 — Drive platform bring-up

> **Proposal exit criteria:** *rover drives forward/back/left/right reliably inside the
> 1.4 m arena width; battery draw measured.*

**Blocked until parts arrive**, and as of 2026-08-13 this is **the primary line of work** — it
is no longer a stepping stone toward Phase 2. Unchanged by Revision 2 and unaffected by the
reopened sensing decision: the drive platform is identical under every candidate, which is
exactly why it is safe to build first. Bring the board up before the chassis exists: a fault
found on the bench is far cheaper than one found inside an assembled rover.

### Step 1.1 — ESP32 board sanity
**Goal:** prove the toolchain reaches real silicon.
**Owner:** 🔧💻 · **Size:** S · **Blocked on:** a board existing

**Do:** identify the COM port, flash a blink-plus-serial build, confirm the upload speed is
stable (drop from 921600 if uploads are flaky), and note the exact board variant in
`platformio.ini` if it isn't `esp32dev`.

**Verify:** LED blinks, serial monitor readable at 115200, repeated flashes succeed.

**Review gate:** serial output and the confirmed board/port settings. ⛔ Stop for approval.

#### 💻 Prepared 2026-08-13 — the software half is done and builds

The code half of 1.1 needs no hardware and is finished; what remains is genuinely a bench
job. [firmware/src/bringup.cpp](firmware/src/bringup.cpp) and `[env:bringup]` in
`platformio.ini` deliver blink + serial with **no Wi-Fi and no `secrets.h`** — deliberately,
so a wrong password can never look like a dead board. Builds clean (273 KB, 20.8% flash).

The bench session, in order:

```powershell
cd firmware
python -m platformio device list                       # 1. find the COM port
python -m platformio run -e bringup -t upload -t monitor   # 2. flash and watch
```

1. **Find the port.** The board appears as a CP210x, CH340 or FTDI bridge. If nothing new
   appears when you plug it in, it is the cable — USB cables that carry power but not data
   are extremely common and are the single most likely cause of a board that seems dead.
2. **Flash at 460 800**, which is what `[env:bringup]` sets rather than `esp32dev`'s 921 600.
   A failed upload at the fast speed looks exactly like a dead board, and on a first flash
   you cannot yet tell those apart. Once the board is proven, `esp32dev` uses the fast rate.
3. **Read the identity block.** It prints chip model, revision, cores, CPU and flash, PSRAM,
   the eFuse MAC, and the reset reason — read off the chip, not assumed from
   `platformio.ini`. **This is what closes "note the exact board variant"**: if it does not
   say what `board = esp32dev` implies, change the board line and rebuild.
4. **Type into the monitor.** Characters are echoed back with their hex code. This proves the
   *host → board* direction, which uploading depends on and which a heartbeat alone does not
   demonstrate. A board that prints happily but never receives will fail to flash next time.
5. **Reset it two or three times** and re-flash twice, confirming the heartbeat counter and
   uptime restart cleanly each time and that uploads are repeatable.

Capture the identity block and the heartbeat for the review gate.

#### ✅ 1.1 passed, 2026-08-13 — first hardware in the project

| | |
|---|---|
| Port | **COM3**, Silicon Labs CP210x (VID:PID `10C4:EA60`) |
| Chip | **ESP32-D0WD-V3 rev 3**, 2 cores, 240 MHz |
| Flash | 4 MB @ 40 MHz · **no PSRAM** |
| eFuse MAC | `5046FDFFC9EC` |
| SDK | `v4.4.7-dirty` (arduino-esp32 2.0.x, as pinned) |
| Reset reason | `power-on` — **not** brownout |

**`board = esp32dev` is correct and stays.** A D0WD-V3 with 4 MB and no PSRAM is exactly what
that board definition describes, so `platformio.ini` needs no change — which is the question
this step existed to answer rather than assume.

All four gate items met:

1. **LED blinks** on GPIO2 — confirmed by eye.
2. **Serial readable at 115200** — identity block captured, heartbeat at exact 2000 ms
   intervals, heap flat at 351384 bytes across the run (no leak in the loop).
3. **Repeated flashes succeed** — 3/3 at 460800, then **2/2 at 921600**, every one ending
   `Hash of data verified`.
4. **Board variant confirmed** — read off the chip, per the table above.

**Upload speed settled:** 460800 wrote 273392 bytes in 3.7 s (587 kbit/s); 921600 did it in
2.2 s (1014 kbit/s) with no retries. **`esp32dev`'s 921600 is safe on this board**, which
matters because 1.2 onward uses it. `[env:bringup]` deliberately stays at 460800 — not because
the board needs it, but so a first flash onto a *future* board still fails safely.

Also proven, though not asked for by the gate: the **host → board direction**, by echoing
`M z 1` plus a non-printable `0x07` and getting `0x4D 'M' / 0x7A 'z' / 0x31 '1' / 0x07` back.
Uploading depends on that direction, so it is worth having evidence of rather than inferring
it from the fact that flashing worked.

⛔ **Gate cleared. 1.2 needs `secrets.h` filled in with real Wi-Fi credentials**, and is where
the RSSI-through-glass survey settles risk R6.

**Watch for `reset reason  BROWNOUT`.** It should say `power-on` here. If a brownout ever
appears at 1.3 or 1.7, that is [docs/power-budget.md](docs/power-budget.md)'s F6 arriving in
person — a rail sagging under a motor stall, not a firmware crash. Printing it from step 1.1
onward means the evidence is already on screen when it happens.

⚠️ **Do not wire the battery divider for this step.** 1.1 needs USB power only. The divider
is F5's hazard — `BATTERY_DIVIDER_RATIO = 2.0` against a 3S pack puts 6.3 V on a 3.6 V pin —
and it is not needed until power monitoring goes in. Settle F5 at 0.3 before any pack is
connected to GPIO34.

---

### Step 1.2 — Wi-Fi join, WebSocket echo, and the RSSI survey
**Goal:** settle R6 with measurements instead of assumptions — **before** any motor is wired.
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 1.1, 0.5 · **Risk:** R6

**Do:**
1. Flash the real firmware with actual credentials in `secrets.h`; confirm the console
   connects to the ESP32's own IP.
2. RSSI survey with the board **inside the closed box**: near end, mid, far end, corners, and
   with the lid open vs. closed for comparison.
3. Repeat with the terrain dressing (sand, rocks) in place — mass on the floor changes things.
4. Record every reading in a table; the far corner with the lid closed is the number that
   matters.

**Verify:** link holds at every position for a 10-minute soak with no dropout.

**Review gate:** the RSSI table and the soak result. **If the far corner is marginal, we stop
and revisit AP placement (0.5) rather than continuing into 1.3.** ⛔ Stop for approval.

#### ⏳ 1.2 started 2026-08-13 — blocked on credentials and on the arena existing

**Attempted:** flashed `esp32dev` to the real board. It does not associate —
`Reason: 201 - NO_AP_FOUND`, repeating. The SSID in `secrets.h` is still a placeholder, so
this is expected rather than a fault.

**Built to unblock it:** [firmware/src/wifi_scan.cpp](firmware/src/wifi_scan.cpp) and
`[env:wifiscan]` — a continuous 2.4 GHz survey that needs **no credentials**, so it both names
the networks actually on the air and serves as the instrument for the survey itself. Beacon
RSSI from a scan is a valid survey measurement and needs no association, so **steps 2 and 3 of
this step can be run before the arena network exists.**

**Desk baseline, 2026-08-13** — 14 networks visible, strongest first:

| RSSI | Ch | SSID | |
|---|---|---|---|
| **−57** | **12** | **`Dean_WiFi`** | good — the likely target |
| −69 | 6 | `Teemo_2.4G` | workable |
| −74 | 5 | `NONGHOME_2.4G` | workable |
| −88 and below | various | 9 others | unusable |

Channel congestion, for 0.5: **ch 11 is the least contended** (1 on channel, 4 within ±2)
against ch 1 (2 / 4) and ch 6 (2 / 6).

### 🟠 F7 — the rover hangs forever on a Wi-Fi failure, and looks identical to a dead board

`setup()` in [firmware/src/main.cpp](firmware/src/main.cpp) joins Wi-Fi with

```cpp
while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print('.'); }
```

No timeout, no retry limit, no diagnosis. A wrong password, a downed AP, or a rover parked in a
Wi-Fi hole means **the board never leaves `setup()`**: the WebSocket server never starts,
telemetry never flows, and the console sits on CONNECTING indefinitely. The `NO_AP_FOUND`
reason only appeared above because `CORE_DEBUG_LEVEL=3` is on — at a normal log level there is
nothing but dots.

This is precisely the confusion step 1.1's Wi-Fi-free design was built to avoid, reintroduced
one step later, and it interacts directly with **R6**: if the far corner is marginal, the rover
does not come up degraded, it does not come up at all.

Worth fixing before the arena exists, because in the arena the symptom is a rover that is
simply dead with no way to ask it why. Options, cheapest first: bound the join with a timeout
and carry on into `loop()` reporting the failure over serial; retry in the background so a
returning AP recovers the rover without a power cycle; or fall back to SoftAP so the console
can always reach it. **Not fixed — it changes boot behaviour of the safety-critical firmware
and deserves its own gate.**

### Still blocked

| Blocker | Needs |
|---|---|
| Association, and everything downstream of it | **the real SSID and password typed into `firmware/include/secrets.h`** — that file is gitignored; enter them locally rather than sending them to anyone |
| Survey inside the closed box | the 1.4 × 3.0 m glass arena, which is not built |
| Survey with dressing in place | sand and rocks, not procured |
| 10-minute soak at every position | both of the above |

⚠️ **`Dean_WiFi` is on channel 12.** Channels 12–13 are restricted in several regulatory
domains, and an ESP32 whose country policy resolves to a 1–11 domain can see such an AP in a
scan yet fail to associate with it. If association still fails *after* the credentials are
correct, that is the first suspect. Moving the AP to **channel 11** removes the risk and is the
least-contended non-overlapping channel here anyway — which makes it a 0.5 recommendation on
two independent grounds.

---

### Step 1.3 — Bench-spin one motor
**Goal:** first motion, with the chassis nowhere near it.
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 1.2

**Do:**
1. Wire one motor through the H-bridge on the bench, motor free-spinning, wheels off.
2. Update `config.h` to the **real** pins — the current values are placeholders.
3. Drive it from the console at 25 / 50 / 100 % and confirm the deadband constant in `Drive`
   matches where this motor actually stops buzzing and starts turning.
4. Confirm the standby pin actually cuts drive.

**Verify:** speed tracks the command; direction reverses correctly; STOP is immediate.

**Review gate:** video of the bench spin plus the tuned deadband figure. ⛔ Stop for approval.

#### ⏳ 1.3 in progress, 2026-08-13 — first motion achieved

**The driver changed.** The L293D chosen at 0.2 left only ~2.9 V at the motor from the 4.8 V
pack after its ~2 V bridge drop, and nothing moved usefully. Replaced with a **DRV8833**
(~0.2 V drop, 1.2 A/channel, 3.3 V logic, no separate logic supply). Same pack now delivers
~4.6 V to the motor. Module silkscreen:
`VM · NC · GND · AO1 · AO2 · BO2 · BO1 · BIN1 · BIN2 · AIN1 · AIN2 · STBY`.

| Check | Result |
|---|---|
| Drive motor forward / reverse | ✅ both directions, PWM speed control |
| Steering left / right | ✅ **and `←` steers left — no sign flip needed** |
| Steering springs back to centre on release | ✅ **§6.1's safety property confirmed on hardware** |
| Speed tracks command at 25 / 50 / 100 % | ✅ |
| STOP immediate | ✅ now a hardware standby, not just zero duty |
| `config.h` at real pins | ✅ |
| **Deadband figure** | ⏳ **outstanding — the one thing left** |

**Two findings from the bench:**

- **The steering must not be PWM'd.** `kSteerHoldDuty = 0.7` was a guess written to keep a
  continuous stall inside the L293D's 600 mA channel. It survived the driver change when it
  should not have, and at 70% duty the steering motor could not shift the axle against its
  return spring — the drive motor worked and the steering did not. `lib/Drive` now gives the
  two motors *different types*: `PwmMotor` for the drive bridge, `SwitchedMotor` for steering.
  Removing the duty parameter entirely means it cannot creep back.
- **Two L293D-era decisions are refunded.** PWM returns to 20 kHz from the 1.5 kHz forced at
  S.14, so the deliberate whine is gone and low-speed torque improves. And the DRV8833's
  standby pin makes `stop()` a hardware disable of both bridges — protocol.md §3.3 updated,
  since it claimed the driver had none.

**Phase 0 consequences to fold in when 0.3 is decided:** finding **F6** (L293D thermal limit
on held steering) is largely dissolved by the part change; but `VM` maxes at **10.8 V**, so the
3S pack 0.3 recommended would destroy this board. The reason for that recommendation also
evaporates — the 8 V buck existed to overcome a 2 V drop that no longer exists — so **2S direct
is now the strong candidate.**

---

### Step 1.4 — Both motors, direction and polarity calibration
**Goal:** left is left, and forward is forward.
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 1.3

*Rewritten at the 0.2 decision: this chassis has one drive motor and one steering motor, not
four motors on two sides.*

**Do:** wire both motors through the L293D; verify the drive motor's polarity so a positive
`fwd` drives forward, and the steering motor's so a negative `steer` goes **left**; add a
per-motor inversion flag in `config.h` rather than swapping physical wires; check both LEDC
channels are independent and that neither collides with a channel reserved for the mast servos
later. Confirm the §6.1 spring-centre property survives real wiring — cut power at full lock
and watch the axle return.

**Verify:** `{"cmd":"drive","fwd":1,"steer":0}` drives forward; `steer:-1` swings the axle
fully left and `steer:0` lets it spring back to centre.

**Review gate:** video of the drive wheel and the steering axle for each of the five drive-pad
commands. ⛔ Stop for approval.

---

### Step 1.5 — Chassis assembly and first driven run
**Goal:** a rover that moves across the floor under console control.
**Owner:** 🔧 · **Size:** L · **Depends on:** 1.4, 0.2 · **Risk:** R5

**Do:** assemble the chassis with battery and electronics mounted; keep wiring serviceable and
clear of the wheels; drive it first on a hard floor, then **on the actual sand terrain**, which
loads the motors much more heavily; measure the real turning circle and compare it to the 0.2
calculation.

Also measure, because S.13/S.15 depend on all of these and all are currently guesses:
- **The breakaway throttle on sand** — the lowest throttle that reliably starts the rover
  from a standstill. This is *not* the `Drive` deadband tuned at 1.3, which is the H-bridge
  coasting; this is the ground. It sets `MIN_EFFECTIVE_THROTTLE` in `rover_link.gd`, which
  S.15 raised from 0.32 to **0.45** on the L293D voltage-drop argument alone — that number
  has never met a motor.
- **The coast distance** from full throttle, and how far one 350 ms nudge actually travels.
  S.15 predicts **~2 cm per nudge on sand**, and ~0.9° of heading change for an ARC nudge; if
  either is far off, `NUDGE_SEC` is retuned here, not guessed at again.
- **The minimum turning circle**, at full lock, with a tape measure. S.15 predicts **0.95 m
  against a 1.30 m drivable width** from a 0.155 m wheelbase and 25° of lock — but that
  verdict is knife-edge in the lock angle: **15° of lock instead of 25° needs 1.41 m and does
  not fit.** Measure the real lock angle before trusting the sim's answer.

Feed all of it back into the simulator through its switches (`--breakaway`, `--wheelbase`,
`--steer-lock`, `--drift`, `--coast-time`) so the two stay in step.

**Verify:** drives the length of the arena on sand without stalling, and either completes a
U-turn in one move or demonstrably needs a three-point turn.

**Review gate:** measured turning circle vs. predicted, and a driven run on sand. **A miss
here is an R5 hit — we stop and reassess the chassis.** ⛔ Stop for approval.

---

### Step 1.6 — Failsafe proving
**Goal:** prove the rover stops itself, because it will eventually need to.
**Owner:** 🔧💻 · **Size:** S · **Depends on:** 1.5 · **Risk:** R1

**Do:** with the rover driving at full throttle, in turn: kill the console app; power off the
AP; walk the rover out of range; close the socket cleanly. Time how far it travels after each.

**Verify:** it stops within the command timeout in every case, and needs a fresh command to
re-arm — it must not lurch back into motion on reconnect. The S.4 tests already cover this
logic on the host; this is the same contract against real motors.

**Review gate:** coast distance for each failure mode. **If it doesn't stop, this is the one
gate we do not negotiate past.** ⛔ Stop for approval.

---

### Step 1.7 — Current draw per subsystem
**Goal:** replace the 0.3 estimate with measurements, closing R4.
**Owner:** 🔧 · **Size:** M · **Depends on:** 1.5 · **Risk:** R4

**Do:** measure idle, driving on sand, steering at full lock against the ground (the worst
drive case for a steered chassis), and stall; log pack voltage over a sustained run to get
real runtime; calibrate the `BATTERY_DIVIDER_RATIO` constant against a meter so the telemetry
reading is true.

**Added at the 0.2 decision:** compare measured stall current against the **L293D's 600 mA
per-channel continuous rating**. A drive motor pushing through sand can exceed it, and a
steering motor held against its end stop certainly will. If either does, the driver is
undersized and a TB6612FNG-class part is the fix — better found here than after the arena is
dressed.

**Verify:** measured runtime comfortably exceeds a **full multi-rock session**, with margin
for the mast servos and for 0.3's sensing allowance, neither of which is fitted yet.

**Review gate:** the measurement table and the calibrated divider constant. **This satisfies
"battery draw measured" in the proposal's Phase 1 gate.** ⛔ Stop for approval.

---

### Step 1.8 — Control latency, idle link
**Goal:** the baseline R1 number, before video exists to compete with it.
**Owner:** 🔧💻 · **Size:** S · **Depends on:** 1.5, S.7

**Do:** run the S.7 latency HUD against the real rover in the box; capture several minutes of
RTT at the near and far ends; compare against the target agreed in S.7 — **p95 ≤ 100 ms,
ceiling 250 ms** ([docs/protocol.md §4.4](docs/protocol.md)) — and against the simulator
baseline of ~7 ms.

**Review gate:** RTT distribution, real hardware vs. simulator. **This is the number Step 3.3
is measured against — keep the CSV.** ⛔ Stop for approval.
**— Phase 1 gate: all of 1.5, 1.6, 1.7 passed. —**

---

# Phase 2 — Rock identification ⏸ ON HOLD

> **Proposal exit criteria:** *reader gets a clean, repeatable read at a usable approach
> distance, across multiple rock/tag orientations.* — **written for the RFID candidate; it
> gets restated in the method's own terms once 2.0 lands.**

**Suspended 2026-08-13.** This phase was retargeted from manipulator integration to RFID
sensing in Revision 2; it is now suspended again because the sensing method itself is
reopened. Steps 2.1–2.7 below are **retained unchanged as the fully worked-out RFID
candidate** — they are not deleted, because if 2.0 picks RFID they are ready to run as
written, and because they are the reference for how much work a candidate actually costs.

Nothing downstream waits on this: Phase 1 and Phase 3 proceed without it, and Phase 4 is where
it rejoins.

---

### Step 2.0 — Decide the identification method 📋 **GATE**
**Goal:** one decision, which unsuspends this phase, 0.1, 0.6, and Phase 4.
**Owner:** 📋 · **Size:** M · **Depends on:** nothing — **can be taken at any time** · **Risk:** **R2**

This step exists because the question "how does the rover know *which* rock it is looking at"
was answered by Revision 2 and then reopened. It is a decision, not an investigation, and it
does not need hardware — but the better it is informed, the less of Phase 2 gets thrown away.

**The candidates on the table.** These are starting points, not a closed list:

| | Method | Standoff needed | Gives bearing? | Main cost |
|---|---|---|---|---|
| **A** | **UHF RFID** — passive tag sealed in each rock, front antenna | tens of cm to ~1–2 m, tunable by reader power | no — RSSI only | reader + antenna + tag batch; R7 embedding risk |
| **B** | **Probe arm + IR/colour sensor**, rocks finished in distinct colours | **1–3 cm** — reflective colour sensors are near-contact | no | reinstates the arm Revision 2 removed |
| **C** | **Colour/shape recognition on the mast camera**, no arm | across the arena | **yes** | CV outside Godot; worsens R8 |
| **D** | **Visual fiducials** (ArUco/AprilTag) on each rock, mast camera | ~1–2 m for a 5–10 cm marker | **yes — ID, bearing and distance** | CV outside Godot; markers are visible, so rocks look less natural; worsens R8 |

**Do:** decide against these criteria, and write down the reasoning — this is the decision the
rest of the plan hangs off, so the record matters more than the speed:

1. **Does it solve the alignment complaint that reopened this?** The concern was having to
   manoeuvre the rover into precise alignment with a rock. Note that **B is the strictest of
   the four on this axis, not the loosest** — a reflective colour sensor wants to be
   centimetres from the surface, which is tighter than A, and the arm has to be positioned
   there. B trades a *rover* alignment problem for an *arm* alignment problem; that may still
   be a good trade, because the arm can be moved with the rover parked, but it should be a
   deliberate one rather than a surprise at step 2.4.
2. **What does it do to the proposal?** A is what
   [plan/storyboard-rev2.md](plan/storyboard-rev2.md) currently specifies. **B reinstates the
   robotic arm, which is the single change Revision 2 exists to make** — it reverses §2's
   "fewer moving parts, fewer failure points" rationale, brings back the manipulator BOM, and
   puts the arm's reach envelope back into 0.2's clearance check. That is a legitimate choice,
   but it is a Revision 3 of the proposal, not a plan edit.
3. **What does it do to R8?** C and D put identification on the *same single camera* that
   already does driving and survey. One failure then costs the rover its eyes **and** its
   instrument.
4. **What does it demonstrate?** §9 of the proposal sells Revision 2 as trading a mechanical
   challenge for an **RF sensing** one. A keeps that. B makes it mechanical again. C and D
   make it computer vision. All defensible — but §9 gets rewritten to match.
5. **How much of Phase S survives?** All of it, under every candidate: the composition table,
   session log and analysis panel key off an ID string and a signal number. Only the detection
   source changes, and the `tag` frame in [docs/protocol.md](docs/protocol.md) §4.4 is already
   generic enough to carry a colour code or a marker ID without a version bump.

**Carried forward from the 2026-08-13 discussion, so it is not lost** — two findings against
candidate A that were raised but never recorded:

- **Over-range is the likelier failure than under-range.** In a 1.4 × 3.0 m box, a UHF reader
  at full power may read every rock in the arena from anywhere in it, making "which rock am I
  pointing at" meaningless. Reader power is a **tunable**, so this is manageable — but it
  means the read range is a design choice to be set at 2.3, not a property to be discovered.
- **RSSI may not be monotonic.** Multipath inside a small glass-and-metal-framed enclosure can
  produce nulls, so driving closer can make the signal meter *drop*. S.10's simulator models a
  clean fourth-power falloff, which is optimistic. If A is chosen, the "getting warmer" cue in
  Scene 5 needs testing against this specifically — it is a bigger threat to the mission loop
  than tag orientation is.

A third point that applies to A regardless: a **circularly polarised** antenna largely removes
the tag-orientation sensitivity that R2 and step 2.4 are built around, at roughly 3 dB of
range. If A wins, that choice belongs in 0.1.

**Review gate:** the chosen method with the reasoning written down, and — if it is anything
other than A — a note of which of 2.1–2.7 are replaced and what replaces them. **If the choice
changes the proposal (B, C or D all do), Revision 3 of the storyboard follows before Phase 2
restarts.** ⛔ Stop for approval.

---

## The RFID candidate, as worked out under Revision 2 (steps 2.1 – 2.7) ⏸

*Retained verbatim. These run as written **if and only if** 2.0 picks candidate A.*

The ordering below is deliberate: **prove the tag survives embedding, and prove the read works
on a bench, before either is attached to a rover.** Debugging a marginal read on a moving
platform when you do not yet know whether the tag is even alive is the trap this sequence
avoids. *(That principle generalises — whatever 2.0 picks, characterise the sensor on a bench
before putting it on something that moves.)*

### Step 2.1 — Reader bring-up on the bench ⏸
**Goal:** prove the reader talks, off the rover entirely.
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 0.6

**Do:** power the reader on the bench, talk to it over UART from a PC first and then from the
ESP32; confirm the command set, the baud rate, and the frame format for a tag report; read a
bare tag held in free air; confirm the reported RSSI changes when you move the tag.

**Verify:** a bare tag reads reliably at close range, and RSSI moves in the right direction.

**Review gate:** the serial capture of a tag report, decoded field by field. ⛔ Stop for
approval.

---

### Step 2.2 — Tag embedding trial and survivability ⏸
**Goal:** close R7 on three rocks before committing the whole batch.
**Owner:** 🔧 · **Size:** M · **Depends on:** 2.1, 0.5 · **Risk:** **R7**

**Do:** using the process agreed at 0.5, embed tags in **three** trial rocks; **read-test each
tag immediately before embedding, immediately after, and again after full cure**; record
adhesive type and cure temperature against the tag manufacturer's stated tolerance.

**Verify:** all three read after curing, at a range comparable to the bare tag.

**Review gate:** the before/during/after read table. **A tag killed by embedding fails
invisibly and is unrecoverable once the rock is in the arena — if any of the three dies, we
change the process here, not after the batch is made.** ⛔ Stop for approval.

---

### Step 2.3 — Read-range characterisation, bare tags ⏸
**Goal:** the numbers 0.1's required range is actually checked against.
**Owner:** 🔧 · **Size:** M · **Depends on:** 2.1 · **Risk:** R2

**Do:** with the antenna fixed, measure read success rate and RSSI against distance in steps,
out to beyond the expected limit; repeat at several angles off the antenna's boresight; plot
it. Do this with a bare tag first so it is the *radio* being characterised, not the rock.

**Verify:** a curve of range against read rate, and the angle at which reads start failing.

**Review gate:** the plot, compared against the range agreed in 0.1. ⛔ Stop for approval.

---

### Step 2.4 — Read through rock, sand, and orientation ⏸
**Goal:** the real question behind R2 — the tag is inside an irregular rock at an angle nobody
chose.
**Owner:** 🔧 · **Size:** L · **Depends on:** 2.2, 2.3 · **Risk:** **R2**

**Do:** repeat 2.3 with the embedded trial rocks; rotate each rock through a spread of
orientations, including the worst one you can find; test with the rock sitting on sand and
partly buried, as it will be in the arena; note how much range the rock itself costs versus
the bare tag.

**Verify:** a usable read at the 0.1 range in **every** orientation tested, not just the
favourable ones.

**Review gate:** the orientation matrix with read rates. **If some orientations simply do not
read, the options are a bigger antenna, more reader power, a different tag, or changing how
tags are oriented during embedding — that is a 0.1 revision, not a workaround.**
⛔ Stop for approval.

---

### Step 2.5 — Reader mounted and powered on the rover ⏸
**Goal:** the reader on the vehicle, on its own rail, without disturbing anything already
working.
**Owner:** 🔧 · **Size:** M · **Depends on:** 2.4, 1.7, 0.3 · **Risk:** R4

**Do:** mount the antenna low on the front, facing the ground ahead, at the position the arm
would have occupied; build the reader's supply from the 0.3 plan with bulk capacitance close
to it, since UHF readers draw bursty current while transmitting; keep the antenna feed away
from the motor wiring; re-measure current draw with the reader transmitting.

**Verify:** transmitting does not brown out the ESP32, does not reset the link, and the drive
motors do not visibly degrade the read.

**Review gate:** meter trace of the 3.3 V rail during a read burst, plus the updated current
table. ⛔ Stop for approval.

---

### Step 2.6 — Firmware RFID module and tag-read frames ⏸
**Goal:** the reader integrated the same way everything else is — a self-contained module,
tested on the host.
**Owner:** 💻 · **Size:** M · **Depends on:** 2.1, S.9

**Do:** `firmware/lib/Rfid/` — self-contained like `Drive`, taking its UART through the
constructor rather than including `config.h`; parse the reader's frame format into a typed
tag-read struct with **no Arduino headers**, so it is host-testable; poll it from `main.cpp`
without blocking the control loop or the failsafe; emit the S.9 tag-read frame; report reader
status in telemetry. Native tests for the frame parser, including malformed and partial
frames.

**Verify:** `pio test -e native` green; the control-latency figure from 1.8 is unchanged with
the reader polling — **the failsafe must not be delayed by a sensor read.**

**Review gate:** the test list, and RTT with and without polling. ⛔ Stop for approval.

---

### Step 2.7 — On-rover read reliability acceptance ⏸
**Goal:** the proposal's Phase 2 gate — the moment R2 is settled.
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 2.5, 2.6 · **Risk:** **R2**

**Do:** drive the rover up to a tagged rock on sand and read it, 20 times, across at least
three different rocks and a spread of approach bearings; count clean first-time reads,
retries, and outright misses; confirm the console's signal meter actually helps rather than
just moving.

**Verify:** 20 of 20 rocks identified, with retries counted and acceptable.

**Review gate:** the tally and video of the worst-case approach. **This is the proposal's
Phase 2 gate.** ⛔ Stop for approval.
**— Phase 2 gate: 2.4 and 2.7 passed. —**

---

# Phase 3 — Vision & operator console

> **Proposal exit criteria:** *operator can drive, steer, and read a tag using only the
> console and the single camera feed, no direct sightline.* — **the "read a tag" half is
> deferred to Phase 2's method; the driving half is met at 3.6 and demonstrated at 3.7.**

**One camera now, not two.** That makes 3.3 easier and 3.4 more important: if the mast head
fails, the rover has no eyes at all (R8).

**Runs directly after Phase 1 as of 2026-08-13.** It previously waited on Phase 2 through
3.3's dependency on 2.7; that edge is cut. R8 matters more than ever now, because with sensing
suspended the camera is the *only* thing making the rover useful — and if 2.0 later picks a
camera-based method (candidates C or D), this phase becomes the foundation that method is
built on rather than a parallel track.

### Step 3.1 — The mast camera streaming to its monitor
**Owner:** 🔧 · **Size:** M · **Depends on:** 1.2 · **Risk:** R3

**Do:** mount the camera on its pole; stream to its monitor over the 0.5 network; find the
resolution/framerate/quality point that keeps latency usable — the proposal explicitly trades
resolution away to protect frame rate.

**Verify:** measure glass-to-glass latency by pointing the camera at a running stopwatch and
photographing both together.

**Review gate:** the latency figure and the chosen stream settings. ⛔ Stop for approval.

---

### Step 3.2 — Forward-and-down driving view
**Goal:** confirm one camera can actually do both jobs — the central bet of Revision 2's
single-camera design.
**Owner:** 🔧 · **Size:** M · **Depends on:** 3.1 · **Risk:** **R8**

**Do:** find the pan/tilt position that gives a usable forward driving view, including enough
of the ground immediately ahead to judge where the **front mounting point** sits relative to a
rock — whatever 2.0 ends up putting there; drive the full arena from that view alone;
establish it as the **default position the head returns to** between surveys, so the operator
is never blind while approaching.

**Verify:** the rover can be driven the length of the arena and lined up on a rock without
repointing the head.

**Review gate:** the driving-view screenshot and a driven run. **If one view cannot do both
jobs, that is an R8 hit and we reconsider the second camera before going further.**
⛔ Stop for approval.

---

### Step 3.3 — Concurrent load test
**Goal:** the moment R1 is either confirmed or dismissed — everything shares one 2.4 GHz band
for the first time.
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 3.2, 1.8 · **Risk:** **R1**

*The dependency on 2.7 was cut on 2026-08-13. This step now measures video + driving only,
which is a **partial** R1 verdict: the sensing payload is not yet in the band. Whatever 2.0
picks gets its own re-measurement against this run's CSV — that is why the file is kept.*

**Do:** run the camera stream at full rate while driving; capture RTT with the S.7 HUD;
compare directly against the 1.8 idle-link baseline; test at the far end of the arena, worst
case; attempt Scene 5-style approach moves with the S.13 fine-drive controls and judge whether
they are actually controllable, not merely connected.

**Verify:** control latency stays within the S.7 target — **p95 ≤ 100 ms** — with video live.

**Review gate:** the two RTT distributions side by side, plus your hands-on verdict on fine
control. **Revision 2 gives this step more headroom than Revision 1 did, since there is one
video stream instead of two — and more still while sensing is suspended. If it fails even
here, the fallbacks in priority order are: reduce video bitrate → move video to 5 GHz if the
camera supports it → Bluetooth control fallback. We choose together; we don't quietly
proceed.** ⛔ Stop for approval. **Keep the CSV — Phase 2 re-runs this comparison.**

---

### Step 3.4 — Mast pan/tilt under console control
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 3.2 · **Risk:** R8

**Do:** `firmware/lib/Mast/` on the same self-contained pattern as `Drive`; two servos on
LEDC channels that do not collide with the drive channels; travel limited so the head cannot
wrap its own camera cable; console pan/tilt controls in the region S.8 reserved for them;
a **return-to-driving-position** control, since R8 makes that the safe default; confirm the
pole doesn't oscillate visibly after a fast pan — Scene 3 depends on a sweep being watchable.

**Verify:** full sweep left/right/up/down, settles quickly, no cable strain, and returns
reliably to the driving position.

**Review gate:** a Scene 3-style survey sweep, watched on the monitor. **Treat mast
reliability as higher priority than under Revision 1 — this is the rover's only eye.**
⛔ Stop for approval.

---

### Step 3.5 — Console final layout on the real touchscreen
**Owner:** 💻 · **Size:** L · **Depends on:** 3.4, S.11

**Do:** run full-screen on the actual console hardware; **replace the S.8 display assumption
with the real panel's measurements** and re-run the touch audit; lay the screen out around the
storyboard's actual scene order rather than by subsystem; add the Scene 1 pre-flight readiness
check that gates the mission start — **with the sensing row showing "not fitted" rather than a
false green**; include the S.13 speed-mode indicator in the audit; confirm multi-touch on real
glass, which S.8 could only verify by construction; kiosk behaviour — no window chrome, no
accidental exit.

*The S.11 signal meter and analysis panel stay on screen but are **inert** until Phase 2. Lay
them out anyway — the region is reserved either way, and an operator seeing a dead panel is
better than a layout that has to move when sensing lands.*

**Verify:** the touch audit passes against measured DPI; an operator who hasn't seen it before
can find drive, mast, and the speed mode unaided.

**Review gate:** the console running on the real screen; ideally someone unfamiliar tries it.
⛔ Stop for approval.

---

### Step 3.6 — Blind-driving acceptance
**Goal:** the driving half of the proposal's Phase 3 gate. *The scanning half is deferred with
Phase 2 and rejoins when 2.0 lands.*
**Owner:** 🔧 · **Size:** M · **Depends on:** 3.5

**Do:** operator positioned with **no direct view** of the rover — screen only. Drive the full
length of the arena, navigate around obstacle rocks, and park the rover with the front
mounting point positioned over a specific target rock — which is the manoeuvre every sensing
candidate needs, whatever ends up mounted there.

**Verify:** completed on the single camera feed alone, without anyone peeking at the box.

**Review gate:** the run, performed with line of sight physically blocked. ⛔ Stop for
approval. **— Phase 3 gate, driving half. —**

---

### Step 3.7 — Driving-only mission rehearsal (Scenes 1–4)
**Goal:** **the finish line for the driving work** — something complete and demonstrable to a
sponsor while the sensing decision is still open.
**Owner:** 🔧 · **Size:** M · **Depends on:** 3.6 · **Risk:** R8

*Added 2026-08-13. Phase 4 rehearses all eight scenes and is blocked on Phase 2; Scenes 1–4
are not, and they stand on their own as a mission opening.*

**Do:** run Scenes 1 through 4 from
[plan/storyboard-rev2.md §6](plan/storyboard-rev2.md) end to end — power-up and handshake,
deploy from base, survey the field on the mast camera, target acquired and lined up — then
drive back to base. Each against its own "success looks like" line. Note every point where the
operator had to improvise; those are console gaps, and fixing them is cheaper now than during
a full-mission rehearsal.

**Verify:** three consecutive clean runs, no resets, no hands in the box.

**Review gate:** a recording of one run, plus a pass/fail line per scene. **Scene 1's readiness
row must honestly show sensing as not fitted.** ⛔ Stop for approval.
**— End of the driving track. Everything beyond here waits on step 2.0. —**

---

# Phase 4 — Full mission integration ⏸ BLOCKED ON PHASE 2

> **Proposal exit criteria:** *a full mission run (Scenes 1–8) completes for at least two
> different tagged rocks without manual intervention.*

**Blocked from 2026-08-13.** Scenes 5–8 are the identify half of the mission, so this phase
cannot run until step 2.0 picks a method and Phase 2 clears. **Step 3.7 is the interim
milestone** — Scenes 1–4, driving only. The one piece that *can* proceed is the terrain half of
4.1, split out below.

### Step 4.1a — Arena dressing and base zone *(proceeds)*
**Owner:** 🔧 · **Size:** M · **Depends on:** 3.6, 0.5

**Do:** dress the floor as Martian terrain; mark the base zone; scatter untagged rocks at
realistic distances; re-check RSSI (1.2) and driveability (1.5) with the **final** dressing in
place, since both were measured earlier and the dressing changes both.

**Verify:** the link holds and the rover drives across the finished terrain.

**Review gate:** photos of the dressed arena and the re-checked RSSI and driveability figures.
⛔ Stop for approval.

---

### Step 4.1b — Preparing the rock batch ⏸ ON HOLD
**Owner:** 🔧 · **Size:** M · **Depends on:** 4.1a, **2.0**, 2.2 · **Risk:** R7

> ⏸ **Held behind 2.0** — what "preparing" means depends entirely on the method: embedding
> tags, finishing rocks in distinct colours, or applying fiducial markers.

**Do (RFID candidate):** **embed the full batch of tags using the process proven at 2.2,
read-testing every single tag immediately after curing**; record the tag ID of every rock and
load them into the S.11 composition table.

**Verify:** every rock in the arena is identifiable, and every ID has a composition entry.

**Review gate:** the ID-to-rock register and the post-cure read results. ⛔ Stop for approval.

---

### Step 4.2 — Session log and multi-rock run state ⏸
**Owner:** 💻 · **Size:** M · **Depends on:** 3.5, S.11

**Do:** console-side mission state following the eight scenes, **built around the loop
repeating** rather than ending at Scene 8; the running sample count and per-rock log Scene 8
appends to; a timestamped event log written to disk per session — this is what makes a failed
demo diagnosable afterwards; handling for a rock scanned twice; a reset-for-next-session
action.

**Review gate:** a log file from a simulated multi-rock session. ⛔ Stop for approval.

---

### Step 4.3 — Scene-by-scene rehearsal ⏸
**Goal:** run each of the eight scenes in isolation before attempting them in sequence.
**Owner:** 🔧 · **Size:** L · **Depends on:** 4.1a, 4.1b, 4.2

**Do:** work through Scenes 1–8 one at a time, each against its own "success looks like" line
in the proposal §6; note every point where the operator had to improvise — those are either
console gaps or procedure gaps.

**Review gate:** a pass/fail line per scene, with notes. ⛔ Stop for approval — we fix the
gaps before 4.4 rather than during it.

---

### Step 4.4 — Full repeatable mission run ⏸
**Owner:** 🔧 · **Size:** M · **Depends on:** 4.3

**Do:** all eight scenes end to end for **at least two different tagged rocks in one session**,
no resets, no hands in the box — the loop returning to Scene 3 between rocks is the thing
being demonstrated, not an afterthought; run it three times to show it's repeatable rather
than lucky; record one session start to finish.

**Verify:** three consecutive complete sessions, each identifying two or more rocks.

**Review gate:** the recording. **This is the proposal's Phase 4 gate and the project's
finish line.** ⛔ Stop for approval.

---

### Step 4.5 — Handover documentation ⏸
**Owner:** 💻 · **Size:** M · **Depends on:** 4.4

**Do:** operator guide (power-on through a survey session through shutdown); wiring diagram
and pin map as actually built; the tag-ID-to-rock register and how to edit the composition
table; troubleshooting for the failures seen during bring-up; the measured figures — RSSI,
RFID read range, latency, current, runtime — collected in one place; update `CLAUDE.md`,
`docs/protocol.md` and the proposal so they describe what was **built**, not what was
proposed.

**Review gate:** the document set. ⛔ Stop for approval.

---

## Risk register

Quoted from the proposal §8, for reference from the steps above, with the 2026-08-13
re-prioritisation applied to where each one is settled.

| # | Risk | Where it is settled |
|---|---|---|
| R1 | Wi-Fi control latency under load — **eased**, one video stream not two | 1.8 baseline, **3.3 partial verdict** (video + driving), re-run once sensing lands |
| R2 | **Which identification method, and will it work** *(reopened 2026-08-13)* — was "RFID read reliability through an irregular rock at an unknown angle" | **2.0 decides**, then the method's own steps — 2.3, **2.4**, **2.7** for the RFID candidate |
| R3 | Camera latency/framerate — **higher stakes**, it is the only camera | 3.1 |
| R4 | Power budget for an extended multi-rock session *(broadened)* | 0.3 estimate **with a sensing allowance, not a part**, **1.7** measured, re-measured when sensing is fitted |
| R5 | **Minimum turning circle inside 1.4 m** *(reopened and inverted at 0.2, 2026-08-13)* — was "can a skid-steer chassis pivot in place", which 0.2 settled comfortably. The chosen chassis **steers instead of pivoting**, so it has a turning circle that may not fit, and may need a three-point turn | S.15 in simulation, **1.5 measured with a tape** |
| R6 | RF through the glass — **Wi-Fi now, the sensing band only if 2.0 picks a radio** | 1.2 Wi-Fi, 2.4 if RFID |
| R7 | ⏸ **Tag survivability during embedding** — fails silently and unrecoverably. **RFID-specific**; if 2.0 picks another method this risk is retired and replaced by that method's equivalent | 0.6 process, **2.2** trial, 4.1b batch |
| R8 | **Single point of vision** — one camera does driving and survey, **and may do identification too** if 2.0 picks candidate C or D | **3.2**, 3.4, **and 2.0's decision** |

**Not yet a numbered risk, and worth watching:** driveability itself. The complaint that
reopened R2 on 2026-08-13 was as much about *manoeuvring the rover precisely* as about the
sensor, and no sensing choice fixes it. S.12 and S.13 attack it in software; 1.5 and 3.6 are
where it is judged for real. If those go badly it becomes R9 rather than a footnote.

---

## Deliberate ordering choices

A few places where this plan's sequence is a decision rather than an obvious consequence:

- **Software before hardware (Phase S).** The console is the piece most likely to need
  iteration and the piece least dependent on parts. Building it against a simulator means
  Phase 1 tests the *hardware*, not the software and hardware at once.
- **Protocol revision (S.9) before console work (S.11).** The contract is what the three
  implementations agree on. Changing the console first would mean changing it twice.
- **RF survey (1.2) before motors (1.3).** R6 could invalidate the network design. Finding
  that out with a bare board on the bench is cheap; finding it out with an assembled rover is
  not.
- **Failsafe proving (1.6) early.** A rover that can't stop itself is hazardous to the glass
  and the terrain, and every later step involves driving it.
- **Tag embedding trial (2.2) before the batch (4.1b).** R7 fails silently: a tag cooked by
  the adhesive is indistinguishable from a tag out of range, and once a rock is sealed and
  placed there is no way to fix it. Three rocks first, the batch only after.
- **Bench read range (2.3) before on-rover reads (2.7).** Characterise the radio where
  nothing moves. A marginal read on a driving rover has too many possible causes to debug.
- **Latency baseline (1.8) before video exists (3.3).** Without the earlier number, the later
  measurement has nothing to be compared to and R1 stays a matter of opinion.
- **Single-camera driving view (3.2) before everything downstream depends on it.** It is the
  central bet of Revision 2. If one camera cannot do both jobs, we want to know before the
  console layout and the mission rehearsal are built on the assumption.

Added with the 2026-08-13 re-prioritisation:

- **The whole drive platform before the sensing decision (2.0).** Not a delaying tactic — the
  chassis, motors, link, failsafe, power rails and mast camera are byte-for-byte the same
  under all four candidates. Building them commits nothing and forecloses nothing, while the
  sensing question stays genuinely open instead of being settled by schedule pressure.
- **An honest simulator (S.12) before better drive controls (S.13).** The current simulator
  drives on ideal kinematics, so any control scheme feels fine against it. Tuning fine-drive
  against a forgiving model would produce controls that are tuned to nothing.
- **Fine-drive (S.13) before the concurrent load test (3.3).** 3.3's real question is not
  "does RTT hold" but "is the rover still *controllable* under video load". That is only
  answerable with the precision controls actually present.
- **2.0 has no dependencies and can be taken at any time.** It is placed at the head of
  Phase 2 for readability, not because anything must finish first. Taking it early costs
  nothing and unblocks 0.1, 0.6, 4.1b and Phase 4; the only thing that gets more expensive by
  waiting is 0.2's chassis choice, which is why it carries a worst-case front-mount
  allowance in the meantime.
