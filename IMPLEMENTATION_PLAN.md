# Implementation Plan — Mars Rover Sample Return

Companion to [plan/storyboard.md](plan/storyboard.md). The storyboard says *what* gets built
and *why*; this file says *in what order*, in pieces small enough to review one at a time.

**Status: no hardware procured.** Everything in Phase S below is doable today. Everything
from Phase 1 onward is blocked until parts exist.

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
  problem into the next step. That is the same go/no-go discipline as §7 of the storyboard.
- Phase exit criteria are quoted from the storyboard §7 and are the real gates; the per-step
  gates below are finer-grained checkpoints inside them.

---

## Step index

| # | Step | Owner | Size | Risk |
|---|---|---|---|---|
| **S — Software groundwork** (parallel with Phase 0, no spend, no hardware) | | | |
| S.1 | Put the repo under version control | 💻 | S | — |
| S.2 | Freeze the control protocol as a written contract | 💻 | S | R1 |
| S.3 | Extract command parsing into a host-testable module | 💻 | M | — |
| S.4 | Native unit tests for the parser + failsafe logic | 💻 | M | R1 |
| S.5 | Desktop fake-rover simulator | 💻 | M | — |
| S.6 | Console: telemetry + link states against the simulator | 💻 | M | — |
| S.7 | Console: latency HUD and round-trip measurement | 💻 | M | R1 |
| S.8 | Console: touch-target and layout pass | 💻 | M | — |
| **0 — Approval & procurement** | | | |
| 0.1 | Fix the sample spec (max rock size and mass) | 📋 | S | R2 |
| 0.2 | Chassis footprint vs. 1.4 m turning check, on paper | 📋 | M | R5 |
| 0.3 | Power budget estimate and rail plan | 📋 | M | R4 |
| 0.4 | Bill of materials and sourcing | 📋 | L | — |
| 0.5 | Arena and Wi-Fi infrastructure plan | 📋 | M | R6 |
| **1 — Drive platform bring-up** | | | |
| 1.1 | ESP32 board sanity — blink and serial | 🔧💻 | S | — |
| 1.2 | Wi-Fi join, WebSocket echo, RSSI survey through the glass | 🔧💻 | M | R6 |
| 1.3 | Bench-spin one motor, no chassis | 🔧💻 | M | — |
| 1.4 | Both sides, direction and polarity calibration | 🔧💻 | M | — |
| 1.5 | Chassis assembly and first driven run | 🔧 | L | R5 |
| 1.6 | Failsafe proving — cut the link mid-drive | 🔧💻 | S | R1 |
| 1.7 | Current draw measurement per subsystem | 🔧 | M | R4 |
| 1.8 | Control-latency measurement, idle link | 🔧💻 | S | R1 |
| **2 — Manipulator integration** | | | |
| 2.1 | Separate servo power rail and common ground | 🔧 | M | R4 |
| 2.2 | Single servo sweep and travel limits | 🔧💻 | M | — |
| 2.3 | Arm module with named poses | 💻 | L | — |
| 2.4 | Gripper close, hold, and release | 🔧💻 | M | R2 |
| 2.5 | Load test against the spec rock | 🔧 | M | R2 |
| 2.6 | Console arm controls | 💻 | M | — |
| 2.7 | Carry test — hold through a full drive | 🔧 | M | R2 |
| **3 — Vision & operator console** | | | |
| 3.1 | One camera streaming to one monitor | 🔧 | M | R3 |
| 3.2 | Both cameras, both monitors, measured | 🔧 | M | R3 |
| 3.3 | Concurrent load test — video plus driving | 🔧💻 | M | **R1** |
| 3.4 | Mast pan/tilt servos under console control | 🔧💻 | M | — |
| 3.5 | Console final layout on the real touchscreen | 💻 | L | — |
| 3.6 | Blind-driving acceptance run | 🔧 | M | — |
| **4 — Full mission integration** | | | |
| 4.1 | Arena dressing and base zone | 🔧 | M | — |
| 4.2 | Mission log and run state | 💻 | M | — |
| 4.3 | Scene-by-scene rehearsal, 1 through 8 | 🔧 | L | — |
| 4.4 | Full uninterrupted mission run | 🔧 | M | — |
| 4.5 | Handover documentation | 💻 | M | — |

