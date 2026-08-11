# Mars Rover — Project Storyboard & Proposal

**Educational robotics project · Simulated Mars sample-collection mission**
**Status: Design & Planning Stage** — proposal for internal review and approval
Prepared for employer/sponsor review · 2026-07-21 · originally drafted 2026-07-15

---

## 0. Executive Summary

This document proposes building a desktop-scale Mars rover: a four-wheeled vehicle that
lives inside a sealed glass terrarium dressed as Martian terrain, driven from an outside
touchscreen console to find a rock, pick it up with a robotic arm, and bring it back to a
base zone. **No hardware has been procured or built yet** — everything below is a target
design, not a report of working equipment. The rest of this document lays out: what the
finished system is meant to do (Sections 1–3), the hardware and software proposed to build
it (Sections 4–5), the mission experience it's designed to deliver, scene by scene (Section
6), a phased build plan with go/no-go checkpoints (Section 7), the technical risks we
already know about and how we intend to de-risk them (Section 8), the skills and
engineering disciplines the project exercises (Section 9), and what we're asking for to
move from paper design to a working bench-top demonstrator (Section 10). A costed bill of
materials and a dated schedule are intentionally **not** included here — those follow once
the scope below is approved.

**The ask:** approval to proceed to Phase 0 — procurement and detailed costing — described
in Section 7.

---

## 1. The Pitch in One Line

A four-wheeled Mars rover, sized to sit on a desk, lives inside a sealed 1.4 m × 3 m glass
terrarium dressed as the Martian surface. From a touchscreen mission console outside the
box, an operator drives it downrange, hunts for rocks of interest through two live camera
feeds, picks a target up with a robotic arm, and carries the sample home to base. It is a
complete "seek → collect → return" mission loop — small enough to sit on a bench and real
enough to exercise every layer of a robotics stack: mechanics, embedded firmware, wireless
control, computer-controlled cameras, and a purpose-built operator UI.

---

## 2. Why This Project

A rover build sounds like a big undertaking; this one is deliberately scoped to stay small
while still being a genuine, end-to-end engineering exercise.

- **Contained scope, contained risk.** The whole system fits on a bench inside a sealed
  1.4 m × 3.0 m glass box. Low-voltage electronics, low-speed motors, no exposed moving
  parts near the operator — no dedicated lab or special safety sign-off should be needed.
- **Full stack, small package.** One build touches mechanical design, embedded firmware,
  wireless networking, camera/video handling, and a custom operator interface — a compact
  way to exercise (and demonstrate) the same disciplines used on larger robotics projects.
- **Reusable subsystems.** The drive platform, the arm, the camera/vision path, and the
  console app are each self-contained. Any one of them can be lifted out and reused in a
  future prototype — this isn't a single-purpose build that gets shelved when the demo ends.
- **A clear, visible finish line.** Success is a working "seek → collect → return" loop
  that anyone can watch happen in under a few minutes — an unambiguous go/no-go milestone,
  not an open-ended research effort.

---

## 3. Mission Parameters — Target Specification

*Subject to refinement once Phase 0/1 (see Section 7) confirms real part dimensions and
performance.*

| Parameter | Target value |
|---|---|
| Arena | Sealed glass box, **1.4 m wide × 3.0 m long** |
| Rover drive | **4 wheels**, independent/differential steering |
| Manipulator | **1 robotic arm** with gripper (front-mounted) |
| Cameras | **2** — front (drive) + mast (survey, pan/tilt) |
| Displays | **2 monitors**, one per IP camera, over Wi-Fi |
| Console | **1 touchscreen** running the controller app |
| Onboard brain | **ESP32** (Wi-Fi + Bluetooth) |
| Mission goal | Locate rock(s), collect, return to base |

---

## 4. Hardware Specification — Proposed

*Sourcing status: nothing procured yet — the parts and part classes below are the proposed
build, pending the approval described in Section 10.*

### 4.1 Rover platform
- **Chassis:** 4-wheel-drive rover sized to operate within the 1.4 m box width, leaving
  clearance to turn.
- **Drive:** 4 × DC geared motors driven through a dual H-bridge motor driver
  (e.g. TB6612FNG or L298N). Steering is differential (skid-steer): left/right wheel
  speed difference produces turns — forward, backward, left, right.
  *Why differential steering:* it needs no separate steering mechanism — fewer moving
  parts, fewer failure points, and it's the simplest way to turn a 4-wheel platform in a
  tight arena.
