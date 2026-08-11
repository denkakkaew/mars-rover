#!/usr/bin/env python3
"""Desktop fake rover — a stand-in for the ESP32, so the console can be built and
demonstrated with no hardware in existence (IMPLEMENTATION_PLAN.md S.5).

It speaks the rover half of docs/protocol.md v1 and mirrors the firmware's behaviour
deliberately closely: the same strict parsing as lib/Protocol, the same failsafe state
machine as lib/Safety, the same 500 ms telemetry cadence. Where the two disagree, one of
them has a bug — that is the point of writing it twice.

    python tools/fake_rover.py --view

Then point the console at it:

    $env:ROVER_URL = "ws://127.0.0.1:81/"
    & $env:GODOT_BIN --path console

Fault injection is available up front as command-line flags and at runtime by typing
commands into the terminal (see `help`). The nasty cases — latency, packet loss, a hard
disconnect mid-drive — are the ones worth rehearsing before real hardware exists.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import math
import random
import sys
import threading
import time
from dataclasses import dataclass, field

from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

# ---------------------------------------------------------------------------------
# Protocol constants. These mirror firmware/lib/Protocol/Protocol.h and
# firmware/include/config.h — if you change one, change both.
# ---------------------------------------------------------------------------------

PROTOCOL_VERSION = 1
FIRMWARE_VERSION = "sim-0.1.0"
MAX_FRAME_BYTES = 512
MAX_ARM_JOINTS = 4
COMMAND_TIMEOUT_S = 0.500
TELEMETRY_INTERVAL_S = 0.500
# A console that never identifies itself is treated as an unknown version
# (protocol.md 5). Mirrors HANDSHAKE_DEADLINE_MS in firmware/include/config.h.
HANDSHAKE_DEADLINE_S = 2.0

# Commands that count as proof of a live console (protocol.md 6.2). Note the absentees:
# ping and hello do not keep the motors alive.
REFRESHING = {"drive", "stop", "mast", "arm"}

# ---------------------------------------------------------------------------------
# Physical model of the rover and the arena
# ---------------------------------------------------------------------------------

ARENA_W = 1.4  # metres across, the dimension that constrains turning (risk R5)
ARENA_L = 3.0  # metres downrange
ROVER_RADIUS = 0.10
WHEELBASE = 0.18
MAX_SPEED = 0.25  # m/s at full throttle
DEADBAND = 0.05  # matches kDeadband in lib/Drive/Drive.cpp

BATTERY_FULL_V = 12.6  # 3S LiPo, charged
BATTERY_EMPTY_V = 10.5
IDLE_DRAIN_FRACTION = 0.2  # idle current as a fraction of full-throttle current

PHYSICS_HZ = 50
VIEW_HZ = 5


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def is_number(value: object) -> bool:
    """JSON numbers only. `True` is not 1 and "0.6" is not 0.6 (protocol.md 2.5, 2.7)."""
    return isinstance(value, (int, float)) and not isinstance(value, bool)


# ---------------------------------------------------------------------------------
# Parsing — mirrors firmware/lib/Protocol/Protocol.cpp
# ---------------------------------------------------------------------------------


@dataclass
class Command:
    kind: str  # a verb, or "unknown" / "malformed"
    error: str = ""
    version: int = 0
    left: float = 0.0
    right: float = 0.0
    pan: float | None = None
    tilt: float | None = None
    joints: list[float] | None = None
    grip: bool | None = None
    ts: int = 0


def _bad(error: str) -> Command:
    return Command(kind="malformed", error=error)


def parse(raw: str | bytes) -> Command:
    """Decode one frame. A bad field rejects the whole frame; nothing is half-applied."""
    if isinstance(raw, bytes):
        # The protocol is text frames only.
        return _bad("binary_frame")
    if len(raw.encode("utf-8")) > MAX_FRAME_BYTES:
        return _bad("too_large")

    try:
        doc = json.loads(raw)
    except (ValueError, TypeError):
        return _bad("bad_json")
    if not isinstance(doc, dict):
        return _bad("not_an_object")

    verb = doc.get("cmd")
    if not isinstance(verb, str):
        return _bad("missing_cmd")

    if verb == "drive":
        out = Command(kind="drive")
        for field_name, attr in (("l", "left"), ("r", "right")):
            value = doc.get(field_name)
            if value is None:
                continue
            if not is_number(value):
                return _bad("bad_field")
            # Clamped, not rejected (protocol.md 3.2).
            setattr(out, attr, clamp(float(value), -1.0, 1.0))
        return out

    if verb == "stop":
        return Command(kind="stop")

    if verb == "hello":
        version = doc.get("v")
        if not isinstance(version, int) or isinstance(version, bool):
            return _bad("bad_field")
        return Command(kind="hello", version=version)

    if verb == "ping":
        ts = doc.get("ts")
        if not isinstance(ts, int) or isinstance(ts, bool):
            return _bad("bad_field")
        return Command(kind="ping", ts=ts)

    if verb == "mast":
        out = Command(kind="mast")
        for field_name in ("pan", "tilt"):
            value = doc.get(field_name)
            if value is None:
                continue  # absent means "hold this axis"
            if not is_number(value):
                return _bad("bad_field")
            setattr(out, field_name, float(value))
        return out

    if verb == "arm":
        out = Command(kind="arm")
        joints = doc.get("joints")
        if joints is not None:
            if not isinstance(joints, list):
                return _bad("bad_field")
            if not 1 <= len(joints) <= MAX_ARM_JOINTS:
                return _bad("bad_joint_count")
            if not all(is_number(angle) for angle in joints):
                return _bad("bad_field")
            out.joints = [float(angle) for angle in joints]
        grip = doc.get("grip")
        if grip is not None:
            if not isinstance(grip, bool):
                return _bad("bad_field")
            out.grip = grip
        return out

    # Unknown verbs are dropped, not fatal (protocol.md 2.2).
    return Command(kind="unknown")


# ---------------------------------------------------------------------------------
# Failsafe — mirrors firmware/lib/Safety/Safety.cpp
# ---------------------------------------------------------------------------------


@dataclass
class Failsafe:
    timeout_s: float = COMMAND_TIMEOUT_S
    connected: bool = False
    commanded_since_connect: bool = False
    last_command_t: float = 0.0
    peer_compatible: bool = True
    handshake_ok: bool = False

    def on_connect(self) -> None:
        self.connected = True
        self.commanded_since_connect = False
        self.handshake_ok = False
        self.peer_compatible = True

    def on_disconnect(self) -> None:
        self.connected = False
        self.commanded_since_connect = False
        self.handshake_ok = False

    def on_command(self, now: float) -> None:
        self.last_command_t = now
        self.commanded_since_connect = True

    def state(self, now: float) -> str:
        if not self.peer_compatible:
            return "incompatible"
        if not self.connected:
            return "safe"
        # Nothing moves until the console has identified itself. Failing closed here is
        # what makes the 2 s deadline a safety property rather than a log message.
        if not self.handshake_ok:
            return "safe"
        if not self.commanded_since_connect:
            return "safe"
        # `>=`, so 500 ms of silence is a timeout — same boundary as the firmware.
        if now - self.last_command_t >= self.timeout_s:
            return "safe"
        return "drive"


# ---------------------------------------------------------------------------------
# Simulated rover state
# ---------------------------------------------------------------------------------


@dataclass
class Rover:
    """Pose is metres in the arena; heading is radians clockwise from downrange."""

    x: float = ARENA_W / 2
    y: float = 0.25
    heading: float = 0.0
    left: float = 0.0
    right: float = 0.0
    battery_v: float = BATTERY_FULL_V
    joints: list[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    grip: bool = False
    pan: float = 0.0
    tilt: float = 0.0
    bumped: bool = False
    battery_minutes: float = 20.0

    def reset(self) -> None:
        self.x, self.y, self.heading = ARENA_W / 2, 0.25, 0.0
        self.left = self.right = 0.0
        self.battery_v = BATTERY_FULL_V
        self.bumped = False

    def stop(self) -> None:
        self.left = self.right = 0.0

    def step(self, dt: float, armed: bool) -> None:
        left = self.left if armed else 0.0
        right = self.right if armed else 0.0

        # Below the deadband the geared motors buzz rather than turn, so the firmware
        # coasts — reproduced here so console tuning against the sim carries over.
        if abs(left) < DEADBAND:
            left = 0.0
        if abs(right) < DEADBAND:
            right = 0.0

        speed = (left + right) / 2.0 * MAX_SPEED
        turn = (left - right) * MAX_SPEED / WHEELBASE

        self.heading = (self.heading + turn * dt) % (2 * math.pi)
        nx = self.x + speed * math.sin(self.heading) * dt
        ny = self.y + speed * math.cos(self.heading) * dt

        # The glass does not move. Clamping rather than bouncing keeps it obvious that
        # the operator drove into a wall.
        cx = clamp(nx, ROVER_RADIUS, ARENA_W - ROVER_RADIUS)
        cy = clamp(ny, ROVER_RADIUS, ARENA_L - ROVER_RADIUS)
        self.bumped = (cx != nx) or (cy != ny)
        self.x, self.y = cx, cy

        load = (abs(left) + abs(right)) / 2.0
        drain_per_s = (BATTERY_FULL_V - BATTERY_EMPTY_V) / (self.battery_minutes * 60.0)
        self.battery_v = max(
            BATTERY_EMPTY_V,
            self.battery_v - drain_per_s * (IDLE_DRAIN_FRACTION + load) * dt,
        )

    def rssi(self) -> int:
        """Weaker downrange, which is the R6 case step 1.2 goes looking for."""
        return int(-42 - (self.y / ARENA_L) * 28 + random.uniform(-2, 2))


# ---------------------------------------------------------------------------------
# The server
# ---------------------------------------------------------------------------------


class Simulator:
    def __init__(self, args: argparse.Namespace) -> None:
        self.rover = Rover(battery_minutes=args.battery_minutes)
        self.failsafe = Failsafe()
        self.clients: set = set()
        self.latency_ms = args.latency
        self.loss_pct = args.loss
        self.drop_after = args.drop_after
        self.caps = [c.strip() for c in args.caps.split(",") if c.strip()]
        self.version = args.protocol_version
        self.telemetry_on = True
        self.quiet = args.quiet
        self.state = "safe"
        self.frames_in = 0
        self.frames_dropped = 0
        self.last_event = "waiting for a console"

    # -- logging ------------------------------------------------------------------

    def log(self, message: str) -> None:
        self.last_event = message
        if not self.quiet:
            print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)

    # -- outbound with fault injection --------------------------------------------

    async def _deliver(self, ws, text: str) -> None:
        if self.latency_ms:
            await asyncio.sleep(self.latency_ms / 2000.0)
        try:
            await ws.send(text)
        except (ConnectionClosed, RuntimeError):
            pass

    def send(self, ws, payload: dict) -> None:
        """Fire-and-forget so an injected delay never stalls the physics loop."""
        if self.loss_pct and random.random() * 100 < self.loss_pct:
            return
        asyncio.create_task(self._deliver(ws, json.dumps(payload, separators=(",", ":"))))

    def broadcast(self, payload: dict) -> None:
        for ws in list(self.clients):
            self.send(ws, payload)

    # -- command handling ----------------------------------------------------------

    async def handle(self, ws) -> None:
        peer = f"{ws.remote_address[0]}:{ws.remote_address[1]}"
        self.clients.add(ws)
        self.failsafe.on_connect()
        self.log(f"console connected from {peer}")

        if self.drop_after:
            asyncio.create_task(self._drop_later(ws, self.drop_after))
        asyncio.create_task(self._expire_handshake(ws))

        try:
            async for raw in ws:
                self.frames_in += 1
                if self.loss_pct and random.random() * 100 < self.loss_pct:
                    self.frames_dropped += 1
                    continue
                if self.latency_ms:
                    await asyncio.sleep(self.latency_ms / 2000.0)
                self._apply(ws, parse(raw))
        except ConnectionClosed:
            pass
        finally:
            self.clients.discard(ws)
            if not self.clients:
                self.failsafe.on_disconnect()
                self.rover.stop()
            self.log(f"console {peer} disconnected — motors cut")

    def _apply(self, ws, cmd: Command) -> None:
        now = time.monotonic()

        if cmd.kind == "malformed":
            self.log(f"dropped frame: {cmd.error}")
            return  # deliberately does not refresh the failsafe
        if cmd.kind == "unknown":
            self.log("unknown command — ignored")
            return

        if cmd.kind == "hello":
            compatible = cmd.version == self.version
            self.failsafe.peer_compatible = compatible
            self.failsafe.handshake_ok = compatible
            if compatible:
                self.log(f"handshake ok — console speaks v{cmd.version}")
            else:
                self.log(
                    f"version mismatch: console v{cmd.version}, rover "
                    f"v{self.version} — refusing to arm"
                )
            self.send(
                ws,
                {
                    "t": "hello",
                    "v": self.version,
                    "fw": FIRMWARE_VERSION,
                    "caps": self.caps,
                },
            )
            return

        if cmd.kind == "ping":
            self.send(ws, {"t": "pong", "ts": cmd.ts})
            return

        if cmd.kind in REFRESHING:
            self.failsafe.on_command(now)
        self.state = self.failsafe.state(now)

        if cmd.kind == "stop":
            self.rover.stop()  # honoured in every state
        elif cmd.kind == "drive":
            if self.state == "drive":
                self.rover.left, self.rover.right = cmd.left, cmd.right
            else:
                self.rover.stop()
        elif cmd.kind == "mast":
            if self.state == "drive":
                if cmd.pan is not None:
                    self.rover.pan = cmd.pan
                if cmd.tilt is not None:
                    self.rover.tilt = cmd.tilt
        elif cmd.kind == "arm":
            if self.state == "drive":
                if cmd.joints is not None:
                    self.rover.joints = cmd.joints
                if cmd.grip is not None:
                    self.rover.grip = cmd.grip

    async def _expire_handshake(self, ws) -> None:
        """A console that never says hello is treated as an unknown version."""
        await asyncio.sleep(HANDSHAKE_DEADLINE_S)
        if ws in self.clients and not self.failsafe.handshake_ok:
            self.failsafe.peer_compatible = False
            self.log(f"no hello within {HANDSHAKE_DEADLINE_S:g}s — refusing to arm")

    def hard_close(self, ws) -> None:
        """Abort the TCP connection with no close handshake.

        This is the failure worth rehearsing: the rover going out of range or losing
        power does not send a courteous close frame. A clean `close()` would exercise a
        gentler path than the one that actually happens, and 1006 is a reserved code the
        library refuses to put on the wire anyway.
        """
        try:
            ws.transport.abort()
        except AttributeError:
            asyncio.create_task(ws.close(code=1001))

    async def _drop_later(self, ws, seconds: float) -> None:
        await asyncio.sleep(seconds)
        if ws in self.clients:
            self.log(f"injected hard disconnect after {seconds:g}s")
            self.hard_close(ws)

    async def drop_all(self) -> None:
        for ws in list(self.clients):
            self.hard_close(ws)

    # -- background loops ----------------------------------------------------------

    async def physics_loop(self) -> None:
        dt = 1.0 / PHYSICS_HZ
        previous = "safe"
        while True:
            await asyncio.sleep(dt)
            now = time.monotonic()
            self.state = self.failsafe.state(now)
            if self.state != previous:
                if previous == "drive":
                    self.log(f"failsafe: {previous} -> {self.state}, motors cut")
                previous = self.state
            self.rover.step(dt, armed=self.state == "drive")

    async def telemetry_loop(self) -> None:
        while True:
            await asyncio.sleep(TELEMETRY_INTERVAL_S)
            if not self.telemetry_on:
                continue  # link stays up, frames stop: the stale-telemetry case
            frame = {
                "t": "tlm",
                "battery_v": round(self.rover.battery_v, 2),
                "mode": self.state,
                "rssi": self.rover.rssi(),
            }
            # Reserved names (protocol.md 4.2), additive and only sent for subsystems
            # this build claims to actuate.
            if "arm" in self.caps:
                frame["arm"] = [round(a, 1) for a in self.rover.joints]
                frame["grip"] = self.rover.grip
            if "mast" in self.caps:
                frame["pan"] = round(self.rover.pan, 1)
                frame["tilt"] = round(self.rover.tilt, 1)
            self.broadcast(frame)

    async def view_loop(self) -> None:
        while True:
            await asyncio.sleep(1.0 / VIEW_HZ)
            sys.stdout.write("\033[H\033[J" + self.render())
            sys.stdout.flush()

    # -- ASCII top-down view -------------------------------------------------------

    def render(self) -> str:
        cols, rows = 28, 24
        grid = [[" "] * cols for _ in range(rows)]

        # Base zone across the near end (Scene 1 and Scene 8 happen here).
        base_rows = max(1, int(rows * 0.4 / ARENA_L))
        for r in range(rows - base_rows, rows):
            for c in range(cols):
                grid[r][c] = "."

        cx = int(self.rover.x / ARENA_W * (cols - 1))
        cy = int((1.0 - self.rover.y / ARENA_L) * (rows - 1))
        arrows = "↑↗→↘↓↙←↖"
        index = int(((self.rover.heading + math.pi / 8) % (2 * math.pi)) / (math.pi / 4))
        grid[clamp(cy, 0, rows - 1)][clamp(cx, 0, cols - 1)] = arrows[index % 8]

        badge = {"drive": "ARMED", "safe": "SAFE", "incompatible": "INCOMPATIBLE"}
        lines = [
            "  Mars rover simulator — fake_rover.py",
            f"  arena {ARENA_W} x {ARENA_L} m   downrange is up",
            "",
            "  +" + "-" * cols + "+",
        ]
        lines += ["  |" + "".join(row) + "|" for row in grid]
        lines += [
            "  +" + "-" * cols + "+",
            "",
            f"  mode     {badge[self.state]}"
            + ("   <-- WALL" if self.rover.bumped else ""),
            f"  pose     x={self.rover.x:4.2f} y={self.rover.y:4.2f} "
            f"hdg={math.degrees(self.rover.heading):5.0f}deg",
            f"  throttle l={self.rover.left:+.2f} r={self.rover.right:+.2f}",
            f"  battery  {self.rover.battery_v:5.2f} V     rssi {self.rover.rssi()} dBm",
            f"  clients  {len(self.clients)}   frames in {self.frames_in}"
            f"   dropped {self.frames_dropped}",
            f"  faults   latency {self.latency_ms} ms   loss {self.loss_pct}%",
            f"  last     {self.last_event}",
            "",
            "  type `help` + Enter for runtime switches",
        ]
        return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------------
# Runtime switches, typed into the terminal
# ---------------------------------------------------------------------------------

HELP = """
runtime switches:
  lat <ms>     inject link latency (applied half each way, so RTT rises by <ms>)
  loss <pct>   drop this percentage of frames in both directions
  drop         hard-disconnect every console right now
  tlm on|off   stop sending telemetry while leaving the link up (stale-frame case)
  ver <n>      change the protocol version advertised, to force a mismatch
  batt <volts> force the pack voltage, for testing the low-battery display
  reset        recentre the rover and refill the pack
  quit         stop the simulator
