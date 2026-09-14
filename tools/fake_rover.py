#!/usr/bin/env python3
"""Desktop fake rover — a stand-in for the ESP32, so the console can be built and
demonstrated with no hardware in existence (IMPLEMENTATION_PLAN.md S.5).

It speaks the rover half of docs/protocol.md v2 and mirrors the firmware's behaviour
deliberately closely: the same strict parsing as lib/Protocol — including the retired
`l`/`r` rejection — the same failsafe state machine as lib/Safety, the same 500 ms
telemetry cadence. Where the two disagree, one of them has a bug: that is the point of
writing it twice.

    python tools/fake_rover.py --view

Then point the console at it:

    $env:ROVER_URL = "ws://127.0.0.1:81/"
    & $env:GODOT_BIN --path console

Fault injection is available up front as command-line flags and at runtime by typing
commands into the terminal (see `help`). The nasty cases — latency, packet loss, a hard
disconnect mid-drive — are the ones worth rehearsing before real hardware exists.

The drive model is deliberately pessimistic: a commanded straight line curves, the rover
has to break away from rest, and cutting the motors coasts rather than stops. Step S.15
moved it from skid steer onto a **steered (bicycle) chassis** — the one chosen at step
0.2 — so the headline behaviour is that **turn-in-place is gone**: steering at zero
throttle swings the front axle and moves the rover not at all. See DriveModel below for
why each effect is there, and `--surface`/`--wheelbase`/`--steer-lock`/`--chassis` for
tuning them once real numbers exist.
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
from dataclasses import dataclass, field, replace

from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

# ---------------------------------------------------------------------------------
# Protocol constants. These mirror firmware/lib/Protocol/Protocol.h and
# firmware/include/config.h — if you change one, change both.
# ---------------------------------------------------------------------------------

PROTOCOL_VERSION = 2
FIRMWARE_VERSION = "sim-0.2.0"
MAX_FRAME_BYTES = 512
MAX_TAG_ID_CHARS = 64
# At most one tag frame per tag per this interval (protocol.md 4.4). A UHF reader can
# report the same tag dozens of times a second and this is the *control* channel.
TAG_RATE_LIMIT_S = 0.100
COMMAND_TIMEOUT_S = 0.500
TELEMETRY_INTERVAL_S = 0.500
# A console that never identifies itself is treated as an unknown version
# (protocol.md 5). Mirrors HANDSHAKE_DEADLINE_MS in firmware/include/config.h.
HANDSHAKE_DEADLINE_S = 2.0

# Commands that count as proof of a live console (protocol.md 6.2). Note the absentees:
# ping and hello do not keep the motors alive. `arm` was retired with the manipulator in
# proposal Revision 2 and is now simply an unknown verb.
REFRESHING = {"drive", "stop", "mast"}

# ---------------------------------------------------------------------------------
# Physical model of the rover and the arena
# ---------------------------------------------------------------------------------

ARENA_W = 1.4  # metres across, the dimension that constrains turning (risk R5)
ARENA_L = 3.0  # metres downrange
MAX_SPEED = 0.25  # m/s at full throttle
DEADBAND = 0.05  # matches kThrottleDeadband in lib/Drive/Drive.cpp
# Where a continuous `steer` demand rounds onto three positions. Mirrors kSteerThreshold
# in lib/Drive/Drive.cpp — the two must agree or the sim is not the same rover
# (docs/protocol.md 3.2.1).
STEER_THRESHOLD = 0.5

# Steered-chassis geometry (step S.15). These two numbers *are* risk R5 now: together
# they set the minimum turning circle, and whether a U-turn fits the arena.
WHEELBASE = 0.155  # metres, front axle to rear axle
STEER_LOCK_DEG = 25.0  # steering angle at full lock

# How fast the front axle swings to lock and springs back. Not instant: the steering
# motor takes a moment to reach its end stop, which is what makes a *timed* steering tap
# a usable primitive rather than a binary one (docs/chassis-envelope.md 8.5).
STEER_SLEW_DEG_S = 120.0

# Chassis envelope. Nothing has been bought, so these are stand-ins — but the *shape*
# matters: a steered chassis sweeps an arc whose outer radius is set by the turn centre
# and the outer front corner, and that arc is what has to fit the arena.
CHASSIS_L = 0.22
CHASSIS_W = 0.20
# How far the front corners sit ahead of the front axle. Small on a kit chassis, but it
# is on the *outside* of the swept arc, so it counts against the U-turn.
FRONT_OVERHANG = 0.035
# Terrain dressing piles up at the walls, so the drivable width is less than the glass
# width. Step 0.2 compares against 1.4 m *minus* this, twice over.
DRESSING_DEPTH = 0.05

# 3S LiPo. A 3S pack is no longer viable -- step 1.3 fitted a DRV8833 whose V_M maxes at
# 10.8 V, and docs/power-budget.md 3.3 recommends 2S instead, which would make these 8.4
# and 6.6. Not changed yet: the pack is decision 1 at 0.3's review gate. When it is taken,
# these move together with BATTERY_LOW_V / BATTERY_CRITICAL_V in console/scripts/console.gd
# and BATTERY_DIVIDER_RATIO in firmware/include/config.h.
BATTERY_FULL_V = 12.6  # 3S LiPo, charged
BATTERY_EMPTY_V = 10.5
IDLE_DRAIN_FRACTION = 0.2  # idle current as a fraction of full-throttle current

PHYSICS_HZ = 50
VIEW_HZ = 5
RFID_POLL_HZ = 20  # how often the firmware would poll the reader over UART

# ---------------------------------------------------------------------------------
# Drive model (step S.12, retargeted onto a steered chassis at S.15)
#
# The model is deliberately *pessimistic* rather than accurate: every constant here is a
# guess until steps 1.3 to 1.5 measure the real thing, and the switches exist so those
# measurements can be fed back in.
#
# **The structural change at S.15 is the bicycle model.** Yaw rate is now
#
#     w = v * tan(steer_angle) / wheelbase
#
# and the `v` in front of it is the whole story: **yaw rate is proportional to speed, so
# a stationary rover cannot turn at all.** Steering at zero throttle swings the front
# axle and moves nothing. Under S.12's skid steer, opposing the two sides pivoted the
# rover on the spot; that manoeuvre no longer exists, and every control idea that leaned
# on it has to be re-thought rather than re-tuned.
#
# Four effects survive the change, each a reason the rover is awkward to place precisely:
#
#   turn_efficiency  Understeer. The front tyres slip rather than following exactly where
#                    they point, so the achieved yaw rate is under what the geometry
#                    predicts — and much more so on sand, where a steered front axle
#                    ploughs instead of biting (docs/chassis-envelope.md 8.6).
#   drift            A residual steering angle when commanded straight: linkage slop and
#                    wheel misalignment, where S.12 had motor/gearbox mismatch. Same
#                    observable, and still probably the single biggest reason open-loop
#                    driving is hard to aim — worse here, because three-position steering
#                    offers no small correction to trim it out with.
#   breakaway        From rest the wheels must break out of their own ruts. Tap the
#                    throttle gently and nothing happens; push harder and it lurches.
#                    Distinct from DEADBAND, which is the *firmware* coasting below
#                    5% throttle — this is the ground refusing to cooperate.
#   accel / coast    Mass. It does not reach speed instantly, and — because Drive coasts
#                    (both direction pins LOW) rather than braking — it does not stop
#                    when the throttle does. That coast is what step 1.6 measures after a
#                    link cut.
# ---------------------------------------------------------------------------------


@dataclass
class DriveModel:
    """Everything about how the rover moves. Swappable per surface, tunable at runtime."""

    name: str
    speed_factor: float  # of MAX_SPEED, once the surface has loaded the motors
    turn_efficiency: float  # 1.0 follows the geometry exactly; below that is understeer
    accel_time: float  # seconds to ~63% of commanded speed
    coast_time: float  # seconds to decay to ~37% once the motors are cut
    breakaway: float  # throttle needed to start moving from a standstill
    slip_noise: float  # how repeatable a turn is; 0 makes every arc identical
    icr_offset: float  # metres of rear-axle sideslip per rad/s of yaw


# Sand is the default because that is what the arena floor is. The hard-floor preset
# exists because step 1.5 drives on a bench first, and the difference between the two
# is itself worth seeing.
#
# Both breakaway figures rose at S.15, and not because the ground changed. An L293D drops
# ~1.8-2 V against a MOSFET bridge's ~0.5 V, so on a 6 V rail the motor sees about 4 V and
# needs roughly 6/4 of the duty to make the same torque (docs/chassis-envelope.md 8.3).
# One driven axle instead of four driven wheels pushes the same way.
#
# That justification is void as of step 1.3: the driver is now a DRV8833 dropping ~0.36 V,
# and the rear axle has two motors on it rather than one. Both push these figures back
# DOWN. They are left as they are on purpose -- step 1.5 measures breakaway on real sand
# and feeds it back through --breakaway, and a second guess is not better than the first.
# Keep these in step with MIN_EFFECTIVE_THROTTLE in console/scripts/rover_link.gd.
SURFACES = {
    "sand": DriveModel(
        name="sand",
        speed_factor=0.72,
        # A steered front axle ploughs sand rather than biting it, so understeer on sand
        # is worse than the skid-steer scrub figure this replaced.
        turn_efficiency=0.45,
        accel_time=0.45,
        coast_time=0.20,  # rolling resistance stops it sooner than a hard floor does
        breakaway=0.42,  # was 0.28 under the TB6612FNG assumption
        slip_noise=0.22,
        icr_offset=0.045,
    ),
    "hard": DriveModel(
        name="hard",
        speed_factor=1.00,
        turn_efficiency=0.80,
        accel_time=0.25,
        coast_time=0.45,
        breakaway=0.18,  # was 0.12
        slip_noise=0.10,
        icr_offset=0.020,
    ),
}

# How quickly the traction noise wanders. Long enough that it acts as a bias over a
# whole manoeuvre rather than averaging out within one — the thing that makes a pivot
# unrepeatable is the ground being different this time, not high-frequency jitter.
SCRUB_TAU_S = 1.2

# ---------------------------------------------------------------------------------
# RFID propagation model
#
# Deliberately crude but the right *shape*, so the console is built against behaviour
# it will actually meet: signal climbing steeply as the rover closes, falling away off
# to the side, and going ragged at the edge of range rather than stopping cleanly.
# Real numbers replace all of this at steps 2.3 and 2.4.
# ---------------------------------------------------------------------------------

# Antenna sits low on the front, facing the ground ahead — the mount the arm used to
# occupy (proposal 4.3). Reads are measured from here, not from the rover's centre.
ANTENNA_OFFSET_M = 0.12

# Backscatter is a round trip, so received power falls with roughly the fourth power of
# distance — 40 dB per decade, not the 20 of a one-way link. That steepness is why the
# signal meter is usable as a proximity cue at all.
PATH_LOSS_EXPONENT_DB = 40.0
RSSI_AT_REF = -35.0  # dBm at REF_DISTANCE_M, boresight
REF_DISTANCE_M = 0.05

# Off-boresight the antenna simply stops hearing. Beyond this there is no read at any
# distance, which is what makes bearing matter as much as range.
BEAM_HALF_ANGLE_DEG = 60.0
BEAM_EDGE_LOSS_DB = 12.0

# Within this many dB of the sensitivity floor, reads come and go instead of locking.
# The margin exists because a clean pass/fail cutoff would let the console get away with
# assuming every approach ends in a solid read (risk R2).
MARGINAL_BAND_DB = 6.0


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
    throttle: float = 0.0
    steer: float = 0.0
    pan: float | None = None
    tilt: float | None = None
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
        # v1's per-side throttle is *retired*, not merely unknown, so it is rejected
        # rather than ignored (protocol.md 2.3a). Ignoring it would leave a valid
        # zero-throttle command that refreshes the failsafe, so a v1 console would hold
        # FORWARD against an armed rover that never moves. Mirrors parseDrive() in
        # firmware/lib/Protocol/Protocol.cpp — and this line is exactly the kind of thing
        # the two implementations exist to keep each other honest about.
        if doc.get("l") is not None or doc.get("r") is not None:
            return _bad("retired_field")

        out = Command(kind="drive")
        for field_name, attr in (("fwd", "throttle"), ("steer", "steer")):
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

    # Unknown verbs are dropped, not fatal (protocol.md 2.2) — which is exactly what
    # lets `arm` disappear in Revision 2 without a protocol version bump.
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
class Rock:
    """A tagged rock sitting on the arena floor."""

    tag_id: str
    x: float
    y: float
    label: str = ""
    # Risk R7: a tag cooked by adhesive during embedding reads exactly like a tag out of
    # range — nothing. The rock looks completely normal. This flag is the only way to
    # tell, and it is deliberately invisible to the console.
    dead: bool = False

    last_rssi: float = -120.0
    last_read_t: float = 0.0


def default_rocks() -> list[Rock]:
    """A starter arena. Two of them sit close together on purpose, so the
    two-tags-in-range case is reachable by just driving at them."""
    return [
        Rock("E2801160600001", 0.45, 1.10, "basalt"),
        Rock("E2801160600002", 0.95, 1.75, "olivine"),
        Rock("E2801160600003", 0.70, 2.55, "hematite"),
        Rock("E2801160600004", 0.30, 2.15, "pair-A"),
        Rock("E2801160600005", 0.44, 2.24, "pair-B"),
    ]


@dataclass
class Rover:
    """Pose is metres in the arena; heading is radians clockwise from downrange."""

    x: float = ARENA_W / 2
    y: float = 0.25
    heading: float = 0.0
    throttle: float = 0.0
    ## The demand on the wire, -1..1. What the *axle* is doing is steer_angle.
    steer_cmd: float = 0.0
    battery_v: float = BATTERY_FULL_V
    pan: float = 0.0
    tilt: float = 0.0
    bumped: bool = False
    battery_minutes: float = 20.0

    model: DriveModel = field(default_factory=lambda: SURFACES["sand"])
    chassis_l: float = CHASSIS_L
    chassis_w: float = CHASSIS_W
    wheelbase: float = WHEELBASE
    steer_lock: float = STEER_LOCK_DEG
    dressing: float = DRESSING_DEPTH
    ## Residual steering angle in degrees when commanded straight — linkage slop and
    ## wheel misalignment. Signed. This is why a commanded straight line curves.
    drift: float = 0.5

    # Actual body state, which lags the commanded one.
    v: float = 0.0  # m/s along the heading
    w: float = 0.0  # rad/s, positive clockwise
    steer_angle: float = 0.0  # degrees, actual front-axle angle; slews toward the demand
    scrub: float = 0.0  # slow-wandering traction noise, roughly unit variance

    def reset(self) -> None:
        self.x, self.y, self.heading = ARENA_W / 2, 0.25, 0.0
        self.throttle = self.steer_cmd = 0.0
        self.v = self.w = self.scrub = self.steer_angle = 0.0
        self.battery_v = BATTERY_FULL_V
        self.bumped = False

    def stop(self) -> None:
        """Cuts both motors. It does *not* stop the rover — see coast_distance() — but it
        does centre the steering, because the spring does that whenever the steering motor
        is unpowered (docs/protocol.md 6.1). The axle still takes STEER_SLEW_DEG_S to get
        there, so a rover that fails safe mid-turn finishes the last few degrees of arc
        before it runs straight."""
        self.throttle = self.steer_cmd = 0.0

    # -- geometry ------------------------------------------------------------------

    def axes(self) -> tuple[tuple[float, float], tuple[float, float]]:
        """Unit vectors: (forward, right-hand side) in arena coordinates."""
        s, c = math.sin(self.heading), math.cos(self.heading)
        return ((s, c), (c, -s))

    def turn_radius(self) -> float:
        """Radius of the tightest circle the rover can drive, measured to the centre of
        the rear axle: R = wheelbase / tan(lock).

        This is R5 in its new form. A skid-steer rover pivoted about its own centre and
        the question was whether its diagonal fitted; a steered rover has a minimum
        circle that is typically much *larger* than the vehicle, and the question is
        whether that circle fits (docs/chassis-envelope.md 8.1).
        """
        return self.wheelbase / math.tan(math.radians(max(1.0, self.steer_lock)))

    def swept_radius(self) -> float:
        """Outer radius of the swept arc — turn centre to the outer front corner.

        The corner, not the axle: it is the outermost point of the chassis and the first
        thing to touch the glass. Longitudinally it sits a wheelbase plus the front
        overhang ahead of the rear axle, laterally half a chassis width outboard of the
        turn centre, and Pythagoras does the rest.
        """
        return math.hypot(self.turn_radius() + self.chassis_w / 2.0,
                          self.wheelbase + FRONT_OVERHANG)

    def inner_radius(self) -> float:
        """Inner radius of the swept arc — the hole in the middle of the doughnut."""
        return max(0.0, self.turn_radius() - self.chassis_w / 2.0)

    def turning_circle(self) -> float:
        """Width the rover needs to turn itself round in one continuous move.

        The full outer diameter, which is what has to fit between the walls: the rover
        starts on one side of the circle and finishes on the other.
        """
        return 2.0 * self.swept_radius()

    def usable_width(self) -> float:
        return ARENA_W - 2.0 * self.dressing

    def turn_clearance(self) -> float:
        """Metres of spare width for a U-turn in one move. Negative means it needs a
        three-point turn against the glass instead — acceptable under the free-drive
        ruling at 0.2, but the operator has to know which one they are in."""
        return self.usable_width() - self.turning_circle()

    def coast_distance(self) -> float:
        """How far it travels after the motors are cut, which is what step 1.6 times.

        The integral of v0·e^(-t/tau) is v0·tau, so this is exact for the model rather
        than an approximation of it.
        """
        return abs(self.v) * self.model.coast_time

    def footprint(self, px: float, py: float) -> bool:
        """Is this arena point inside the chassis rectangle?"""
        fwd, side = self.axes()
        dx, dy = px - self.x, py - self.y
        along = dx * fwd[0] + dy * fwd[1]
        across = dx * side[0] + dy * side[1]
        return abs(along) <= self.chassis_l / 2 and abs(across) <= self.chassis_w / 2

    def _collides(self, x: float, y: float, heading: float) -> bool:
        """Corner-by-corner against the drivable rectangle, so a long chassis fouls the
        wall when it turns even though its centre is well clear."""
        s, c = math.sin(heading), math.cos(heading)
        fwd, side = (s, c), (c, -s)
        lo, hi_x = self.dressing, ARENA_W - self.dressing
        hi_y = ARENA_L - self.dressing
        for along in (self.chassis_l / 2, -self.chassis_l / 2):
            for across in (self.chassis_w / 2, -self.chassis_w / 2):
                cx = x + fwd[0] * along + side[0] * across
                cy = y + fwd[1] * along + side[1] * across
                if not (lo <= cx <= hi_x and lo <= cy <= hi_y):
                    return True
        return False

    # -- motion --------------------------------------------------------------------

    def step(self, dt: float, armed: bool) -> None:
        # Not armed means both motors unpowered — which centres the steering as surely
        # as it cuts the throttle, since the spring needs no power to work.
        throttle = self.throttle if armed else 0.0
        steer_cmd = self.steer_cmd if armed else 0.0

        # Below the deadband the geared motor buzzes rather than turns, so the firmware
        # coasts — reproduced here so console tuning against the sim carries over.
        if abs(throttle) < DEADBAND:
            throttle = 0.0

        model = self.model

        # Three positions, not a proportional angle: the same threshold the firmware
        # applies in Drive::applySteer. Inside the centre band the motor is unpowered and
        # the spring pulls the axle back to straight.
        if steer_cmd <= -STEER_THRESHOLD:
            target_angle = -self.steer_lock
        elif steer_cmd >= STEER_THRESHOLD:
            target_angle = self.steer_lock
        else:
            target_angle = 0.0

        # The axle takes time to get there, in both directions.
        slew = STEER_SLEW_DEG_S * dt
        self.steer_angle += clamp(target_angle - self.steer_angle, -slew, slew)

        # Traction wanders. Low-passed so it reads as ground rather than as jitter, and
        # the innovation is scaled by sqrt((2-a)/a) so the filter's output holds unit
        # variance whatever the tick rate — otherwise `slip_noise` would silently mean
        # something different at a different PHYSICS_HZ.
        alpha_scrub = min(1.0, dt / SCRUB_TAU_S)
        gain = math.sqrt((2.0 - alpha_scrub) / alpha_scrub)
        self.scrub += (random.gauss(0.0, gain) - self.scrub) * alpha_scrub

        v_cmd = throttle * MAX_SPEED * model.speed_factor

        # Stiction. Only applies from a standstill — once rolling, the wheels stay free.
        # Note it gates the *throttle* only: a stationary rover with the throttle below
        # breakaway still steers, because swinging the front axle does not need traction.
        at_rest = abs(self.v) < 0.01
        if at_rest and abs(throttle) < model.breakaway:
            v_cmd = 0.0

        # First-order lag toward the commanded speed. Cutting the throttle uses the coast
        # constant, because the H-bridge coasts rather than brakes.
        tau_v = model.accel_time if v_cmd else model.coast_time
        self.v += (v_cmd - self.v) * min(1.0, dt / tau_v)

        # The bicycle model, and the line that removes turn-in-place: yaw rate is
        # proportional to *speed*. Steering a stationary rover yields w = 0 exactly, no
        # matter how hard the axle is turned.
        effective_angle = self.steer_angle + self.drift
        self.w = (self.v * math.tan(math.radians(effective_angle)) / self.wheelbase
                  * model.turn_efficiency * (1.0 + self.scrub * model.slip_noise))

        # The rear axle does not track the arc perfectly; it slips outward a little,
        # proportionally to how hard the rover is turning.
        lateral = self.w * model.icr_offset

        heading = (self.heading + self.w * dt) % (2 * math.pi)
        fwd, side = self.axes()
        nx = self.x + (self.v * fwd[0] + lateral * side[0]) * dt
        ny = self.y + (self.v * fwd[1] + lateral * side[1]) * dt

        # The glass does not move, and there is no rotate-on-the-spot escape any more:
        # heading only changes as a by-product of travelling, so a rover with its nose
        # against the wall stops dead and has to be reversed out. That is the honest
        # behaviour of a steered chassis and worth feeling in the simulator, because it
        # is what a cornered operator will actually face.
        self.bumped = False
        if not self._collides(nx, ny, heading):
            self.x, self.y, self.heading = nx, ny, heading
        else:
            self.v = 0.0
            self.w = 0.0
            self.bumped = True

        # The steering motor is a real load too, and a held turn parks it against its end
        # stop -- a continuous stall. Step 1.3 removed the steering PWM (full voltage is
        # what it takes to shift the axle against its return spring), so that stall is at
        # FULL rail voltage: docs/power-budget.md finding F6 puts it at ~2.0 A against a
        # 1.5 A RMS bridge rating, and at about a fifth of the whole session's energy.
        #
        # Counted here at half the drive motor's weight, which now looks too LIGHT -- the
        # power budget makes it comparable to both drive motors together. Left alone until
        # step 1.4 puts a meter on it rather than swapped for a second guess.
        load = abs(throttle) + 0.5 * (1.0 if target_angle else 0.0)
        drain_per_s = (BATTERY_FULL_V - BATTERY_EMPTY_V) / (self.battery_minutes * 60.0)
        self.battery_v = max(
            BATTERY_EMPTY_V,
            self.battery_v - drain_per_s * (IDLE_DRAIN_FRACTION + load) * dt,
        )

    def rssi(self) -> int:
        """Weaker downrange, which is the R6 case step 1.2 goes looking for."""
        return int(-42 - (self.y / ARENA_L) * 28 + random.uniform(-2, 2))

    def antenna(self) -> tuple[float, float]:
        """Where the RFID antenna actually is — ahead of the rover's centre."""
        return (self.x + ANTENNA_OFFSET_M * math.sin(self.heading),
                self.y + ANTENNA_OFFSET_M * math.cos(self.heading))


