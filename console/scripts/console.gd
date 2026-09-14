extends Control
## Operator console root — telemetry strip, subsystem readiness, drive pad
## (plan/storyboard.md 5.1, Scene 1).
##
## The displayed state is deliberately more than the socket state: a console that is
## connected but hearing nothing, or talking to a rover of the wrong protocol version,
## must not look the same as one that is ready to drive. A dead console that still shows
## green is the failure this screen exists to prevent.
##
## Scaffold state: drive pad only. Mast pan/tilt and arm controls are Phase 2/3 work and
## get added alongside the camera feeds.

const DRIVE_SPEED := 1.0

## Steering demand at full press. There is no partial lock to ask for on this chassis
## (docs/protocol.md 3.2.1), so the pad asks for all of it and lets the rover round.
const STEER_DEMAND := 1.0

# --- Touch sizing ---------------------------------------------------------------------
#
# ASSUMPTION, to be revisited when step 0.4 picks the actual panel: a 15.6" 16:9
# touchscreen at 1920x1080. That gives a 13.60" x 7.65" active area and ~141 ppi.
#
# 15 mm is the smallest target an adult fingertip hits reliably without looking — and the
# operator will be looking at the camera monitors, not at the console. At 141 ppi that is
# 15 / 25.4 * 141 = 83 px. Every interactive control is audited against it at startup, so
# a bad layout reports itself instead of being discovered on the bench.
const ASSUMED_DIAGONAL_IN := 15.6
const ASSUMED_ASPECT := Vector2(16.0, 9.0)
const MIN_TOUCH_MM := 15.0

## 3S LiPo. Below LOW the operator should be finishing up; below CRITICAL the pack is
## into the region where cells start being damaged. Both are provisional until the
## divider is calibrated against a meter in step 1.7.
##
## ⚠️ **A 3S pack is no longer viable**: step 1.3 fitted a DRV8833, whose V_M maxes at
## 10.8 V against 3S's 12.6 V charged. docs/power-budget.md §3.3 recommends **2S**, which
## would make these 7.2 and 6.8. Not changed here because the pack is decision 1 at 0.3's
## review gate and has not been taken. When it is, these two move together with
## BATTERY_FULL_V / BATTERY_EMPTY_V in tools/fake_rover.py and BATTERY_DIVIDER_RATIO in
## firmware/include/config.h — four constants and one resistor pair, all or nothing.
const BATTERY_LOW_V := 11.1
const BATTERY_CRITICAL_V := 10.5

## Control-latency target, agreed at step S.7 and the number steps 1.8 and 3.3 are
## measured against. The 95th percentile is what matters, not the average: fine
## alignment in Scene 5 is ruined by the occasional 400 ms sample, not by the mean.
##
## Below the target, driving feels immediate. Between target and ceiling it is workable
## but no longer precise. The ceiling is half the rover's 500 ms command timeout — past
## that, ordinary jitter starts tripping the failsafe mid-drive, so it is a hard limit
## rather than a comfort one.
const RTT_TARGET_MS := 100
const RTT_CEILING_MS := 250

const COLOR_READY := Color("3ddc84")
const COLOR_WARN := Color("ffb300")
const COLOR_FAULT := Color("ff5252")
const COLOR_IDLE := Color("78909c")

## What the operator is shown. Wider than the link's own state because safe mode and
## stale telemetry are rover/link conditions the driver has to be able to tell apart.
enum Display { DISCONNECTED, CONNECTING, LINKED, SAFE_MODE, STALE, INCOMPATIBLE }

