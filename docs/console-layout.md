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
    Forward     249 x  256 px    44.8 x  46.0 mm   ok
    Left        250 x  256 px    45.0 x  46.0 mm   ok
    Stop        249 x  256 px    44.8 x  46.0 mm   ok
    Right       249 x  256 px    44.8 x  46.0 mm   ok
    Back        249 x  256 px    44.8 x  46.0 mm   ok
    TiltUp      167 x  258 px    30.0 x  46.4 mm   ok
    PanLeft     137 x  257 px    24.6 x  46.2 mm   ok
    ...
    all 10 controls clear 15 mm
```

The smallest control is `PanLeft` at **24.6 mm**, still 1.6× the minimum. Drive buttons are
44.8 × 46.0 mm, roughly three times the minimum in each axis, because they are the ones
pressed under time pressure.

The count dropped from 16 to 10 at step S.9, when Revision 2 removed the arm and its six
buttons. The analysis panel that replaced them has no touch targets of its own yet.

The audit warns and marks its own figures untrustworthy if the viewport is not the assumed
size — which is what happens in a headless run, where there is no window and the viewport
comes out square (1920 × 1920). **Millimetre figures are only meaningful from a windowed
run at the target resolution.**

## 3. Region map

```
+------------------------------------------------------------------------------+
| TELEMETRY STRIP            link · mode · battery · rssi · rtt/p95             |  ~72 px
+------------------------------------------------------------------------------+
| SUBSYSTEMS   drive · mast · rfid                             firmware / caps  |  ~72 px
+---------------------+--------------------------------+-----------------------+
|                     |                                |                       |
|  MAST               |  DRIVE                         |  ANALYSIS             |
|  reserved, step 3.4 |  live now                      |  reserved, step S.11  |
|                     |                                |                       |
|      [ TILT UP ]    |         [ FORWARD ]            |     signal meter      |
|                     |                                |                       |
| [PAN L][CTR][PAN R] |   [ LEFT ][ STOP ][ RIGHT ]    |     composition       |  ~840 px
|                     |                                |      report card      |
|      [ TILT DN ]    |         [  BACK  ]             |                       |
|                     |                                |      session log      |
|   507 px / 0.62     |        818 px / 1.00           |    507 px / 0.62      |
+---------------------+--------------------------------+-----------------------+
```

Widths are `size_flags_stretch_ratio` on three panels in one `HBoxContainer`, so the split
holds at any resolution. Drive gets the centre and the largest share; the two hands rest
naturally on the outer panels.

**The mast panel is not a placeholder — it is the real control set, disabled.** Every button
step 3.4 needs is already present, named, and sized, so adding the behaviour is wiring up
existing controls rather than a redesign.

**The analysis panel is different: it is a display, not controls**, so there are no touch
targets to pre-size — only the region to reserve. It held the arm controls until proposal
Revision 2 removed the manipulator (step S.9); the region survived the mission change intact,
which is the payoff for reserving space by *area* rather than by widget.

| Region | Contents | Arrives at |
|---|---|---|
| Drive | forward / back / left / right / stop | **live** |
| Mast | pan left / right, tilt up / down, centre | step 3.4 |
| Analysis | RFID signal meter, composition report card, session log | step S.11 |

The readiness row reports `caps` from the handshake honestly and separately: a rover that
*has* an RFID reader shows RFID READY even while the console's analysis panel is still a
placeholder. Hardware presence and console support are different facts and are displayed as
such.

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

Each control is an independent `Button` and nothing takes a global input grab, so touches
route by index to whatever is under them — one finger on drive and another on mast do not
contend by construction.

**This has not been confirmed on real hardware, because there is no touchscreen yet.** It
is confirmed at step 3.5, on the actual panel, along with the DPI retune. What *is* handled:

- Hold-to-drive throughout: press starts the repeat, release ends it.
- Losing window focus mid-press releases the held command. Without that, alt-tabbing while
  driving would leave the 150 ms repeat running against a rover nobody is watching.
- A control that becomes disabled while held is released explicitly, because a disabled
  `Button` never emits `button_up`.

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
