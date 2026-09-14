extends Node
## Control-channel client: console -> ESP32 over a WebSocket (docs/protocol.md).
##
## Owns every transport concern — connecting and reconnecting, the version handshake,
## JSON framing, the held-command repeat, and link health. UI scripts call the send_*
## methods and read `state`; they never touch the socket.
##
## This is the control channel only. Camera video never travels over it — each IP
## camera streams straight to its own monitor, so a busy video feed can never make the
## drive controls laggy (plan/storyboard.md 5.4, risk R1).

signal link_state_changed(new_state: State)
signal telemetry_received(data: Dictionary)
signal handshake_completed(rover_version: int, firmware: String, caps: Array)
signal telemetry_stale_changed(stale: bool)
signal rtt_updated(rtt_ms: int, p95_ms: int)

## One RFID tag read (docs/protocol.md 4.4). `rssi` is the reader's signal strength for
## this read — the proximity cue for Scene 5 — and is a different radio from the Wi-Fi
## RSSI in telemetry. The signal meter and analysis panel consume this in step S.11.
signal tag_read(tag_id: String, rssi: int, rover_ts_ms: int)

enum State {
	DISCONNECTED,  ## No socket.
	CONNECTING,    ## Socket opening, or the handshake sent and still unanswered.
	LINKED,        ## Versions agreed. Commands may be sent.
	INCOMPATIBLE,  ## Rover speaks a different protocol version; it will not arm.
}

## Bumped only for a breaking change (docs/protocol.md 5).
##
## v2 (step S.15 on this side, S.14 on the rover's): `drive` carries `fwd`/`steer`
## instead of `l`/`r`, after step 0.2 chose a steered chassis. A v1 rover and a v2
## console must NOT interoperate — the old frame parses as a valid zero-throttle command
## and moves nothing — so the handshake mismatch here is load-bearing, not cosmetic.
const PROTOCOL_VERSION := 2

## Steering kinds a rover may advertise in `caps` (docs/protocol.md 4.1). Exactly one
## must accompany `drive`; see _handle_hello() for why a missing one is refused rather
## than assumed.
const STEER_THREE_POSITION := "steer3"
const STEER_PROPORTIONAL := "steerprop"

const RECONNECT_INTERVAL_SEC := 2.0

## Resend the handshake this often while it goes unanswered. The rover fails closed if
## it hears no `hello` within 2 s, so retrying costs nothing and recovers a console that
## connected before the rover finished booting.
const HANDSHAKE_RETRY_SEC := 1.0

## A held drive control must be resent at least this often (docs/protocol.md 6.5).
## Chosen against the rover's 500 ms command timeout: at 150 ms it absorbs two
## consecutive lost frames before the failsafe trips. If COMMAND_TIMEOUT_MS ever
## changes, this has to be rechecked with it.
const DRIVE_REPEAT_SEC := 0.15

## Telemetry arrives every 500 ms; three missed frames means something is wrong.
const TELEMETRY_STALE_SEC := 1.5

## Round-trip probes, four a second. Frequent enough to build a percentile in under a
## minute, and cheap: a ping/pong pair is under 70 bytes on a channel that carries
## nothing else (docs/protocol.md 1.1). `ping` deliberately does not refresh the rover's
## failsafe, so probing an idle rover cannot arm it.
const PING_INTERVAL_SEC := 0.25

## A pong later than this is counted as lost rather than measured. Well beyond the
## 500 ms command timeout — anything this late already cost us the link.
const PING_TIMEOUT_SEC := 2.0

## Samples kept for the percentile: 240 at 4 Hz is a rolling minute.
const RTT_WINDOW := 240

# --- Fine drive (step S.13, onto the steered model at S.15) --------------------------
#
# Everything here shapes the `fwd` number the console sends. `steer` is deliberately
# NOT shaped: the rover thresholds it to full lock or centre anyway (docs/protocol.md
# 3.2.1), so curving it or lifting it off a floor would be shaping a value that gets
# rounded away. Steering demand goes on the wire as the operator meant it.