@onready var _link: Node = $RoverLink
@onready var _link_label: Label = $Margin/Layout/Telemetry/Strip/LinkLabel
@onready var _mode_label: Label = $Margin/Layout/Telemetry/Strip/ModeLabel
@onready var _battery_label: Label = $Margin/Layout/Telemetry/Strip/BatteryLabel
@onready var _rssi_label: Label = $Margin/Layout/Telemetry/Strip/RssiLabel
@onready var _rtt_label: Label = $Margin/Layout/Telemetry/Strip/RttLabel
@onready var _firmware_label: Label = $Margin/Layout/Readiness/Row/FirmwareLabel
@onready var _drive_panel: PanelContainer = $Margin/Layout/Main/DrivePanel
@onready var _drive_pad: GridContainer = $Margin/Layout/Main/DrivePanel/Column/DrivePad
@onready var _speed_button: Button = $Margin/Layout/Main/DrivePanel/Column/SpeedMode
@onready var _steering_label: Label = $Margin/Layout/Main/DrivePanel/Column/SteeringLabel
@onready var _nudge_row: HBoxContainer = $Margin/Layout/Main/DrivePanel/Column/NudgeRow
@onready var _mast_panel: PanelContainer = $Margin/Layout/Main/MastPanel
@onready var _analysis_panel: PanelContainer = $Margin/Layout/Main/AnalysisPanel

## Scene 1 gates the mission on drive, mast and the RFID reader all reporting green.
@onready var _chips := {
	"drive": $Margin/Layout/Readiness/Row/DriveChip,
	"mast": $Margin/Layout/Readiness/Row/MastChip,
	"rfid": $Margin/Layout/Readiness/Row/RfidChip,
}

## Drive pad button name -> (throttle, steer). Steer is negative-left, as on the wire.
##
## **The two axes are separate, and buttons combine** (step S.15). FORWARD sets throttle
## and nothing else; LEFT sets steering and nothing else. Held together they make a
## turn, which on a steered chassis is the only kind of turn there is — under the old
## skid-steer pad, LEFT alone pivoted the rover on the spot, and that manoeuvre no longer
## exists. Summing held buttons rather than replacing on each press is what lets the
## operator hold FORWARD and tap LEFT, which is the normal way to drive this thing.
const DRIVE_VECTORS := {
	"Forward": Vector2(DRIVE_SPEED, 0.0),
	"Back": Vector2(-DRIVE_SPEED, 0.0),
	"Left": Vector2(0.0, -STEER_DEMAND),
	"Right": Vector2(0.0, STEER_DEMAND),
}

## Nudge button name -> (throttle, steer). The link scales and times it; these are
## directions, not speeds (step S.13).
##
## **The steering nudges carry throttle on purpose.** A steering-only nudge would swing
## the wheels and move the rover nowhere, which is not a nudge at all — so ARC L and
## ARC R are a short forward step taken at full lock. That timed tap is the fine heading
## correction three-position steering otherwise cannot make
## (docs/chassis-envelope.md 8.5).
const NUDGE_VECTORS := {
	"NudgeForward": Vector2(DRIVE_SPEED, 0.0),
	"NudgeBack": Vector2(-DRIVE_SPEED, 0.0),
	"NudgeLeft": Vector2(DRIVE_SPEED, -STEER_DEMAND),
	"NudgeRight": Vector2(DRIVE_SPEED, STEER_DEMAND),
}

## Which pad buttons are currently held, name -> vector. Held rather than derived from
## the buttons themselves because a disabled Button never emits button_up, so the link
## dropping mid-press would otherwise leave a phantom press in the sum forever.
##
## Keys are always plain String, never StringName — touch and keyboard both write here and
## a mix of the two types would risk two entries for one control, which would double a
## throttle and leave a release unmatched.
var _held: Dictionary = {}

## Arrow keys drive the same pad the touchscreen does (step 1.3 bench request). Mapping to
## button names rather than to vectors is deliberate: keyboard and touch then land in the
## same `_held` sum, so holding FORWARD on screen while tapping LEFT on the keyboard
## behaves exactly like holding both on screen.
const KEY_TO_BUTTON := {
	KEY_UP: "Forward",
	KEY_DOWN: "Back",
	KEY_LEFT: "Left",
	KEY_RIGHT: "Right",
}

