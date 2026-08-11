# Implementation Plan — Mars Rover RFID Exploration

Companion to the proposal. The proposal says *what* gets built and *why*; this file says
*in what order*, in pieces small enough to review one at a time.

**Tracking Revision 2** — [plan/storyboard-exploration.docx](plan/storyboard-exploration.docx)
(English) and [plan/storyboard-exploration-th.docx](plan/storyboard-exploration-th.docx)
(Thai), both dated 2026-07-27. Revision 2 supersedes the Sample-Return concept in
[plan/storyboard.md](plan/storyboard.md); that file is Revision 1 and is now historical
except for its §2 project rationale, which Revision 2 carries forward unchanged.

**Status: no hardware procured.** Phase S is software groundwork and is doable today.
Everything from Phase 1 onward is blocked until parts exist.

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
| **S — Revision 2 rework** (new; brings Phase S onto the revised mission) | | | |
| S.9 | Revise the protocol: drop `arm`, add the tag-read path | 💻 | M | R2 |
| S.10 | Simulator: tagged rocks and distance-dependent RSSI | 💻 | M | R2 |
| S.11 | Console: signal meter, analysis panel, session log | 💻 | L | R2 |
| **0 — Approval & procurement** | | | |
| 0.1 | Fix the RFID spec — tag, reader, and required read range | 📋 | M | R2 |
| 0.2 | Chassis footprint vs. the 1.4 m turning check, on paper | 📋 | M | R5 |
| 0.3 | Power budget estimate and rail plan | 📋 | M | R4 |
| 0.4 | Bill of materials and sourcing | 📋 | L | — |
| 0.5 | Arena, Wi-Fi and UHF infrastructure plan | 📋 | M | R6 R7 |
| **1 — Drive platform bring-up** | | | |
| 1.1 | ESP32 board sanity — blink and serial | 🔧💻 | S | — |
| 1.2 | Wi-Fi join, WebSocket echo, RSSI survey through the glass | 🔧💻 | M | R6 |
| 1.3 | Bench-spin one motor, no chassis | 🔧💻 | M | — |
| 1.4 | Both sides, direction and polarity calibration | 🔧💻 | M | — |
| 1.5 | Chassis assembly and first driven run | 🔧 | L | R5 |
| 1.6 | Failsafe proving — cut the link mid-drive | 🔧💻 | S | R1 |
| 1.7 | Current draw measurement per subsystem | 🔧 | M | R4 |
| 1.8 | Control-latency measurement, idle link | 🔧💻 | S | R1 |
| **2 — RFID sensing integration** *(retargeted in Revision 2)* | | | |
| 2.1 | Reader bring-up on the bench, off the rover | 🔧💻 | M | — |
| 2.2 | Tag embedding trial and survivability | 🔧 | M | **R7** |
| 2.3 | Read-range characterisation, bare tags | 🔧 | M | R2 |
| 2.4 | Read through rock, sand, and tag orientation | 🔧 | L | **R2** |
| 2.5 | Reader mounted and powered on the rover | 🔧 | M | R4 |
| 2.6 | Firmware RFID module and tag-read frames | 💻 | M | — |
| 2.7 | On-rover read reliability acceptance | 🔧💻 | M | **R2** |
| **3 — Vision & operator console** | | | |
| 3.1 | The mast camera streaming to its monitor | 🔧 | M | R3 |
| 3.2 | Forward-and-down driving view on one camera | 🔧 | M | **R8** |
| 3.3 | Concurrent load test — video plus driving plus reader | 🔧💻 | M | **R1** |
| 3.4 | Mast pan/tilt under console control | 🔧💻 | M | R8 |
| 3.5 | Console final layout on the real touchscreen | 💻 | L | — |
| 3.6 | Blind-driving and blind-scanning acceptance run | 🔧 | M | R8 |
| **4 — Full mission integration** | | | |
| 4.1 | Arena dressing, base zone, and the tagged rock batch | 🔧 | L | R7 |
| 4.2 | Session log and multi-rock run state | 💻 | M | — |
| 4.3 | Scene-by-scene rehearsal, 1 through 8 | 🔧 | L | — |
| 4.4 | Full repeatable mission run, two or more rocks | 🔧 | M | — |
| 4.5 | Handover documentation | 💻 | M | — |

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
⛔ Stop for approval. **— End of Phase S —**

