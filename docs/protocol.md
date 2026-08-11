# Control Protocol v1 — console ↔ rover

Authoritative definition of every message exchanged between the Godot operator console and
the ESP32 rover firmware. Companion to [plan/storyboard.md](../plan/storyboard.md) §5.2/§5.4
and [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) step S.2.

**This document is the contract.** The console and the firmware are built against it
independently. When code and document disagree, the code is wrong and gets fixed — the
exception is a deliberate protocol change, which is a change to this file first, with the
version number bumped, and then to both codebases.

**Protocol version: `1`.** Nothing in this file has shipped to hardware; no hardware has been
procured. Section 9 states exactly which parts are implemented today and which are specified
for a later step.

---

## 1. Transport

| Property | Value |
|---|---|
| Protocol | WebSocket (RFC 6455), text frames |
| Rover role | **Server**, listening on port **81**, path `/` |
| Console role | **Client**, connects to `ws://<rover-ip>:81/` |
| TLS | None. Closed arena network, no secrets on the wire. |
| Encoding | UTF-8 JSON, exactly **one complete JSON object per frame** |
| Max frame size | **512 bytes.** A larger frame is dropped, counted, and logged; it is never partially applied. |
| Concurrent clients | The firmware accepts multiple, but exactly **one console is supported**. Telemetry is broadcast to all connected clients; commands from any client are honoured. A second console is an operating error, not a feature. |

The rover is a Wi-Fi **station** joining an existing access point (§0.5 decides the topology),
so its address is assigned by the network and printed on the serial console at boot. The
console's `rover_url` must be set to match; a static DHCP reservation for the rover is the
intended arrangement.

**Bluetooth fallback** (§4.3, risk R1) reuses this entire message catalogue unchanged. Only
the transport differs — the framing rule becomes one JSON object per BT packet. Nothing in
sections 3–6 is Wi-Fi-specific.

### 1.1 Why video never shares this socket

Control and video are **architecturally separate channels**, and that separation is the
central design decision of the whole system ([storyboard §5.4](../plan/storyboard.md), risk
R1). Each IP camera streams over Wi-Fi directly to its own monitor. Video bytes never enter
this WebSocket, are never relayed by the ESP32, and are never framed by this protocol.

Two consequences that this protocol depends on:

- **A saturated video feed cannot make driving laggy.** The control channel carries a few
  hundred bytes per second; the video channels carry megabits. Multiplexing them would put
  drive commands behind a queue of video frames precisely during Scenes 5–6, where fine
  alignment matters most.
- **Either camera can fail without stranding the rover.** A dead feed costs the operator
  visibility, not control — they can still drive home blind.

Any proposal to carry video, still frames, or a camera control API over this socket
contradicts §5.4 and reopens R1. It requires a protocol version bump and an explicit decision,
not an added message type.

---

## 2. Framing conventions

These rules apply to every message in both directions.

1. **Discriminator.** Console → rover frames carry a `cmd` string. Rover → console frames
   carry a `t` string. A frame missing its discriminator is malformed.
2. **Unknown discriminators are ignored, not errors.** A receiver logs and drops a `cmd` or
   `t` it does not recognise, and carries on. This is what lets the firmware accept `mast` and
   `arm` today while ignoring them, and lets a newer console talk to an older rover without
   the link collapsing.
3. **Unknown fields are ignored.** Additive fields are therefore backward compatible and do
   **not** require a version bump.
4. **Missing fields take the documented default.** A `drive` frame with no `r` means
   `r = 0.0`. There is no partial application: a frame either parses or is dropped whole.
5. **Wrong types are a malformed frame**, dropped and counted — not coerced. `{"l":"0.6"}` is
   not `0.6`.
6. **A malformed frame does not refresh the failsafe timer** (§5). Garbage is not proof of a
   healthy console.
7. **Numbers** are JSON numbers, never strings. Booleans are `true`/`false`, never `0`/`1`.
8. **Angles** are degrees, and **throttles** are dimensionless fractions. There are no other
   units in the protocol except volts, dBm, and milliseconds, each named in its field.

---