def tag_rssi(rover: Rover, rock: Rock) -> tuple[float, float, float]:
    """Returns (rssi_dbm, distance_m, bearing_deg) for one rock.

    `rssi` is -inf when the rock is outside the antenna's beam, which is a different
    failure from being merely far away and is worth keeping distinguishable.
    """
    ax, ay = rover.antenna()
    dx, dy = rock.x - ax, rock.y - ay
    distance = max(math.hypot(dx, dy), 0.01)

    # Heading is clockwise from downrange, so bearing is the same convention.
    bearing = math.degrees(math.atan2(dx, dy) - rover.heading)
    bearing = (bearing + 180.0) % 360.0 - 180.0

    if abs(bearing) > BEAM_HALF_ANGLE_DEG:
        return (float("-inf"), distance, bearing)

    path = RSSI_AT_REF - PATH_LOSS_EXPONENT_DB * math.log10(distance / REF_DISTANCE_M)
    off_axis = BEAM_EDGE_LOSS_DB * (abs(bearing) / BEAM_HALF_ANGLE_DEG) ** 2
    return (path - off_axis, distance, bearing)


# ---------------------------------------------------------------------------------
# The server
# ---------------------------------------------------------------------------------


class Simulator:
    def __init__(self, args: argparse.Namespace) -> None:
        model = SURFACES[args.surface]
        # Each override is applied on top of the surface preset rather than replacing
        # it, so `--surface hard --turn-efficiency 0.4` means what it looks like.
        if args.turn_efficiency is not None:
            model = replace(model, turn_efficiency=args.turn_efficiency,
                            name=f"{model.name}*")
        if args.accel_time is not None:
            model = replace(model, accel_time=args.accel_time, name=f"{model.name}*")
        if args.coast_time is not None:
            model = replace(model, coast_time=args.coast_time, name=f"{model.name}*")
        if args.breakaway is not None:
            model = replace(model, breakaway=args.breakaway, name=f"{model.name}*")

        chassis_l, chassis_w = args.chassis
        self.rover = Rover(
            battery_minutes=args.battery_minutes,
            model=model,
            chassis_l=chassis_l,
            chassis_w=chassis_w,
            wheelbase=args.wheelbase,
            steer_lock=args.steer_lock,
            dressing=args.dressing,
            drift=args.drift,
        )
        self.failsafe = Failsafe()
        self.clients: set = set()
        self.latency_ms = args.latency
        self.loss_pct = args.loss
        self.drop_after = args.drop_after
        self.caps = [c.strip() for c in args.caps.split(",") if c.strip()]
        self.version = args.protocol_version
        self.telemetry_on = True
        self.quiet = args.quiet
        # "absent" unless this build claims a reader. Tagged rocks and real
        # distance-dependent reads arrive in step S.10; for now a tag frame can be
        # injected by hand with the `tag` runtime switch, which is enough to exercise
        # the console's dispatch path.
        self.reader_state = "ready" if "rfid" in self.caps else "absent"
        self._last_tag_sent: dict[str, float] = {}

        self.rocks = default_rocks()
        for tag_id in (t.strip().upper() for t in args.dead_tags.split(",") if t.strip()):
            for rock in self.rocks:
                if rock.tag_id.upper() == tag_id:
                    rock.dead = True

        # Sensitivity is derived from the requested read range rather than set
        # independently, so the two can never disagree: "range" means "the distance at
        # which a boresight read is exactly marginal".
        self.read_range = args.read_range
        self.sensitivity = (RSSI_AT_REF - PATH_LOSS_EXPONENT_DB
                            * math.log10(self.read_range / REF_DISTANCE_M))
        self.flaky_pct = args.flaky
        self._last_any_read_t = 0.0
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

    def model_summary(self) -> str:
        rover, model = self.rover, self.rover.model
        return (f"surface {model.name}  speed x{model.speed_factor:.2f}  "
                f"turn eff {model.turn_efficiency:.2f}  accel {model.accel_time:.2f}s  "
                f"coast {model.coast_time:.2f}s  breakaway {model.breakaway:.2f}  "
                f"slip noise {model.slip_noise:.2f}  sideslip {model.icr_offset:.3f} m\n"
                f"    drift {rover.drift:+.2f}deg  chassis "
                f"{rover.chassis_l:.2f}x{rover.chassis_w:.2f} m  "
                f"wheelbase {rover.wheelbase:.3f} m  lock {rover.steer_lock:.0f}deg\n"
                f"    turn radius {rover.turn_radius():.2f} m  "
                f"U-turn needs {rover.turning_circle():.2f} m  "
                f"drivable width {rover.usable_width():.2f} m  "
                f"clearance {rover.turn_clearance():+.2f} m")

    def turn_verdict(self) -> str:
        """One line on whether a U-turn fits, since half the switches change it."""
        rover = self.rover
        clearance = rover.turn_clearance()
        fits = "FITS" if clearance >= 0 else "BLOCKED"
        return (f"turn radius {rover.turn_radius():.2f} m, U-turn needs "
                f"{rover.turning_circle():.2f} m of {rover.usable_width():.2f} m "
                f"— {fits} ({clearance:+.2f} m)")

    def find_rock(self, needle: str) -> Rock | None:
        """Matches a rock by tag ID or by any unique suffix of it, so the runtime
        switches do not need the full 14 characters typed."""
        needle = needle.upper()
        for rock in self.rocks:
            if rock.tag_id.upper().endswith(needle) or rock.label.upper() == needle:
                return rock
        return None

    def set_read_range(self, metres: float) -> None:
        self.read_range = max(0.02, metres)
        self.sensitivity = (RSSI_AT_REF - PATH_LOSS_EXPONENT_DB
                            * math.log10(self.read_range / REF_DISTANCE_M))

    def rock_table(self) -> str:
        rows = ["    tag            label      dist   bearing    rssi"]
        for rock in sorted(self.rocks, key=lambda k: math.hypot(k.x - self.rover.x,
                                                                k.y - self.rover.y)):
            rssi, distance, bearing = tag_rssi(self.rover, rock)
            signal = "out of beam" if rssi == float("-inf") else f"{rssi:7.1f} dBm"
            rows.append(f"    {rock.tag_id} {rock.label:<9} {distance:5.2f} "
                        f"{bearing:+6.0f}   {signal}"
                        + ("   DEAD" if rock.dead else ""))
        return "\n".join(rows)

    def emit_tag(self, tag_id: str, rssi: int) -> bool:
        """Reports one tag read, subject to the per-tag rate limit (protocol.md 4.4)."""
        if not tag_id or len(tag_id) > MAX_TAG_ID_CHARS:
            self.log(f"refusing to emit tag with unusable id {tag_id!r}")
            return False

        now = time.monotonic()
        if now - self._last_tag_sent.get(tag_id, 0.0) < TAG_RATE_LIMIT_S:
            return False
        self._last_tag_sent[tag_id] = now

        self.broadcast({
            "t": "tag",
            "id": tag_id.upper(),
            "rssi": int(rssi),
            "ts": int(now * 1000) & 0xFFFFFFFF,
        })
        return True

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
                self.rover.throttle, self.rover.steer_cmd = cmd.throttle, cmd.steer
            else:
                self.rover.stop()
        elif cmd.kind == "mast":
            if self.state == "drive":
                if cmd.pan is not None:
                    self.rover.pan = cmd.pan
                if cmd.tilt is not None:
                    self.rover.tilt = cmd.tilt

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

    async def rfid_loop(self) -> None:
        """Polls every rock the way the firmware will poll the reader (step 2.6)."""
        interval = 1.0 / RFID_POLL_HZ
        while True:
            await asyncio.sleep(interval)
            if self.reader_state == "absent" or "rfid" not in self.caps:
                continue

            now = time.monotonic()
            any_read = False

            for rock in self.rocks:
                rssi, distance, _bearing = tag_rssi(self.rover, rock)
                rock.last_rssi = rssi

                if rock.dead or rssi == float("-inf"):
                    continue

                margin = rssi - self.sensitivity
                if margin < 0.0:
                    continue

                # Ragged at the edge, solid once well inside. A hard cutoff would let the
                # console assume every approach ends in a clean lock (risk R2).
                probability = min(1.0, margin / MARGINAL_BAND_DB)
                if self.flaky_pct:
                    probability *= max(0.0, 1.0 - self.flaky_pct / 100.0)
                if random.random() > probability:
                    continue

                any_read = True
                rock.last_read_t = now
                # Rounded to whole dBm, as a real reader reports it.
                self.emit_tag(rock.tag_id, int(round(rssi)))

            if any_read:
                self._last_any_read_t = now
            # Held briefly rather than recomputed per poll: at 20 Hz against a marginal
            # tag the state would otherwise chatter, and telemetry sampling it at 2 Hz
            # would report whichever side of the coin it happened to land on.
            self.reader_state = ("scanning"
                                 if now - self._last_any_read_t < 0.5 else "ready")

    async def physics_loop(self) -> None:
        # Integrate over *measured* elapsed time, not the nominal tick. Windows timers
        # round `asyncio.sleep(0.02)` up to about 31 ms, so advancing a fixed 20 ms per
        # iteration ran simulated time at roughly two-thirds of wall clock — the rover
        # arrived where the arithmetic said only about 65% of the time, which made every
        # timed approach test quietly wrong.
        target = 1.0 / PHYSICS_HZ
        previous_t = time.monotonic()
        previous = "safe"
        while True:
            await asyncio.sleep(target)
            now = time.monotonic()
            # Clamped so a stalled process cannot teleport the rover across the arena.
            dt = min(now - previous_t, 0.25)
            previous_t = now
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
                "rfid": self.reader_state,
            }
            # Reserved names (protocol.md 4.2), additive and only sent for subsystems
            # this build claims to have.
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
        rover = self.rover

        # Base zone across the near end (Scene 1 and Scene 8 happen here).
        base_rows = max(1, int(rows * 0.4 / ARENA_L))
        for r in range(rows - base_rows, rows):
            for c in range(cols):
                grid[r][c] = "."

        now = time.monotonic()
        cell_w, cell_h = ARENA_W / cols, ARENA_L / rows

        def cell(x: float, y: float) -> tuple[int, int]:
            return (clamp(int((1.0 - y / ARENA_L) * (rows - 1)), 0, rows - 1),
                    clamp(int(x / ARENA_W * (cols - 1)), 0, cols - 1))

        def centre(r: int, c: int) -> tuple[float, float]:
            return ((c + 0.5) * cell_w, ARENA_L - (r + 0.5) * cell_h)

        # The dressing piles up at the walls and narrows what can actually be driven.
        if rover.dressing > 0:
            for r in range(rows):
                for c in range(cols):
                    px, py = centre(r, c)
                    if (px < rover.dressing or px > ARENA_W - rover.dressing
                            or py < rover.dressing or py > ARENA_L - rover.dressing):
                        grid[r][c] = ":"

        # Rocks next, so the rover draws over them rather than under.
        for rock in self.rocks:
            r, c = cell(rock.x, rock.y)
            if now - rock.last_read_t < 0.4:
                grid[r][c] = "@"  # reading right now
            elif rock.dead:
                grid[r][c] = "x"  # dead tag — visible here, invisible to the console
            else:
                grid[r][c] = "o"

        # The minimum turning circle, drawn whenever the axle is turned — this is R5 in
        # its new form, and seeing whether a U-turn fits between the walls is the whole
        # question (docs/chassis-envelope.md 8.1).
        #
        # Drawn on the *steering angle*, not on the yaw rate: at zero throttle the rover
        # is not rotating at all, and that is exactly the moment the operator most wants
        # to see where the arc would take them if they opened the throttle.
        if abs(rover.steer_angle) > 1.0:
            # Turn centre is abeam the rear axle, on the inside of the turn.
            radius = rover.turn_radius()
            _, side = rover.axes()
            hand = 1.0 if rover.steer_angle > 0 else -1.0
            cx = rover.x + side[0] * radius * hand
            cy = rover.y + side[1] * radius * hand
            outer, inner = rover.swept_radius(), rover.inner_radius()
            for r in range(rows):
                for c in range(cols):
                    px, py = centre(r, c)
                    reach = math.hypot(px - cx, py - cy)
                    near_edge = (abs(reach - outer) < cell_w / 2
                                 or abs(reach - inner) < cell_w / 2)
                    if near_edge and grid[r][c] in (" ", ".", ":"):
                        grid[r][c] = "·"

        # Chassis footprint, so its size relative to the arena is visible rather than
        # implied by a single character.
        for r in range(rows):
            for c in range(cols):
                px, py = centre(r, c)
                if rover.footprint(px, py):
                    grid[r][c] = "#"

        r, c = cell(rover.x, rover.y)
        arrows = "↑↗→↘↓↙←↖"
        index = int(((rover.heading + math.pi / 8) % (2 * math.pi)) / (math.pi / 4))
        grid[r][c] = arrows[index % 8]

        clearance = rover.turn_clearance()
        if clearance >= 0:
            verdict = (f"U-turn FITS in one move, {clearance:.2f} m spare "
                       f"({clearance / 2:.2f} m each side)")
        else:
            verdict = (f"U-turn NEEDS {-clearance:.2f} m MORE — three-point turn only "
                       f"(R5)")

        # What the steering is actually doing, which is not always what was asked for:
        # the axle slews, and it springs back to centre whenever the motor is unpowered.
        if rover.steer_angle < -1.0:
            steer_word = "LEFT "
        elif rover.steer_angle > 1.0:
            steer_word = "RIGHT"
        else:
            steer_word = "AHEAD"
        # The case the console has to be honest about: axle turned, rover going nowhere.
        wheels_only = abs(rover.steer_angle) > 1.0 and abs(rover.v) < 0.005

        model = rover.model
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
            + ("   <-- WALL" if rover.bumped else ""),
            f"  pose     x={rover.x:4.2f} y={rover.y:4.2f} "
            f"hdg={math.degrees(rover.heading):5.0f}deg",
            f"  command  fwd={rover.throttle:+.2f}  steer={rover.steer_cmd:+.2f}",
            f"  steering {steer_word} {rover.steer_angle:+5.1f}deg of "
            f"{rover.steer_lock:.0f}"
            + ("   <-- WHEELS ONLY, NO THROTTLE" if wheels_only else ""),
            f"  motion   v={rover.v:+5.3f} m/s  w={math.degrees(rover.w):+5.0f} deg/s"
            f"   coast {rover.coast_distance() * 100:4.1f} cm",
            f"  surface  {model.name:<6} turn eff {model.turn_efficiency:.2f}"
            f"   drift {rover.drift:+.2f}deg"
            f"   accel {model.accel_time:.2f}s  coast {model.coast_time:.2f}s",
            f"  chassis  {rover.chassis_l:.2f} x {rover.chassis_w:.2f} m"
            f"   wheelbase {rover.wheelbase:.3f} m   lock {rover.steer_lock:.0f}deg",
            f"  turning  radius {rover.turn_radius():.2f} m   U-turn needs "
            f"{rover.turning_circle():.2f} m   drivable "
            f"{rover.usable_width():.2f} m",
            f"           {verdict}",
            f"  battery  {rover.battery_v:5.2f} V     rssi {rover.rssi()} dBm",
            f"  clients  {len(self.clients)}   frames in {self.frames_in}"
            f"   dropped {self.frames_dropped}",
            f"  faults   latency {self.latency_ms} ms   loss {self.loss_pct}%"
            f"   flaky {self.flaky_pct}%",
            f"  reader   {self.reader_state}   range {self.read_range:.2f} m"
            f"   floor {self.sensitivity:.0f} dBm",
            "",
            "  # chassis   · turning circle   : dressing   o rock   @ reading"
            "   x dead tag",
        ]

        # Nearest few rocks, so the numbers behind the picture are checkable.
        ranked = sorted(self.rocks, key=lambda k: math.hypot(k.x - self.rover.x,
                                                             k.y - self.rover.y))
        for rock in ranked[:4]:
            rssi, distance, bearing = tag_rssi(self.rover, rock)
            if rssi == float("-inf"):
                signal = "  out of beam"
            else:
                signal = f"{rssi:6.1f} dBm" + ("" if rssi >= self.sensitivity else "  --")
            lines.append(f"    {rock.tag_id[-6:]} {rock.label:<9} {distance:4.2f} m "
                         f"{bearing:+5.0f}deg {signal}"
                         + ("   DEAD" if rock.dead else ""))

        lines += ["", "  type `help` + Enter for runtime switches"]
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
  tag <id> [rssi]   inject one RFID tag read by hand
  reader <absent|ready|scanning|fault>   force the reported reader state
  rocks             list the tagged rocks, with distance and signal
  kill <id>         make a tag dead, as if cooked during embedding (risk R7)
  revive <id>       undo it
  range <m>         change the RFID read range
  flaky <pct>       drop this share of otherwise-good reads
  batt <volts> force the pack voltage, for testing the low-battery display
  reset        recentre the rover and refill the pack
  quit         stop the simulator

