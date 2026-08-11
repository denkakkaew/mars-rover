extends Control
## Operator console root — telemetry strip + drive pad (plan/storyboard.md 5.1).
##
## Scaffold state: drive pad only. Mast pan/tilt and arm controls are Phase 2/3
## work and get added alongside the camera feeds.

const DRIVE_SPEED := 1.0
const TURN_SPEED := 0.6

@onready var _link: Node = $RoverLink
@onready var _link_label: Label = $Margin/Layout/Telemetry/Strip/LinkLabel
@onready var _battery_label: Label = $Margin/Layout/Telemetry/Strip/BatteryLabel
@onready var _mode_label: Label = $Margin/Layout/Telemetry/Strip/ModeLabel

## Drive pad button name -> (left wheel throttle, right wheel throttle).
const DRIVE_VECTORS := {
	"Forward": Vector2(DRIVE_SPEED, DRIVE_SPEED),
	"Back": Vector2(-DRIVE_SPEED, -DRIVE_SPEED),
	"Left": Vector2(-TURN_SPEED, TURN_SPEED),
	"Right": Vector2(TURN_SPEED, -TURN_SPEED),
	"Stop": Vector2.ZERO,
}


func _ready() -> void:
	_link.link_state_changed.connect(_on_link_state_changed)
	_link.telemetry_received.connect(_on_telemetry_received)
	_on_link_state_changed(false)

	for button in $Margin/Layout/DrivePad.get_children():
		if button is Button and DRIVE_VECTORS.has(button.name):
			var vector: Vector2 = DRIVE_VECTORS[button.name]
			# Hold-to-drive: the rover stops as soon as the operator lifts off.
			button.button_down.connect(_drive.bind(vector))
			button.button_up.connect(_drive.bind(Vector2.ZERO))


func _drive(vector: Vector2) -> void:
	_link.send_drive(vector.x, vector.y)


func _on_link_state_changed(connected: bool) -> void:
	_link_label.text = "LINK ESTABLISHED" if connected else "LINK DOWN"
	_link_label.modulate = Color.GREEN if connected else Color.ORANGE_RED


func _on_telemetry_received(data: Dictionary) -> void:
	if data.has("battery_v"):
		_battery_label.text = "BATT %.2f V" % float(data["battery_v"])
	if data.has("mode"):
		_mode_label.text = "MODE %s" % str(data["mode"]).to_upper()
