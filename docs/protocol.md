# Control Protocol v2 — console ↔ rover

Authoritative definition of every message exchanged between the Godot operator console and
the ESP32 rover firmware. Companion to [plan/storyboard-rev2.md](../plan/storyboard-rev2.md)
§5.2/§5.5 and [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) steps S.2, S.9 and S.14.

**Tracks proposal Revision 2** (2026-07-27): no robotic arm, one camera, and a UHF RFID reader
that identifies rocks in place. The `arm` command was retired at step S.9 and the `tag` frame
(§4.4) added, with no version bump.

**Bumped to v2 at step S.14** (2026-08-13), because step 0.2 chose a chassis this protocol did
not describe: **one drive motor and one steering motor**, not two independently driven sides.
`drive` is now throttle plus steering, `l` and `r` are retired, and §5.1 works through message
by message why that one *is* breaking where Revision 2's changes were not.

**This document is the contract.** The console and the firmware are built against it
independently. When code and document disagree, the code is wrong and gets fixed — the
exception is a deliberate protocol change, which is a change to this file first, with the
version number bumped, and then to both codebases.

**Protocol version: `2`.** Nothing in this file has shipped to hardware; no hardware has been
procured. Section 8 states exactly which parts are implemented today and which are specified
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
central design decision of the whole system ([proposal §5.5](../plan/storyboard-rev2.md), risk
R1). The mast IP camera streams over Wi-Fi directly to its monitor. Video bytes never enter
this WebSocket, are never relayed by the ESP32, and are never framed by this protocol.

Two consequences that this protocol depends on:

- **A saturated video feed cannot make driving laggy.** The control channel carries a few
  hundred bytes per second; the video channel carries megabits. Multiplexing them would put
  drive commands behind a queue of video frames precisely during Scenes 5–6, where the
  approach and the tag read happen.
- **The feed can fail without stranding the rover.** A dead feed costs the operator vision,
  not control — they can still drive home blind. With one camera this matters more than it did
  under Revision 1, not less (risk R8).

Any proposal to carry video, still frames, or a camera control API over this socket
contradicts proposal §5.5 and reopens R1. It requires a protocol version bump and an explicit
decision, not an added message type.

Tag reads are the one sensor stream that *does* travel here, and deliberately so: they are a
few dozen bytes, rate-limited (§4.4), and useless to the operator if they arrive late.

---

## 2. Framing conventions

These rules apply to every message in both directions.

1. **Discriminator.** Console → rover frames carry a `cmd` string. Rover → console frames
   carry a `t` string. A frame missing its discriminator is malformed.
2. **Unknown discriminators are ignored, not errors.** A receiver logs and drops a `cmd` or
   `t` it does not recognise, and carries on. This is what lets the firmware accept `mast`
   today while ignoring it, let the `arm` command be retired in Revision 2 without a version
   bump, and lets a newer console talk to an older rover without the link collapsing.
3. **Unknown fields are ignored.** Additive fields are therefore backward compatible and do
   **not** require a version bump.
   - **§2.3a — retired fields are not unknown fields.** A field this document lists as
     *retired* — today only `l` and `r` on `drive` (§3.2) — makes the frame **malformed**,
     dropped whole. It is not ignored. A retired field is one whose meaning was removed, and a
     peer still sending it is a peer that believes something false about this rover; silently
     ignoring it is precisely how a version skew becomes invisible. See §5.1.
4. **Missing fields take the documented default.** A `drive` frame with no `steer` means
   `steer = 0.0`. There is no partial application: a frame either parses or is dropped whole.
5. **Wrong types are a malformed frame**, dropped and counted — not coerced. `{"fwd":"0.6"}` is
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

### 3.2 `drive` — throttle and steering

The drive pad. The rover has **one driven axle and one steered axle** (step 0.2), so a drive
command is a speed and a direction, not two side speeds.

```json
{"cmd":"drive","fwd":0.6,"steer":-1.0}
```

| Field | Type | Unit | Range | Default | Meaning |
|---|---|---|---|---|---|
| `cmd` | string | — | `"drive"` | — | Discriminator |
| `fwd` | number | fraction | −1.0 … 1.0 | `0.0` | Throttle. Positive = forward, negative = reverse |
| `steer` | number | fraction | −1.0 … 1.0 | `0.0` | Steering demand. **Negative = left**, positive = right, `0.0` = straight ahead |
| ~~`l`~~ | — | — | — | — | **Retired at v2.** Present ⇒ malformed frame (§2.3a) |
| ~~`r`~~ | — | — | — | — | **Retired at v2.** Present ⇒ malformed frame (§2.3a) |