## How hard the pad drives. TRANSIT crosses the arena; PRECISION is for lining up on a
## rock in Scenes 4-5, where a full-throttle tap overshoots.
enum Speed { TRANSIT, PRECISION }

## The operator's intent, before shaping. Not the throttle that goes on the wire.
const SPEED_SCALE := {
	Speed.TRANSIT: 1.0,
	Speed.PRECISION: 0.35,
}

## Below this the rover does not reliably break out of its own ruts on sand — it buzzes
## and stays put, which reads to the operator as a dead control rather than a slow one.
## So the shaped output is either zero or at least this: there is no useful throttle in
## between.
##
## This is NOT the firmware's deadband (kThrottleDeadband, 0.05), which is the H-bridge
## coasting. This is the ground.
##
## **Raised from 0.32 to 0.45 at step S.15**, and not because the sand changed. The old
## figure came from the S.12 model's sand breakaway of 0.28 plus margin, which assumed a
## MOSFET bridge. The chosen L293D drops ~1.8-2 V against that part's ~0.5 V, so on a 6 V
## rail the motor sees about 4 V and needs roughly 6/4 of the duty for the same torque
## (docs/chassis-envelope.md 8.3) — about 0.42, and one driven axle instead of four
## driven wheels pushes the same way. Still **provisional until step 1.5 drives on real
## sand**, which is where the real number comes from.
const MIN_EFFECTIVE_THROTTLE := 0.45

## Above the floor, response is curved rather than linear so that most of the pad's
## range lives at the slow end where the aiming happens.
const THROTTLE_GAMMA := 1.8

## One nudge. Long enough to clear breakaway and actually move, short enough that the
## result is a step rather than a drive.
##
## A steering nudge is the more useful of the two on this chassis: three-position
## steering offers no small heading correction, so a *timed* full-lock tap while rolling
## is the only fine correction there is (docs/chassis-envelope.md 8.5).
const NUDGE_SEC := 0.35

signal speed_mode_changed(mode: Speed)
signal nudge_state_changed(running: bool)

@export var rover_url := "ws://192.168.4.1:81/"
@export var auto_connect := true

var state: State = State.DISCONNECTED
var rover_version := 0
var rover_firmware := ""
var rover_caps: Array = []

## Most recent round trip, the rolling 95th percentile, and the share of probes that
## never came back. -1 means "not measured yet". Risk R1 is judged on these.
var rtt_ms := -1
var rtt_p95_ms := -1
var rtt_loss_pct := 0.0

var _socket := WebSocketPeer.new()
var _reconnect_timer := 0.0
var _handshake_timer := 0.0
var _repeat_timer := 0.0
var _since_telemetry := 0.0
var _stale := true
var _had_telemetry := false

var speed_mode: Speed = Speed.TRANSIT

var _holding := false
## What goes on the wire. `_hold_throttle` is shaped; `_hold_steer` is not (see above).
var _hold_throttle := 0.0
var _hold_steer := 0.0
## What the operator asked for, kept unshaped so a mid-press mode change can be reapplied.
var _raw_throttle := 0.0
var _raw_steer := 0.0
var _request_scale := 1.0
var _nudge_remaining := 0.0

var _ping_timer := 0.0
var _outstanding: Dictionary = {}  ## ping timestamp -> the same value, awaiting its pong
var _samples: Array[int] = []
var _pings_sent := 0
var _pongs_lost := 0


func _ready() -> void:
	# The desktop simulator (tools/fake_rover.py) listens on localhost, not on the
	# rover's own address. Point the console at it with the ROVER_URL environment
	# variable rather than editing the scene, so the committed default stays the
	# real rover.
	var override := OS.get_environment("ROVER_URL")
	if override != "":
		rover_url = override
		print("Rover link: using ROVER_URL override %s" % rover_url)

	set_process(auto_connect)
	if auto_connect:
		connect_to_rover()


func connect_to_rover() -> void:
	_reset_session()
	var err := _socket.connect_to_url(rover_url)
	if err != OK:
		push_warning("Rover link: connect_to_url(%s) failed with %d" % [rover_url, err])
		_set_state(State.DISCONNECTED)
	else:
		_set_state(State.CONNECTING)