## A panic key that is bigger than a mouse target and needs no aim. `stop` is honoured by
## the rover in every state, so this is always safe to hit.
const KEY_STOP := KEY_SPACE

var _display: Display = Display.DISCONNECTED
var _rover_mode := ""

## Whether the rover has been armed at least once since this connection opened.
## Before it has, `mode: safe` is simply the resting state and Scene 1 wants to read
## LINK ESTABLISHED. After it has, the same `mode: safe` means the failsafe tripped —
## a different thing, and the one worth showing loudly.
var _has_armed := false

const RttLog := preload("res://scripts/rtt_log.gd")
var _rtt_log := RttLog.new()
var _last_telemetry: Dictionary = {}

## Held rather than written straight to the label: the handshake lands before the first
## telemetry frame, and _refresh() clears the strip while still CONNECTING, so a
## write-once label got wiped and never came back.
var _firmware_text := "FW ---"


func _ready() -> void:
	_link.link_state_changed.connect(_on_link_changed)
	_link.telemetry_received.connect(_on_telemetry_received)
	_link.telemetry_stale_changed.connect(_on_stale_changed)
	_link.handshake_completed.connect(_on_handshake_completed)
	_link.rtt_updated.connect(_on_rtt_updated)
	_link.tag_read.connect(_on_tag_read)
	_rtt_log.open()
	print("Analysis: %s" % _analysis_panel.table_summary())
	_schedule_touch_audit()

	# Debug affordance, like ROVER_URL and RTT_LOG: CONSOLE_SHOT=<path> captures the
	# window to a PNG and exits, so the layout can be reviewed without a person sitting
	# at the screen. Needs a real window — there is nothing to capture headless.
	var shot := OS.get_environment("CONSOLE_SHOT")
	if shot != "":
		_capture_and_quit(shot)

	for button in _drive_pad.get_children():
		if not (button is Button):
			continue
		if DRIVE_VECTORS.has(button.name):
			# Hold-to-drive: the rover stops as soon as the operator lifts off. The
			# link repeats the command while held, because the rover cuts the motors
			# after 500 ms of silence (docs/protocol.md 6.5).
			var vector: Vector2 = DRIVE_VECTORS[button.name]
			button.button_down.connect(_on_drive_pressed.bind(button.name, vector))
			button.button_up.connect(_on_drive_released.bind(button.name))
		elif button.name == "Stop":
			button.pressed.connect(_link.send_stop)

	# Fine drive (step S.13). Nudges are `pressed`, not hold-to-drive: the whole point
	# is a step whose length the link decides, not one the operator has to time.
	for button in _nudge_row.get_children():
		if button is Button and NUDGE_VECTORS.has(button.name):
			button.pressed.connect(_on_nudge_pressed.bind(NUDGE_VECTORS[button.name]))
	_speed_button.pressed.connect(_on_speed_toggled)
	_link.speed_mode_changed.connect(_on_speed_mode_changed)
	_link.nudge_state_changed.connect(_on_nudge_state_changed)
	_refresh_speed_button()
	_refresh_steering_label()

	_refresh()


func _on_drive_pressed(name: StringName, vector: Vector2) -> void:
	_held[String(name)] = vector
	_apply_pad()


func _on_drive_released(name: StringName) -> void:
	_held.erase(String(name))
	_apply_pad()


## Keyboard driving. Handled in _input rather than _unhandled_key_input, and marked
## handled, because Godot's default UI navigation also claims the arrow keys — without
## that the first press would move focus between pad buttons instead of driving.
##
## `echo` events are ignored: the OS auto-repeats a held key, and treating each repeat as
## a fresh press would re-arm the pad dozens of times a second. The physical hold is
## already covered — `rover_link.gd` owns the 150 ms command repeat.
func _input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or key.echo:
		return

	if key.keycode == KEY_STOP:
		if key.pressed:
			_held.clear()
			_link.send_stop()
			_refresh_steering_label()
		get_viewport().set_input_as_handled()
		return

	if not KEY_TO_BUTTON.has(key.keycode):
		return
	get_viewport().set_input_as_handled()

	# Gated on the same condition as the on-screen pad. Without this a keypress while the
	# link is down would leave an entry in `_held` that springs back to life on reconnect.
	if not _pad_enabled():
		return

	var button_name: String = KEY_TO_BUTTON[key.keycode]
	if key.pressed:
		_held[button_name] = DRIVE_VECTORS[button_name]
	else:
		_held.erase(button_name)
	_apply_pad()