- **The two axes are independent.** `fwd` sets how fast, `steer` sets which way; either may be
  zero. Physical polarity of either motor is corrected in firmware config (step 1.4), never by
  flipping a sign here.
- **Out-of-range values are clamped, not rejected.** `fwd: 2.5` is applied as `1.0`. The
  console clamps before sending and the rover clamps again on receipt; both are required.
- **`steer` with `fwd: 0` moves the wheels, not the rover.** A steered chassis cannot pivot in
  place: with no throttle the steering motor swings the front axle and the rover stays exactly
  where it is. This is a real behaviour change from v1, where opposite-signed `l`/`r` turned
  the rover on the spot, and the console must not present steering as if it were a turn
  command (§6.4).
- **Throttle deadband.** A commanded `fwd` magnitude below **0.05** coasts the drive motor
  (both direction pins low, zero duty) rather than energising it, because the geared motor
  buzzes instead of turning down there. So `fwd: 0.03` and `fwd: 0.0` are indistinguishable at
  the wheels. The figure is retuned against the real motor in step 1.3 and is a property of the
  `Drive` module, not of this protocol.
- **Resolution.** The console quantises to 0.01. The PWM stage is 8-bit, so the rover's usable
  throttle resolution is 1/255 — finer values on the wire are harmless but meaningless.
- **Repeat rate:** a held drive command **must be resent** — see §6. This is not optional.

#### 3.2.1 `steer` is a float, but this build's steering is three-position

Confirmed at step 0.2 ([chassis-envelope §8.5](chassis-envelope.md)): the steering motor drives
to a **mechanical end stop**, and a spring recentres it when unpowered. There is no
intermediate angle — the front axle is at full left lock, straight, or full right lock.

`steer` stays a **float on the wire anyway**, and the *rover* does the rounding:

| Received `steer` | This build does |
|---|---|
| `steer` ≤ −0.5 | Full left lock |
| −0.5 < `steer` < 0.5 | Unpowered — the spring centres the axle |
| `steer` ≥ 0.5 | Full right lock |

The **0.5 threshold is a property of the `Drive` module**, like the throttle deadband, not of
this protocol; it is stated here so the console's behaviour is predictable, and it is retuned
at step 1.3 if the real steering proves to need it.

Keeping the field continuous is deliberate. A later chassis with proportional steering honours
the same field with no version bump, and a console written today needs no change to drive it —
it simply gets a smoother response to values it was already sending. Which kind of steering a
build has is **discovered in the handshake, not assumed**: see the `steer3` / `steerprop`
capability in §4.1. That is the same mechanism §5.2 credits for letting Revision 2 through
without a flag day, used a second time.

Two consequences the console has to respect, both from
[chassis-envelope §8.5](chassis-envelope.md):

- **There is no small heading correction.** The only correction available is a full-lock turn
  the other way, so driving straight is a series of taps rather than a held input. The S.13
  nudge primitive matters more here, not less.
- **Failing safe centres the steering** — see §6.1.

### 3.3 `stop` — explicit stop

```json
{"cmd":"stop"}
```

| Field | Type | Range | Required | Meaning |
|---|---|---|---|---|
| `cmd` | string | `"stop"` | yes | Discriminator |

Cuts current to **both** motors immediately: both bridge inputs to zero so the outputs coast,
then the driver's **standby pin dropped**, which is a hardware disable of both bridges rather
than merely a zero duty cycle. The steering therefore spring-centres, per §6.1. Takes no
arguments and is always valid, including while in safe mode.

*Step 1.3 changed the driver from an L293D to a DRV8833, which is where that standby pin came
from — the L293D had none, and `stop()` could only ask for zero duty. A stop is now
strictly stronger than it was when this section was written at S.14.*

`stop` and `drive` with `fwd: 0, steer: 0` both halt the rover, and differ in intent:

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