---

# Phase 0 — Approval & procurement

> **Proposal exit criteria:** *budget and part list approved.*

Mostly your decisions; I can draft and calculate. Nothing here is ordered until you approve.

### Step 0.1 — Fix the RFID spec
**Goal:** the decisions that size the whole sensing chain, settled before anything is quoted.
**Owner:** 📋 · **Size:** M · **Risk:** R2

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
3. Check the front-mounted antenna's projection and ground clearance. *Simpler than Revision
   1, which had to check an arm's reach envelope against the glass — but the antenna still
   sticks out in front and still has to clear rocks.*

**Verify:** a dimensioned sketch with the numbers, per candidate.

**Review gate:** I show the sketch and the arithmetic; you pick the chassis or reject all.
⛔ Stop for approval.

---

### Step 0.3 — Power budget estimate and rail plan
**Goal:** an estimate to buy against, replaced by measurement in 1.7.
**Owner:** 📋 · **Size:** M · **Risk:** R4

**Do:**
1. Tabulate worst-case current per subsystem from datasheets. *Much smaller servo load than
   Revision 1 — two mast servos instead of an arm's four or five — but note that servo
   **stall** current still sizes that rail.*
2. Add the **RFID reader**, which is a new and non-trivial load: UHF readers draw a
   meaningful current while transmitting, and the draw is bursty rather than steady.
3. Plan **separate rails**: motors at pack voltage, servos on their own regulator, ESP32,
   camera and reader on a clean 5 V/3.3 V rail, all sharing a common ground.
4. Size the LiPo for a **full multi-rock session** plus margin — Revision 2's loop repeats, so
   the runtime demand is higher than a single collect-and-return (revised R4).

**Review gate:** the rail diagram and the current table. ⛔ Stop for approval.

---

### Step 0.4 — Bill of materials and sourcing
**Goal:** the costed BOM the proposal defers until this point.
**Owner:** 📋 · **Size:** L · **Depends on:** 0.1–0.3

**Do:** every part with quantity, unit cost, supplier, and lead time — chassis and motors,
H-bridge, ESP32, **one** camera, **one** monitor, mast pan/tilt servos, **RFID reader,
antenna, and the tag batch**, touchscreen, power system, arena materials, plus consumables and
**spares for the parts most likely to fail** (motor driver, servos, tags). The manipulator and
gripper kit from Revision 1 is no longer needed. Flag long-lead items — the RFID reader is the
most likely long pole and the least substitutable.

**Review gate:** the BOM spreadsheet. **This is the proposal's Phase 0 gate — budget and part
list approved.** ⛔ Stop for approval.

---

### Step 0.5 — Arena, Wi-Fi and UHF infrastructure plan
**Goal:** decide both RF setups before either is a problem.
**Owner:** 📋 · **Size:** M · **Risk:** R6 R7

**Do:**
1. Decide the network topology: rover as access point, or a dedicated router with everything
   as clients. *Easier than Revision 1 — one camera stream instead of two.*
2. Pick 2.4 GHz channels and check what else is using them in the room.
3. **Check the UHF band for interference and for legal channel limits** where this will run.
   860–960 MHz is regionally allocated and the permitted band and power differ by country.
   This is new in Revision 2 and has no Revision 1 equivalent.
4. Plan AP placement relative to the box, including the rover at the **far** end of the 3 m
   run, which is the worst case.
5. Plan the **tag embedding process** — adhesive or epoxy type, cure temperature, and where
   in the rock the tag sits. R7 says a tag cooked during embedding fails invisibly, so the
   process is decided here and trialled at 2.2 before the full batch is committed.

**Review gate:** the network diagram, the channel plan for both bands, and the embedding
procedure. ⛔ Stop for approval.

---

# Phase 1 — Drive platform bring-up

> **Proposal exit criteria:** *rover drives forward/back/left/right reliably inside the
> 1.4 m arena width; battery draw measured.*