## Sums the held buttons into one throttle and one steering demand, and sends that.
## Opposed presses cancel — FORWARD with BACK is a stop, LEFT with RIGHT is straight —
## which is the same answer the rover would reach anyway and avoids the pad arguing with
## itself about which finger won.
func _apply_pad() -> void:
	if _held.is_empty():
		_link.release_drive()
		_refresh_steering_label()
		return

	var throttle := 0.0
	var steer := 0.0
	for vector: Vector2 in _held.values():
		throttle += vector.x
		steer += vector.y
	_link.hold_drive(clampf(throttle, -1.0, 1.0), clampf(steer, -1.0, 1.0))
	_refresh_steering_label()
	_highlight_held()


## Lights the pad buttons that are currently held. On-screen presses show their own state,
## but a keyboard press otherwise gives no on-screen confirmation at all — and an operator
## who cannot tell whether the key registered will press it again.
func _highlight_held() -> void:
	for button in _drive_pad.get_children():
		if button is Button and DRIVE_VECTORS.has(button.name):
			button.modulate = COLOR_READY if _held.has(String(button.name)) else Color.WHITE


func _on_nudge_pressed(vector: Vector2) -> void:
	_link.nudge(vector.x, vector.y)


func _on_speed_toggled() -> void:
	_link.toggle_speed_mode()


func _on_speed_mode_changed(_mode: int) -> void:
	_refresh_speed_button()


## A running nudge is shown on the pad, not just implied by the rover moving. Without it
## the operator has no way to tell "the step is still going" from "nothing happened",
## and taps again — which is how you drive into a rock.
func _on_nudge_state_changed(running: bool) -> void:
	_nudge_row.modulate = COLOR_WARN if running else Color.WHITE
	_refresh_steering_label()


## The pad's honesty about what steering actually does here (docs/protocol.md 6.4).
##
## Three-position steering with no throttle behind it moves the wheels and not the rover.
## An operator who presses LEFT expecting the old skid-steer pivot, sees nothing happen,
## and presses harder is the exact failure this label exists to prevent — so that state
## gets a loud amber line rather than silence.
func _refresh_steering_label() -> void:
	var kind: String = _link.steering_kind()
	if kind.is_empty():
		_steering_label.text = "STEERING  ·  ---"
		_steering_label.modulate = COLOR_IDLE
		return

	if _link.is_steering_without_throttle():
		_steering_label.text = "WHEELS TURNED  ·  NO THROTTLE — ROVER WILL NOT MOVE"
		_steering_label.modulate = COLOR_WARN
		return

	_steering_label.text = "STEERING  ·  %s  ·  no turn on the spot" % kind
	_steering_label.modulate = COLOR_IDLE


## Which mode the pad is in has to be readable at a glance. An operator who believes
## they are in PRECISION and is not will put the rover into the glass.
func _refresh_speed_button() -> void:
	var precision: bool = _link.speed_mode == _link.Speed.PRECISION
	_speed_button.text = "SPEED  ·  %s" % _link.speed_mode_name()
	_speed_button.button_pressed = precision
	_speed_button.modulate = COLOR_WARN if precision else COLOR_READY


func _on_link_changed(_new_state: int) -> void:
	_refresh()


func _on_stale_changed(_stale: bool) -> void:
	_refresh()


