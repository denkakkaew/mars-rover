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

enum State {
	DISCONNECTED,  ## No socket.
	CONNECTING,    ## Socket opening, or the handshake sent and still unanswered.
	LINKED,        ## Versions agreed. Commands may be sent.
	INCOMPATIBLE,  ## Rover speaks a different protocol version; it will not arm.
}

## Bumped only for a breaking change (docs/protocol.md 5).
const PROTOCOL_VERSION := 1

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

@export var rover_url := "ws://192.168.4.1:81/"
@export var auto_connect := true

var state: State = State.DISCONNECTED
var rover_version := 0
var rover_firmware := ""
var rover_caps: Array = []

var _socket := WebSocketPeer.new()
var _reconnect_timer := 0.0
var _handshake_timer := 0.0
var _repeat_timer := 0.0
var _since_telemetry := 0.0
var _stale := true
var _had_telemetry := false

var _holding := false
var _hold_left := 0.0
var _hold_right := 0.0


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
## release_drive() — see DRIVE_REPEAT_SEC. `left` and `right` are wheel-side throttles
## in the range -1.0 .. 1.0; differential (skid) steering, so the difference is the turn.
func hold_drive(left: float, right: float) -> void:
	_holding = true
	_hold_left = clampf(left, -1.0, 1.0)
	_hold_right = clampf(right, -1.0, 1.0)
	_repeat_timer = DRIVE_REPEAT_SEC
	_send_drive()


## Ends a held drive command: one explicit zero-throttle frame, then silence. Zero
## throttle rather than `stop` because the rover stays armed with a commanded speed of
## zero, which is what a finger lifting off means (docs/protocol.md 3.3).
func release_drive() -> void:
	if not _holding:
		return
	_holding = false
	_hold_left = 0.0
	_hold_right = 0.0
	_send_drive()


## Deliberate halt. Honoured by the rover in every state, including safe mode and on a
## version mismatch, so this is the one command always worth sending.
func send_stop() -> void:
	_holding = false
	_hold_left = 0.0
	_hold_right = 0.0
	_send({"cmd": "stop"})


## Mast camera pan/tilt head, in degrees. Absolute angles, not increments.
func send_mast(pan_deg: float, tilt_deg: float) -> void:
	_send({"cmd": "mast", "pan": pan_deg, "tilt": tilt_deg})


## Arm joints + gripper. `joints` is one angle per servo, base outward.
func send_arm(joints: Array, gripper_closed: bool) -> void:
	_send({"cmd": "arm", "joints": joints, "grip": gripper_closed})


func _send_drive() -> void:
	_send({
		"cmd": "drive",
		"l": snappedf(_hold_left, 0.01),
		"r": snappedf(_hold_right, 0.01),
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
			_service_drive_repeat(delta)

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
			pass  # Round-trip measurement arrives in step S.7.
		_:
			push_warning("Rover link: unrecognised frame type: %s" % text)


func _handle_hello(frame: Dictionary) -> void:
	rover_version = int(frame.get("v", 0))
	rover_firmware = str(frame.get("fw", "?"))
	rover_caps = frame.get("caps", [])

	if rover_version == PROTOCOL_VERSION:
		_set_state(State.LINKED)
	else:
		push_warning("Rover link: rover speaks v%d, console speaks v%d"
				% [rover_version, PROTOCOL_VERSION])
		_set_state(State.INCOMPATIBLE)

	handshake_completed.emit(rover_version, rover_firmware, rover_caps)


# --- Housekeeping -----------------------------------------------------------------

func _service_handshake(delta: float) -> void:
	if state == State.LINKED or state == State.INCOMPATIBLE:
		return
	_handshake_timer -= delta
	if _handshake_timer <= 0.0:
		_handshake_timer = HANDSHAKE_RETRY_SEC
		_send({"cmd": "hello", "v": PROTOCOL_VERSION})


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
	_hold_left = 0.0
	_hold_right = 0.0
	_handshake_timer = 0.0
	_since_telemetry = 0.0
	_had_telemetry = false
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