**Blocked until parts arrive.** Unchanged by Revision 2 — the drive platform is the same
either way. Bring the board up before the chassis exists: a fault found on the bench is far
cheaper than one found inside an assembled rover.

### Step 1.1 — ESP32 board sanity
**Goal:** prove the toolchain reaches real silicon.
**Owner:** 🔧💻 · **Size:** S

**Do:** identify the COM port, flash a blink-plus-serial build, confirm the upload speed is
stable (drop from 921600 if uploads are flaky), and note the exact board variant in
`platformio.ini` if it isn't `esp32dev`.

**Verify:** LED blinks, serial monitor readable at 115200, repeated flashes succeed.

**Review gate:** serial output and the confirmed board/port settings. ⛔ Stop for approval.

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

---

### Step 1.4 — Both sides, direction and polarity calibration
**Goal:** left is left, and forward is forward.
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 1.3

**Do:** wire all four motors, two per side; verify each side's polarity so a positive throttle
drives forward on both; add a per-side inversion flag in `config.h` rather than swapping
physical wires; check both LEDC channels are independent and that neither collides with a
channel reserved for the mast servos later.

**Verify:** `{"cmd":"drive","l":1,"r":1}` spins every wheel forward; `l:-1, r:1` yields a clean
turn in place.

**Review gate:** video of all four wheels for each of the five drive-pad commands.
⛔ Stop for approval.

---

### Step 1.5 — Chassis assembly and first driven run
**Goal:** a rover that moves across the floor under console control.
**Owner:** 🔧 · **Size:** L · **Depends on:** 1.4, 0.2 · **Risk:** R5

**Do:** assemble the chassis with battery and electronics mounted; keep wiring serviceable and
clear of the wheels; drive it first on a hard floor, then **on the actual sand terrain**, which
loads the motors much more heavily; measure the real turning circle and compare it to the 0.2
calculation.

**Verify:** turns in place within the usable arena width, on sand, without stalling.

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

**Do:** measure idle, driving on sand, turning in place (the worst drive case), and stall;
log pack voltage over a sustained run to get real runtime; calibrate the
`BATTERY_DIVIDER_RATIO` constant against a meter so the telemetry reading is true.

**Verify:** measured runtime comfortably exceeds a **full multi-rock session**, with margin
for the mast servos and the RFID reader that aren't fitted yet.

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

# Phase 2 — RFID sensing integration

> **Proposal exit criteria:** *reader gets a clean, repeatable read at a usable approach
> distance, across multiple rock/tag orientations.*

**Retargeted in Revision 2** — this phase was manipulator integration. The whole of it is new
work, and it carries the risk most likely to sink the mission (R2), plus a new one that fails
silently (R7).

The ordering below is deliberate: **prove the tag survives embedding, and prove the read works
on a bench, before either is attached to a rover.** Debugging a marginal read on a moving
platform when you do not yet know whether the tag is even alive is the trap this sequence
avoids.

### Step 2.1 — Reader bring-up on the bench
**Goal:** prove the reader talks, off the rover entirely.
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 0.4

**Do:** power the reader on the bench, talk to it over UART from a PC first and then from the
ESP32; confirm the command set, the baud rate, and the frame format for a tag report; read a
bare tag held in free air; confirm the reported RSSI changes when you move the tag.

**Verify:** a bare tag reads reliably at close range, and RSSI moves in the right direction.

**Review gate:** the serial capture of a tag report, decoded field by field. ⛔ Stop for
approval.

---

### Step 2.2 — Tag embedding trial and survivability
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

### Step 2.3 — Read-range characterisation, bare tags
**Goal:** the numbers 0.1's required range is actually checked against.
**Owner:** 🔧 · **Size:** M · **Depends on:** 2.1 · **Risk:** R2

**Do:** with the antenna fixed, measure read success rate and RSSI against distance in steps,
out to beyond the expected limit; repeat at several angles off the antenna's boresight; plot
it. Do this with a bare tag first so it is the *radio* being characterised, not the rock.

**Verify:** a curve of range against read rate, and the angle at which reads start failing.