drive model (S.12, steered at S.15) — every one of these is a guess until steps 1.3-1.5:
  surface <hard|sand>    swap the whole terrain preset
  slip <0..1>            turn efficiency; 1.0 follows the geometry, lower understeers
  drift <deg>            residual steering angle — why a commanded straight curves
  coast <sec>            how long it takes to stop once the motors are cut
  breakaway <throttle>   what it takes to start moving from rest on this surface
  chassis <L> <W>        chassis size in metres
  wheelbase <m>          front axle to rear axle — sets the minimum turning circle
  lock <deg>             steering angle at full lock — the other half of that circle
  dressing <m>           terrain depth at the walls, which narrows the drivable width
  model                  print the current drive model and the U-turn verdict
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
            elif verb == "tag" and rest:
                rssi = int(rest[1]) if len(rest) > 1 else -55
                sent = loop.call_soon_threadsafe(sim.emit_tag, rest[0], rssi)
                sim.log(f"injected tag read {rest[0].upper()} at {rssi} dBm")
            elif verb == "reader" and rest:
                state = rest[0].lower()
                if state not in ("absent", "ready", "scanning", "fault"):
                    sim.log(f"unknown reader state {state!r}")
                else:
                    sim.reader_state = state
                    sim.log(f"reader state now {state}")
            elif verb == "rocks":
                sim.log("\n" + sim.rock_table())
            elif verb in ("kill", "revive") and rest:
                rock = sim.find_rock(rest[0])
                if rock is None:
                    sim.log(f"no rock with tag ending {rest[0]!r}")
                else:
                    rock.dead = verb == "kill"
                    sim.log(f"{rock.tag_id} ({rock.label}) is now "
                            + ("DEAD — reads exactly like out of range" if rock.dead
                               else "alive again"))
            elif verb == "range" and rest:
                sim.set_read_range(float(rest[0]))
                sim.log(f"read range now {sim.read_range:.2f} m "
                        f"(floor {sim.sensitivity:.0f} dBm)")
            elif verb == "flaky" and rest:
                sim.flaky_pct = float(rest[0])
                sim.log(f"flaky now {sim.flaky_pct}%")
            elif verb == "batt" and rest:
                sim.rover.battery_v = float(rest[0])
                sim.log(f"battery forced to {sim.rover.battery_v} V")
            elif verb == "surface" and rest:
                key = rest[0].lower()
                if key not in SURFACES:
                    sim.log(f"unknown surface {key!r} — try {'/'.join(SURFACES)}")
                else:
                    sim.rover.model = SURFACES[key]
                    sim.log(f"surface now {key}: {sim.model_summary()}")
            elif verb == "slip" and rest:
                sim.rover.model = replace(sim.rover.model,
                                          turn_efficiency=clamp(float(rest[0]), 0.05, 1.0),
                                          name=sim.rover.model.name.rstrip("*") + "*")
                sim.log(f"turn efficiency now {sim.rover.model.turn_efficiency:.2f}")
            elif verb == "drift" and rest:
                sim.rover.drift = float(rest[0])
                sim.log(f"steering drift now {sim.rover.drift:+.2f} deg residual")
            elif verb == "breakaway" and rest:
                sim.rover.model = replace(sim.rover.model,
                                          breakaway=clamp(float(rest[0]), 0.0, 1.0),
                                          name=sim.rover.model.name.rstrip("*") + "*")
                sim.log(f"breakaway now {sim.rover.model.breakaway:.2f}")
            elif verb == "wheelbase" and rest:
                sim.rover.wheelbase = max(0.02, float(rest[0]))
                sim.log(f"wheelbase now {sim.rover.wheelbase:.3f} m — "
                        f"{sim.turn_verdict()}")
            elif verb == "lock" and rest:
                sim.rover.steer_lock = clamp(float(rest[0]), 1.0, 75.0)
                sim.log(f"steering lock now {sim.rover.steer_lock:.0f} deg — "
                        f"{sim.turn_verdict()}")
            elif verb == "coast" and rest:
                sim.rover.model = replace(sim.rover.model,
                                          coast_time=max(0.01, float(rest[0])),
                                          name=sim.rover.model.name.rstrip("*") + "*")
                sim.log(f"coast time now {sim.rover.model.coast_time:.2f}s")
            elif verb == "chassis" and len(rest) >= 2:
                sim.rover.chassis_l = max(0.05, float(rest[0]))
                sim.rover.chassis_w = max(0.05, float(rest[1]))
                sim.log(f"chassis now {sim.rover.chassis_l:.2f} x "
                        f"{sim.rover.chassis_w:.2f} m — {sim.turn_verdict()}")
            elif verb == "dressing" and rest:
                sim.rover.dressing = max(0.0, float(rest[0]))
                sim.log(f"dressing now {sim.rover.dressing:.2f} m — drivable width "
                        f"{sim.rover.usable_width():.2f} m, {sim.turn_verdict()}")
            elif verb == "model":
                sim.log(sim.model_summary())
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
        asyncio.create_task(sim.rfid_loop()),
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
    # `steer3` declares three-position steering (docs/protocol.md 4.1). Exactly one
    # steering token must accompany `drive`, so the console knows whether a `steer` of
    # 0.3 will be honoured or rounded away; drop it deliberately to check that the
    # console refuses to arm rather than guessing.
    parser.add_argument("--caps", default="drive,steer3",
                        help="subsystems to advertise, e.g. drive,steer3,mast,rfid")
    parser.add_argument("--protocol-version", type=int, default=PROTOCOL_VERSION,
                        metavar="N",
                        help="version to advertise; set it wrong to force a mismatch")
    parser.add_argument("--read-range", type=float, default=0.35, metavar="M",
                        help="distance at which a boresight read is exactly marginal")
    parser.add_argument("--flaky", type=float, default=0.0, metavar="PCT",
                        help="drop this share of otherwise-good tag reads")
    parser.add_argument("--dead-tags", default="", metavar="IDS",
                        help="comma-separated tag IDs that never read, as if killed "
                             "during embedding (risk R7)")
    parser.add_argument("--battery-minutes", type=float, default=20.0,
                        help="minutes of full-throttle driving from full to empty")

    drive = parser.add_argument_group(
        "drive model (steps S.12, S.15)",
        "Every default here is a guess until steps 1.3-1.5 measure the real rover. "
        "These switches exist so those measurements can be fed back in. --wheelbase and "
        "--steer-lock together set the minimum turning circle, which is what risk R5 "
        "now means.")
    drive.add_argument("--surface", choices=sorted(SURFACES), default="sand",
                       help="terrain preset; the arena floor is sand (default: sand)")
    drive.add_argument("--wheelbase", type=float, default=WHEELBASE, metavar="M",
                       help="front axle to rear axle in metres; with --steer-lock this "
                            f"sets the turning circle (default: {WHEELBASE})")
    drive.add_argument("--steer-lock", type=float, default=STEER_LOCK_DEG, metavar="DEG",
                       help="steering angle at full lock; smaller means a wider turning "
                            f"circle (default: {STEER_LOCK_DEG:g})")
    drive.add_argument("--turn-efficiency", type=float, default=None, metavar="F",
                       help="fraction of the geometric yaw rate actually achieved; 1.0 "
                            "removes understeer but leaves drift, breakaway and momentum")
    drive.add_argument("--accel-time", type=float, default=None, metavar="SEC",
                       help="time constant to reach commanded speed")
    drive.add_argument("--coast-time", type=float, default=None, metavar="SEC",
                       help="time constant to stop once the motors are cut; step 1.6 "
                            "measures the real one")
    drive.add_argument("--breakaway", type=float, default=None, metavar="THROTTLE",
                       help="throttle needed to start moving from a standstill")
    drive.add_argument("--drift", type=float, default=0.5, metavar="DEG",
                       help="residual steering angle when commanded straight, signed — "
                            "why a straight line curves (default: 0.5)")
    drive.add_argument("--chassis", type=float, nargs=2, default=[CHASSIS_L, CHASSIS_W],
                       metavar=("L", "W"),
                       help="chassis length and width in metres; the width widens the "
                            f"swept arc (default: {CHASSIS_L} {CHASSIS_W})")
    drive.add_argument("--dressing", type=float, default=DRESSING_DEPTH, metavar="M",
                       help="terrain depth at the walls; the drivable width is the "
                            f"arena minus twice this (default: {DRESSING_DEPTH})")
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