## State predicates, so UI scripts never have to reach across into this script's enum.
func is_linked() -> bool:
	return state == State.LINKED


func is_connecting() -> bool:
	return state == State.CONNECTING


func is_disconnected() -> bool:
	return state == State.DISCONNECTED


func is_incompatible() -> bool:
	return state == State.INCOMPATIBLE


func is_telemetry_stale() -> bool:
	return _stale


## False until the first telemetry frame of this connection arrives. A handshake agreed
## but no data yet is still "connecting" — reporting it as stale would flash a warning
## on every single connect.
func has_telemetry() -> bool:
	return _had_telemetry


## True when the rover reported this subsystem in its handshake `caps` list. The
## console must not send commands to a subsystem that does not exist on this build.
func has_capability(name: String) -> bool:
	return rover_caps.has(name)


# --- Sending ----------------------------------------------------------------------

## Begins (or updates) a held drive command. The frame is repeated automatically until
## release_drive() — see DRIVE_REPEAT_SEC.
##
## `throttle` is -1.0 .. 1.0, positive forward, and is shaped by the current speed mode
## before it goes on the wire. `steer` is -1.0 .. 1.0, **negative left**, and goes out
## unshaped. The two are independent: steering with no throttle turns the wheels and
## moves the rover nowhere, which is a real state the pad has to own up to — see
## is_steering_without_throttle().
func hold_drive(throttle: float, steer: float) -> void:
	_cancel_nudge()
	_begin_drive(throttle, steer, SPEED_SCALE[speed_mode])


## One discrete step, then stop — for lining up on a rock without having to time a
## button press (Scene 5). It is exactly a held press of NUDGE_SEC: the same repeated
## `drive` frames, ended by the same zero-throttle frame, so there is nothing new for
## the rover to understand and nothing new for the failsafe to reason about.
##
## Always PRECISION-scaled whatever the mode is set to — a nudge is a fine move by
## definition, and one that flung the rover forward because the mode happened to be
## TRANSIT would be a trap. The scaling applies to the throttle only; a steering nudge
## is full lock for a short time, because full lock is the only lock there is.
func nudge(throttle: float, steer: float, seconds: float = NUDGE_SEC) -> void:
	_cancel_nudge()
	_begin_drive(throttle, steer, SPEED_SCALE[Speed.PRECISION])
	_nudge_remaining = maxf(0.05, seconds)
	nudge_state_changed.emit(true)


## True while the console is commanding steering with no throttle behind it. The wheels
## move; the rover does not (docs/protocol.md 3.2). The pad must say so — an operator who
## presses LEFT expecting a pivot, sees nothing, and presses harder is the failure this
## exists to prevent (docs/protocol.md 6.4).
func is_steering_without_throttle() -> bool:
	return _holding and absf(_hold_steer) >= 0.5 and absf(_hold_throttle) < 0.001


func is_nudging() -> bool:
	return _nudge_remaining > 0.0


## Ends a held drive command: one explicit zero-throttle frame, then silence. Zero
## throttle rather than `stop` because the rover stays armed with a commanded speed of
## zero, which is what a finger lifting off means (docs/protocol.md 3.3).
func release_drive() -> void:
	_cancel_nudge()
	if not _holding:
		return
	_holding = false
	_clear_drive_inputs()
	_send_drive()


## Deliberate halt. Honoured by the rover in every state, including safe mode and on a
## version mismatch, so this is the one command always worth sending.
func send_stop() -> void:
	_cancel_nudge()
	_holding = false
	_clear_drive_inputs()
	_send({"cmd": "stop"})


## Zeroes throttle and steering together. Steering is included deliberately: releasing
## the pad must also release the steering motor, which is what lets the spring recentre
## the axle (docs/protocol.md 6.1).
func _clear_drive_inputs() -> void:
	_raw_throttle = 0.0
	_raw_steer = 0.0
	_hold_throttle = 0.0
	_hold_steer = 0.0