### 3.5 `ping` — latency probe

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
{"t":"hello","v":2,"fw":"0.2.0","caps":["drive","steer3"]}
```

| Field | Type | Range | Required | Meaning |
|---|---|---|---|---|
| `t` | string | `"hello"` | yes | Discriminator |
| `v` | integer | ≥ 1 | yes | Protocol version the rover speaks |
| `fw` | string | ≤ 16 chars | yes | Firmware version, for the record in a mission log |
| `caps` | array of string | subset of the table below | yes | Subsystems and behaviours this build actually has |

| Token | Meaning |
|---|---|
| `drive` | The rover has a drivetrain and will act on `fwd` |
| `steer3` | **Three-position steering**: `steer` is thresholded to left / centre / right (§3.2.1) |
| `steerprop` | **Proportional steering**: `steer` is honoured continuously |
| `mast` | Pan/tilt head fitted and actuated (step 3.4) |
| `rfid` | RFID reader fitted and polled (step 2.6) |

`caps` is how the console knows what is real on this build. During Phases S–1 it is
`["drive","steer3"]`; `rfid` appears when Phase 2 fits the reader and `mast` when Phase 3 fits
the servos. The console **must** disable controls for absent capabilities rather than sending
commands into a void — this is what makes the Scene 1 readiness row (step S.6) honest instead
of decorative.

**Exactly one steering token accompanies `drive`.** `steer3` and `steerprop` are mutually
exclusive, and a `caps` carrying `drive` with neither of them — or with both — is a rover
misreporting itself; the console treats that as it treats a version mismatch, and does not
arm. This is what lets §3.2.1 keep `steer` continuous on the wire without the console having to
guess what the far end will do with a value of 0.3.

### 4.2 `tlm` — telemetry

Unsolicited broadcast every **500 ms**, from the moment the socket opens — including while in
safe mode and including on a version mismatch. Telemetry is the channel that explains *why*
the rover will not move, so it must never be gated on the rover being ready to move.

```json
{"t":"tlm","battery_v":11.84,"mode":"safe","rssi":-58,"rfid":"ready"}
```

| Field | Type | Unit | Range | Required | Meaning |
|---|---|---|---|---|---|
| `t` | string | — | `"tlm"` | yes | Discriminator |
| `battery_v` | number | volts | 0.0 … 30.0 | yes | Pack voltage at the divider. Accuracy depends on `BATTERY_DIVIDER_RATIO`, which is **uncalibrated until step 1.7** — treat as indicative before then. |
| `mode` | string | — | see below | yes | Rover state |
| `rssi` | integer | dBm | −100 … 0 | yes | Rover's own view of the **Wi-Fi** link. Not to be confused with the RFID `rssi` in §4.4 — different radio, different question. The number risk R6 is judged on (step 1.2). |
| `rfid` | string | — | see below | yes | RFID reader state |

`mode` values, and no others:

| `mode` | Meaning | Console must |
|---|---|---|
| `"drive"` | Armed. Commands are being acted on. | Enable drive controls |
| `"safe"` | Motors cut. Awaiting a fresh command to re-arm. | Show safe mode; drive controls remain usable, since a command is what re-arms |
| `"incompatible"` | Protocol version mismatch (§4). Will not arm. | Disable drive controls and show the mismatch |

`rfid` values, and no others. Scene 1 requires the reader to report green before the mission
may start, so "no reader fitted" and "reader fitted but broken" must be distinguishable:

| `rfid` | Meaning |
|---|---|
| `"absent"` | No reader on this build. `caps` will not contain `rfid` either. |
| `"ready"` | Reader is up and answering, no tag currently in range |
| `"scanning"` | A tag is being read right now — expect `tag` frames (§4.4) |
| `"fault"` | Reader fitted but not responding, or reporting an error |

**Reserved field names**, so later additions cannot collide with something else: `mast`,
`pan`, `tilt`, `uptime_ms`, `errors`, `run`, `session`. Adding any of these is additive and
needs no version bump (§2.3).

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

### 4.4 `tag` — RFID tag read

Sent **unsolicited, once per accepted read**, whenever the reader has a tag in range. This is
the payload of the revised mission: it drives both the "getting warmer" signal meter of
Scene 5 and the TAG DETECTED / composition lookup of Scenes 6–7.

```json
{"t":"tag","id":"E280116060000208A1B2C3D4","rssi":-47,"ts":184320}
```

| Field | Type | Unit | Range | Required | Meaning |
|---|---|---|---|---|---|
| `t` | string | — | `"tag"` | yes | Discriminator |
| `id` | string | — | 4–64 hex chars, **uppercase** | yes | The tag's EPC, as hex. Opaque to the rover — it never interprets it. |
| `rssi` | integer | dBm | −100 … 0 | yes | Reader's signal strength **for this read**. The proximity cue, not the Wi-Fi RSSI of §4.2. |
| `ts` | integer | ms | ≥ 0 | yes | Rover uptime at the read, for ordering and for the session log |

**Why a separate unsolicited frame rather than fields on telemetry:**

- **A read is an event, not a state.** Folding it into a 2 Hz periodic snapshot would silently
  drop reads that happen between frames, and a missed read is exactly what the operator is
  trying to detect.
- **The signal meter needs to be faster than telemetry.** At 500 ms the "getting warmer" cue
  of Scene 5 would lag the rover's motion badly enough to be useless for closing in.
- Telemetry stays a fixed-size heartbeat, which keeps the stale-telemetry detector in the
  console meaningful.

**Rate limit: at most one `tag` frame per tag per 100 ms**, coalescing anything faster and
keeping the strongest RSSI in the window. A UHF reader can report the same tag dozens of times
a second, and this socket is the *control* channel — flooding it with reads would attack R1
directly, which is the risk the whole channel separation exists to protect (§1.1).

**Multiple tags in range are each reported separately**, under their own rate limit. Deciding
which one the operator means is the console's problem, not the rover's — the rover reports
what it hears.

The rover does **not** look up compositions, hold a tag database, or decide what a read means.
Tag ID to composition is a console-side lookup table (proposal §5.4), editable per arena setup
without reflashing.

### 4.5 Latency target

Agreed at step S.7, before any hardware existed, so that steps 1.8 and 3.3 measure against a
number rather than against an opinion formed after seeing the result. **This is the pass/fail
line for risk R1.**

| | Value | Why |
|---|---|---|
| **Target** | 95th percentile ≤ **100 ms** | Below this, driving feels immediate and Scene 5 fine alignment is precise |
| **Ceiling** | 95th percentile ≤ **250 ms** | Half the 500 ms command timeout. Past it, ordinary jitter starts tripping the failsafe mid-drive, so it is a hard limit rather than a comfort one |
| **Loss** | < 1% of probes unanswered | Above this the 150 ms command repeat stops covering the gaps |

The **percentile**, not the mean: fine alignment is ruined by the occasional 400 ms sample, not
by a good average. The console colours its readout on p95 for the same reason.

Measured on the simulator at step S.7, the console-to-rover baseline is **~7 ms**, leaving
roughly 93 ms of headroom for real Wi-Fi and two concurrent camera streams. Every run since
S.7 writes samples to CSV in one fixed format (`console/scripts/rtt_log.gd`), so the idle-link
distribution from 1.8 and the both-feeds-live distribution from 3.3 can be compared directly.

If 3.3 misses this, the fallbacks in priority order are: reduce video bitrate → move video to
5 GHz if the cameras support it → Bluetooth control fallback per storyboard §4.3.

---

## 5. Versioning

- The current version is **`2`**, and it is a single integer. There is no minor version.
- **Bump it only for a breaking change**: removing a field, renaming one, changing a unit,
  narrowing a range, or changing the meaning of an existing value. *Adding* a message type or
  an optional field is backward compatible and does not bump it (§2.2, §2.3).
- Both sides compare the `v` they receive against the single version they themselves speak.

### 5.1 Why the steered chassis *did* bump the version

Step 0.2 chose a chassis with one drive motor and one steering motor. The same message-by-
message check that let Revision 2 through at v1 gives the opposite answer here:

| Change | Breaking? | Why |
|---|---|---|
| `l` / `r` removed from `drive` | **Yes** | Removing a field is breaking by the rule above. Worse, it breaks *quietly*: §2.4 makes missing fields default, so a v1 console's `{"cmd":"drive","l":0.6,"r":0.6}` would have parsed at a v2 rover as `fwd: 0, steer: 0` — a perfectly valid frame that refreshes the failsafe, keeps the rover armed, and moves nothing. The operator holds FORWARD and watches a healthy link do nothing. |
| `fwd` / `steer` added to `drive` | No, on its own | Additive fields alone would not bump it (§2.3) — it is the retirement above that does. |
| Meaning of a turn changed | **Yes** | v1 turned by opposing the two sides, and could pivot in place. v2 cannot: `steer` without `fwd` moves no part of the rover across the ground (§3.2). That is a changed meaning, not a changed field. |
| `caps` gains `steer3` / `steerprop` | No | The *shape* is unchanged — still an array of strings, still ignorable by a console that has not heard of a token. This is the part that did not need the bump, and is why a future proportional-steering rover will not need another one. |
| `stop`, `mast`, `ping`, `hello`, `tlm`, `pong`, `tag` | No | Untouched at S.14, field for field. |

**The first row is the whole argument.** A silent failure is exactly what the handshake exists
to catch, so it must be caught rather than tolerated: this is a case where *refusing* to
interoperate is the safe behaviour and quietly interoperating is the dangerous one.

Two mechanisms enforce it, deliberately belt-and-braces:

1. **The handshake** (below) — a v1 console gets `mode: "incompatible"`, both version numbers
   on screen, and no actuation at all. This is the primary defence.
2. **Retiring `l`/`r` as hard rejections** (§2.3a) rather than letting §2.3 ignore them. A
   frame carrying them is malformed, so it is dropped *and does not refresh the failsafe*
   (§2.6) — meaning a console that somehow drives without a correct handshake leaves the rover
   in safe mode within 500 ms rather than armed and inert. Belt to the handshake's braces, and
   it costs one check in the parser.

### 5.2 Why Revision 2 did not bump the version

Proposal Revision 2 removed the robotic arm and added RFID sensing, which sounds like a
breaking change and is not one. Checked message by message at step S.9:

| Change | Breaking? | Why |
|---|---|---|
| `arm` command removed | No | §2.2 already requires an unknown `cmd` to be logged and dropped, never treated as an error. An old console that still sends `arm` is ignored by a new rover; a new console simply never sends it. |
| `tag` frame added | No | New message type. §2.2 requires an unrecognised `t` to be dropped with a warning, so an old console tolerates it. |
| `rfid` field added to telemetry | No | Additive field; §2.3 says unknown fields are ignored. |
| `caps` values changed | No | The *shape* is unchanged — still an array of strings. `caps` exists precisely so the set can change without the protocol changing, and a console that has never heard of `rfid` just never enables a control for it. |

So Revision 2 kept `v` at **1**. This is worth recording because it is the payoff of two
decisions made in S.2 that looked like over-engineering at the time: making unknown verbs
non-fatal, and putting capability discovery in the handshake instead of hard-coding it. A whole
mission change went through the contract without a flag day.

It is also worth keeping next to §5.1, because the two together are the honest picture: the
mechanism bought Revision 2 for free and did **not** buy the chassis change for free. The line
between them is that Revision 2 only ever *added* and *removed whole verbs*, while S.14 changed
what an existing verb's existing fields mean. Note the last line of that older analysis, which
called this exactly: *"what would force a bump: renaming `l`/`r`…"*. It did.

What *would* force a bump to v3: changing throttle to a percentage, changing the failsafe
timeout's meaning, or making `id` in §4.4 something other than hex. Notably **not** on that
list: fitting a chassis with proportional steering, which §3.2.1 and the `caps` tokens already
accommodate.

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

**Nothing moves before the handshake agrees.** The rover refuses to arm until it has
exchanged a matching `hello` on the *current* connection, whatever commands arrive in the
meantime. Until then it reports `mode: "safe"` — it is waiting, not broken.

**If the console sends no `hello` within 2000 ms** of the socket opening, the rover stops
waiting and reports `mode: "incompatible"`, which is the difference between the operator
reading "it hasn't started yet" and "it is never going to move". An old console that has never
heard of `hello` therefore fails closed, not open.

The mismatch state is **not** safe mode: safe mode is cleared by the next valid command, while
an incompatible peer is not cleared by driving at it or by waiting. They are deliberately
distinct `mode` values for that reason.

**A new connection does clear it.** The handshake is scoped to the connection, so a correct
console can take over from a wrong one without power-cycling the rover. This is not a
loophole — the replacement connection must complete its own handshake before anything moves.

---

## 6. Failsafe contract

**This section is the safety-critical part of the protocol. It is not an optimisation, and no
refactor may weaken it.** A degraded link must never leave the rover driving into the glass
(risk R1). Step S.4 covers all of it in native unit tests; step 1.6 proves it on real hardware
and is the one gate the plan does not negotiate past.

### 6.1 Safe mode

**Safe mode means: current cut to both drive and steering motors, servo commands ignored,
`mode` reported as `"safe"`.** The mast head is *not* forcibly moved — it holds its last
commanded angle, because slewing a camera on a pole while the link is degrading helps nobody,
and with one camera it is the operator's only way of seeing what happened. Safe mode stops the
rover; it does not reset it.

#### Failing safe centres the steering, at no cost

A property of the chosen chassis rather than of this code, and the one piece of good news in
the 0.2 decision ([chassis-envelope §8.5](chassis-envelope.md)): the steering motor is held
against its end stop **only while powered**, and a spring recentres the axle when it is not.
Safe mode cuts that power along with the drive.

**So a rover that fails safe mid-turn coasts straight rather than continuing to arc into the
glass.** Under v1's skid steer the equivalent failure coasted along the curve it was already
on; here the geometry unwinds itself for free.

This was written into the contract as a property because things stated in a contract get
checked — and **step 1.3 checked it on real hardware (2026-08-13): the axle springs back to
centre on release, confirmed.** The property is real rather than hoped for, so §6.1 stands as
written and step 1.6 can rely on it when it measures coast distance after a link cut.

Re-check it if the steering linkage is ever rebuilt or the spring replaced. A weak or absent
spring removes the property silently, and this paragraph would have to come back out.

The RFID reader **keeps reading** in safe mode. It actuates nothing and cannot move the rover,
and a tag read while stopped is still useful information — it may be exactly why the operator
stopped.

### 6.2 What trips it

| Trigger | Condition |
|---|---|
| **Command timeout** | No valid command for **`COMMAND_TIMEOUT_MS` = 500 ms** |
| **Disconnect** | WebSocket closes, cleanly or otherwise |
| **Boot** | The rover boots into safe mode and stays there until the first valid command |

Only **valid** frames refresh the timer: `drive`, `stop`, and `mast` that parsed
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
  Lifting a finger sends a stop. No latching, no toggle, no "set speed" control. This maps onto
  three-position steering unchanged: hold-LEFT / release-to-centre is the same gesture as
  hold-FORWARD / release-to-stop.
- **Repeat while held** — see §6.5.
- **Disable drive controls whenever the link is not established**, so a frozen console cannot
  look live (step S.6).
- **Do not present steering as turning.** A steering control pressed at zero throttle moves the
  wheels and not the rover (§3.2). The pad must not imply otherwise — an operator who presses
  LEFT expecting a pivot, sees nothing happen, and presses harder is the failure mode here.
  What it should show instead is step S.15's call.

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

Scene 1, then a Scene 5 approach into a Scene 6 tag read, with time flowing downward.
Telemetry is trimmed to the frames that change something.

```
t=0.00  console → rover   {"cmd":"hello","v":2}
t=0.01  rover → console   {"t":"hello","v":2,"fw":"0.2.0","caps":["drive","steer3","rfid"]}
t=0.01  rover → console   {"t":"tlm","battery_v":12.42,"mode":"safe","rssi":-54,"rfid":"ready"}
        ...console shows LINK ESTABLISHED, drive + rfid green, mast greyed (not in caps);
           steer3 tells it the steering is three-position, so it labels the pad accordingly

        ...operator presses and holds FORWARD — Scene 5, closing on a rock
