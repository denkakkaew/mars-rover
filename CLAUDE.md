# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current state

This is a design-and-planning workspace for a proposed educational Mars rover robot, now with
two scaffolded project skeletons. **No hardware has been procured**, so nothing here has run
against real motors, servos, or cameras — pin assignments in
[firmware/include/config.h](firmware/include/config.h) are placeholders to be fixed during
Phase 1. The repository is under git on branch `main`, with `origin` pointing at
`https://github.com/denkakkaew/mars-rover`.

Phase S of the implementation plan is in progress: S.1–S.4 are done, so the control protocol
is frozen in writing and the firmware's parsing and failsafe logic are covered by host tests.

- [plan/](plan/) — the proposal documents (see next section)
- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — the ordered build plan, in small steps
- [docs/protocol.md](docs/protocol.md) — the frozen console↔rover message contract
- [docs/console-layout.md](docs/console-layout.md) — screen regions, touch sizing, palette
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
```

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
python tools/fake_rover.py --view                 # ASCII arena, port 81
python tools/fake_rover.py --latency 250 --loss 5 # start with faults injected
python tools/fake_rover.py --caps drive,mast,arm  # pretend Phases 2-3 are built
```

Point the console at it with `ROVER_URL` rather than editing the scene — the committed
default in `rover_link.gd` stays the real rover:

```powershell
$env:ROVER_URL = "ws://127.0.0.1:81/"
& $env:GODOT_BIN --path console
```

While it runs, type `help` + Enter for runtime switches: `lat <ms>`, `loss <pct>`, `drop`
(hard disconnect), `batt <volts>`, `reset`. The simulator mirrors the firmware's strict
parsing and failsafe deliberately — **it is a second implementation of the same contract**,
so a behaviour difference between it and `lib/Protocol`/`lib/Safety` means one of them has a
bug. Keep them in step when the protocol changes.

## Documents and their authority

The `plan/` directory holds several renderings of the same proposal at different stages of
revision. Precedence when they disagree:

1. [plan/mars-rover](plan/mars-rover) — the original raw requirements from the customer
   (9 numbered items). This is the source of truth for *what the system must do*. It is a
   plain-text file with no extension.
2. [plan/storyboard.md](plan/storyboard.md) — the current, most complete proposal
   (executive summary, hardware/software spec, 8-scene mission storyboard, phased build plan,
   risk register, glossary). **Edit this file** when the proposal changes.
3. [plan/storyboard.html](plan/storyboard.html) — a standalone, styled presentation rendering
   of an earlier draft of the storyboard (self-contained CSS with light/dark theming). It is
   a separate artifact, not generated from the Markdown — changes to the Markdown do not
   propagate here.
4. `plan/*.docx` — exported/reviewer copies (`mar-rover.docx`, `storyboard.docx`,
   `storyboard-exploration.docx`, `storyboard-exploration-th.docx` — the last is a Thai
   translation). Treat these as downstream exports, not sources. `~$r-rover.docx` is a Word
   lock file, not content.

The proposal is written for a non-technical employer/sponsor audience and is deliberately
careful about tense: it describes what the system is *designed to* do, never claims working
hardware, and explicitly omits costs and dates until Phase 0 is approved. Preserve that
framing when editing — do not let the storyboard read as a report of a completed build.

## Target architecture (from the proposal)

Three independent channels, and keeping them separate is the central architectural decision:

- **Control channel** — Godot touchscreen console → ESP32 over Wi-Fi (WebSocket/UDP), with
  Bluetooth as fallback. ESP32 drives 4 DC motors through a dual H-bridge (differential /
  skid steering), plus arm servos (3–4 joints + gripper) and mast pan/tilt servos, and
  publishes telemetry back.
- **Video channel A** — front IP camera → its own monitor over Wi-Fi (driving, fine alignment).
- **Video channel B** — mast IP camera on a pan/tilt head → its own monitor over Wi-Fi (survey).

Video never shares the control channel, so streaming load cannot make driving laggy and either
feed can drop without losing the ability to drive home. Any proposed change that merges these
channels contradicts the design rationale in §5.4 and risk R1.

The arena is a sealed 1.4 m × 3.0 m glass box; the rover operates inside, the operator station
outside. The 1.4 m width constrains chassis footprint and turning radius (risk R5).

## Control protocol

**[docs/protocol.md](docs/protocol.md) is the authoritative contract** — every message, field,
unit, and range, plus the failsafe rules. It is frozen at v1. When code and that document
disagree, the code is wrong; changing the protocol means editing the document first, bumping
the version, and then changing both codebases. Its §8 tracks what is implemented versus
specified, and carries the open findings.

In short: JSON text frames over a WebSocket, ESP32 as server on port 81, console as client.
Console → rover carries a `cmd` discriminator, rover → console carries a `t`. `drive` and
`stop` are actuated; `mast` and `arm` are accepted shapes the firmware logs and ignores until
Phases 2–3.

```
{"cmd":"drive","l":0.6,"r":-0.6}        # per-side throttle, -1.0 .. 1.0
{"cmd":"mast","pan":0,"tilt":15}        # degrees
{"cmd":"arm","joints":[..],"grip":true} # base-outward joint angles
{"t":"tlm","battery_v":11.8,"mode":"safe","rssi":-58}   # broadcast every 500 ms
```

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
- LEDC channels 0 and 1 are reserved for the drive motors; servo work must claim its own.
- [console/scripts/rover_link.gd](console/scripts/rover_link.gd) owns all transport concerns
  (connect, reconnect, handshake, JSON framing, the drive repeat, RTT probing); UI scripts
  call its methods and read its state, and never touch the socket. It exposes `is_linked()`
  and friends rather than making callers reach into its `State` enum.
- The console assumes a **15.6" 1920×1080 touchscreen until step 0.4 picks one**, and audits
  every touch target against a 15 mm minimum at startup — see
  [docs/console-layout.md](docs/console-layout.md). The audit's millimetre figures are only
  valid from a *windowed* run at the target resolution; headless has no window and reports a
  square viewport, which the audit detects and warns about.
- Three env vars drive the console for development, none of which change committed defaults:
  `ROVER_URL` (point at the simulator), `RTT_LOG` (latency CSV path), `CONSOLE_SHOT` (capture
  a PNG and exit).

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
