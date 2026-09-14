# Mars Rover — RFID Exploration

**Educational robotics project · Simulated Mars geological survey mission**
**Proposal · Revision 2 · Awaiting approval**
Status: Design & Planning Stage · Prepared for internal review · 2026-07-27
**Supersedes:** the Sample-Return concept ([storyboard.md](storyboard.md) / `storyboard.docx`)

> Markdown transcription of [storyboard-exploration.docx](storyboard-exploration.docx), made
> so the authoritative proposal can be diffed and edited in the repository.
> [storyboard-exploration-th.docx](storyboard-exploration-th.docx) is a Thai translation of
> the same revision. **Edit this file** when the proposal changes, and re-export the Word
> copies from it.

---

## 0. Executive Summary

Following discussion with the client, the mission has been revised. The rover no longer
collects rock samples and physically carries them back to base — instead it explores the
terrain, and identifies rocks of interest by reading a passive RFID tag embedded inside each
one. When the onboard antenna picks up a tag, the operator console displays a simulated
elemental composition analysis of that rock, mirroring how instruments on real Mars rovers
(e.g. APXS or PIXL on Curiosity and Perseverance) report a rock's composition without needing
to retrieve it. **The robotic arm is removed entirely** from this design — there is nothing
left to physically pick up.

This document is a companion storyboard to the original Sample-Return proposal, updated for
the new mission. **No hardware has been procured or built yet** for either version —
everything below is a target design. It covers what the finished system is meant to do
(Sections 1–3), the revised hardware and software (Sections 4–5, including the arm's removal
and the new RFID sensing path), the mission experience scene by scene (Section 6), a phased
build plan (Section 7), the technical risks specific to RFID sensing (Section 8), the skills
the project exercises (Section 9), and what is needed to proceed (Section 10). As before, no
costed bill of materials or dated schedule is included — those follow once scope is approved.

**The ask** is unchanged from the original proposal: approval to proceed to Phase 0 —
procurement and detailed costing — described in Section 7, now against this revised scope.

---

## 1. The Pitch in One Line

A four-wheeled Mars rover, sized to sit on a desk, lives inside a sealed 1.4 m × 3 m glass
terrarium dressed as the Martian surface. From a touchscreen mission console outside the box,
an operator drives it downrange, hunts for rocks of interest through a single pole-mounted,
pan/tilt camera feed, and sweeps a front-mounted RFID antenna over each candidate. Every rock
has a passive RFID tag sealed inside it; when the antenna reads a tag, the console instantly
displays that rock's simulated elemental composition — no picking up, no carrying, no
returning to base required. It is a complete "seek → scan → identify" mission loop that can
repeat across as many rocks as the operator wants to survey in a session — small enough to sit
on a bench, and real enough to exercise mechanics, embedded firmware, wireless control, RF
sensing, and a purpose-built operator UI.

---

## 2. Why This Revision

The mission changed after discussion with the client; this section explains what changed and
why it still holds up as a strong project scope.

- **Closer to how real rovers actually work.** Curiosity and Perseverance do not grab most of
  the rocks they study — instruments like APXS and PIXL read composition in place, from a
  short standoff distance. Swapping a gripping arm for a sensing antenna makes the simulated
  mission more representative of real planetary science operations, not less.
- **Fewer moving parts, fewer failure points.** Removing the arm eliminates the multi-servo
  manipulator, its torque/payload sizing problem, and the "will it drop the sample" failure
  mode entirely (this was Risk R2 in the original proposal). The mechanical scope shrinks to
  the drive platform and the mast pan/tilt.
- **A new, genuinely interesting sensing problem.** Reading a passive RFID tag reliably from a
  moving platform, embedded in an irregular rock, at an unknown orientation, is its own real
  engineering problem — signal strength, antenna placement, and read range now matter in a way
  they did not before. This trades a mechanical challenge (the arm) for an RF/sensing one,
  which is arguably a better fit for a project meant to showcase a broad engineering skill set.