## 3. Console → rover

### 3.1 `hello` — handshake

Sent as the **first frame** after the socket opens, before any other command.

```json
{"cmd":"hello","v":1}
```

| Field | Type | Range | Required | Meaning |
|---|---|---|---|---|
| `cmd` | string | `"hello"` | yes | Discriminator |
| `v` | integer | ≥ 1 | yes | Protocol version the console speaks |

Version negotiation and the behaviour on mismatch are in §4.

### 3.2 `drive` — per-side throttle

The drive pad. Differential (skid) steering: the left/right difference *is* the turn, so
there is no steering command.

```json
{"cmd":"drive","l":0.6,"r":-0.6}
```

| Field | Type | Unit | Range | Default | Meaning |
|---|---|---|---|---|---|
| `cmd` | string | — | `"drive"` | — | Discriminator |
| `l` | number | fraction | −1.0 … 1.0 | `0.0` | Left-side throttle |
| `r` | number | fraction | −1.0 … 1.0 | `0.0` | Right-side throttle |

- **Sign:** positive drives that side **forward**. Both positive = forward; both negative =
  reverse; opposite signs = turn in place. Per-side physical polarity is corrected in
  firmware config (step 1.4), never by flipping the sign here.
- **Out-of-range values are clamped, not rejected.** `l: 2.5` is applied as `1.0`. The console
  clamps before sending and the rover clamps again on receipt; both are required.
- **Deadband.** A commanded magnitude below **0.05** coasts that side (both H-bridge direction
  pins low, zero duty) rather than energising the motors, because the geared motors buzz
  instead of turning down there. So `l: 0.03` and `l: 0.0` are indistinguishable at the
  wheels. The exact figure is retuned against real motors in step 1.3 and is a property of
  the `Drive` module, not of this protocol.
- **Resolution.** The console quantises to 0.01. The PWM stage is 8-bit, so the rover's usable
  resolution is 1/255 — finer values on the wire are harmless but meaningless.
- **Repeat rate:** a held drive command **must be resent** — see §6. This is not optional.

### 3.3 `stop` — explicit stop

```json
{"cmd":"stop"}
```

| Field | Type | Range | Required | Meaning |
|---|---|---|---|---|
| `cmd` | string | `"stop"` | yes | Discriminator |

Cuts drive current and puts the H-bridge into standby immediately. Takes no arguments and is
always valid, including while in safe mode.

`stop` and `drive` with `l: 0, r: 0` both halt the rover, and differ in intent:

- `drive 0,0` means *"commanded speed is now zero"* — a normal drive command that refreshes
  the failsafe timer and leaves the rover armed. This is what a drive button's release sends.
- `stop` means *"cease driving"* — it also refreshes the timer, but is the frame to send on a
  deliberate halt, a mode change, or an abort.

Neither is a substitute for the failsafe. §5 stops the rover when the console says *nothing at
all*, which is the case a `stop` frame cannot cover.

### 3.4 `mast` — pan/tilt head

Absolute angles for the mast camera head. Not incremental.

```json
{"cmd":"mast","pan":0,"tilt":15}
```

| Field | Type | Unit | Range | Default | Meaning |
|---|---|---|---|---|---|
| `cmd` | string | — | `"mast"` | — | Discriminator |
| `pan` | number | degrees | −90 … 90 | hold current | Left/right; **0 = straight ahead**, positive = right |
| `tilt` | number | degrees | −30 … 60 | hold current | Down/up; **0 = horizon**, positive = up |

- **Absolute, not relative.** Sending the same frame twice is idempotent.
- **Ranges above are placeholders** pending the mechanical travel limits measured in step 2.2.
  Those measured limits become the authority; the firmware clamps to them and a commanded
  angle can never drive a joint into its own mechanical stop.
- **A missing field holds that axis** at its current angle, so pan and tilt can be commanded
  independently.
- Ignored while in safe mode (§5).

### 3.5 `arm` — joint angles and gripper

```json
{"cmd":"arm","joints":[0,45,90],"grip":true}
```

