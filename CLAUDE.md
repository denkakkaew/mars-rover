# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current state

This is a design-and-planning workspace for a proposed educational Mars rover robot, now with
two scaffolded project skeletons. The repository is under git on branch `main`, with `origin`
pointing at `https://github.com/denkakkaew/mars-rover`.

**An ESP32 exists and passed step 1.1 on 2026-08-13** — an ESP32-D0WD-V3 rev 3 (4 MB flash, no
PSRAM) on **COM3** via a Silicon Labs CP210x, confirming `board = esp32dev`. Uploads are clean
at both 460800 and 921600. That is the *only* hardware: **no motors, driver, servos, camera,
chassis or battery have been procured**, so pin assignments in
[firmware/include/config.h](firmware/include/config.h) remain placeholders to be fixed at
step 1.3, and Phase 0 (the BOM and the power-budget decisions) is still unapproved.

**The mission was revised on 2026-07-27 (Revision 2): the robotic arm is gone, replaced by a
UHF RFID reader that identifies rocks in place, and the two cameras are reduced to one.** See
"Documents and their authority" below — several files in this repo still describe the old
Sample-Return mission.

**Phase S is complete (S.1–S.15).** It delivered the protocol contract, host-tested firmware
modules, a desktop simulator with tagged rocks, and a console that runs the whole
seek → scan → identify loop against that simulator with no hardware in existence. S.9–S.11
retargeted the Revision 1 work onto the revised mission; S.12–S.13 made the simulator honest
and gave the console fine-drive modes; **S.14–S.15 retargeted the protocol, firmware,
simulator and console onto the steered chassis chosen at 0.2, which is what protocol v2 is.**
Everything from Phase 0 onward is either your decisions or blocked on procurement.

**Rock identification is on hold** and the sensing method is reopened (see
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md), step 2.0) — UHF RFID is one candidate rather
than a settled decision. Driving is the primary line of work.

- [plan/](plan/) — the proposal documents (see next section)
- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — the ordered build plan, in small steps
- [docs/protocol.md](docs/protocol.md) — the console↔rover message contract, **v2**
- [docs/console-layout.md](docs/console-layout.md) — screen regions, touch sizing, palette
- [docs/chassis-envelope.md](docs/chassis-envelope.md) — step 0.2's swept-circle arithmetic
  against the 1.4 m arena, why R5 did not discriminate between the skid-steer candidates, and
  **§8: what the steered chassis actually chosen changes** — the live section
- [docs/power-budget.md](docs/power-budget.md) — step 0.3's load table, four-rail plan and
  session energy estimate. **Awaiting your decisions (its §7).** Carries two open findings:
  F5 (the committed `BATTERY_DIVIDER_RATIO` would destroy the ADC pin) and F6 (a held turn is
  a continuous stall, and the L293D runs out of thermal headroom before current headroom)
- [console/](console/) — Godot 4 touchscreen operator console
- [firmware/](firmware/) — PlatformIO / Arduino-ESP32 rover firmware

## Commands

Neither toolchain is on `PATH`; invoke both by full path as shown.

**Console** — Godot 4.6.3, installed at
`C:\Users\User\Programs\Godot_v4.6.33\Godot_v4.6.3-stable_win64_console.exe` (also exported as
`$GODOT_BIN` in the VS Code integrated terminal). Use the `_console` binary — the plain
`.exe` detaches from the terminal and prints nothing.

```powershell
& $env:GODOT_BIN --path console                      # open in the editor
& $env:GODOT_BIN --path console --headless --import  # reimport assets, validate project
& $env:GODOT_BIN --path console --headless --quit-after 60   # smoke-run the main scene
```

The headless `--quit-after` run is the cheapest check that scripts, node paths, and signal
connections are all still valid; it exits 0 and prints nothing when the scene is healthy.

**Firmware** — PlatformIO 6.1.19 installed via pip. `pio.exe` is *not* on `PATH`, so drive it
as a Python module:

```powershell
cd firmware
python -m platformio run                    # build (esp32dev; it is the default_env)
python -m platformio test -e native         # host unit tests — no board needed
python -m platformio run -t upload          # flash (add -t upload -t monitor to do both)
python -m platformio device monitor         # serial monitor, 115200
python -m platformio run -t clean
python -m platformio run -e bringup -t upload -t monitor   # step 1.1 board sanity
python -m platformio run -e wifiscan -t upload -t monitor  # steps 1.2/0.5 Wi-Fi survey
```