---

# Phase S — Software groundwork

Runs **in parallel with Phase 0**, not ahead of it. Nothing here commits spend or touches
hardware, so it bypasses no gate — it exists so that when parts arrive, the console and the
protocol are already proven against a simulator and Phase 1 is purely a hardware exercise.

### Step S.1 — Put the repo under version control
**Goal:** every step after this is reviewable as a diff.
**Owner:** 💻 · **Size:** S · **Depends on:** —

**Do:**
1. `git init` at the repo root, default branch `main`.
2. Add a root `.gitignore` covering Windows and editor noise (`~$*.docx`, `Thumbs.db`,
   `.vscode/`); the `console/` and `firmware/` ones already exist.
3. One initial commit containing the plan documents, both scaffolds, `CLAUDE.md`, and this
   file.

**Verify:** `git status` clean; `git log --stat` shows no `.pio/`, no `.godot/`, and no
`firmware/include/secrets.h`.

**Review gate:** I show you the file list in the initial commit and confirm the placeholder
credentials file is untracked. ⛔ Stop for approval.

---

### Step S.2 — Freeze the control protocol as a written contract
**Goal:** one authoritative definition of the console↔rover messages, so the two codebases
can be built against it independently instead of drifting.
**Owner:** 💻 · **Size:** S · **Depends on:** S.1 · **Risk:** R1

**Do:**
1. Write `docs/protocol.md`: every message, field, unit, range, and direction — `drive`,
   `mast`, `arm`, `stop`, plus the telemetry frame.
2. Add a `v` protocol-version field to the handshake and define the behaviour on mismatch
   (refuse to arm the motors, report it on the telemetry strip).
3. State the failsafe contract explicitly: command timeout, disconnect behaviour, and what
   "safe mode" means.
4. Record why control and video never share a socket, pointing at storyboard §5.4 / R1.

**Verify:** existing `rover_link.gd` and `main.cpp` message shapes match the document exactly;
any mismatch is fixed in code, not in the document.

**Review gate:** you read `docs/protocol.md` — this is the piece hardest to change later, so
it's worth reading closely. ⛔ Stop for approval.

---

### Step S.3 — Extract command parsing into a host-testable module
**Goal:** get the command/failsafe logic out of `main.cpp` so it can be tested on a PC with no
ESP32 attached.
**Owner:** 💻 · **Size:** M · **Depends on:** S.2

**Do:**
1. New `firmware/lib/Protocol/` — parses a JSON string into a typed `Command` struct and
   serialises telemetry. **No Arduino headers**, so it compiles for the host.
2. New `firmware/lib/Safety/` — the failsafe state machine as a pure function of
   (last-command-time, connected, now) → armed/safe.
3. Rewrite `main.cpp` to be transport-and-glue only: Wi-Fi, socket, and calls into
   `Protocol`, `Safety`, and `Drive`.

**Verify:** `python -m platformio run` still succeeds; flash/RAM figures roughly unchanged.

**Review gate:** I show you the diff and the before/after `main.cpp`. ⛔ Stop for approval.

---

### Step S.4 — Native unit tests for the parser and failsafe
**Goal:** prove the safety-critical logic without a rover, and keep it proven.
**Owner:** 💻 · **Size:** M · **Depends on:** S.3 · **Risk:** R1

**Do:**
1. Add `[env:native]` to `platformio.ini` (`platform = native`) so tests run on the PC.
2. Tests for `Protocol`: valid commands, out-of-range throttles clamped, malformed JSON,
   missing fields, wrong types, unknown `cmd`, oversize payload.
3. Tests for `Safety`: arms on first command, trips at exactly the timeout boundary, trips on
   disconnect, refuses to re-arm without a fresh command.
4. Document `python -m platformio test -e native` in `CLAUDE.md`.

**Verify:** all tests pass; deliberately breaking the timeout comparison makes a test fail.

**Review gate:** I show the test list and passing output. ⛔ Stop for approval.

---

### Step S.5 — Desktop fake-rover simulator
**Goal:** develop and demo the whole console with no ESP32 in existence.
**Owner:** 💻 · **Size:** M · **Depends on:** S.2