| Field | Type | Unit | Range | Default | Meaning |
|---|---|---|---|---|---|
| `cmd` | string | — | `"arm"` | — | Discriminator |
| `joints` | array of number | degrees | per-joint, from step 2.2 | hold current | One angle per joint, **ordered base outward** |
| `grip` | boolean | — | — | hold current | `true` = closed on the sample, `false` = open |

- **`joints` length must equal the rover's joint count** (3 or 4, fixed in Phase 0). A
  wrong-length array is a malformed frame and is dropped whole — a short array must never be
  applied to a prefix of the joints.
- **Ordered base outward**, so `joints[0]` is the shoulder. Named poses (`STOW`, `DEPLOY`,
  `LOWER`, `LIFT`, `RELEASE`) are resolved **console-side** into joint angles in step 2.3; the
  wire format stays angles, so the console never depends on firmware pose tables.
- Angles are clamped to the per-joint software limits from step 2.2.
- Ignored while in safe mode (§5).

### 3.6 `ping` — latency probe

```json
{"cmd":"ping","ts":1734001234567}
```

| Field | Type | Unit | Range | Required | Meaning |
|---|---|---|---|---|---|
| `cmd` | string | — | `"ping"` | yes | Discriminator |
| `ts` | integer | ms | ≥ 0 | yes | Console's own clock at send time |

The rover echoes `ts` back verbatim in a `pong` (§3.8). `ts` is **opaque to the rover** — it
is never interpreted, compared, or used to set any rover-side clock, so the two clocks never
need to agree. Round-trip time is `now − ts`, measured entirely on the console.

`ping` **does not refresh the failsafe timer.** A console that can ping but not drive is not a
console that should keep the motors live.

Used by the latency HUD in step S.7, which is the measurement that makes risk R1 assessable
before integration rather than after.

---

## 4. Rover → console

### 4.1 `hello` — handshake reply

Sent once, in response to the console's `hello`.

```json
{"t":"hello","v":1,"fw":"0.1.0","caps":["drive"]}
```

| Field | Type | Range | Required | Meaning |
|---|---|---|---|---|
| `t` | string | `"hello"` | yes | Discriminator |
| `v` | integer | ≥ 1 | yes | Protocol version the rover speaks |
| `fw` | string | ≤ 16 chars | yes | Firmware version, for the record in a mission log |
| `caps` | array of string | subset of `["drive","mast","arm"]` | yes | Subsystems this build actually actuates |

`caps` is how the console knows what is real on this build. During Phases S–1 it is
`["drive"]`; the mast and arm entries appear as Phases 2–3 wire them up. The console **must**
disable controls for absent capabilities rather than sending commands into a void — this is
what makes the Scene 1 readiness row (step S.6) honest instead of decorative.

### 4.2 `tlm` — telemetry

Unsolicited broadcast every **500 ms**, from the moment the socket opens — including while in
safe mode and including on a version mismatch. Telemetry is the channel that explains *why*
the rover will not move, so it must never be gated on the rover being ready to move.

```json
{"t":"tlm","battery_v":11.84,"mode":"safe","rssi":-58}
```

| Field | Type | Unit | Range | Required | Meaning |
|---|---|---|---|---|---|
| `t` | string | — | `"tlm"` | yes | Discriminator |
| `battery_v` | number | volts | 0.0 … 30.0 | yes | Pack voltage at the divider. Accuracy depends on `BATTERY_DIVIDER_RATIO`, which is **uncalibrated until step 1.7** — treat as indicative before then. |
| `mode` | string | — | see below | yes | Rover state |
| `rssi` | integer | dBm | −100 … 0 | yes | Rover's own view of link strength. The number risk R6 is judged on (step 1.2). |

`mode` values, and no others:

| `mode` | Meaning | Console must |
|---|---|---|
| `"drive"` | Armed. Commands are being acted on. | Enable drive controls |
| `"safe"` | Motors cut. Awaiting a fresh command to re-arm. | Show safe mode; drive controls remain usable, since a command is what re-arms |
| `"incompatible"` | Protocol version mismatch (§4). Will not arm. | Disable drive controls and show the mismatch |

