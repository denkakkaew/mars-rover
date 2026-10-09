# Console layout and touch sizing

Companion to [protocol.md](protocol.md). That file says what the console *says*; this one
says how it is *laid out*, and why the regions are the size they are.

Written at step S.8. **Superseded in part by step 3.5**, which retunes all of this against
the real touchscreen once one exists — at which point the assumption below becomes a
measurement.

---

## 1. The display assumption

**No display has been chosen.** The panel is picked in step 0.4 as part of the bill of
materials. Everything here assumes:

| | Value |
|---|---|
| Panel | 15.6″ diagonal, 16:9 |
| Resolution | 1920 × 1080 |
| Active area | 13.60″ × 7.65″ (345 × 194 mm) |
| Density | **5.56 px/mm** (≈141 ppi) |

Derivation, so it can be redone for a different panel: a 16:9 rectangle with a 15.6″
diagonal is 15.6 × 16/√(16²+9²) = 13.60″ wide. The diagonal in pixels is
√(1920² + 1080²) = 2203 px, over 15.6″ × 25.4 = 396.2 mm, giving 5.56 px/mm.

The assumption lives in two constants in [console.gd](../console/scripts/console.gd) —
`ASSUMED_DIAGONAL_IN` and the project viewport size. **Changing the panel means changing
those and re-reading the audit**, not hunting through the scene.

## 2. Touch targets

**Minimum 15 mm = 83 px.** Fifteen millimetres is about the smallest target an adult
fingertip hits reliably *without looking* — and the operator will be looking at the camera
monitors, not at the console, for most of a mission run.

The console audits every interactive control against this at startup and prints the result,
so a bad layout reports itself rather than being discovered on the bench:

```
Touch audit — assuming 15.6" 1920x1080 panel, 5.56 px/mm, 83 px minimum
    viewport actually 1920 x 1080
    DriveStick  461 x  461 px    82.9 x  82.9 mm   ok
    HeadStick   636 x  636 px   114.4 x 114.4 mm   ok
    Stop        328 x  100 px    59.0 x  18.0 mm   ok
    SpeedMode   329 x  100 px    59.2 x  18.0 mm   ok
    NudgeLeft   157 x  100 px    28.2 x  18.0 mm   ok
    ...
    all 9 controls clear 15 mm
```

**The drive and camera controls are thumbsticks, not button grids** (branch `ui-improve`,
[virtual_joystick.gd](../console/scripts/virtual_joystick.gd)). A stick is audited on the
diameter of its drawn circle, not the rectangle it sits in. The smallest targets are now
the 100 px rows (STOP, SPEED, the nudges, CENTRE) at 18.0 mm — 1.2× the minimum on their
short axis.

The audit warns and marks its own figures untrustworthy if the viewport is not the assumed
size — which is what happens in a headless run, where there is no window and the viewport
comes out square (1920 × 1920). **Millimetre figures are only meaningful from a windowed
run at the target resolution.**

## 3. Region map

```
+------------------------------------------------------------------------------+
| TELEMETRY STRIP            link · mode · battery · rssi · rtt/p95             |  ~72 px
+------------------------------------------------------------------------------+
| SUBSYSTEMS   drive · pan/tilt · detector                     firmware / caps  |  ~72 px
+---------------------+--------------------------------+-----------------------+
|                     |                                |                       |
|  CAMERA PAN/TILT    |  DRIVE                         |  ANALYSIS             |
|                     |                                |                       |
|      .-----.        |           .-----.              |     signal meter      |
|     /   o   \       |          /  :o:  \             |                       |
|     \       /       |          \  : :  /             |     composition       |  ~840 px
|      '-----'        |           '-----'              |      report card      |
|    [ CENTRE ]       |   [ STOP ]     [ SPEED ]       |                       |
|   PAN +0  TILT +0   |   [ARC L][FWD][BACK][ARC R]    |      session log      |
|   507 px / 0.62     |        818 px / 1.00           |    507 px / 0.62      |
+---------------------+--------------------------------+-----------------------+
```