**Do:**
1. `tools/fake_rover.py` — a Python WebSocket server on port 81 speaking the S.2 protocol.
2. It holds simulated state: pose in a 1.4 × 3.0 m arena, battery draining over time, arm
   state, and echoes telemetry at the real 500 ms cadence.
3. Switches for testing the nasty cases: injected latency, packet loss, and hard disconnect.
4. Optional ASCII top-down view of the arena in the terminal, so drive commands are visibly
   doing something.

**Verify:** the Godot console connects to it, the drive pad moves the simulated rover, and the
telemetry strip updates.

**Review gate:** live demo — I drive the fake rover from the console in front of you.
⛔ Stop for approval.

---

### Step S.6 — Console: telemetry and link states against the simulator
**Goal:** Scene 1 of the storyboard ("LINK ESTABLISHED", green-before-proceed) working for
real, against the simulator.
**Owner:** 💻 · **Size:** M · **Depends on:** S.5

**Do:**
1. Explicit link state machine in the UI: `DISCONNECTED → CONNECTING → LINKED → SAFE-MODE`,
   each visually distinct.
2. Subsystem readiness row — drive / arm / mast — from telemetry, matching Scene 1's
   "all report green before the operator may proceed".
3. Battery display with a low-battery threshold, and a stale-telemetry indicator when frames
   stop arriving.
4. Disable the drive pad whenever the link isn't `LINKED`, so a dead console can't look live.

**Verify:** with the simulator's disconnect switch, the UI degrades correctly and recovers on
reconnect without a restart.

**Review gate:** I show each state on screen, including the injected-failure ones.
⛔ Stop for approval.

---

### Step S.7 — Console: latency HUD and round-trip measurement
**Goal:** make R1 measurable from day one, rather than a judgement call at integration time.
**Owner:** 💻 · **Size:** M · **Depends on:** S.6 · **Risk:** R1

**Do:**
1. Add a `ping` / `pong` message pair carrying a timestamp (defined in S.2).
2. Console measures round-trip time continuously; shows current and 95th-percentile in the
   telemetry strip.
3. Log RTT samples to a CSV so Phase 1 and Phase 3 numbers can be compared like for like.
4. Agree a target now — a number to test against later, not after the fact.

**Verify:** the simulator's injected-latency switch moves the displayed figure by the amount
injected.

**Review gate:** I show the HUD with 0 ms, 50 ms, and 250 ms injected, plus a sample CSV, and
we agree the target. ⛔ Stop for approval.

---

### Step S.8 — Console: touch-target and layout pass
**Goal:** a console usable with a fingertip, sized for the real monitor.
**Owner:** 💻 · **Size:** M · **Depends on:** S.6

**Do:**
1. Minimum touch target ~15 mm; compute the pixel size from the actual screen dimensions
   (needs the display picked in 0.4 — otherwise use a stated assumption and revisit).
2. Reserve screen regions now for the Phase 2/3 controls (arm, mast) so adding them later
   isn't a redesign.
3. High-contrast palette that survives room lighting; test with the window at the real
   resolution.
4. Confirm multi-touch behaviour: one finger on drive, another on mast, without either
   sticking.

**Verify:** headless smoke run stays clean; a manual pass at target resolution.

**Review gate:** screenshots at the real resolution, plus the reserved-region map.
⛔ Stop for approval. **— End of Phase S —**

---

# Phase 0 — Approval & procurement

> **Storyboard exit criteria:** *budget and part list approved.*

Mostly your decisions; I can draft and calculate. Nothing here is ordered until you approve.

### Step 0.1 — Fix the sample spec
**Goal:** the single number that sizes the whole manipulator.
**Owner:** 📋 · **Size:** S · **Risk:** R2

**Do:** decide maximum rock **mass**, **width across the gripper**, and surface type (smooth
vs. irregular). Everything downstream — servo torque, gripper opening, arm reach, chassis
payload — is derived from this, so it is decided first and treated as fixed.

**Review gate:** written spec, one short table, agreed by you before 0.4 quotes anything.
⛔ Stop for approval.

---

### Step 0.2 — Chassis footprint vs. the 1.4 m turn, on paper
**Goal:** catch R5 before anything is bought.
**Owner:** 📋 · **Size:** M · **Depends on:** 0.1 · **Risk:** R5