- **The core project rationale from the original proposal still applies.** Contained scope
  inside the sealed 1.4 m × 3.0 m box, low-voltage and low-risk, a full stack in a small
  package, reusable subsystems, and a clear, visible finish line — see
  [§2 of the original storyboard](storyboard.md) for the full argument, which is unchanged by
  this revision.

---

## 3. Mission Parameters — Target Specification

*Subject to refinement once Phase 0/1 (see Section 7) confirms real part dimensions and
performance. Rows that changed from the original Sample-Return specification are marked.*

| Parameter | Target value |
|---|---|
| Arena | Sealed glass box, **1.4 m wide × 3.0 m long** |
| Rover drive | **4 wheels** — driven rear axle, **steered front axle** (car-style, not tank-style) *(changed)* |
| Manipulator | **None — removed in this revision** *(changed)* |
| Sensing | **RFID reader + antenna, front-mounted** *(new)* |
| Tagged samples | **Passive RFID tag sealed inside each rock** *(new)* |
| Cameras | **1** — mast-mounted, pan/tilt (drive + survey) *(changed)* |
| Displays | **1 monitor**, mast camera feed, over Wi-Fi *(changed)* |
| Console | **1 touchscreen** running the controller + analysis app |
| Onboard brain | **ESP32** (Wi-Fi + Bluetooth) |
| Mission goal | Locate rocks, read embedded RFID tag, log elemental composition *(changed)* |

---

## 4. Hardware Specification — Proposed

*Sourcing status: the chassis kit, its motors and the motor driver have been selected and
bench-tested; everything else below is a proposed part class, pending the approval described in
Section 10.*

### 4.1 Rover platform *(revised — steered chassis)*
- **Chassis:** a **steered** rover — a driven rear axle and a steering front axle — sized to
  operate within the 1.4 m box width, leaving clearance to turn.
- **Drive:** **two DC geared motors on the rear axle**, driven together, plus **one motor that
  swings the front axle** to steer, all through a dual H-bridge motor driver (a DRV8833).
  Turning is by steering angle, not by wheel-speed difference.
- *Why this changed:* the earlier draft proposed a 4-wheel skid-steer platform, which turns by
  spinning its wheels at different speeds. The chassis selected instead steers like a car. The
  practical differences for the operator are that **the rover cannot spin on the spot** — it
  needs forward or backward motion to change direction, and has a minimum turning circle — and
  that **steering is full-lock or straight**, with a spring returning the wheels to centre, so
  fine heading corrections are made with brief taps rather than a held input. Both are
  accounted for in the console design (Section 5) and in Risk R5 (Section 8).
- *A safety benefit that came with it:* because the steering springs back to centre whenever it
  is unpowered, a rover that loses its link mid-turn **coasts straight rather than continuing
  to arc**. This has been confirmed on the bench.
- **No manipulator in this revision:** the robotic arm from the original design is removed.
  The front mounting point previously reserved for the arm now carries the RFID antenna
  (Section 4.3) instead.

### 4.2 Vision *(revised — single camera)*
- **Mast camera** — mounted on a vertical pole, on a **pan/tilt head**, so the operator can
  look left / right / up / down to survey the field, and aim it forward and down to drive. The
  front camera from the earlier design has been removed; the mast camera is now the rover's
  only camera.
- **Four small servos** carry that movement: a pan/tilt pair at the mast and a second pan/tilt
  pair at the camera itself.
- *Why a single camera:* dropping the front camera cuts part count and wiring, and — more
  importantly — halves the video bandwidth competing with the control channel over Wi-Fi (see
  the revised Risk R1 in Section 8). The trade-off is that the operator now time-shares one
  view between driving, surveying, and lining up the RFID sweep, instead of having a dedicated
  forward-facing feed available at all times (see Risk R8).
- Still an **IP camera** streaming over Wi-Fi (an ESP32-CAM module or a dedicated IP cam).

### 4.3 RFID sensing *(new)*
- **Reader & antenna:** a **UHF RFID reader module (860–960 MHz band)** with its antenna
  mounted low on the front of the rover, facing the ground ahead — the position formerly
  occupied by the arm.
