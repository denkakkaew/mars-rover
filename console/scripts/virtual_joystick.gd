class_name VirtualJoystick
extends Control
## A game-style thumbstick for the touchscreen: a fixed base circle and a knob that
## follows the finger and springs back to centre on release.
##
## `value` is (x, y) in -1.0 .. 1.0 with **x positive right and y positive up** — up is
## up on screen, not Godot's y-down, so callers never flip a sign. The disc is stretched
## onto the square, so pushing into a corner gives full deflection on both axes rather
## than 0.71 of each: full throttle at full lock has to be reachable.
##
## Touch is tracked by finger index from `_input`, not by the GUI's single mouse focus,
## so two sticks under two fingers do not contend — drive with one thumb while aiming the
## camera with the other. With `emulate_touch_from_mouse` on (project.godot) the mouse
## arrives as touch index 0, so one code path serves the bench and the panel.

## Every change of `value` while held, including back to zero inside the dead zone.
signal moved(value: Vector2)
## The finger lifted, or the stick was disabled or released under it. Always follows a
## press, and always means "stop asking for anything".
signal released

## Fraction of the radius that reads as zero, so a thumb resting on the knob is not a
## command. Outside it the scale restarts from zero rather than jumping to the edge value.
@export_range(0.0, 0.5) var dead_zone := 0.15

## Faint vertical guides at ±guide_x. The drive stick draws them where the rover's
## three-position steering snaps to full lock (±0.5), so the operator can see the
## threshold instead of discovering it.
@export_range(0.0, 1.0) var guide_x := 0.0

## Hint text around the ring: up, right, down, left. Empty entries are skipped.
@export var axis_labels := PackedStringArray()

const COLOR_BASE := Color(0.071, 0.09, 0.114, 1)
const COLOR_RING := Color(0.35, 0.44, 0.53, 1)
const COLOR_GUIDE := Color(0.22, 0.28, 0.34, 1)
const COLOR_KNOB := Color(0.165, 0.216, 0.271, 1)
const COLOR_KNOB_EDGE := Color(0.47, 0.58, 0.69, 1)
const COLOR_ACTIVE := Color(0.145, 0.42, 0.72, 1)
const COLOR_ACTIVE_EDGE := Color(0.53, 0.78, 1, 1)
const COLOR_MIRROR := Color("3ddc84")
const COLOR_TEXT := Color(0.47, 0.58, 0.69, 1)

## Knob radius as a fraction of the base radius.
const KNOB_FRACTION := 0.34
const LABEL_FONT_SIZE := 18

var disabled := false:
	set(v):
		if disabled == v:
			return
		disabled = v
		# Same trap as a disabled Button never emitting button_up: a stick disabled under
		# the finger would otherwise hold its last value until the finger happened to lift.
		if disabled:
			release()
		queue_redraw()

var value := Vector2.ZERO

## Finger index holding the stick, or -1.
var _touch_index := -1
## Knob offset from centre in pixels, already clamped to the ring.
var _knob := Vector2.ZERO
## A value shown on the knob while nobody is touching it — the keyboard's command, so a
## key press gets the same on-screen confirmation a thumb does.
var _mirror := Vector2.ZERO


func _ready() -> void:
	resized.connect(queue_redraw)


func is_active() -> bool:
	return _touch_index != -1


## Lets go programmatically — focus loss, link drop, STOP.
func release() -> void:
	if _touch_index == -1:
		return
	_touch_index = -1
	_knob = Vector2.ZERO
	value = Vector2.ZERO
	queue_redraw()
	released.emit()


## Draws `v` (same axes as `value`) on the knob while the stick is not being touched.
func show_value(v: Vector2) -> void:
	v = v.clamp(Vector2(-1, -1), Vector2(1, 1))
	if v == _mirror:
		return
	_mirror = v
	if not is_active():
		queue_redraw()


func _radius() -> float:
	return maxf(1.0, minf(size.x, size.y) * 0.5 - 8.0)


func _centre() -> Vector2:
	return size * 0.5


func _input(event: InputEvent) -> void:
	if disabled or not is_visible_in_tree():
		return

	if event is InputEventScreenTouch:
		var touch := event as InputEventScreenTouch
		if touch.pressed:
			if _touch_index != -1:
				return
			var local := _to_local(touch.position)
			# A little slack past the ring, so a thumb landing on the edge still catches.
			if local.distance_to(_centre()) > _radius() * 1.1:
				return
			_touch_index = touch.index
			_track(local)
			get_viewport().set_input_as_handled()
		elif touch.index == _touch_index:
			release()
			get_viewport().set_input_as_handled()

	elif event is InputEventScreenDrag:
		var drag := event as InputEventScreenDrag
		if drag.index != _touch_index:
			return
		_track(_to_local(drag.position))
		get_viewport().set_input_as_handled()