## Switches the pad between transit and precision. Takes effect on a command already
## being held, immediately rather than at the next repeat — an operator who thumbs the
## mode button mid-press is asking for the rover to slow down now.
func set_speed_mode(mode: Speed) -> void:
	if speed_mode == mode:
		return
	speed_mode = mode
	speed_mode_changed.emit(speed_mode)
	# A nudge keeps its own scale; it is a fine move regardless of the mode.
	if _holding and not is_nudging():
		_request_scale = SPEED_SCALE[speed_mode]
		_apply_shaping()
		_send_drive()


func toggle_speed_mode() -> void:
	set_speed_mode(Speed.TRANSIT if speed_mode == Speed.PRECISION else Speed.PRECISION)


func speed_mode_name() -> String:
	return "PRECISION" if speed_mode == Speed.PRECISION else "TRANSIT"


func _begin_drive(throttle: float, steer: float, scale: float) -> void:
	_holding = true
	_raw_throttle = clampf(throttle, -1.0, 1.0)
	_raw_steer = clampf(steer, -1.0, 1.0)
	_request_scale = scale
	_apply_shaping()
	_repeat_timer = DRIVE_REPEAT_SEC
	_send_drive()


## Throttle is scaled and curved; steering is not. Scaling `steer` by the speed mode
## would be a lie on a three-position chassis — a PRECISION-scaled 0.35 falls below the
## rover's 0.5 threshold and rounds to *straight ahead*, so the pad would silently stop
## steering in the mode where careful steering matters most.
func _apply_shaping() -> void:
	_hold_throttle = _shape(_raw_throttle * _request_scale)
	_hold_steer = _raw_steer


## Maps operator intent onto a throttle the rover can actually act on: curved, then
## lifted clear of the floor. The result is zero or usable, never in the dead band
## between — see MIN_EFFECTIVE_THROTTLE.
func _shape(value: float) -> float:
	var magnitude := absf(value)
	if magnitude < 0.001:
		return 0.0
	magnitude = minf(magnitude, 1.0)
	var curved: float = pow(magnitude, THROTTLE_GAMMA)
	return signf(value) * (MIN_EFFECTIVE_THROTTLE
			+ (1.0 - MIN_EFFECTIVE_THROTTLE) * curved)


## A nudge must be abandonable the instant anything else happens — a moving rover with a
## command that cannot be interrupted is a worse failure than a twitchy one.
func _cancel_nudge() -> void:
	if _nudge_remaining <= 0.0:
		return
	_nudge_remaining = 0.0
	nudge_state_changed.emit(false)


## Mast camera pan/tilt head, in degrees. Absolute angles, not increments.
func send_mast(pan_deg: float, tilt_deg: float) -> void:
	_send({"cmd": "mast", "pan": pan_deg, "tilt": tilt_deg})


func _send_drive() -> void:
	_send({
		"cmd": "drive",
		"fwd": snappedf(_hold_throttle, 0.01),
		"steer": snappedf(_hold_steer, 0.01),
	})


func _send(payload: Dictionary) -> void:
	if _socket.get_ready_state() != WebSocketPeer.STATE_OPEN:
		return
	# Before the handshake agrees, and after it fails, the only traffic allowed is the
	# handshake itself and `stop` (docs/protocol.md 5).
	var verb: String = payload.get("cmd", "")
	if state != State.LINKED and verb != "hello" and verb != "stop":
		return
	_socket.send_text(JSON.stringify(payload))


# --- Receiving --------------------------------------------------------------------