**Do:**
1. For each candidate chassis, compute the swept circle of a skid-steer turn-in-place
   (diagonal across the wheelbase), not just the static footprint.
2. Compare against 1.4 m **minus** the terrain dressing depth at the walls — the usable width
   is narrower than the glass width.
3. Check the arm's reach envelope doesn't strike the glass at full extension while turning.

**Verify:** a dimensioned sketch with the numbers, per candidate.

**Review gate:** I show the sketch and the arithmetic; you pick the chassis or reject all.
⛔ Stop for approval.

---

### Step 0.3 — Power budget estimate and rail plan
**Goal:** an estimate to buy against, replaced by measurement in 1.7.
**Owner:** 📋 · **Size:** M · **Depends on:** 0.1 · **Risk:** R4

**Do:**
1. Tabulate worst-case current per subsystem from datasheets — note that servo **stall**
   current, not running current, sizes the rail (MG996R-class parts stall in the amps).
2. Plan **separate rails**: motors at pack voltage, servos on their own regulator, ESP32 and
   cameras on a clean 5 V/3.3 V rail, all sharing a common ground. Servos on the ESP32's
   regulator is the classic brownout-on-grip failure.
3. Size the LiPo for a full mission run plus margin; specify the fusing and the master cutoff.

**Review gate:** the rail diagram and the current table. ⛔ Stop for approval.

---

### Step 0.4 — Bill of materials and sourcing
**Goal:** the costed BOM the storyboard defers until this point.
**Owner:** 📋 · **Size:** L · **Depends on:** 0.1–0.3

**Do:** every part with quantity, unit cost, supplier, and lead time — chassis and motors,
H-bridge, servos and gripper, ESP32, both cameras, both monitors, touchscreen, power system,
arena materials, plus consumables and **spares for the parts most likely to fail** (servos,
motor driver). Flag long-lead items so they're ordered first.

**Review gate:** the BOM spreadsheet. **This is the storyboard's Phase 0 gate — budget and
part list approved.** ⛔ Stop for approval.

---

### Step 0.5 — Arena and Wi-Fi infrastructure plan
**Goal:** decide the RF setup before it's a problem in 1.2.
**Owner:** 📋 · **Size:** M · **Risk:** R6

**Do:**
1. Decide the network topology: rover as access point, or a dedicated router with everything
   as clients. A dedicated AP placed near the glass is usually the better bet with two camera
   streams in play.
2. Pick 2.4 GHz channels and check what else is using them in the room — the ESP32 is
   2.4 GHz-only, so it's sharing whatever's there.
3. Plan AP placement relative to the box, including where the rover sits at the **far** end of
   the 3 m run, which is the worst case.
4. Confirm the box's construction and lid, and where cabling passes through the seal.

**Review gate:** the network diagram and channel plan. ⛔ Stop for approval.

---

# Phase 1 — Drive platform bring-up

> **Storyboard exit criteria:** *rover drives forward/back/left/right reliably inside the
> 1.4 m arena width; battery draw measured.*

**Blocked until parts arrive.** Bring the board up before the chassis exists — a fault found
on the bench is far cheaper than one found inside an assembled rover.

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
channel reserved for servos later.

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
re-arm — it must not lurch back into motion on reconnect.

**Review gate:** coast distance for each failure mode. **If it doesn't stop, this is the one
gate we do not negotiate past.** ⛔ Stop for approval.

---

### Step 1.7 — Current draw per subsystem
**Goal:** replace the 0.3 estimate with measurements, closing R4.
**Owner:** 🔧 · **Size:** M · **Depends on:** 1.5 · **Risk:** R4

**Do:** measure idle, driving on sand, turning in place (the worst drive case), and stall;
log pack voltage over a sustained run to get real runtime; calibrate the
`BATTERY_DIVIDER_RATIO` constant against a meter so the telemetry reading is true.

**Verify:** measured runtime comfortably exceeds a full mission run, with margin for the
servos that aren't fitted yet.

**Review gate:** the measurement table and the calibrated divider constant. **This satisfies
"battery draw measured" in the storyboard's Phase 1 gate.** ⛔ Stop for approval.

---