func _to_local(viewport_position: Vector2) -> Vector2:
	return get_global_transform_with_canvas().affine_inverse() * viewport_position


func _track(local: Vector2) -> void:
	var radius := _radius()
	_knob = (local - _centre()).limit_length(radius)

	var disc := Vector2(_knob.x, -_knob.y) / radius
	var magnitude := disc.length()
	var next := Vector2.ZERO
	if magnitude > dead_zone:
		disc = disc / magnitude * inverse_lerp(dead_zone, 1.0, magnitude)
		# Radial stretch from the disc onto the square: the direction is unchanged, the
		# dominant axis reaches 1.0 at the ring.
		var dominant := maxf(absf(disc.x), absf(disc.y))
		next = (disc * disc.length() / dominant).clamp(Vector2(-1, -1), Vector2(1, 1))

	queue_redraw()
	if next != value:
		value = next
		moved.emit(value)


func _draw() -> void:
	var centre := _centre()
	var radius := _radius()

	draw_circle(centre, radius, COLOR_BASE)
	draw_arc(centre, radius, 0.0, TAU, 96, COLOR_RING, 3.0, true)
	draw_arc(centre, radius * dead_zone, 0.0, TAU, 48, COLOR_GUIDE, 2.0, true)

	# Crosshair.
	draw_line(centre + Vector2(-radius, 0), centre + Vector2(radius, 0), COLOR_GUIDE, 1.5)
	draw_line(centre + Vector2(0, -radius), centre + Vector2(0, radius), COLOR_GUIDE, 1.5)

	if guide_x > 0.0:
		var gx := _guide_offset(guide_x) * radius
		var half := sqrt(maxf(0.0, radius * radius - gx * gx))
		for side in [-1.0, 1.0]:
			var x: float = centre.x + side * gx
			_draw_dashed(Vector2(x, centre.y - half), Vector2(x, centre.y + half))

	_draw_axis_labels(centre, radius)

	var knob_offset := _knob
	var fill := COLOR_KNOB
	var edge := COLOR_KNOB_EDGE
	if is_active():
		fill = COLOR_ACTIVE
		edge = COLOR_ACTIVE_EDGE
	elif _mirror != Vector2.ZERO:
		knob_offset = _value_to_offset(_mirror) * radius
		fill = COLOR_MIRROR.darkened(0.45)
		edge = COLOR_MIRROR

	var knob_radius := radius * KNOB_FRACTION
	if not disabled:
		draw_line(centre, centre + knob_offset, edge, 2.0, true)
	draw_circle(centre + knob_offset, knob_radius, fill)
	draw_arc(centre + knob_offset, knob_radius, 0.0, TAU, 64, edge, 3.0, true)


## Where on the ring (as a fraction of the radius, screen x) a given output x lands when
## y is zero — the inverse of the dead-zone rescale, so the guide sits exactly where the
## stick crosses the threshold.
func _guide_offset(x: float) -> float:
	return lerpf(dead_zone, 1.0, x)


## Inverse of _track's mapping, for drawing a mirrored value. Screen y is down.
func _value_to_offset(v: Vector2) -> Vector2:
	var dominant := maxf(absf(v.x), absf(v.y))
	if dominant <= 0.0:
		return Vector2.ZERO
	var stretched := v.length()
	var disc := v * dominant / stretched  # back onto the disc
	var magnitude := lerpf(dead_zone, 1.0, disc.length())
	disc = disc.normalized() * magnitude
	return Vector2(disc.x, -disc.y)


func _draw_dashed(from: Vector2, to: Vector2) -> void:
	var length := from.distance_to(to)
	var step := 10.0
	var direction := (to - from) / maxf(length, 0.001)
	var t := 0.0
	while t < length:
		draw_line(from + direction * t, from + direction * minf(t + step * 0.6, length),
				COLOR_GUIDE, 2.0)
		t += step


func _draw_axis_labels(centre: Vector2, radius: float) -> void:
	if axis_labels.is_empty():
		return
	var font := get_theme_default_font()
	var inset := radius * 0.72
	var offsets := [Vector2(0, -inset), Vector2(inset, 0), Vector2(0, inset), Vector2(-inset, 0)]
	for i in mini(axis_labels.size(), 4):
		var text: String = axis_labels[i]
		if text.is_empty():
			continue
		var extent := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, LABEL_FONT_SIZE)
		var at: Vector2 = centre + offsets[i] + Vector2(-extent.x * 0.5, extent.y * 0.3)
		draw_string(font, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, LABEL_FONT_SIZE, COLOR_TEXT)