- **Tags:** small **passive UHF RFID tags**, sealed inside each rock during arena preparation.
  Passive tags need no battery and nothing to maintain, which matters because the tags are
  permanently embedded and unreachable once a rock is placed in the arena.
- *Why UHF rather than a short-range (NFC/HF) tag:* UHF gives a workable read range — roughly
  tens of centimetres up to a couple of metres, tunable by antenna and reader power — instead
  of requiring near-contact. That range is what makes a "sweep and detect" gameplay loop
  possible without needing millimetre-precise alignment, and it can double as a coarse
  proximity cue (via signal strength) while the operator is still approaching a rock, not only
  once they are on top of it.
- **Signal strength (RSSI):** the reader reports a signal-strength value per read, which the
  console can show as a live meter — the closer and better-aligned the antenna is to a tag,
  the stronger the signal, giving the operator real-time feedback while closing in (see
  Scene 5 in Section 6).

### 4.4 Compute & communications
- **Main board:** ESP32 (dual-core, integrated **Wi-Fi + Bluetooth**). Runs motor and
  mast-servo control loops, polls the RFID reader over UART, and handles console commands.
- *Why ESP32:* Wi-Fi and Bluetooth are built into the chip, it is inexpensive, and it has
  enough spare UART/GPIO capacity to add the RFID reader alongside the motor and servo control
  it was already doing in the original design.
- **Link:** Wi-Fi is the primary control + video channel; Bluetooth is available as a fallback
  control link if Wi-Fi is degraded (see Risk R1 in Section 8).

### 4.5 Power
Onboard **LiPo battery pack** with a buck regulator supplying 5 V / 3.3 V rails for the ESP32,
mast servos, camera module, and the RFID reader. Motor bus fed at pack voltage through the
driver. Exact capacity to be sized from measured current draw in Phase 1 (Section 7); the RFID
reader adds a modest, but non-zero, load to that budget compared to the original design.

### 4.6 The arena
Sealed **glass box, 1.4 m × 3.0 m**, floor dressed with sand and scattered rocks to simulate
Martian terrain. Each rock has a passive RFID tag sealed inside during preparation — since the
tags are unpowered and have no moving parts, they need no maintenance once embedded. The rover
lives **inside**; the operator station sits **outside** the glass. Glass is largely transparent
to both Wi-Fi and UHF RFID frequencies (unlike a metal enclosure), but signal strength through
the box is a checkpoint to verify for both — not assume (see Risk R6 in Section 8).

---

## 5. Software Specification — Proposed

### 5.1 Controller application (touchscreen console)
Built in **Godot**, running full-screen on the touchscreen monitor, extending the original
console rather than replacing it.

- **Drive pad** — forward / backward / left / right.
- **Mast camera** — pan left/right, tilt up/down. Since the front camera is removed, this feed
  also serves as the forward driving view whenever it is aimed ahead.
- **Signal meter** *(new)* — a live RFID signal-strength indicator, active while the antenna is
  sweeping, to help the operator home in on a tag.
- **Analysis panel** *(new)* — replaces the old arm controls. Shows the elemental composition
  readout once a tag is successfully read, plus a running session log of every rock identified
  so far.
- **Telemetry strip** — link status, battery, current mode.

Sends commands to the rover over **Wi-Fi (WebSocket/UDP)**, with **Bluetooth** fallback. The
arm deploy/grip/lift controls from the original design are removed entirely.

### 5.2 Rover firmware (ESP32)
- Connects to the console link, parses incoming drive/camera commands, and drives the motor
  H-bridge and mast servos.
- Continuously polls the RFID reader; when a tag is in range, forwards its unique ID and signal
  strength to the console. Arm servo control from the original design is removed.
- Publishes telemetry (link state, battery, RFID reader status) back to the console.

### 5.3 Video path *(revised — single stream)*
With the front camera removed, there is now a single video path: the mast camera streams over
Wi-Fi to its one monitor. Video still stays off the control channel so driving stays
responsive, and this single-stream design now uses roughly half the Wi-Fi video bandwidth the
original two-camera design did, easing pressure on the shared 2.4 GHz link (see Risk R1).