### Step 1.8 — Control latency, idle link
**Goal:** the baseline R1 number, before video exists to compete with it.
**Owner:** 🔧💻 · **Size:** S · **Depends on:** 1.5, S.7

**Do:** run the S.7 latency HUD against the real rover in the box; capture several minutes of
RTT at the near and far ends; compare against the target agreed in S.7 and against the
simulator baseline.

**Review gate:** RTT distribution, real hardware vs. simulator. **This is the number Step 3.3
is measured against — keep the CSV.** ⛔ Stop for approval.
**— Phase 1 gate: all of 1.5, 1.6, 1.7 passed. —**

---

# Phase 2 — Manipulator integration

> **Storyboard exit criteria:** *arm reliably grips and lifts a representative sample rock
> without dropping it.*

### Step 2.1 — Separate servo power rail
**Goal:** stop the arm from browning out the ESP32 the first time it grips something.
**Owner:** 🔧 · **Size:** M · **Depends on:** 1.7, 0.3 · **Risk:** R4

**Do:** build the servo rail from 0.3 — its own regulator sized for **stall** current, bulk
capacitance close to the servos, common ground with the ESP32, and servo signal lines kept
away from the motor wiring.

**Verify:** stalling a servo deliberately does not reset the ESP32 and does not drop the link.

**Review gate:** scope or meter trace of the 3.3 V rail during a deliberate servo stall.
⛔ Stop for approval.

---

### Step 2.2 — Single servo sweep and travel limits
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 2.1

**Do:** add `ESP32Servo` to `lib_deps` (already noted as a commented-out entry in
`platformio.ini`); claim LEDC channels that don't collide with the drive channels; drive one
joint and find its **mechanical** limits by hand; record them as software limits so no
commanded angle can ever drive the joint into its own stop.

**Verify:** the joint tracks commanded angles and refuses out-of-limit commands.

**Review gate:** the per-joint limit table and a sweep video. ⛔ Stop for approval.

---

### Step 2.3 — Arm module with named poses
**Goal:** the operator commands *"deploy"*, not four joint angles.
**Owner:** 💻 · **Size:** L · **Depends on:** 2.2

**Do:** `firmware/lib/Arm/` — self-contained like `Drive`, pins via constructor; named poses
`STOW / DEPLOY / LOWER / LIFT / RELEASE` matching Scenes 6 and 8; **interpolated** motion
between poses rather than instant jumps, which spike current and shake the chassis; refuse
pose changes while in safe mode.

**Verify:** each pose is repeatable from any starting pose; native tests cover the limit
clamping.

**Review gate:** video of the pose sequence plus the state diagram. ⛔ Stop for approval.

---

### Step 2.4 — Gripper close, hold, release
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 2.3 · **Risk:** R2

**Do:** find the close position that grips the spec rock without stalling the servo
indefinitely; decide the hold strategy (commanded position vs. current-limited close) and
whether continuous holding torque is thermally acceptable over a full mission; report gripper
state in telemetry.

**Verify:** closes on the rock, holds it, releases cleanly, and the servo doesn't get hot over
a mission-length hold.

**Review gate:** hold test with a temperature check at the end. ⛔ Stop for approval.

---

### Step 2.5 — Load test against the spec rock
**Goal:** settle R2 — the risk most likely to sink Scene 6.
**Owner:** 🔧 · **Size:** M · **Depends on:** 2.4 · **Risk:** R2

**Do:** lift the 0.1 maximum-mass rock, from the sand, at full arm extension (the worst-case
moment); repeat 20 times and count failures; test an awkward irregular rock as well as a
convenient one; confirm the chassis doesn't tip at full extension.

**Verify:** 20 of 20 lifts, no tipping, no brownout.

**Review gate:** the tally and video of the worst-case lift. **This is the storyboard's
Phase 2 gate. A miss means new servos or a smaller sample spec — a 0.1 revision, not a
workaround.** ⛔ Stop for approval.

---

### Step 2.6 — Console arm controls
**Owner:** 💻 · **Size:** M · **Depends on:** 2.3

**Do:** arm panel in the region reserved in S.8 — deploy, grip/release, lift/lower as
storyboard §5.1 specifies; disable arm controls while driving and drive controls while the arm
is deployed, so the arm can't be dragged through the sand; show arm state and gripper state
live.