- **Robotic arm:** front-mounted articulated arm, **3–4 servo joints + gripper**
  (e.g. MG996R / SG90-class servos). Reach clears the front bumper so the gripper can
  close on a rock resting on the surface, lift it, and hold it during transit.

### 4.2 Vision
- **Front camera** — mounted low on the front of the rover, used for driving and for
  fine alignment when approaching a rock.
- **Mast camera** — mounted on a vertical pole, on a **pan/tilt head (2 servos)** so the
  operator can look **left / right / up / down** to survey the field.
- Both are **IP cameras** streaming over Wi-Fi (ESP32-CAM modules or dedicated IP cams).

### 4.3 Compute & communications
- **Main board:** ESP32 (dual-core, integrated **Wi-Fi + Bluetooth**). Runs motor and
  servo control loops and receives commands from the console.
  *Why ESP32:* Wi-Fi and Bluetooth are built into the chip, it's inexpensive, and it's
  fast enough to run motor/servo control loops and handle wireless commands at once —
  no separate radio module needed.
- **Link:** Wi-Fi is the primary control + video channel; Bluetooth is available as a
  fallback control link if Wi-Fi is degraded (see Risk R1 in Section 8).

### 4.4 Power
- Onboard **LiPo battery pack** with a buck regulator supplying 5 V / 3.3 V rails for the
  ESP32, servos, and camera modules. Motor bus fed at pack voltage through the driver.
  Exact capacity to be sized from measured current draw in Phase 1 (Section 7).

### 4.5 The arena
- Sealed **glass box, 1.4 m × 3.0 m**, floor dressed with sand and scattered rocks to
  simulate Martian terrain. The rover lives **inside**; the operator station sits
  **outside** the glass. Glass is largely transparent to Wi-Fi (unlike a metal
  enclosure), but signal strength through the box is a checkpoint to verify — not assume
  (see Risk R6 in Section 8).

---

## 5. Software Specification — Proposed

### 5.1 Controller application (touchscreen console)
- Built in **Godot**, running full-screen on the touchscreen monitor. Godot was chosen
  because it gives a fast path to a responsive, custom touch UI without building a UI
  toolkit from scratch, and it runs the same on the eventual console hardware regardless
  of the underlying OS.
- On-screen controls:
  - **Drive pad** — forward / backward / left / right.
  - **Mast camera** — pan left/right, tilt up/down.
  - **Arm** — deploy, grip / release, lift / lower.
  - **Telemetry strip** — link status, battery, current mode.
- Sends commands to the rover over **Wi-Fi (WebSocket/UDP)**, with **Bluetooth** fallback.

### 5.2 Rover firmware (ESP32)
- Connects to the console link, parses incoming commands, and drives the motor H-bridge
  and the arm/mast servos.
- Publishes telemetry (link state, battery, arm state) back to the console.

### 5.3 Video path
- Each **IP camera** streams over Wi-Fi to **its own monitor** via a Wi-Fi module —
  front feed on one screen, mast feed on the other. This keeps video off the control
  channel so driving stays responsive even while both camera feeds are live.

### 5.4 System signal flow

```
[Touchscreen Console · Godot]
        │  drive / camera / arm commands  (Wi-Fi WebSocket, BT fallback)
        ▼
   [ESP32 on rover] ── motors (H-bridge ×4)
        │            ── arm servos + gripper
        │            ── mast pan/tilt servos
        └── telemetry ──► back to console

[Front IP cam] ──Wi-Fi──► [Monitor 1]
[Mast IP cam]  ──Wi-Fi──► [Monitor 2]
```

Separating the control channel from the two video channels is the key architectural
decision here: it means a busy video stream can never make the drive controls feel
laggy, and either video feed can drop out without losing the ability to drive the rover
home.

---

## 6. The Storyboard — Target Mission Experience

*This section describes the mission experience the system is being designed to deliver.
It is a design target, walked through scene by scene — not a report of a demonstrated
run.*

The mission is a single run, told in eight scenes. In each, the rover starts at **base**
(near the operator, one end of the box) and works its way downrange, then back.

### Scene 1 — Power-Up & Handshake
**Objective:** confirm every subsystem is alive before the mission starts.
**What happens:** the rover sits in the base zone, powered off. The operator powers it on;
the ESP32 boots and joins Wi-Fi, the console shows **LINK ESTABLISHED**, and both camera
feeds come up on the two monitors.
**Systems engaged:** ESP32 boot, Wi-Fi handshake, both IP cameras online.
**Success looks like:** drive, arm, and mast all report green before the operator is
allowed to proceed to Scene 2.