Two bench sketches sit alongside `main.cpp`, each in its own env. All three environments
exclude the others' sources via `build_src_filter`, so nothing ever links two `setup()`s.

- `[env:bringup]` → [src/bringup.cpp](firmware/src/bringup.cpp): blink, a serial identity
  block (chip model, MAC, reset reason) and a character echo, with **no Wi-Fi and no
  `secrets.h`**, so a board can be proven alive before credentials are true of anything. It
  uploads at 460800 rather than 921600 on purpose — a failed upload at the fast rate is
  indistinguishable from a dead board on a *first* flash.
- `[env:wifiscan]` → [src/wifi_scan.cpp](firmware/src/wifi_scan.cpp): continuous 2.4 GHz
  scan, also credential-free. It names the SSIDs actually on the air (so `secrets.h` gets the
  exact spelling) and reports non-overlapping channel load for step 0.5. Because beacon RSSI
  needs no association, **this is the instrument for 1.2's RSSI-through-glass survey** and can
  be run before the arena network exists.

**Known defect F7**: `main.cpp`'s Wi-Fi join is an unbounded `while` loop — a wrong password or
an absent AP hangs the board in `setup()` forever, so the WebSocket server never starts and the
console cannot tell it from dead silicon. See IMPLEMENTATION_PLAN.md step 1.2.

The first build downloads the Xtensa toolchain and the pinned libraries. Copy
`firmware/include/secrets.h.example` to `secrets.h` (gitignored) and fill in real Wi-Fi
credentials before flashing — the committed placeholder will not associate.