func _on_handshake_completed(rover_version: int, firmware: String, caps: Array) -> void:
	_firmware_text = "FW %s  v%d  [%s]" % [firmware, rover_version, ", ".join(caps)]
	# The steering kind arrives with the handshake, so the pad cannot label itself until
	# now — before this it does not know which rover it is talking to.
	_refresh_steering_label()
	_refresh()


func _on_rtt_updated(rtt: int, p95: int) -> void:
	var loss: float = _link.rtt_loss_pct
	_rtt_label.text = "RTT %d ms  p95 %d" % [rtt, p95]
	if loss >= 1.0:
		_rtt_label.text += "  loss %.0f%%" % loss

	# Coloured on the percentile, not the latest sample, so the strip reports the link's
	# behaviour rather than flickering on one unlucky frame.
	if p95 > RTT_CEILING_MS:
		_rtt_label.modulate = COLOR_FAULT
	elif p95 > RTT_TARGET_MS:
		_rtt_label.modulate = COLOR_WARN
	else:
		_rtt_label.modulate = COLOR_READY

	_rtt_log.append(rtt, p95, loss, _last_telemetry)


func _on_tag_read(tag_id: String, rssi: int, rover_ts_ms: int) -> void:
	_analysis_panel.on_tag_read(tag_id, rssi, rover_ts_ms)


func _exit_tree() -> void:
	_rtt_log.close()


## Losing the window mid-press means the release will never arrive, so the repeat would
## keep driving a rover the operator is no longer looking at. Alt-tab must stop it.
func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT or what == NOTIFICATION_WM_CLOSE_REQUEST:
		if is_instance_valid(_link):
			_held.clear()
			_link.release_drive()


func _capture_and_quit(path: String) -> void:
	# Long enough for the link to come up and telemetry to populate the strip, so the
	# capture shows a working console rather than its disconnected state. Override with
	# CONSOLE_SHOT_DELAY when the interesting state takes longer to reach — a tag read,
	# say, which needs the rover driven across the arena first.
	var delay := 4.0
	var override := OS.get_environment("CONSOLE_SHOT_DELAY")
	if override != "":
		delay = maxf(0.5, float(override))
	await get_tree().create_timer(delay).timeout
	await RenderingServer.frame_post_draw
	var image := get_viewport().get_texture().get_image()
	var err := image.save_png(path)
	print("Screenshot: %s (%d)" % [path, err])
	get_tree().quit()


# --- Touch-target audit ---------------------------------------------------------------

## Nested containers re-sort over several frames, and measuring before they have settled
## reports an intermediate layout — which is worse than not measuring, because the numbers
## look authoritative.
func _schedule_touch_audit() -> void:
	await get_tree().create_timer(0.5).timeout
	_audit_touch_targets()


## Pixels per millimetre for the assumed panel, derived rather than hard-coded so that
## changing the display in step 0.4 changes one constant.
func _pixels_per_mm() -> float:
	var px := Vector2(
		ProjectSettings.get_setting("display/window/size/viewport_width"),
		ProjectSettings.get_setting("display/window/size/viewport_height"))
	var diagonal_px: float = px.length()
	var diagonal_in: float = ASSUMED_DIAGONAL_IN
	return diagonal_px / (diagonal_in * 25.4)