### 5.4 Composition database & analysis display *(new)*
Each RFID tag's unique ID is matched against a small **lookup table** (stored on the console,
editable per arena setup) that maps that ID to a preset, simulated elemental composition
profile — e.g. relative percentages of iron, silicon, magnesium, calcium, oxygen, and trace
elements, plus a mineral-class label.

This is a **deliberate simulation, not a real spectrometer**: it stands in for instruments like
APXS or PIXL, which is the honest and appropriate scope for a bench-top educational build,
while still teaching the real operational pattern — detect target, read instrument, interpret
result, log finding.

On a successful read, the console's analysis panel (Section 5.1) renders the composition as a
simple report card and appends it to the session log, tagged with which rock (by tag ID) it
came from.

### 5.5 System signal flow

```
[Touchscreen Console · Godot]
        │  drive / mast commands  (Wi-Fi WebSocket, BT fallback)
        ▼
   [ESP32 on rover] ── motors (H-bridge ×4)
        │            ── mast pan/tilt servos
        │            ── RFID reader + antenna ──reads──► [Passive tag in rock]
        └── telemetry (link, battery, RFID status) ──► back to console

[Mast IP cam] ──Wi-Fi──► [Monitor]

[RFID tag ID] ──► [Composition lookup table] ──► [Elemental analysis panel on console]
```

The control/video separation from the original design is unchanged and still the key
architectural decision for responsiveness. What is new is the **RFID data path**: tag reads
travel from the reader through the ESP32 to the console alongside telemetry, and the console —
not the rover — is responsible for turning a tag ID into a displayed composition, keeping that
lookup logic easy to edit without re-flashing the rover.

---

## 6. The Storyboard — Target Mission Experience

*This section describes the mission experience the system is being designed to deliver. It is
a design target, walked through scene by scene — not a report of a demonstrated run. Unlike
the original Sample-Return storyboard, this loop is intended to repeat: after Scene 8 the
operator can return to Scene 3 and survey for the next rock in the same session.*

The mission is told in eight scenes. The rover starts at **base** (near the operator, one end
of the box), searches downrange, and — because nothing needs to be carried back — can keep
exploring for as many rocks as the session calls for.

### Scene 1 — Power-Up & Handshake
**Objective:** confirm every subsystem is alive before the mission starts.
**What happens:** the rover sits in the base zone, powered off. The operator powers it on; the
ESP32 boots and joins Wi-Fi, the console shows **LINK ESTABLISHED**, the mast camera feed comes
up on its monitor, and the RFID reader reports ready.
**Systems engaged:** ESP32 boot · Wi-Fi handshake · RFID reader ready. **Feed:** mast camera
online.
**Success looks like:** drive, mast, and RFID reader all report green before the operator is
allowed to proceed to Scene 2.

### Scene 2 — Deploy from Base
**Objective:** move the rover from base onto open terrain.
**What happens:** the operator taps **forward** on the drive pad. The rover rolls out of the
base zone; the mast camera, aimed forward and down, shows the sand and rocks ahead.
**Controller actions:** forward. **Feed in focus:** mast camera (forward view).

### Scene 3 — Survey the Field
**Objective:** find a candidate rock without having to drive blindly across the whole arena.
**What happens:** the rover advances and pauses. Using the mast camera's pan/tilt, the operator
sweeps **left, right, up, down**, scanning the terrain visually for an interesting-looking rock.
**Controller actions:** mast pan/tilt. **Feed in focus:** mast camera.

### Scene 4 — Target Acquired
**Objective:** lock in a visual target and orient the rover toward it.
**What happens:** on the mast feed, a promising rock comes into view. The operator marks it as
the target and lines the rover up toward it. No RFID signal has been detected yet at this range.
**Controller actions:** steer to bearing. **Feed in focus:** mast camera.