func _process(delta: float) -> void:
	_socket.poll()

	match _socket.get_ready_state():
		WebSocketPeer.STATE_OPEN:
			while _socket.get_available_packet_count() > 0:
				_handle_packet(_socket.get_packet().get_string_from_utf8())
			_service_handshake(delta)
			# Before the repeat, so the frame that ends a nudge goes out on this tick
			# rather than one tick after the timer expired.
			_service_nudge(delta)
			_service_drive_repeat(delta)
			_service_ping(delta)

		WebSocketPeer.STATE_CLOSED:
			if state != State.DISCONNECTED:
				# State first, then the reset. The reset emits its own signals, and
				# doing it the other way round refreshes the UI once while the link
				# still looks open, flashing a state the operator never actually is in.
				_set_state(State.DISCONNECTED)
				_reset_session()
			_reconnect_timer -= delta
			if _reconnect_timer <= 0.0:
				_reconnect_timer = RECONNECT_INTERVAL_SEC
				connect_to_rover()

		WebSocketPeer.STATE_CLOSING:
			# A link on its way down is not a link coming up. Without this the operator
			# sees a hopeful "CONNECTING" flash at the exact moment they lost the rover.
			if state != State.DISCONNECTED:
				# State first, then the reset. The reset emits its own signals, and
				# doing it the other way round refreshes the UI once while the link
				# still looks open, flashing a state the operator never actually is in.
				_set_state(State.DISCONNECTED)
				_reset_session()

		_:
			_set_state(State.CONNECTING)

	_service_staleness(delta)


func _handle_packet(text: String) -> void:
	var parsed: Variant = JSON.parse_string(text)
	if not (parsed is Dictionary):
		push_warning("Rover link: frame was not a JSON object: %s" % text)
		return

	var frame: Dictionary = parsed
	# Every rover -> console frame carries a `t` discriminator (docs/protocol.md 2.1).
	# Dispatching on it is what stops a `pong` being displayed as telemetry.
	match frame.get("t", ""):
		"tlm":
			_since_telemetry = 0.0
			_had_telemetry = true
			_set_stale(false)
			telemetry_received.emit(frame)
		"hello":
			_handle_hello(frame)
		"pong":
			_handle_pong(frame)
		"tag":
			_handle_tag(frame)
		_:
			push_warning("Rover link: unrecognised frame type: %s" % text)


func _handle_tag(frame: Dictionary) -> void:
	var tag_id := str(frame.get("id", "")).to_upper()
	if tag_id.is_empty():
		# A read the console cannot attribute to a rock is worse than a dropped one —
		# it would put an entry in the session log that means nothing.
		push_warning("Rover link: tag frame with no id, dropped")
		return
	tag_read.emit(tag_id, int(frame.get("rssi", -100)), int(frame.get("ts", 0)))


func _handle_hello(frame: Dictionary) -> void:
	rover_version = int(frame.get("v", 0))
	rover_firmware = str(frame.get("fw", "?"))
	rover_caps = frame.get("caps", [])

	if rover_version != PROTOCOL_VERSION:
		push_warning("Rover link: rover speaks v%d, console speaks v%d"
				% [rover_version, PROTOCOL_VERSION])
		_set_state(State.INCOMPATIBLE)
	elif not _steering_declared():
		# A rover claiming `drive` with no steering token, or with both, is misreporting
		# itself, and the console cannot know whether a `steer` of 0.3 will be honoured
		# or rounded to straight (docs/protocol.md 4.1). Refusing is the same call as a
		# version mismatch: guessing here means driving a rover we do not understand.
		push_warning("Rover link: caps %s declares drive with no single steering kind"
				% str(rover_caps))
		_set_state(State.INCOMPATIBLE)
	else:
		_set_state(State.LINKED)

	handshake_completed.emit(rover_version, rover_firmware, rover_caps)


## Exactly one steering kind must accompany `drive`. A build with no drivetrain at all
## needs neither, so the check only bites when `drive` is claimed.
func _steering_declared() -> bool:
	if not rover_caps.has("drive"):
		return true
	var three: bool = rover_caps.has(STEER_THREE_POSITION)
	var proportional: bool = rover_caps.has(STEER_PROPORTIONAL)
	return three != proportional


## "3-POSITION", "PROPORTIONAL", or "" when the rover has no drivetrain. Used by the pad
## to label itself honestly — the operator should not have to remember which rover this
## build is.
func steering_kind() -> String:
	if rover_caps.has(STEER_THREE_POSITION):
		return "3-POSITION"
	if rover_caps.has(STEER_PROPORTIONAL):
		return "PROPORTIONAL"
	return ""


# --- Housekeeping -----------------------------------------------------------------