**Review gate:** the plot, compared against the range agreed in 0.1. ⛔ Stop for approval.

---

### Step 2.4 — Read through rock, sand, and orientation
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

### Step 2.5 — Reader mounted and powered on the rover
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

### Step 2.6 — Firmware RFID module and tag-read frames
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

### Step 2.7 — On-rover read reliability acceptance
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
> console and the single camera feed, no direct sightline.*

**One camera now, not two.** That makes 3.3 easier and 3.4 more important: if the mast head
fails, the rover has no eyes at all (R8).

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
of the ground immediately ahead to judge the antenna's position over a rock; drive the full
arena from that view alone; establish it as the **default position the head returns to**
between surveys, so the operator is never blind while approaching.

**Verify:** the rover can be driven the length of the arena and lined up on a rock without
repointing the head.

**Review gate:** the driving-view screenshot and a driven run. **If one view cannot do both
jobs, that is an R8 hit and we reconsider the second camera before going further.**
⛔ Stop for approval.

---

### Step 3.3 — Concurrent load test
**Goal:** the moment R1 is either confirmed or dismissed — everything shares one 2.4 GHz band
for the first time.
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 3.2, 1.8, 2.7 · **Risk:** **R1**

**Do:** run the camera stream at full rate while driving **and** while the reader is polling;
capture RTT with the S.7 HUD; compare directly against the 1.8 idle-link baseline; test at the
far end of the arena, worst case; attempt Scene 5-style approach moves and judge whether they
are actually controllable, not merely connected.

**Verify:** control latency stays within the S.7 target — **p95 ≤ 100 ms** — with video live
and the reader running.

**Review gate:** the two RTT distributions side by side, plus your hands-on verdict on fine
control. **Revision 2 gives this step more headroom than Revision 1 did, since there is one
video stream instead of two. If it still fails, the fallbacks in priority order are: reduce
video bitrate → move video to 5 GHz if the camera supports it → Bluetooth control fallback.
We choose together; we don't quietly proceed.** ⛔ Stop for approval.

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
check that gates the mission start; confirm multi-touch on real glass, which S.8 could only
verify by construction; kiosk behaviour — no window chrome, no accidental exit.

**Verify:** the touch audit passes against measured DPI; an operator who hasn't seen it before
can find drive, mast, and the signal meter unaided.

**Review gate:** the console running on the real screen; ideally someone unfamiliar tries it.
⛔ Stop for approval.

---

### Step 3.6 — Blind-driving and blind-scanning acceptance
**Goal:** the proposal's Phase 3 gate, exactly as written.
**Owner:** 🔧 · **Size:** M · **Depends on:** 3.5

**Do:** operator positioned with **no direct view** of the rover — screen only. Drive the full
length of the arena, navigate around obstacle rocks, approach a tagged rock, and get a read.

**Verify:** completed on the single camera feed and the signal meter alone, without anyone
peeking at the box.

**Review gate:** the run, performed with line of sight physically blocked. ⛔ Stop for
approval. **— Phase 3 gate. —**

---

# Phase 4 — Full mission integration

> **Proposal exit criteria:** *a full mission run (Scenes 1–8) completes for at least two
> different tagged rocks without manual intervention.*

### Step 4.1 — Arena dressing, base zone, and the tagged rock batch
**Owner:** 🔧 · **Size:** L · **Depends on:** 3.6, 2.2 · **Risk:** R7

**Do:** dress the floor as Martian terrain; mark the base zone; **embed the full batch of tags
using the process proven at 2.2, read-testing every single tag immediately after curing**;
record the tag ID of every rock and load them into the S.11 composition table; scatter the
rocks at realistic distances; re-check RSSI (1.2) and driveability (1.5) with the **final**
dressing in place, since both were measured earlier.

**Verify:** every rock in the arena reads, and every tag ID has a composition entry.

**Review gate:** photos of the dressed arena, the tag-ID-to-rock register, and the re-checked
RSSI figures. ⛔ Stop for approval.

---

### Step 4.2 — Session log and multi-rock run state
**Owner:** 💻 · **Size:** M · **Depends on:** 3.5, S.11