**Reserved field names**, so Phase 2–3 additions cannot collide with something else: `arm`,
`grip`, `mast`, `pan`, `tilt`, `uptime_ms`, `errors`, `run`. Adding any of these is additive
and needs no version bump (§2.3).

### 4.3 `pong` — latency probe reply

```json
{"t":"pong","ts":1734001234567}
```

| Field | Type | Unit | Required | Meaning |
|---|---|---|---|---|
| `t` | string | — | yes | `"pong"` |
| `ts` | integer | ms | yes | The `ts` from the `ping`, echoed **byte-for-byte unmodified** |

Replied to immediately, ahead of any queued telemetry, so the measurement reflects the link
rather than the rover's own scheduling.

---

## 5. Versioning

- The current version is **`1`**, and it is a single integer. There is no minor version.
- **Bump it only for a breaking change**: removing a field, renaming one, changing a unit,
  narrowing a range, or changing the meaning of an existing value. *Adding* a message type or
  an optional field is backward compatible and does not bump it (§2.2, §2.3).
- Both sides compare the `v` they receive against the single version they themselves speak.

**On mismatch — the rover:**

1. Refuses to arm. It stays in `mode: "incompatible"` and **will not actuate anything**,
   whatever commands arrive afterwards.
2. Keeps the socket open and keeps broadcasting telemetry, so the console can display the
   reason. Dropping the connection would look identical to a dead rover.
3. `stop` is still honoured, because a stop must never be refused.

**On mismatch — the console:**

1. Disables the drive pad and every actuator control.
2. Shows the mismatch on the telemetry strip with both version numbers — "ROVER v2, CONSOLE
   v1" diagnoses itself; "LINK ERROR" does not.
3. Sends no further commands except `stop`.

**If the console sends no `hello` within 2000 ms** of the socket opening, the rover treats the
peer as an unknown version and behaves exactly as on mismatch. An old console that has never
heard of `hello` therefore fails closed, not open.

The mismatch state is **not** safe mode: safe mode is cleared by the next valid command, while
an incompatible peer can only be resolved by reflashing or updating one side. They are
deliberately distinct `mode` values for that reason.

---

## 6. Failsafe contract

**This section is the safety-critical part of the protocol. It is not an optimisation, and no
refactor may weaken it.** A degraded link must never leave the rover driving into the glass
(risk R1). Step S.4 covers all of it in native unit tests; step 1.6 proves it on real hardware
and is the one gate the plan does not negotiate past.

### 6.1 Safe mode

**Safe mode means: drive current cut, H-bridge in standby, servo commands ignored, `mode`
reported as `"safe"`.** Servos are *not* forcibly moved — an arm holding a rock keeps holding
it, because dropping the sample and flopping into the sand is a worse failure than staying
put. Safe mode stops the rover; it does not reset it.

### 6.2 What trips it

| Trigger | Condition |
|---|---|
| **Command timeout** | No valid command for **`COMMAND_TIMEOUT_MS` = 500 ms** |
| **Disconnect** | WebSocket closes, cleanly or otherwise |
| **Boot** | The rover boots into safe mode and stays there until the first valid command |

Only **valid** frames refresh the timer: `drive`, `stop`, `mast`, and `arm` that parsed
successfully. Explicitly **not** refreshing it:

- Malformed JSON, unknown `cmd`, wrong types, oversize frames (§2.6)
- `ping` (§3.6) and `hello`
- WebSocket-level ping/pong, which the library answers without the application ever seeing it
  — a TCP connection that is technically alive proves nothing about the console

### 6.3 Re-arming

**Re-arming requires a fresh, valid command, and nothing else.** Reconnecting does not
re-arm. Receiving telemetry does not re-arm. There is no stored throttle: the rover **must
not** resume the last commanded speed when the link returns.

The failure this forbids is a rover that sits still through a dropout and then lurches away
the instant Wi-Fi recovers, with the operator's finger nowhere near the screen. Step 1.6 tests
for exactly that.

### 6.4 The console's obligation

The console holds up its half of the failsafe:

- **Hold-to-drive.** Drive controls act on press and release (`button_down` / `button_up`).
  Lifting a finger sends a stop. No latching, no toggle, no "set speed" control.
- **Repeat while held** — see §6.5.
- **Disable drive controls whenever the link is not established**, so a frozen console cannot
  look live (step S.6).

### 6.5 Command repeat rate

While a drive control is held, the console **must resend the active `drive` command at 150 ms
intervals**, and must never exceed **200 ms** between repeats.

This follows directly from §6.2: the rover cuts the motors after 500 ms of silence, so a
console that sends one frame on press and then goes quiet gets its motors cut roughly half a
second into every sustained hold. A press-only console and this failsafe are mutually
incompatible; the repeat is what reconciles them.

The 150 ms figure absorbs **two** consecutive lost frames (450 ms < 500 ms) before the
failsafe trips, so ordinary Wi-Fi packet loss does not stutter the drive. It is chosen against
the timeout, so the two constants move together: **if `COMMAND_TIMEOUT_MS` changes, this
figure must be rechecked.**

Bandwidth is not a concern — a repeated `drive` frame is ~30 bytes, so ~200 bytes/s while
driving, against a channel deliberately reserved for exactly this (§1.1).

The repeat stops the moment the control is released, and the release frame is sent
immediately rather than waiting for the next interval.

---

## 7. A worked exchange

Scene 1 through the start of Scene 2, with time flowing downward.

```
t=0.00  console → rover   {"cmd":"hello","v":1}
t=0.01  rover → console   {"t":"hello","v":1,"fw":"0.1.0","caps":["drive"]}
t=0.01  rover → console   {"t":"tlm","battery_v":12.42,"mode":"safe","rssi":-54}
        ...console shows LINK ESTABLISHED, drive green, arm/mast greyed (not in caps)

t=0.50  rover → console   {"t":"tlm","battery_v":12.42,"mode":"safe","rssi":-55}
t=1.00  rover → console   {"t":"tlm","battery_v":12.41,"mode":"safe","rssi":-54}

        ...operator presses and holds FORWARD
t=1.20  console → rover   {"cmd":"drive","l":1.0,"r":1.0}     ← arms the rover
t=1.35  console → rover   {"cmd":"drive","l":1.0,"r":1.0}     ← §6.5 repeat
t=1.50  console → rover   {"cmd":"drive","l":1.0,"r":1.0}
t=1.50  rover → console   {"t":"tlm","battery_v":11.88,"mode":"drive","rssi":-56}
t=1.65  console → rover   {"cmd":"drive","l":1.0,"r":1.0}

        ...operator lifts off
t=1.72  console → rover   {"cmd":"drive","l":0.0,"r":0.0}     ← still armed, speed zero
t=2.00  rover → console   {"t":"tlm","battery_v":12.30,"mode":"drive","rssi":-56}

        ...console goes quiet — crash, or Wi-Fi drops
t=2.22  rover: 500 ms since last command → safe mode, drive current cut
t=2.50  rover → console   {"t":"tlm","battery_v":12.40,"mode":"safe","rssi":-71}

        ...link recovers. The rover does NOT resume driving (§6.3).
```

---

## 8. Implementation status

Honest accounting of what exists today, against this document. Nothing here has run on
hardware — the ✅ rows mean "written and matches this document", not "proven".