### Scene 2 — Deploy from Base
**Objective:** move the rover from base onto open terrain.
**What happens:** the operator taps **forward** on the drive pad. The rover rolls out of
the base zone; the front camera shows the sand and rocks ahead.
**Controller actions:** forward. **Feed in focus:** front camera.

### Scene 3 — Survey the Field
**Objective:** find a candidate rock without having to drive blindly across the whole
arena.
**What happens:** the rover advances and pauses. Using the mast camera's pan/tilt, the
operator sweeps **left, right, up, down**, scanning the terrain.
**Controller actions:** mast pan/tilt. **Feed in focus:** mast camera.

### Scene 4 — Target Acquired
**Objective:** lock in a target and orient the rover toward it.
**What happens:** on the mast feed, a promising rock comes into view. The operator marks
it as the target and lines the rover up toward it.
**Controller actions:** steer to bearing. **Feed in focus:** mast camera.

### Scene 5 — Approach & Align
**Objective:** put the rock exactly where the arm can reach it.
**What happens:** switching to the front camera, the operator makes small
**left/right/forward** moves to bring the rock squarely in front of the arm.
**Controller actions:** fine drive. **Feed in focus:** front camera.

### Scene 6 — Collect the Sample
**Objective:** physically secure the sample for transit.
**What happens:** the operator deploys the arm, lowers it, closes the gripper on the rock,
and lifts. The rock is now secured in the gripper.
**Systems engaged:** arm servos + gripper. **Feed in focus:** front camera.
**Success looks like:** the gripper holds the rock through the return drive without
dropping it — this is the load-bearing test of the manipulator design (see Risk R2).

### Scene 7 — Return to Base
**Objective:** get the sample home intact.
**What happens:** holding the sample, the rover reverses and steers back across the box
toward base, the operator watching the front feed to stay clear of other rocks.
**Controller actions:** drive to base. **Feed in focus:** front camera.

### Scene 8 — Deposit & Debrief
**Objective:** close the loop and reset for the next run.
**What happens:** back in the base zone, the arm lowers and releases the rock into the
collection tray. Mission log updated: **1 sample collected**. The rover is ready for the
next target.
**Systems engaged:** arm release. **Outcome:** sample delivered, loop complete.

---

## 7. Build Plan — Phased Approach

No dates or costs are attached to these phases yet — each phase ends with a checkpoint
review before work (and spend) continues into the next one. This keeps risk incremental:
nothing downstream is built until the phase it depends on has proven itself.

| Phase | Goal | Key deliverable | Exit criteria to proceed |
|---|---|---|---|
| **0 — Approval & Procurement** | Turn this proposal into a costed, scheduled plan | Detailed bill of materials, sourcing plan, confirmed arena dimensions | Budget and part list approved |
| **1 — Drive Platform Bring-up** | Prove the chassis moves and is controllable | Chassis + 4-motor drive under ESP32 control, driven manually over Wi-Fi | Rover drives forward/back/left/right reliably inside the 1.4 m arena width; battery draw measured |
| **2 — Manipulator Integration** | Prove the arm can pick up and hold a target rock | Arm + gripper mounted and servo-controlled, front-mounted | Arm reliably grips and lifts a representative sample rock without dropping it |
| **3 — Vision & Operator Console** | Give the operator eyes and a real UI | Both IP cameras streaming to their own monitors; Godot console driving the rover live | Operator can drive and steer using only the console and camera feeds, no direct line of sight |
| **4 — Full Mission Integration & Demo** | Run the complete 8-scene mission loop end to end | Working "seek → collect → return" demonstration | A full mission run (Scenes 1–8) completes without manual intervention |

Each phase's exit criteria is the go/no-go gate for the next — if a phase doesn't clear
its bar, the plan gets revisited rather than compounding the problem downstream.

---

## 8. Risks & Open Questions

These are the technical unknowns we're aware of going in, and how the phased plan above
is structured to catch each one early rather than discover it during final integration.