t=1.20  console → rover   {"cmd":"drive","fwd":0.4,"steer":0}   ← arms the rover
t=1.35  console → rover   {"cmd":"drive","fwd":0.4,"steer":0}   ← §6.5 repeat
t=1.50  rover → console   {"t":"tlm","battery_v":11.88,"mode":"drive","rssi":-56,"rfid":"ready"}
t=1.50  console → rover   {"cmd":"drive","fwd":0.4,"steer":0}

        ...drifting right of the rock; operator taps LEFT while still holding FORWARD
t=1.55  console → rover   {"cmd":"drive","fwd":0.4,"steer":-1}  ← full left lock (§3.2.1)
t=1.65  console → rover   {"cmd":"drive","fwd":0.4,"steer":0}   ← released; spring centres

        ...the antenna comes into range — Scene 6
t=1.71  rover → console   {"t":"tag","id":"E2801160600002","rssi":-63,"ts":1710}
t=1.80  console → rover   {"cmd":"drive","fwd":0.4,"steer":0}
t=1.81  rover → console   {"t":"tag","id":"E2801160600002","rssi":-52,"ts":1810}
        ...signal meter climbing; reads rate-limited to 10 Hz (§4.4)

        ...operator lifts off with the rock in range
t=1.86  console → rover   {"cmd":"drive","fwd":0.0,"steer":0}   ← still armed, speed zero
t=1.91  rover → console   {"t":"tag","id":"E2801160600002","rssi":-47,"ts":1910}
t=2.00  rover → console   {"t":"tlm","battery_v":12.30,"mode":"drive","rssi":-56,"rfid":"scanning"}
        ...console: TAG DETECTED, composition looked up locally — Scene 7

        ...console goes quiet — crash, or Wi-Fi drops