## Reports every interactive control in millimetres and complains about anything under
## MIN_TOUCH_MM. Runs on every startup, headless included, so the check is part of the
## smoke run rather than something to remember.
func _audit_touch_targets() -> void:
	var ppmm := _pixels_per_mm()
	var minimum := MIN_TOUCH_MM * ppmm
	var undersized := 0
	var lines: Array[String] = []

	for button in _interactive_controls():
		var size := button.size
		var ok := size.x >= minimum and size.y >= minimum
		if not ok:
			undersized += 1
		lines.append("    %-10s %4d x %4d px   %5.1f x %5.1f mm   %s" % [
			button.name, size.x, size.y, size.x / ppmm, size.y / ppmm,
			"ok" if ok else "UNDERSIZED"])

	var viewport := get_viewport_rect().size
	print("Touch audit — assuming %.1f\" %dx%d panel, %.2f px/mm, %.0f px minimum"
			% [ASSUMED_DIAGONAL_IN,
			ProjectSettings.get_setting("display/window/size/viewport_width"),
			ProjectSettings.get_setting("display/window/size/viewport_height"),
			ppmm, minimum])
	print("    viewport actually %d x %d" % [viewport.x, viewport.y])
	if absf(viewport.y - float(
			ProjectSettings.get_setting("display/window/size/viewport_height"))) > 1.0:
		push_warning("Touch audit: viewport is not the assumed panel size; "
				+ "the millimetre figures below are not trustworthy")
	for line in lines:
		print(line)

	if undersized > 0:
		push_warning("Touch audit: %d control(s) below %.0f mm" % [undersized, MIN_TOUCH_MM])
	else:
		print("    all %d controls clear %.0f mm" % [lines.size(), MIN_TOUCH_MM])


func _interactive_controls() -> Array[Button]:
	var found: Array[Button] = []
	for root in [_drive_panel, _mast_panel, _analysis_panel]:
		_collect_buttons(root, found)
	return found


func _collect_buttons(node: Node, into: Array[Button]) -> void:
	for child in node.get_children():
		if child is Button:
			into.append(child)
		elif child is Node:
			_collect_buttons(child, into)


func _on_telemetry_received(data: Dictionary) -> void:
	_last_telemetry = data
	_rover_mode = str(data.get("mode", ""))
	if _rover_mode == "drive":
		_has_armed = true

	if data.has("battery_v"):
		var volts := float(data["battery_v"])
		_battery_label.text = "BATT %.2f V" % volts
		if volts <= BATTERY_CRITICAL_V:
			_battery_label.modulate = COLOR_FAULT
		elif volts <= BATTERY_LOW_V:
			_battery_label.modulate = COLOR_WARN
		else:
			_battery_label.modulate = COLOR_READY

	if data.has("rssi"):
		var rssi := int(data["rssi"])
		_rssi_label.text = "RSSI %d dBm" % rssi
		# Below about -75 dBm a 2.4 GHz link starts dropping frames — the number risk
		# R6 is judged on once the rover is inside the closed box (step 1.2).
		_rssi_label.modulate = COLOR_WARN if rssi <= -75 else COLOR_READY

	_refresh()


# --- Display state ------------------------------------------------------------------

func _resolve_display() -> Display:
	if _link.is_disconnected():
		return Display.DISCONNECTED
	if _link.is_connecting():
		return Display.CONNECTING
	if _link.is_incompatible():
		return Display.INCOMPATIBLE

	# Handshake agreed, but the first telemetry frame has not landed yet. Still
	# connecting — calling it stale would flash a warning on every connect.
	if not _link.has_telemetry():
		return Display.CONNECTING

	# A live socket with no telemetry behind it is not a working link.
	if _link.is_telemetry_stale():
		return Display.STALE
	if _rover_mode == "incompatible":
		return Display.INCOMPATIBLE
	if _rover_mode == "safe" and _has_armed:
		return Display.SAFE_MODE
	return Display.LINKED