| # | Risk | Why it matters | How we plan to de-risk it |
|---|---|---|---|
| R1 | Wi-Fi control latency under load — two video streams and the control channel share the same 2.4 GHz link | Driving could feel laggy or unresponsive right when precision matters most (Scenes 5–6) | Control and video are architecturally separate channels (Section 5.4); Bluetooth is a fallback control link; latency gets tested as soon as Phase 1 hardware exists |
| R2 | Servo torque margin — can the arm reliably lift a representative sample rock? | If the gripper can't hold or lift the target rock, Scene 6 (the core "collect" step) fails | Define a maximum sample size/mass up front; select servos with torque margin above that spec; load-test in Phase 2 before moving to Phase 3 |
| R3 | Camera latency/framerate over Wi-Fi | Choppy or delayed video makes fine alignment (Scene 5) hard to judge | Keep resolution modest to protect frame rate; verify both feeds run smoothly together during Phase 3, before the console is considered done |
| R4 | Power budget / battery runtime for a full mission run | Rover could die mid-mission if the pack is undersized | Measure actual current draw per subsystem in Phase 1; size the LiPo pack with margin once real numbers exist |
| R5 | Mechanical clearance for a 4-wheel chassis to turn inside a 1.4 m-wide arena | A chassis that can't turn in place limits maneuvering room significantly | Validate chassis footprint and turning radius against arena dimensions before any fabrication (Phase 0/1) |
| R6 | RF performance through the glass enclosure | Wi-Fi signal could be weaker than expected if the box or its dressing (sand, rocks) attenuates it more than assumed | Glass is largely RF-transparent compared to a metal enclosure, but this is a checkpoint to *verify* in Phase 1, not an assumption to build on |

---

## 9. What This Demonstrates

| Discipline | What the build exercises |
|---|---|
| **Mechanical & mechatronics** | Multi-motor drive platform + a multi-degree-of-freedom manipulator working together under load |
| **Embedded firmware** | Real-time motor/servo control on an ESP32, with wireless command handling and telemetry reporting |
| **Wireless systems & networking** | Deliberately separating a low-latency control channel from two independent camera video streams |
| **Human–machine interface** | A purpose-built touchscreen operator console (Godot), designed around the operator's actual task sequence |
| **Systems integration & project management** | Mechanics, electronics, firmware, networking, and UI brought together into one working mission loop, delivered through staged, checkpointed phases |

---

## 10. What's Needed to Proceed

This is the ask referenced in the Executive Summary: approval to move this project out of
the design stage.

- **Approval to enter Phase 0** (Section 7) — procurement and detailed costing.
- **Engineering time**, allocated roughly across four areas: mechanical assembly, embedded
  firmware, console/UI development, and integration & test.
- **Hardware procurement**, by category — chassis and drive components, manipulator/servo
  kit, compute and wireless module (ESP32), camera modules, display/console hardware,
  power system, and arena materials. A fully costed bill of materials will follow once
  this scope is approved — none is attached here.
- **Bench/desk workspace** for build and testing. Given the compact, low-voltage, fully
  enclosed design, no special facility or safety sign-off is anticipated.
- **A checkpoint review at the end of each phase** in Section 7, before work proceeds
  (and any further spend is committed) to the next one.

---

## 11. Glossary — Plain-Language Reference

For reviewers who don't work with embedded robotics day to day:

| Term | What it means |
|---|---|
| **ESP32** | A small, inexpensive computer chip with Wi-Fi and Bluetooth built in — the "brain" that controls the rover's motors and sensors. |
| **H-bridge** | An electronic switch circuit that lets a small computer control a motor's direction and speed. |
| **Differential (skid) steering** | Turning by spinning the left and right wheels at different speeds — like a tank — instead of a car-style steering wheel. |
| **Servo** | A small motor that moves to a precise commanded angle; used here for the arm's joints and the camera's pan/tilt head. |
| **Degrees of freedom (DOF)** | The number of independent ways a mechanism — like the arm — can move or bend. |
| **LiPo battery** | Lithium-polymer battery — a common lightweight, rechargeable battery type used in small robots and drones. |
| **IP camera** | A camera that sends video over a network (Wi-Fi) instead of a direct cable, so it can stream to a screen elsewhere. |
| **Pan/tilt** | A mount that swivels left-right (pan) and up-down (tilt), like a security camera head. |
| **WebSocket** | A way of sending small messages back and forth over Wi-Fi with very low delay — used here to carry drive commands. |
| **Telemetry** | Status data (battery level, connection state, arm position, etc.) sent back from the rover to the operator console. |

---

*Mars Rover · Sample Return — Project Storyboard & Proposal · Design & Planning Stage ·
2026-07-21*