Widths are `size_flags_stretch_ratio` on three panels in one `HBoxContainer`, so the split
holds at any resolution. Drive gets the centre and the largest share; the two hands rest
naturally on the outer panels.

**Both sticks are game-style: a fixed base and a knob that springs back to centre.**
Output is x right / y up, -1..1, with a dead zone so a resting thumb commands nothing, and
the disc is stretched onto the square so a corner gives full throttle *and* full lock.

- **Drive stick** — up/down is throttle (proportional, then shaped by the speed mode in
  `rover_link.gd`), left/right is steering. Dashed guides mark ±0.5, where the rover's
  three-position steering snaps to full lock. Lifting off, or the dead zone, is a release
  (one zero-throttle frame), never a held zero.
- **Camera stick** — deflection is a *rate*: the head turns at up to 40 °/s scaled by how
  far the stick is pushed, and holds where it is let go. A small push is a fine aim.
- The keyboard still works and sums with the sticks; a held key is mirrored on the knob in
  green, so it gets the same on-screen confirmation a thumb does.

**The analysis panel is mostly a display**: its one control is the ANALYSE button, sized
like the others (100 px tall) and covered by the startup touch audit. It held the arm controls until proposal
Revision 2 removed the manipulator (step S.9); the region survived the mission change intact,
which is the payoff for reserving space by *area* rather than by widget.

| Region | Contents | Arrives at |
|---|---|---|
| Drive | drive stick (throttle + steer), stop, speed, nudges | **live** |
| Camera pan/tilt | pan/tilt stick (rate), centre | **live** (fw 0.2.0) |
| Analysis | detection readout, ANALYSE, composition report card, session log | **live** (camera, 2026-09-26; analyse step 2026-10-08) |

### The analysis panel

Fed by the host-side camera detector ([vision/detect_stream.py](../vision/detect_stream.py))
through [detection_api.gd](../console/scripts/detection_api.gd), never by the rover. Four
stacked blocks, top to bottom, matching the order the operator needs them in Scenes 5 → 8:

1. **Detection readout**: a status line, a bar and a percentage, from the `POST /view`
   snapshots the detector sends about 4 times a second. The bar shows detection confidence,
   or analysis progress while an analysis runs. The fill is **coloured** as well as sized,
   because the operator is watching the camera monitor rather than this panel, and green in
   peripheral vision means "stop, there is something to analyse".
2. **ANALYSE**: enabled once the same object has held for 3 snapshots.
3. **Report card**: the sample name, its mineral class, then its elements by mass fraction,
   largest first, from `compositions.json` keyed by the detector label. It is filled only
   when an analysis completes.
4. **Session log**: most recent first, five rows shown, one row per analysed sample.

**Detect → analyse → reveal (2026-10-08).** The detector only knows what an object *looks*
like, so **no detector label and no manifest element ever reaches the screen**. The video
overlay says `OBJECT`, and the console says `OBJECT DETECTED`. The sample is named only
after the operator presses ANALYSE and holds the object in view for 3 s, which simulates
instrument integration time. That is the order a real rover works in, and it gives Scene 6
a deliberate operator action.

| State | Shows |
|---|---|
| Detector not running | `DETECTOR OFFLINE`, grey; DETECTOR chip amber |
| Looking, nothing there | `NO OBJECT IN VIEW`, empty grey bar |
| Object seen, not yet stable | `OBJECT DETECTED · acquiring`, amber bar, ANALYSE disabled |
| Same label for 3 snapshots | `OBJECT DETECTED · ready to analyse`, green bar, ANALYSE enabled |
| ANALYSE pressed | `ANALYSING OBJECT · hold position`, amber bar fills over 3 s, card says "analysis in progress" |
| Object lost for > 0.75 s mid-analysis | `ANALYSIS INTERRUPTED · object left view` for 3 s; no result, nothing logged |
| Analysis complete | report card filled, log entry; `ANALYSED · <sample>` while it stays in view |
| Object gone after an analysis | `OUT OF VIEW · last result held`: the bar falls but the result stays |
| Label not in the table | analysed as "Unclassified sample" in amber; the label goes to the console's output, not the screen |