**Review gate:** live demo driving the arm from the console. ⛔ Stop for approval.

---

### Step 2.7 — Carry test
**Goal:** Scene 7 end to end — the sample survives the return drive.
**Owner:** 🔧 · **Size:** M · **Depends on:** 2.5, 2.6 · **Risk:** R2

**Do:** grip the rock and drive the full 3 m length of the arena including turns, over sand,
five times; if it drops, determine whether it's grip force, arm sag under vibration, or
chassis bounce, and fix the actual cause.

**Verify:** 5 of 5 runs with the sample retained.

**Review gate:** the tally and a full-length run video. ⛔ Stop for approval.
**— Phase 2 gate: 2.5 and 2.7 passed. —**

---

# Phase 3 — Vision & operator console

> **Storyboard exit criteria:** *operator can drive and steer using only the console and
> camera feeds, no direct line of sight.*

### Step 3.1 — One camera streaming to one monitor
**Owner:** 🔧 · **Size:** M · **Depends on:** 1.2 · **Risk:** R3

**Do:** mount the front camera low on the front as specified; stream to its own monitor over
the 0.5 network; find the resolution/framerate/quality point that keeps latency usable —
storyboard §5.3 explicitly trades resolution away to protect frame rate.

**Verify:** measure glass-to-glass latency by pointing the camera at a running stopwatch and
photographing both together.

**Review gate:** the latency figure and the chosen stream settings. ⛔ Stop for approval.

---

### Step 3.2 — Both cameras, both monitors
**Owner:** 🔧 · **Size:** M · **Depends on:** 3.1 · **Risk:** R3

**Do:** add the mast camera on its pole with its own monitor; re-measure **both** latencies
with both streaming — this is where the first real contention shows up; confirm each monitor
reliably lands on the correct feed at power-on, which matters for Scene 1.

**Verify:** both feeds smooth and simultaneously usable; latency still acceptable.

**Review gate:** both latency figures, single-stream vs. dual-stream. ⛔ Stop for approval.

---

### Step 3.3 — Concurrent load test
**Goal:** the moment R1 is either confirmed or dismissed — everything shares one 2.4 GHz band
for the first time.
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 3.2, 1.8 · **Risk:** **R1**

**Do:** run both camera streams at full rate while driving; capture RTT with the S.7 HUD;
compare directly against the 1.8 idle-link baseline; test at the far end of the arena, worst
case; attempt Scene 5-style fine alignment moves and judge whether they're actually
controllable, not merely connected.

**Verify:** control latency stays within the S.7 target with both feeds live.

**Review gate:** the two RTT distributions side by side, plus your hands-on verdict on fine
control. **If this fails, the fallbacks in priority order are: reduce video bitrate → move
video to 5 GHz if the cameras support it → Bluetooth control fallback per §4.3. We choose
together; we don't quietly proceed.** ⛔ Stop for approval.

---

### Step 3.4 — Mast pan/tilt under console control
**Owner:** 🔧💻 · **Size:** M · **Depends on:** 3.2, 2.2

**Do:** `firmware/lib/Mast/` on the same pattern as `Arm`; two servos, travel limited so the
head can't wrap its own camera cable; console pan/tilt controls in the reserved region;
confirm the pole doesn't oscillate visibly on the feed after a fast pan — Scene 3 depends on a
sweep being watchable.

**Verify:** full sweep left/right/up/down, settles quickly, no cable strain.

**Review gate:** a Scene 3-style survey sweep, watched on the mast monitor.
⛔ Stop for approval.

---

### Step 3.5 — Console final layout on the real touchscreen
**Owner:** 💻 · **Size:** L · **Depends on:** 3.4, 2.6

**Do:** run full-screen on the actual console hardware; retune touch targets to the real DPI;
lay the screen out around the storyboard's actual scene order rather than by subsystem; add
the Scene 1 pre-flight readiness check that gates the mission start; kiosk behaviour — no
window chrome, no accidental exit.

**Verify:** an operator who hasn't seen it before can find drive, mast, and arm unaided.

**Review gate:** the console running on the real screen; ideally someone unfamiliar tries it.
⛔ Stop for approval.