**Do:** console-side mission state following the eight scenes, **built around the loop
repeating** rather than ending at Scene 8; the running sample count and per-rock log Scene 8
appends to; a timestamped event log written to disk per session — this is what makes a failed
demo diagnosable afterwards; handling for a rock scanned twice; a reset-for-next-session
action.

**Review gate:** a log file from a simulated multi-rock session. ⛔ Stop for approval.

---

### Step 4.3 — Scene-by-scene rehearsal
**Goal:** run each of the eight scenes in isolation before attempting them in sequence.
**Owner:** 🔧 · **Size:** L · **Depends on:** 4.1, 4.2

**Do:** work through Scenes 1–8 one at a time, each against its own "success looks like" line
in the proposal §6; note every point where the operator had to improvise — those are either
console gaps or procedure gaps.

**Review gate:** a pass/fail line per scene, with notes. ⛔ Stop for approval — we fix the
gaps before 4.4 rather than during it.

---

### Step 4.4 — Full repeatable mission run
**Owner:** 🔧 · **Size:** M · **Depends on:** 4.3

**Do:** all eight scenes end to end for **at least two different tagged rocks in one session**,
no resets, no hands in the box — the loop returning to Scene 3 between rocks is the thing
being demonstrated, not an afterthought; run it three times to show it's repeatable rather
than lucky; record one session start to finish.

**Verify:** three consecutive complete sessions, each identifying two or more rocks.

**Review gate:** the recording. **This is the proposal's Phase 4 gate and the project's
finish line.** ⛔ Stop for approval.

---

### Step 4.5 — Handover documentation
**Owner:** 💻 · **Size:** M · **Depends on:** 4.4

**Do:** operator guide (power-on through a survey session through shutdown); wiring diagram
and pin map as actually built; the tag-ID-to-rock register and how to edit the composition
table; troubleshooting for the failures seen during bring-up; the measured figures — RSSI,
RFID read range, latency, current, runtime — collected in one place; update `CLAUDE.md`,
`docs/protocol.md` and the proposal so they describe what was **built**, not what was
proposed.

**Review gate:** the document set. ⛔ Stop for approval.

---

## Risk register (Revision 2)

Quoted from the proposal §8, for reference from the steps above.

| # | Risk | Where it is settled |
|---|---|---|
| R1 | Wi-Fi control latency under load — **eased**, one video stream not two | 1.8 baseline, **3.3 verdict** |
| R2 | **RFID read reliability** through an irregular rock at an unknown angle *(changed)* | 2.3, **2.4**, **2.7** |
| R3 | Camera latency/framerate — **higher stakes**, it is the only camera | 3.1 |
| R4 | Power budget for an extended multi-rock session *(broadened)* | 0.3 estimate, **1.7** measured, 2.5 re-measured |
| R5 | Chassis clearance to turn inside 1.4 m | 0.2 on paper, **1.5** measured |
| R6 | RF through the glass, **for both Wi-Fi and UHF** *(expanded)* | 1.2 Wi-Fi, 2.4 UHF |
| R7 | **Tag survivability during embedding** *(new)* — fails silently and unrecoverably | 0.5 process, **2.2** trial, 4.1 batch |
| R8 | **Single point of vision** *(new)* — one camera does driving and survey | **3.2**, 3.4 |

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
- **Tag embedding trial (2.2) before the batch (4.1).** R7 fails silently: a tag cooked by
  the adhesive is indistinguishable from a tag out of range, and once a rock is sealed and
  placed there is no way to fix it. Three rocks first, the batch only after.
- **Bench read range (2.3) before on-rover reads (2.7).** Characterise the radio where
  nothing moves. A marginal read on a driving rover has too many possible causes to debug.
- **Latency baseline (1.8) before video exists (3.3).** Without the earlier number, the later
  measurement has nothing to be compared to and R1 stays a matter of opinion.
- **Single-camera driving view (3.2) before everything downstream depends on it.** It is the
  central bet of Revision 2. If one camera cannot do both jobs, we want to know before the
  console layout and the mission rehearsal are built on the assumption.