A running analysis follows its own object. A second, more confident rock drifting into frame
does not interrupt it, and `POST /detection` is accepted but ignored, because the log
records analyses rather than sightings.

Holding the result is deliberate. Clearing the readout the instant the rover rolls away
would throw away Scene 7 just as the operator starts reading it; but leaving the *bar* up
would claim a rock that is gone. The two are separated on purpose.

The readiness row reports DRIVE and PAN/TILT from the handshake's `caps`, and DETECTOR from
the detector's own heartbeat — it runs on this PC, not on the rover, so no `caps` token could
describe it. DETECTOR is amber rather than red when offline: the rover is fine, the detector
is usually just not started, but nothing gets identified without it.

## 4. Palette

Dark ground, bright text — not the reverse. The console sits beside a lit glass box, and a
light UI throws reflections onto the very monitors the operator is reading.

| Role | Colour | Used for |
|---|---|---|
| Ready | `#3ddc84` | link established, subsystem ready, RTT within target |
| Warn | `#ffb300` | safe mode after a trip, stale telemetry, RTT over target |
| Fault | `#ff5252` | link down, protocol mismatch, RTT over ceiling, critical battery |
| Idle | `#78909c` | not fitted, no data yet |
| Ground | `#0b0f14` | page background |
| Panel | `#151b23` | panel fill, on a `#2a3542` border |

**State is never carried by colour alone.** The link strip changes its wording as well as
its colour, chips read READY / NO DATA / NOT FITTED in text, and a disabled button changes
its border and fill together. Roughly one man in twelve cannot separate the green and amber
used above.

Two related decisions, both made because a warning that is always on is a warning nobody
reads:

- **`MODE SAFE` is grey, not amber, before the rover has ever been armed.** At rest that is
  simply where a healthy rover sits. It turns amber only once the failsafe has actually
  taken it out of drive.
- **A subsystem the rover never advertised reads NOT FITTED in grey**, not a red fault.
  During Phases S and 1 only the drive exists.

## 5. Multi-touch

Each stick tracks its own finger by touch index from `_input` rather than relying on the
GUI's single mouse focus, so one thumb on the drive stick and the other on the camera stick
do not contend, and a second finger landing on a stick already held is ignored. With
`emulate_touch_from_mouse` on, the mouse is touch index 0, so the bench and the panel take
the same path.

**This has not been confirmed on real hardware, because there is no touchscreen yet.** It
is confirmed at step 3.5, on the actual panel, along with the DPI retune. What *is* handled:

- Hold-to-drive throughout: press starts the repeat, release ends it.
- Losing window focus mid-press releases the held command. Without that, alt-tabbing while
  driving would leave the 150 ms repeat running against a rover nobody is watching.
- A control that becomes disabled while held is released explicitly, because a disabled
  `Button` never emits `button_up` — and a stick disabled under the finger emits `released`
  itself. STOP (button or SPACE) also releases the drive stick, so a thumb still on it cannot
  re-arm the rover.

## 6. Regenerating the screenshots

```powershell
python tools/fake_rover.py --port 8330
$env:ROVER_URL = "ws://127.0.0.1:8330/"
$env:CONSOLE_SHOT = "C:\path\to\console.png"
& $env:GODOT_BIN --path console --resolution 1920x1080
```

`CONSOLE_SHOT` captures the window after four seconds — long enough for the link to come up
— and exits. Needs a real window; there is nothing to capture headless. Add
`--caps drive,mast,arm` or `--protocol-version 2` to the simulator to photograph the other
states.