t=2.36  rover: 500 ms since last command → safe mode, both motors cut
t=2.50  rover → console   {"t":"tlm","battery_v":12.40,"mode":"safe","rssi":-71,"rfid":"scanning"}
        ...steering unpowered, so the axle spring-centres and the coast is straight (§6.1);
           the reader keeps reading — it actuates nothing

        ...link recovers. The rover does NOT resume driving (§6.3).
```

And the failure §5.1 exists to prevent, with a v1 console left in service:

```
t=0.00  console → rover   {"cmd":"hello","v":1}
t=0.01  rover → console   {"t":"hello","v":2,"fw":"0.2.0","caps":["drive","steer3"]}
t=0.01  rover → console   {"t":"tlm","battery_v":12.42,"mode":"incompatible","rssi":-54,"rfid":"absent"}
        ...console: "ROVER v2, CONSOLE v1", drive pad disabled. It never gets to send a frame.

        ...had the handshake not caught it, §2.3a still would:
        console → rover   {"cmd":"drive","l":0.6,"r":0.6}
        rover: retired field `l` → malformed, dropped, failsafe timer NOT refreshed (§2.6)
        rover: 500 ms later → safe mode. Visibly stopped, rather than armed and inert.
```

---

## 8. Implementation status

Honest accounting of what exists today, against this document. Nothing here has run on
hardware — the ✅ rows mean "written and matches this document", not "proven".

Three implementations are held in step: the firmware, the Godot console, and
[tools/fake_rover.py](../tools/fake_rover.py), which speaks the rover half for desktop
development. A behaviour difference between any two of them means one has a bug — that is
what writing it three times is for.

| Message | Firmware | Console | Simulator | Notes |
|---|---|---|---|---|
| `drive` (v2: `fwd`/`steer`) | ✅ S.14 | ✅ S.15 | ✅ S.15 | All three back in step at v2 |
| Retired `l`/`r` rejection (§2.3a) | ✅ S.14 | n/a — sender | ✅ S.15 | The simulator mirrors the firmware's strict parsing, so it owes this check too |
| `caps` steering token (§4.1) | ✅ reports `steer3` | ✅ refuses to arm without exactly one | ✅ advertises, and can be told to omit it | Drop it with `--caps drive` to check the console still refuses |
| Steering ⇒ no pivot (§3.2) | n/a — `Drive` just actuates | ✅ pad says so when steering with no throttle | ✅ bicycle model; `w ∝ v`, so a stationary rover cannot turn | The behaviour S.15 exists to make true everywhere |
| `stop` | ✅ | ✅ | ✅ | F2 closed in S.6 — `send_stop()`, wired to the STOP button. Unchanged by S.14 |
| `mast` | ⚠️ accepted, ignored | ✅ sender exists | ⚠️ accepted, ignored | Actuated in step 3.4 |
| ~~`arm`~~ | **removed** | **removed** | **removed** | Retired in S.9 with the manipulator. No version bump needed — see §5. |
| `hello` | ✅ | ✅ | ✅ | Console sends it on connect and retries every 1 s until answered; rover replies with `caps` and refuses to arm on a mismatch |
| Handshake gate (§5) | ✅ **armed** | ✅ | ✅ | Rover will not arm without a matching `hello` on the current connection; the 2000 ms deadline then flips telemetry to `incompatible`. Closed in S.6. |
| `ping` / `pong` | ✅ | ✅ | ✅ | Console probes at 4 Hz and reports RTT, rolling p95, and probe loss; samples go to CSV. Closed in S.7. |
| `tlm` | ✅ tagged | ✅ dispatches on `t` | ✅ tagged | F1 closed: the console routes by discriminator, so a `pong` cannot land in the telemetry strip |
| `rfid` field (§4.2) | ✅ reports `absent` | ✅ readiness chip | ✅ switchable | No reader hardware exists, so the firmware reports `absent` until Phase 2 |
| `tag` (§4.4) | ⚠️ **frame defined, nothing emits it** | ✅ dispatched, logged | ⚠️ manual injection only | Real reads need the reader (step 2.6); the simulator gets tagged rocks and RSSI in **S.10**; the console gets the signal meter and analysis panel in **S.11** |
| Failsafe timeout + disconnect (§6.2) | ✅ `lib/Safety` | — | ✅ mirrored | 500 ms; a pure function since S.3, unit-tested in S.4 |
| Re-arm rule (§6.3) | ✅ `lib/Safety` | — | ✅ mirrored | Scoped to the connection, so a reconnect inside the timeout window cannot arm |
| Strict parsing (§2.5, §1) | ✅ `lib/Protocol` | — | ✅ mirrored | Wrong types and oversize frames rejected whole; F4 closed in S.3 |
| Command repeat (§6.5) | — | ✅ | — | F3 closed in S.6 — `rover_link.gd` repeats a held drive command every 150 ms |
| Drive pad gating (§6.4) | — | ✅ | — | Buttons disabled unless the link is live and telemetry fresh |

### Findings raised while writing and implementing this document

**All four are now closed.** Kept here because each one records a decision, and because the
same mistakes are easy to reintroduce.

**F3 — the console never repeated a held drive command.** *(Closed in S.6.)* `console.gd`
connected `button_down` and `button_up` and nothing else, so holding FORWARD sent exactly one
frame and the rover cut the motors 500 ms later. Measured against the simulator before the
fix: one tap moved the rover **~0.06 m of the 3.0 m arena** and stopped, with the operator's
finger still down. `rover_link.gd` now owns the repeat at 150 ms (§6.5), which took the same
2-second hold to ~0.44 m.

**F1 — rover → console frames had no discriminator.** *(Closed: firmware S.3, console S.6.)*
Both sides now tag and dispatch on `t`. Before, the console forwarded *any* parsed dictionary
to the telemetry strip, which would have displayed S.7's `pong` frames as telemetry.

**F2 — no console-side `stop` sender.** *(Closed in S.6.)* `rover_link.gd` gained
`send_stop()`, and the STOP button calls it instead of sending `drive 0,0`.

**F4 — parsing was more permissive than §2.5 and §1 require.** *(Closed in S.3.)* The old
`doc["l"] | 0.0f` leaned on ArduinoJson's implicit conversion, so `{"l":"0.6"}` was coerced to
`0.6` rather than dropped, and there was no frame-size check. `protocol::parse` now rejects
both, along with wrong-typed angles, non-boolean `grip`, and a `joints` array that is empty or
over-long — and decodes joints into a scratch array first, so a bad angle halfway along cannot
leave the earlier joints applied.

None of the four was ever a shape mismatch — **every field of every message every
implementation sends matches this document exactly**, which is what step S.2's verification
asked for.

---

## 9. Open items

Deliberately unresolved, each with the step that closes it.

| # | Item | Closed by |
|---|---|---|
| 1 | `mast` pan/tilt ranges are placeholders | Step 3.4 — measured mechanical limits |
| ~~2~~ | ~~Arm joint count and per-joint ranges~~ — **moot; the arm was removed in Revision 2** | Closed |
| ~~3~~ | ~~Round-trip latency target~~ — **agreed at S.7: p95 ≤ 100 ms, ceiling 250 ms (§4.5)** | Closed |
| 8 | **The steering hold duty.** Three-position steering means the motor sits against a mechanical end stop for as long as the operator holds a turn — a continuous stall. `Drive` therefore holds it at a reduced duty rather than full, which is a guess until the stall current is on a meter against the L293D's 600 mA channel rating | Steps 1.3, 1.7 |
| 9 | **Whether the steering spring-centres strongly enough** to make the §6.1 failsafe property real | Step 1.3 |
| 10 | Whether the §3.2.1 threshold of 0.5 is the right place to round, once there is a real steering linkage to feel | Step 1.3 |
| 4 | Whether the mission/session state of Phase 4 rides this socket or stays console-only | Step 4.2 |
| 5 | Bluetooth fallback framing, if Wi-Fi proves inadequate | Step 3.3 decision point |
| 6 | **Tag `id` length and format** as the chosen reader actually reports it — §4.4 says 4–64 uppercase hex, which is a guess until a reader exists | Steps 0.1, 2.1 |
| 7 | **Whether the 100 ms tag rate limit is right.** Too slow and the signal meter feels laggy while closing in; too fast and reads compete with drive commands (R1) | Step 3.3, against the real reader |

---

*Protocol v2 · frozen at S.2 · retargeted to proposal Revision 2 at S.9 · retargeted to the
steered chassis at S.14 · no hardware procured*