func _service_handshake(delta: float) -> void:
	if state == State.LINKED or state == State.INCOMPATIBLE:
		return
	_handshake_timer -= delta
	if _handshake_timer <= 0.0:
		_handshake_timer = HANDSHAKE_RETRY_SEC
		_send({"cmd": "hello", "v": PROTOCOL_VERSION})


## Round-trip probing. The timestamp we send is our own clock, echoed back untouched, so
## the whole measurement happens on this side and the two clocks never need to agree
## (docs/protocol.md 3.6).
func _service_ping(delta: float) -> void:
	if state != State.LINKED:
		return

	_ping_timer -= delta
	if _ping_timer <= 0.0:
		_ping_timer = PING_INTERVAL_SEC
		var ts := Time.get_ticks_msec()
		_outstanding[ts] = ts
		_pings_sent += 1
		_send({"cmd": "ping", "ts": ts})

	_expire_pings()


func _expire_pings() -> void:
	var cutoff := Time.get_ticks_msec() - int(PING_TIMEOUT_SEC * 1000.0)
	for ts in _outstanding.keys():
		if ts < cutoff:
			_outstanding.erase(ts)
			_pongs_lost += 1
			_update_loss()


func _handle_pong(frame: Dictionary) -> void:
	var ts := int(frame.get("ts", -1))
	if not _outstanding.has(ts):
		# Already written off as lost, or not a probe of ours. Counting it now would
		# make the loss figure lie in the flattering direction.
		return
	_outstanding.erase(ts)

	rtt_ms = Time.get_ticks_msec() - ts
	_samples.append(rtt_ms)
	if _samples.size() > RTT_WINDOW:
		_samples.pop_front()

	rtt_p95_ms = _percentile(_samples, 0.95)
	_update_loss()
	rtt_updated.emit(rtt_ms, rtt_p95_ms)


func _percentile(values: Array[int], fraction: float) -> int:
	if values.is_empty():
		return -1
	var sorted := values.duplicate()
	sorted.sort()
	var index := int(ceil(fraction * sorted.size())) - 1
	return sorted[clampi(index, 0, sorted.size() - 1)]


func _update_loss() -> void:
	rtt_loss_pct = 0.0 if _pings_sent == 0 else 100.0 * float(_pongs_lost) / float(_pings_sent)


## Counts a nudge down and ends it. Ending it is release_drive(), so a nudge and a
## finger lifting off a button leave the rover in exactly the same state.
func _service_nudge(delta: float) -> void:
	if _nudge_remaining <= 0.0:
		return
	_nudge_remaining -= delta
	if _nudge_remaining <= 0.0:
		_nudge_remaining = 0.0
		nudge_state_changed.emit(false)
		release_drive()


func _service_drive_repeat(delta: float) -> void:
	if not _holding:
		return
	_repeat_timer -= delta
	if _repeat_timer <= 0.0:
		_repeat_timer = DRIVE_REPEAT_SEC
		_send_drive()


func _service_staleness(delta: float) -> void:
	if state == State.DISCONNECTED or state == State.CONNECTING:
		_set_stale(true)
		return
	_since_telemetry += delta
	if _since_telemetry >= TELEMETRY_STALE_SEC:
		_set_stale(true)


func _reset_session() -> void:
	_holding = false
	_clear_drive_inputs()
	# A nudge in flight when the link drops must not survive the reconnect and resume
	# against a rover that has already failed safe.
	_cancel_nudge()
	_handshake_timer = 0.0
	_since_telemetry = 0.0
	_had_telemetry = false

	# RTT statistics are per-connection: mixing samples from before and after a dropout
	# would average away the very event worth seeing.
	_ping_timer = 0.0
	_outstanding.clear()
	_samples.clear()
	_pings_sent = 0
	_pongs_lost = 0
	rtt_ms = -1
	rtt_p95_ms = -1
	rtt_loss_pct = 0.0

	rover_version = 0
	rover_firmware = ""
	rover_caps = []
	_set_stale(true)


func _set_state(value: State) -> void:
	if state == value:
		return
	state = value
	link_state_changed.emit(state)


func _set_stale(value: bool) -> void:
	if _stale == value:
		return
	_stale = value
	telemetry_stale_changed.emit(_stale)