---

### Step 3.6 — Blind-driving acceptance
**Goal:** the storyboard's Phase 3 gate, exactly as written.
**Owner:** 🔧 · **Size:** M · **Depends on:** 3.5

**Do:** operator positioned with **no direct view** of the rover — screens only. Drive the
full length of the arena, turn, navigate around obstacle rocks, and return.

**Verify:** completed on camera feeds alone, without anyone peeking at the box.

**Review gate:** the run, performed with line of sight physically blocked. ⛔ Stop for
approval. **— Phase 3 gate. —**

---

# Phase 4 — Full mission integration

> **Storyboard exit criteria:** *a full mission run (Scenes 1–8) completes without manual
> intervention.*

### Step 4.1 — Arena dressing and base zone
**Owner:** 🔧 · **Size:** M · **Depends on:** 3.6

**Do:** dress the floor as Martian terrain; mark a clearly identifiable base zone with the
collection tray for Scene 8; scatter target rocks at realistic distances; re-check RSSI (1.2)
and driveability (1.5) with the **final** dressing in place, since both were measured earlier.

**Review gate:** photos of the dressed arena and the re-checked RSSI figures.
⛔ Stop for approval.

---

### Step 4.2 — Mission log and run state
**Owner:** 💻 · **Size:** M · **Depends on:** 3.5

**Do:** console-side mission state following the eight scenes; the sample counter Scene 8
requires ("1 sample collected"); a timestamped event log written to disk per run — this is
what makes a failed demo diagnosable afterwards; a reset-for-next-run action.

**Review gate:** a log file from a simulated run. ⛔ Stop for approval.

---

### Step 4.3 — Scene-by-scene rehearsal
**Goal:** run each of the eight scenes in isolation before attempting them in sequence.
**Owner:** 🔧 · **Size:** L · **Depends on:** 4.1, 4.2

**Do:** work through Scenes 1–8 one at a time, each against its own "success looks like" line
in storyboard §6; note every point where the operator had to improvise — those are either
console gaps or procedure gaps.

**Review gate:** a pass/fail line per scene, with notes. ⛔ Stop for approval — we fix the
gaps before 4.4 rather than during it.

---

### Step 4.4 — Full uninterrupted mission run
**Owner:** 🔧 · **Size:** M · **Depends on:** 4.3

**Do:** all eight scenes end to end, no resets, no hands in the box; run it three times to show
it's repeatable rather than lucky; record one run start to finish.

**Verify:** three consecutive complete runs.

**Review gate:** the recording. **This is the storyboard's Phase 4 gate and the project's
finish line.** ⛔ Stop for approval.

---

### Step 4.5 — Handover documentation
**Owner:** 💻 · **Size:** M · **Depends on:** 4.4

**Do:** operator guide (power-on through mission run through shutdown); wiring diagram and pin
map as actually built; troubleshooting for the failures seen during bring-up; the measured
figures — RSSI, latency, current, runtime — collected in one place; update `CLAUDE.md` and
the storyboard so it describes what was **built**, not what was proposed.

**Review gate:** the document set. ⛔ Stop for approval.

---

## Deliberate ordering choices

A few places where this plan's sequence is a decision rather than an obvious consequence:

- **Software before hardware (Phase S).** The console is the piece most likely to need
  iteration and the piece least dependent on parts. Building it against a simulator means
  Phase 1 tests the *hardware*, not the software and hardware at once.
- **RF survey (1.2) before motors (1.3).** R6 could invalidate the network design. Finding
  that out with a bare board on the bench is cheap; finding it out with an assembled rover is
  not.
- **Failsafe proving (1.6) before anything is carried.** A rover that can't stop itself is
  hazardous to the glass, the terrain, and eventually the arm.
- **Latency baseline (1.8) before video exists (3.3).** Without the earlier number, the later
  measurement has nothing to be compared to and R1 stays a matter of opinion.
- **Load test (2.5) before console arm polish (2.6).** If the servos can't lift the spec rock,
  the arm UI is wasted work until the spec or the servos change.
- **Scene rehearsal (4.3) before the full run (4.4).** Debugging one scene is tractable;
  debugging an eight-scene sequence that fails somewhere in the middle is not.