""".strip()


def stdin_reader(sim: Simulator, loop: asyncio.AbstractEventLoop) -> None:
    for line in sys.stdin:
        parts = line.strip().split()
        if not parts:
            continue
        verb, rest = parts[0].lower(), parts[1:]
        try:
            if verb in ("help", "?"):
                sim.log(HELP)
            elif verb == "lat" and rest:
                sim.latency_ms = float(rest[0])
                sim.log(f"latency now {sim.latency_ms} ms")
            elif verb == "loss" and rest:
                sim.loss_pct = float(rest[0])
                sim.log(f"packet loss now {sim.loss_pct}%")
            elif verb == "drop":
                asyncio.run_coroutine_threadsafe(sim.drop_all(), loop)
                sim.log("hard disconnect injected")
            elif verb == "tlm" and rest:
                sim.telemetry_on = rest[0].lower() in ("on", "1", "true")
                sim.log(f"telemetry {'on' if sim.telemetry_on else 'off'}")
            elif verb == "ver" and rest:
                sim.version = int(rest[0])
                sim.log(f"advertising protocol v{sim.version} — reconnect to apply")
            elif verb == "batt" and rest:
                sim.rover.battery_v = float(rest[0])
                sim.log(f"battery forced to {sim.rover.battery_v} V")
            elif verb == "reset":
                sim.rover.reset()
                sim.log("rover reset")
            elif verb == "quit":
                loop.call_soon_threadsafe(loop.stop)
                return
            else:
                sim.log(f"unrecognised: {line.strip()!r} — try `help`")
        except ValueError:
            sim.log(f"bad argument: {line.strip()!r}")


# ---------------------------------------------------------------------------------


async def main_async(args: argparse.Namespace) -> None:
    sim = Simulator(args)
    loop = asyncio.get_running_loop()

    threading.Thread(target=stdin_reader, args=(sim, loop), daemon=True).start()

    async def stall_later(seconds: float) -> None:
        await asyncio.sleep(seconds)
        sim.telemetry_on = False
        sim.log(f"telemetry stalled after {seconds:g}s — link left up")

    tasks = [
        asyncio.create_task(sim.physics_loop()),
        asyncio.create_task(sim.telemetry_loop()),
    ]
    if args.stall_after:
        tasks.append(asyncio.create_task(stall_later(args.stall_after)))
    if args.view:
        tasks.append(asyncio.create_task(sim.view_loop()))

    async with serve(sim.handle, args.host, args.port, max_size=MAX_FRAME_BYTES * 4):
        print(
            f"Fake rover listening on ws://{args.host}:{args.port}/  "
            f"(protocol v{PROTOCOL_VERSION}, caps {sim.caps})",
            flush=True,
        )
        print("Point the console at it:  $env:ROVER_URL = "
              f'"ws://{args.host}:{args.port}/"', flush=True)
        try:
            await asyncio.gather(*tasks)
        except asyncio.CancelledError:
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=81)
    parser.add_argument("--view", action="store_true",
                        help="ASCII top-down view of the arena")
    parser.add_argument("--quiet", action="store_true",
                        help="suppress the event log (implied useful with --view)")
    parser.add_argument("--latency", type=float, default=0.0, metavar="MS",
                        help="injected link latency; RTT rises by this amount")
    parser.add_argument("--loss", type=float, default=0.0, metavar="PCT",
                        help="percentage of frames dropped in both directions")
    parser.add_argument("--drop-after", type=float, default=0.0, metavar="SEC",
                        help="hard-disconnect a console this long after it connects")
    parser.add_argument("--stall-after", type=float, default=0.0, metavar="SEC",
                        help="stop sending telemetry this long after start, link left up")
    parser.add_argument("--caps", default="drive",
                        help="subsystems to advertise, e.g. drive,mast,arm")
    parser.add_argument("--protocol-version", type=int, default=PROTOCOL_VERSION,
                        metavar="N",
                        help="version to advertise; set it wrong to force a mismatch")
    parser.add_argument("--battery-minutes", type=float, default=20.0,
                        help="minutes of full-throttle driving from full to empty")
    args = parser.parse_args()

    if args.view:
        args.quiet = True
        if sys.platform == "win32":
            # Enable ANSI escape handling in the Windows console.
            import ctypes

            kernel32 = ctypes.windll.kernel32
            kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)

    try:
        asyncio.run(main_async(args))
    except KeyboardInterrupt:
        pass
    print("\nFake rover stopped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
