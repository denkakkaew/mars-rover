extends Node
## Control-channel client: console -> ESP32 over a WebSocket (plan/storyboard.md 5.4).
##
## This is the control channel only. Camera video never travels over this socket —
## each IP camera streams straight to its own monitor, so a busy video feed can
## never make the drive controls laggy (risk R1).

signal link_state_changed(connected: bool)
signal telemetry_received(data: Dictionary)

const RECONNECT_INTERVAL_SEC := 2.0

@export var rover_url := "ws://192.168.4.1:81/"
@export var auto_connect := true

var _socket := WebSocketPeer.new()
var _connected := false
var _reconnect_timer := 0.0


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
	var err := _socket.connect_to_url(rover_url)
	if err != OK:
		push_warning("Rover link: connect_to_url(%s) failed with %d" % [rover_url, err])


func is_connected_to_rover() -> bool:
	return _connected


## Drive pad — forward / backward / left / right, as differential (skid) steering.
## `left` and `right` are wheel-side throttles in the range -1.0 .. 1.0.
func send_drive(left: float, right: float) -> void:
	_send({
		"cmd": "drive",
		"l": snappedf(clampf(left, -1.0, 1.0), 0.01),
		"r": snappedf(clampf(right, -1.0, 1.0), 0.01),
	})


## Mast camera pan/tilt head, in degrees.
func send_mast(pan_deg: float, tilt_deg: float) -> void:
	_send({"cmd": "mast", "pan": pan_deg, "tilt": tilt_deg})


## Arm joints + gripper. `joints` is one angle per servo, base outward.
func send_arm(joints: Array, gripper_closed: bool) -> void:
	_send({"cmd": "arm", "joints": joints, "grip": gripper_closed})


func _send(payload: Dictionary) -> void:
	if not _connected:
		return
	_socket.send_text(JSON.stringify(payload))


func _process(delta: float) -> void:
	_socket.poll()

	match _socket.get_ready_state():
		WebSocketPeer.STATE_OPEN:
			_set_connected(true)
			while _socket.get_available_packet_count() > 0:
				_handle_packet(_socket.get_packet().get_string_from_utf8())
		WebSocketPeer.STATE_CLOSED:
			_set_connected(false)
			_reconnect_timer -= delta
			if _reconnect_timer <= 0.0:
				_reconnect_timer = RECONNECT_INTERVAL_SEC
				connect_to_rover()
		_:
			pass


func _handle_packet(text: String) -> void:
	var parsed: Variant = JSON.parse_string(text)
	if parsed is Dictionary:
		telemetry_received.emit(parsed)
	else:
		push_warning("Rover link: unparseable telemetry: %s" % text)


func _set_connected(value: bool) -> void:
	if _connected == value:
		return
	_connected = value
	link_state_changed.emit(_connected)