**`python -m platformio test -e native` is the cheapest check on the firmware** and the one
to run after touching `lib/Protocol` or `lib/Safety` — it exercises the wire format and the
whole failsafe contract on the PC in about three seconds, with no ESP32 in the loop. It needs
a host compiler on `PATH`; this machine uses MinGW-w64 GCC from MSYS2 at
`C:\msys64\ucrt64\bin` (install packages with the full path `C:\msys64\usr\bin\pacman`, which
is deliberately kept off `PATH` so its Unix `find`/`sort` don't shadow the Windows ones).

Always pass `-e native` to `test`. A bare `pio test` would try to build a test runner for the
board; `test_ignore = *` on `[env:esp32dev]` blocks that, but the explicit flag is clearer.

**Fake rover** — [tools/fake_rover.py](tools/fake_rover.py) stands in for the ESP32 so the
console can be developed and demonstrated with no hardware. Install deps once with
`python -m pip install -r tools/requirements.txt`, then:

```powershell
python tools/fake_rover.py --view                  # ASCII arena, port 81
python tools/fake_rover.py --latency 250 --loss 5  # start with faults injected
python tools/fake_rover.py --caps drive,steer3,mast,rfid   # pretend Phases 2-3 are built
python tools/fake_rover.py --caps drive            # no steering token: console must refuse
python tools/fake_rover.py --steer-lock 15         # a turning circle that does not fit
python tools/fake_rover.py --read-range 0.5 --flaky 20 --dead-tags E2801160600002
```

Its motion model is a **steered bicycle model** (S.15), not skid steer: yaw rate is
`v · tan(δ) / wheelbase`, so **a stationary rover cannot turn** — steering at zero throttle
swings the axle and moves nothing. `--wheelbase` and `--steer-lock` set the minimum turning
circle, which is what risk R5 now means, and the ASCII view reports whether a U-turn fits the
drivable width. At the default 0.155 m / 25° it needs 0.95 m of 1.30 m; at 15° of lock it does
not fit at all, so those two switches are worth turning before trusting the verdict.

It carries five **tagged rocks** in the arena and models UHF backscatter well enough to be
useful: RSSI rising with the fourth power of distance as the rover closes, an antenna beam
that stops hearing past ±60°, and reads that go ragged near the sensitivity floor rather than
cutting off cleanly. Driving past a rock loses the read, because the antenna faces forward.
`--dead-tags` simulates risk R7 — a tag killed during embedding, which reads exactly like a
tag out of range and is invisible to the console by design.

Point the console at it with `ROVER_URL` rather than editing the scene — the committed
default in `rover_link.gd` stays the real rover:

```powershell
$env:ROVER_URL = "ws://127.0.0.1:81/"
& $env:GODOT_BIN --path console
```

While it runs, type `help` + Enter for runtime switches: `lat <ms>`, `loss <pct>`, `drop`
(hard disconnect), `tlm on|off`, `ver <n>`, `batt <volts>`, `reset`, the RFID ones —
`rocks`, `kill <id>`, `revive <id>`, `range <m>`, `flaky <pct>`, `tag <id> [rssi]` — and the
drive-model ones: `surface`, `slip`, `drift <deg>`, `coast`, `breakaway`, `chassis`,
`wheelbase`, `lock <deg>`, `dressing`, `model`.

The simulator mirrors the firmware's strict parsing and failsafe deliberately — **it is a
second implementation of the same contract**, so a behaviour difference between it and
`lib/Protocol`/`lib/Safety` means one of them has a bug. Keep them in step when the protocol
changes.

All three are back in step at **v2** as of S.15. The simulator's `parse()` carries the retired
`l`/`r` rejection for exactly this reason — it is the second opinion on the firmware's.

## Documents and their authority

The `plan/` directory holds several renderings of the proposal at different stages of
revision, **and they are not all the same mission.** Precedence when they disagree:

1. **[plan/storyboard-rev2.md](plan/storyboard-rev2.md) — Revision 2, 2026-07-27. This is the
   current proposal, and the file to edit.** The mission is "seek → scan → identify": no
   robotic arm, one mast camera, a front-mounted UHF RFID reader, and a simulated
   elemental-composition readout on the console. It is a Markdown transcription of
   [plan/storyboard-exploration.docx](plan/storyboard-exploration.docx);
   [the Thai copy](plan/storyboard-exploration-th.docx) is a translation of the same revision,
   not a different document. **Both .docx files are now downstream of the Markdown** — edit
   the Markdown and re-export.
2. [plan/mars-rover](plan/mars-rover) — the original raw requirements from the customer
   (9 numbered items), plain text, no extension. Still the source of truth for the *frame* of
   the project, but **Revision 2 deliberately departs from items 1, 6, 7 and 8** (arm, two
   cameras, collect-and-carry, two monitors) after discussion with the client. Where the two
   conflict on those points, Revision 2 wins.
3. [plan/storyboard.md](plan/storyboard.md) — **Revision 1, superseded.** The Sample-Return
   concept with the arm and two cameras. Historical, except that Revision 2 explicitly carries
   its §2 project rationale forward unchanged. Do not implement from it.
4. [plan/storyboard.html](plan/storyboard.html) and `plan/storyboard.docx` — renderings of
   Revision 1 or earlier. Historical. `~$r-rover.docx` is a Word lock file, not content.

To read a `.docx` without Word — useful for checking the Markdown against the exported copy:

```powershell
python -c "import re,zipfile;x=zipfile.ZipFile('plan/storyboard-exploration.docx').read('word/document.xml').decode();x=x.replace('</w:p>','\n');print(re.sub('<[^>]+>','',x))"
```

[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) tracks Revision 2 and opens with a table of
exactly what changed.

The proposal is written for a non-technical employer/sponsor audience and is deliberately
careful about tense: it describes what the system is *designed to* do, never claims working
hardware, and explicitly omits costs and dates until Phase 0 is approved. Preserve that
framing when editing — do not let the storyboard read as a report of a completed build.

## Target architecture (Revision 2)

Two independent channels, and keeping them separate is still the central architectural
decision:

- **Control channel** — Godot touchscreen console → ESP32 over Wi-Fi (WebSocket), with
  Bluetooth as fallback. ESP32 drives **one drive motor and one steering motor through an
  L293D** (steered chassis, chosen at step 0.2 — *not* skid steer; it cannot pivot in place)
  and the mast pan/tilt servos, polls the RFID reader over UART, and publishes telemetry and
  tag reads back.
- **Video channel** — the mast IP camera on its pan/tilt head → one monitor over Wi-Fi. It
  serves as both the survey view and, aimed forward and down, the driving view.

Video never shares the control channel, so streaming load cannot make driving laggy. Any
proposed change that merges them contradicts the design rationale in proposal §5.5 and risk R1.

**There is no robotic arm.** The front mounting point carries the RFID antenna instead. Tag
reads travel reader → ESP32 → console alongside telemetry; the **console**, not the rover,
turns a tag ID into a displayed composition, so the lookup table can be edited without
reflashing.

**One camera is a single point of vision** (risk R8) — if the mast head fails, the rover is
blind. That makes mast pan/tilt reliability higher priority than it was in Revision 1, and the
head should default to a forward-and-down driving position between surveys.

The arena is a sealed 1.4 m × 3.0 m glass box; the rover operates inside, the operator station
outside. The 1.4 m width constrains chassis footprint and turning radius (risk R5). Glass is
largely transparent to both 2.4 GHz Wi-Fi and UHF RFID, but that is a checkpoint to verify for
both bands, not an assumption (risk R6).

## Control protocol

**[docs/protocol.md](docs/protocol.md) is the authoritative contract** — every message, field,
unit, and range, plus the failsafe rules. It is at **v2**. When code and that document
disagree, the code is wrong; changing the protocol means editing the document first, bumping
the version, and then changing every implementation. Its §8 tracks what is implemented versus
specified, and carries the open findings.

Retargeted to Revision 2 at step S.9 with **no version bump**: the `arm` command went, the
`tag` frame (§4.4) and the `rfid` reader-status field (§4.2) came in. §5.2 shows message by
message why none of that was breaking — the payoff of two S.2 decisions (unknown verbs are
non-fatal, capabilities are discovered in the handshake rather than hard-coded).

**Bumped to v2 at step S.14**, because step 0.2 chose a chassis with one drive motor and one
steering motor. `drive` is now throttle plus steering; `l` and `r` are **retired**, and §5.1
explains why this one *was* breaking where S.9 was not: a v1 `drive` parses at a v2 rover as a
valid zero-throttle command, so the rover would sit armed and motionless while the operator
held FORWARD. Two defences, deliberately doubled up — the handshake refuses to arm a v1
console, and a retired `l`/`r` makes the frame malformed (§2.3a) so it does not refresh the
failsafe.

In short: JSON text frames over a WebSocket, ESP32 as server on port 81, console as client.
Console → rover carries a `cmd` discriminator, rover → console carries a `t`. `drive` and
`stop` are actuated; `mast` is an accepted shape the firmware logs and ignores until Phase 3.

```
{"cmd":"drive","fwd":0.6,"steer":-1.0}  # throttle + steering, -1.0 .. 1.0; steer < 0 = left
{"cmd":"mast","pan":0,"tilt":15}        # degrees
{"t":"tlm","battery_v":11.8,"mode":"safe","rssi":-58,"rfid":"ready"}   # every 500 ms
{"t":"tag","id":"E2801160600002","rssi":-47,"ts":184320}   # unsolicited, per read
```

**Steering is three-position** — full lock or centre, no intermediate angle, with a spring
recentring it unpowered. `steer` stays a **float on the wire** and the firmware thresholds it
at ±0.5; which kind of steering a build has is advertised in the handshake `caps` as `steer3`
or `steerprop`, so a later proportional chassis needs no v3. Two consequences worth holding
onto: there is **no small heading correction** (only a full-lock tap the other way), and
**failing safe centres the steering for free**, so a rover that cuts out mid-turn coasts
straight instead of arcing into the glass (to be confirmed on the bench at 1.3).

Safety behaviours that are deliberate and must survive refactors:

- The firmware cuts the motors if no valid command arrives within `COMMAND_TIMEOUT_MS`, and on
  socket disconnect.
- **Nothing arms without a `hello` handshake on the current connection**, so an unidentified
  console gets no actuation at all.
- Re-arming requires a fresh command, so a reconnect can never resume the last throttle.
- The console's drive buttons are hold-to-drive, and `rover_link.gd` **repeats the held
  command every 150 ms** — without that the failsafe cuts the motors mid-press. The repeat
  interval is chosen against `COMMAND_TIMEOUT_MS`; changing one means rechecking the other.
- The drive pad is disabled unless the link is live and telemetry fresh, so a dead console
  cannot look drivable.

A degraded link must never leave the rover driving into the glass. All of the firmware side is
covered by `test/test_safety` — change the logic and the tests should be what tells you.

**Control-latency target, agreed at S.7: 95th-percentile RTT ≤ 100 ms, hard ceiling 250 ms**
(docs/protocol.md §4.4). This is the pass/fail line for risk R1 at steps 1.8 and 3.3. The
console measures continuously and writes every sample to CSV — set `RTT_LOG` to choose the
file, otherwise it lands in the Godot user data folder and the path is printed at startup.
**Keep those CSVs**: 3.3's whole purpose is comparing its distribution against 1.8's.

## Code layout conventions

- [firmware/lib/Drive/](firmware/lib/Drive/) takes its pins through the constructor rather
  than including `config.h`. Subsystem modules (arm, mast) should stay self-contained the same
  way — §2 of the proposal commits to each subsystem being liftable into a future prototype.
  Only [firmware/src/main.cpp](firmware/src/main.cpp) reads `config.h`.
- **[firmware/lib/Protocol/](firmware/lib/Protocol/) and
  [firmware/lib/Safety/](firmware/lib/Safety/) must not include any Arduino header.** That
  constraint is what lets them build and be tested on the host; `Safety` takes time as a
  parameter rather than calling `millis()`. Decisions belong in these modules, so put new
  logic here rather than in `main.cpp`, which is deliberately transport and glue only.
- `platformio.ini` pins `espressif32@^6.9.0` (arduino-esp32 2.0.x) because `Drive` uses the
  2.x LEDC API (`ledcSetup`/`ledcAttachPin`). Bumping to core 3.x requires switching to
  `ledcAttach`.
- LEDC channel 0 is the drive motor and channel 1 the steering motor; servo work must claim
  its own. `Drive` runs them at **1.5 kHz, not 20 kHz** — an L293D is a slow Darlington part
  that cannot switch cleanly up there, and the low-speed torque it costs is exactly what the
  throttle floor depends on. The motors whine audibly as a result; that is the trade, not a
  fault.
- [console/scripts/rover_link.gd](console/scripts/rover_link.gd) owns all transport concerns
  (connect, reconnect, handshake, JSON framing, the drive repeat, RTT probing); UI scripts
  call its methods and read its state, and never touch the socket. It exposes `is_linked()`
  and friends rather than making callers reach into its `State` enum.
- The console assumes a **15.6" 1920×1080 touchscreen until step 0.4 picks one**, and audits
  every touch target against a 15 mm minimum at startup — see
  [docs/console-layout.md](docs/console-layout.md). The audit's millimetre figures are only
  valid from a *windowed* run at the target resolution; headless has no window and reports a
  square viewport, which the audit detects and warns about.
- Env vars drive the console for development, none of which change committed defaults:
  `ROVER_URL` (point at the simulator), `RTT_LOG` (latency CSV path), `CONSOLE_SHOT` /
  `CONSOLE_SHOT_DELAY` (capture a PNG after N seconds and exit), `COMPOSITION_TABLE`
  (alternative tag→composition file).
- **[console/data/compositions.json](console/data/compositions.json) maps tag ID → simulated
  elemental composition**, and is console-side on purpose: the rover only ever reports a tag
  ID, so an arena can be re-dressed and re-tagged without reflashing anything. Lookup order is
  `$COMPOSITION_TABLE` → `user://compositions.json` → the shipped `res://` copy. The
  percentages in it are **presets, not measurements** — the console has no spectrometer, and
  the file says so at the top. Keep it that way.
- A tag read is not a detection. [analysis_panel.gd](console/scripts/analysis_panel.gd)
  requires **three reads of the same tag within 1.5 s** before it will declare TAG DETECTED,
  because Scene 6 asks for "a stable read … not an intermittent or dropped signal" and the
  simulator produces exactly that ragged stream at the edge of range.

## Work sequencing

Build phases 0–4 (§7) are gated: each has exit criteria that must clear before the next begins
— procurement → drive platform → manipulator → vision & console → full mission integration.
The 8-scene mission storyboard (§6) is the acceptance test for Phase 4. When adding scope or
proposing implementation work, place it in the right phase rather than flattening the plan, and
check whether it interacts with an existing risk (R1–R6, §8) before introducing a new one.

[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) breaks those phases into individually
reviewable steps. **Work one step at a time and stop at its review gate** — do not start the
next step, or fold two together, without the user's explicit go-ahead. Steps in "Phase S" are
the only ones that can proceed with no hardware; everything from Phase 1 onward is blocked on
procurement.