### Scene 5 — Approach & Sweep
**Objective:** bring the front-mounted antenna within RFID read range of the rock.
**What happens:** keeping the mast camera aimed forward and down, the operator drives in small
steps toward the rock, watching the console's signal meter climb as the antenna gets closer and
better aligned — a live "getting warmer" cue that replaces the old fine gripper-alignment step.
**Controller actions:** fine drive. **Feed in focus:** mast camera + signal meter.

### Scene 6 — Signal Acquired
**Objective:** get a clean, reliable read of the embedded tag.
**What happens:** the signal meter locks solid and the console reports **TAG DETECTED** with
the tag's unique ID. The RFID reader has successfully read the passive tag sealed inside the
rock.
**Systems engaged:** RFID reader · tag lock. **Feed in focus:** mast camera + signal meter.
**Success looks like:** a stable read with a consistent tag ID, not an intermittent or dropped
signal — this is the load-bearing test of the sensing design (see Risk R2).

### Scene 7 — Elemental Composition Readout
**Objective:** deliver the payoff moment: what is this rock made of?
**What happens:** the console's analysis panel opens automatically and displays the simulated
elemental composition for the identified rock — a breakdown by element with a mineral-class
label — exactly as described in the mission brief.
**Systems engaged:** composition lookup. **Feed in focus:** analysis panel.

### Scene 8 — Log Sample & Continue
**Objective:** record the finding and free the operator to keep exploring.
**What happens:** the analysis result is appended to the session log as a sample entry. Because
nothing physical needs to be carried anywhere, the rover is immediately free to resume the
survey — the operator can return to Scene 3 and search for the next rock, repeating the loop
for as many samples as the session calls for. At the end of a session, the rover can simply
drive back to base for a full field-report debrief.
**Systems engaged:** session log updated. **Outcome:** sample identified, loop repeats.

---

## 7. Build Plan — Phased Approach

The phase structure from the original proposal still applies; **Phase 2 is retargeted from
manipulator integration to RFID sensing integration**. No dates or costs are attached yet —
each phase ends with a checkpoint review before work (and spend) continues into the next one.

| Phase | Goal | Key deliverable | Exit criteria to proceed |
|---|---|---|---|
| **0 — Approval & Procurement** | Turn this revised proposal into a costed, scheduled plan | Detailed bill of materials (incl. RFID reader, antenna, and tags), sourcing plan, confirmed arena dimensions | Budget and part list approved |
| **1 — Drive Platform Bring-up** | Prove the chassis moves and is controllable | Chassis + 4-motor drive under ESP32 control, driven manually over Wi-Fi | Rover drives F/B/L/R reliably inside the 1.4 m arena width; battery draw measured |
| **2 — RFID Sensing Integration** *(changed)* | Prove the antenna reliably reads a tag embedded in a representative rock | RFID reader + antenna mounted and wired to the ESP32; test tags embedded in sample rocks | Reader gets a clean, repeatable read at a usable approach distance, across multiple rock/tag orientations |
| **3 — Vision & Operator Console** | Give the operator eyes, a signal meter, and an analysis panel | The mast IP camera streaming to its monitor; Godot console driving the rover live using the mast camera's forward-aimed view, showing signal strength and composition readouts | Operator can drive, steer, and read a tag using only the console and the single camera feed, no direct sightline |
| **4 — Full Mission Integration & Demo** | Run the complete 8-scene explore-and-scan loop end to end, repeatably | Working "seek → scan → identify" demonstration across multiple rocks in one session | A full mission run (Scenes 1–8) completes for at least two different tagged rocks without manual intervention |

Each phase's exit criteria is the go/no-go gate for the next — if a phase doesn't clear its
bar, the plan gets revisited rather than compounding the problem downstream.

---

## 8. Risks & Open Questions

Some risks carry over unchanged from the original proposal; the manipulator risk
(torque/payload) is gone, replaced by risks specific to RFID sensing.