func _refresh() -> void:
	var previous := _display
	_display = _resolve_display()
	if _display != previous:
		# Also the only way to observe the state machine in a headless run.
		print("Console: %s" % Display.keys()[_display])

	var text := ""
	var colour := COLOR_FAULT
	match _display:
		Display.DISCONNECTED:
			text = "LINK DOWN"
		Display.CONNECTING:
			text = "CONNECTING..."
			colour = COLOR_WARN
		Display.LINKED:
			text = "LINK ESTABLISHED"
			colour = COLOR_READY
		Display.SAFE_MODE:
			text = "SAFE MODE - FAILSAFE TRIPPED"
			colour = COLOR_WARN
		Display.STALE:
			text = "TELEMETRY STALE"
			colour = COLOR_WARN
		Display.INCOMPATIBLE:
			text = "PROTOCOL MISMATCH (rover v%d, console v%d)" % [
				_link.rover_version, _link.PROTOCOL_VERSION]

	_link_label.text = text
	_link_label.modulate = colour

	_mode_label.text = "MODE %s" % (_rover_mode.to_upper() if _rover_mode != "" else "---")
	if _rover_mode == "drive":
		_mode_label.modulate = COLOR_READY
	elif _rover_mode == "incompatible":
		_mode_label.modulate = COLOR_FAULT
	elif _rover_mode == "safe":
		# Amber only if the failsafe took it out of drive. At rest, before the first
		# command, safe mode is simply where a healthy rover sits — and Scene 1 wants
		# this screen to read as ready, not as a warning.
		_mode_label.modulate = COLOR_WARN if _has_armed else COLOR_IDLE
	else:
		_mode_label.modulate = COLOR_IDLE

	if _display == Display.DISCONNECTED or _display == Display.CONNECTING:
		_battery_label.text = "BATT --.-- V"
		_battery_label.modulate = COLOR_IDLE
		_rssi_label.text = "RSSI --- dBm"
		_rssi_label.modulate = COLOR_IDLE
		_rtt_label.text = "RTT --- ms"
		_rtt_label.modulate = COLOR_IDLE
		_last_telemetry = {}
		_rover_mode = ""
		_has_armed = false
	if _display == Display.DISCONNECTED:
		# Only on a real drop. Clearing it while CONNECTING would wipe the handshake we
		# just completed, since the first telemetry frame has not arrived yet.
		_firmware_text = "FW ---"

	_firmware_label.text = _firmware_text

	_refresh_chips()
	_refresh_drive_pad()


## Scene 1: drive, arm and mast all report green before the operator may proceed.
## A subsystem the rover did not advertise in `caps` is shown as NOT FITTED rather than
## as a fault — during Phases S and 1 only the drive exists, and a permanently red row
## would train the operator to ignore it.
func _refresh_chips() -> void:
	var live := _display == Display.LINKED or _display == Display.SAFE_MODE
	for subsystem in _chips:
		var chip: Label = _chips[subsystem]
		if not _link.has_capability(subsystem):
			chip.text = "%s NOT FITTED" % subsystem.to_upper()
			chip.modulate = COLOR_IDLE
		elif live:
			chip.text = "%s READY" % subsystem.to_upper()
			chip.modulate = COLOR_READY
		else:
			chip.text = "%s NO DATA" % subsystem.to_upper()
			chip.modulate = COLOR_FAULT


## The drive pad is live only when the link genuinely is. Safe mode still allows driving
## — a fresh command is precisely what re-arms the rover (docs/protocol.md 4.2) — but a
## stale, disconnected, or mismatched link must not leave usable-looking buttons.
## Whether driving is allowed at all. Safe mode counts as enabled — a fresh command is
## precisely what re-arms the rover (docs/protocol.md 4.2). Keyboard input is gated on the
## same predicate as the buttons, so there is one rule rather than two that can drift.
func _pad_enabled() -> bool:
	return _display == Display.LINKED or _display == Display.SAFE_MODE


func _refresh_drive_pad() -> void:
	var enabled := _pad_enabled()

	# The whole panel, so the S.13 speed toggle and nudge row go dead with the pad
	# rather than staying tappable against a rover that is not listening.
	var buttons: Array[Button] = []
	_collect_buttons(_drive_panel, buttons)
	for button in buttons:
		button.disabled = not enabled
	_drive_panel.modulate = Color.WHITE if enabled else Color(1, 1, 1, 0.35)

	if not enabled:
		# A disabled Button never emits button_up, so a link that drops mid-press would
		# otherwise leave the repeat running against a rover that is already gone — and
		# leave a phantom press in `_held` that reappears the moment the link recovers.
		# The release also cancels a nudge in flight.
		_held.clear()
		_link.release_drive()
	_refresh_steering_label()
	_highlight_held()