| Message | Firmware | Console | Notes |
|---|---|---|---|
| `drive` | ✅ | ✅ | Shapes verified identical against this doc |
| `stop` | ✅ | ❌ | Firmware handles it; `rover_link.gd` has no sender — see F2 below |
| `mast` | ⚠️ accepted, logged, ignored | ✅ sender exists | Actuated in step 3.4 |
| `arm` | ⚠️ accepted, logged, ignored | ✅ sender exists | Actuated in step 2.3 |
| `hello` (both directions) | ✅ | ❌ | Rover parses it, replies with `caps`, and refuses to arm on a version mismatch. Console sender lands in S.6. |
| No-`hello` deadline (§5) | ⚠️ **not armed** | — | The 2000 ms fail-closed rule is deliberately not enforced yet: today's console never sends `hello`, so arming it now would lock out the only console that exists. Turned on in S.6, together with the console sender. |
| `ping` / `pong` | ✅ | ❌ | Rover echoes `ts` unmodified. Console side is step **S.7**. |
| `tlm` | ✅ tagged `"t":"tlm"` | ⚠️ **ignores `t`** | Firmware half of F1 closed in S.3; console still treats any dictionary as telemetry |
| Failsafe timeout + disconnect (§6.2) | ✅ `lib/Safety` | — | 500 ms; extracted to a pure function in S.3, unit-tested in S.4 |
| Re-arm rule (§6.3) | ✅ `lib/Safety` | — | Scoped to the connection, so a reconnect inside the timeout window cannot arm |
| Strict parsing (§2.5, §1) | ✅ `lib/Protocol` | — | Wrong types and oversize frames rejected whole; F4 closed in S.3 |
| Command repeat (§6.5) | — | ❌ | **See F3 — this is a live defect** |

### Findings raised by writing this document

Recorded rather than silently fixed, because each is a change to working code and the plan
reviews one step at a time.

**F3 — the console never repeats a held drive command.** *(Real defect, drive-affecting.)*
[console.gd:34-35](../console/scripts/console.gd#L34-L35) connects `button_down` and
`button_up` and nothing else — no timer, no `_process`. So holding FORWARD sends exactly one
frame, and [main.cpp:117](../firmware/src/main.cpp#L117) cuts the motors 500 ms later. On real
hardware the rover would crawl for half a second and stop, with the operator's finger still
down. §6.5 is the fix; it needs a repeat timer console-side. Naturally belongs with the S.6
link-state work, or can be fixed on its own now.

**F1 — rover → console frames have no discriminator.** *(Firmware half closed in S.3.)*
The firmware now tags every frame — `"t":"tlm"`, `"t":"hello"`, `"t":"pong"` — but
[rover_link.gd:81-87](../console/scripts/rover_link.gd#L81-L87) still forwards *any* parsed
dictionary to the telemetry strip without looking at `t`. Harmless while the console sends no
`hello` and no `ping`, since telemetry is then the only frame it can receive; it becomes a
real bug the moment S.7 adds pings. Console-side dispatch on `t` is S.6 work.

**F2 — no console-side `stop` sender.** The firmware accepts `stop`; `rover_link.gd` has
`send_drive`, `send_mast`, and `send_arm` but no `send_stop`. The drive pad's STOP button sends
`drive 0,0` instead, which does halt the rover, so nothing is broken today — but §3.3's
distinction is not available to the console. A three-line addition, best made when S.6 gives
it a caller.

**F4 — parsing was more permissive than §2.5 and §1 require.** *(Closed in S.3.)* The old
`doc["l"] | 0.0f` leaned on ArduinoJson's implicit conversion, so `{"l":"0.6"}` was coerced to
`0.6` rather than dropped, and there was no frame-size check. `protocol::parse` now rejects
both, along with wrong-typed angles, non-boolean `grip`, and a `joints` array that is empty or
over-long — and decodes joints into a scratch array first, so a bad angle halfway along cannot
leave the earlier joints applied. S.4 adds the tests.

**Status:** F4 is closed; F1 is closed firmware-side; F2 and F3 are open and both live in the
console. None of the four was ever a shape mismatch — **every field of every message both
sides implement matches this document exactly**, which is what step S.2's verification asked
for.

---

## 9. Open items

Deliberately unresolved, each with the step that closes it.

| # | Item | Closed by |
|---|---|---|
| 1 | `mast` pan/tilt ranges are placeholders | Step 2.2 — measured mechanical limits |
| 2 | Arm joint count (3 or 4) and per-joint ranges | Steps 0.1, 2.2 |
| 3 | Round-trip latency **target** — a number to test against | Step S.7 |
| 4 | Whether the mission/run state of Phase 4 rides this socket or stays console-only | Step 4.2 |
| 5 | Bluetooth fallback framing, if Wi-Fi proves inadequate | Step 3.3 decision point |

---

*Protocol v1 · frozen at step S.2 · no hardware procured*