| # | Risk | Why it matters | How we plan to de-risk it |
|---|---|---|---|
| R1 | Wi-Fi control latency under load — the video stream and the control channel share the same 2.4 GHz link *(eased — now one video stream, not two)* | Driving could feel laggy right when precision matters most (Scenes 4–5) | Control and video are architecturally separate channels (§5.5); removing the front camera roughly halves the video bandwidth competing for the link compared to the two-camera design; Bluetooth is a fallback control link; latency gets tested as soon as Phase 1 hardware exists |
| R2 | **RFID read reliability** — will the antenna get a clean, repeatable read of a tag embedded inside an irregular rock at an unpredictable angle? *(changed)* | If reads are intermittent or missed, Scene 6 (the core "detect" step) becomes frustrating or unreliable, undermining the whole mission loop | Choose tags and antenna gain with margin above the minimum needed range; test against several rock sizes/orientations in Phase 2 before moving to Phase 3; use signal strength (not just a bare pass/fail read) to give the operator feedback while still approaching |
| R3 | Camera latency/framerate over Wi-Fi | Choppy or delayed video makes the visual approach (Scene 5) hard to judge — and with the front camera removed, the mast camera is now the rover's only source of visual feedback, raising the stakes of it being reliable | Keep resolution modest to protect frame rate; verify the feed runs smoothly during Phase 3, before the console is considered done |
| R4 | Power budget / battery runtime for an **extended, repeatable survey session** | Because the mission can now repeat for many rocks in one session rather than ending after one collect-and-return, total runtime demands may be higher than the original design assumed | Measure actual current draw per subsystem (including the RFID reader) in Phase 1; size the LiPo pack with margin for a realistic multi-rock session, once real numbers exist |
| R5 | Mechanical clearance to turn inside a 1.4 m-wide arena *(sharpened — the chassis selected is steered, not skid-steer)* | The rover **cannot turn in place at all**; it has a minimum turning circle, and if that circle is wider than the arena, turning round takes a three-point turn against the glass rather than a single move | Measure the actual turning circle at full lock against the drivable width early in Phase 1, before the arena is dressed; the mission is free-drive, so a three-point turn is acceptable if it comes to that, but it must be known rather than discovered |
| R6 | RF performance through the glass enclosure, **for both Wi-Fi and UHF RFID** *(expanded)* | Signal could be weaker than expected for either link if the box or its dressing (sand, rocks) attenuates it more than assumed | Glass is largely RF-transparent compared to a metal enclosure for both bands, but this is a checkpoint to *verify* for both Wi-Fi and RFID in Phase 1/2, not an assumption to build on |
| R7 | **Tag survivability during rock preparation** *(new)* | If tags are damaged by heat, pressure, or adhesive while being sealed inside rocks, they will simply never be read, and the failure is invisible until Phase 2 testing | Confirm the tag embedding process (adhesive/epoxy type, cure temperature) against the tag manufacturer's tolerances before embedding the full batch of arena rocks; test-read every tag immediately after embedding, not just before |
| R8 | **Single point of vision** *(new)* — with the front camera removed, the mast camera is the rover's only eyes, and the operator must repoint it between a forward driving view and a survey/scan view rather than having both at once | Could make fine positioning in Scene 5 harder (driving toward a target while also wanting a wider view), and if the mast camera or its pan/tilt mechanism fails, the rover has no vision at all — there is no second camera to fall back on | Default the mast camera to a forward-and-down "driving" position between surveys so the operator is not blind while approaching a target; treat mast pan/tilt reliability as higher priority than in the original two-camera design; validate the forward-tilt driving view specifically during Phase 3 |

---

## 9. What This Demonstrates

| Discipline | What the build exercises |
|---|---|
| **Mechanical & mechatronics** | Multi-motor drive platform and a pan/tilt mast working together — narrower scope than the original manipulator-equipped design, traded for depth elsewhere |
| **Embedded firmware** | Real-time motor/servo control on an ESP32, with wireless command handling, telemetry reporting, and a UART sensor integration (RFID reader) |
| **RF sensing & instrumentation** *(new)* | Reading a passive RFID tag reliably from a moving platform, interpreting signal strength as a proximity cue, and treating a sensor read as a triggered event with downstream data to display |
| **Wireless systems & networking** | Deliberately separating a low-latency control channel from the camera video stream, plus a telemetry/sensor data path carrying tag reads |
| **Human–machine interface** | A purpose-built touchscreen operator console (Godot), including a live signal meter and a data-presentation panel for the composition readout — UI that has to communicate sensor state, not just accept commands |
| **Systems integration & project management** | Mechanics, electronics, firmware, networking, sensing, and UI brought together into one working, repeatable mission loop, delivered through staged, checkpointed phases |

---

## 10. What's Needed to Proceed

This is the ask referenced in the Executive Summary: approval to move this revised project
scope out of the design stage.

- **Approval to enter Phase 0** (Section 7) — procurement and detailed costing, against this
  revised scope.
- **Engineering time**, allocated roughly across four areas: mechanical assembly (chassis +
  mast only now), embedded firmware (including RFID integration), console/UI development
  (signal meter + analysis panel), and integration & test.
- **Hardware procurement**, by category — chassis and drive components, compute and wireless
  module (ESP32), a mast-mounted camera module, RFID reader module + antenna + a batch of
  passive tags, display/console hardware, power system, and arena materials. The
  manipulator/servo kit from the original proposal is no longer needed. A fully costed bill of
  materials will follow once this scope is approved — none is attached here.
- **Bench/desk workspace** for build and testing. Given the compact, low-voltage, fully
  enclosed design, no special facility or safety sign-off is anticipated.
- **A checkpoint review at the end of each phase** in Section 7, before work proceeds (and any
  further spend is committed) to the next one.

---

## 11. Glossary — Plain-Language Reference

For reviewers who don't work with embedded robotics or RF sensing day to day. Terms unchanged
from the original proposal are repeated here for convenience.

| Term | What it means |
|---|---|
| **ESP32** | A small, inexpensive computer chip with Wi-Fi and Bluetooth built in — the "brain" that controls the rover's motors and sensors. |
| **H-bridge** | An electronic switch circuit that lets a small computer control a motor's direction and speed. |
| **Differential (skid) steering** | Turning by spinning the left and right wheels at different speeds — like a tank. This is what the earlier draft proposed; **this design does not use it.** |
| **Steered chassis** *(changed)* | Turning by angling the front wheels, like a car. The rover must be moving to change direction, and has a minimum turning circle it cannot turn inside. |
| **Servo** | A small motor that moves to a precise commanded angle; used here for the mast and camera pan/tilt heads. |
| **RFID** *(new)* | Radio-Frequency IDentification — a way to read a small chip's unique ID wirelessly using radio waves, without needing a battery in the chip. |
| **Passive RFID tag** *(new)* | A tiny chip-and-antenna sticker or capsule with no battery of its own — it draws its power from the reader's radio signal, which is why it can be sealed inside a rock indefinitely with nothing to maintain. |
| **UHF** *(new)* | Ultra-High Frequency — the radio band (roughly 860–960 MHz) used by the RFID reader in this design, chosen because it gives more usable read range than short-range (NFC-style) tags. |
| **RSSI** *(new)* | Received Signal Strength Indicator — a number the RFID reader reports alongside a tag read, showing how strong the signal is; used here to give the operator a live "getting warmer" cue while approaching a rock. |
| **Elemental composition analysis** *(new)* | A breakdown of what chemical elements (iron, silicon, etc.) a rock is made of. Here it is a simulated readout looked up from the tag ID, standing in for a real instrument like the spectrometers carried by actual Mars rovers. |
| **LiPo battery** | Lithium-polymer battery — a common lightweight, rechargeable battery type used in small robots and drones. |
| **IP camera** | A camera that sends video over a network (Wi-Fi) instead of a direct cable, so it can stream to a screen elsewhere. |
| **Pan/tilt** | A mount that swivels left-right (pan) and up-down (tilt), like a security camera head. |
| **WebSocket** | A way of sending small messages back and forth over Wi-Fi with very low delay — used here to carry drive commands. |
| **Telemetry** | Status data (battery level, connection state, RFID reader status, etc.) sent back from the rover to the operator console. |

---

*Mars Rover · RFID Exploration — Project Storyboard & Proposal, Revision 2 · Design & Planning
Stage · 2026-07-27*
