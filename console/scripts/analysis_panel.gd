extends PanelContainer
## Scenes 5 through 8: the signal meter, the composition readout, and the session log.
##
## Fed one `tag_read` at a time by the console. Everything here is about turning a stream
## of individually unreliable reads into something an operator can act on — which is the
## whole of risk R2 as far as the UI is concerned.

const CompositionTable := preload("res://scripts/composition_table.gd")

## Signal meter scale. Below the floor the bar sits empty; the ceiling is about what a
## reader sees with the antenna almost touching a tag. Fixed rather than derived from the
## rover, because the console is not told the reader's sensitivity — and a bar that
## rescaled itself would destroy the "getting warmer" cue it exists to provide.
const SIGNAL_FLOOR_DBM := -90.0
const SIGNAL_CEILING_DBM := -40.0

## Scene 6 wants "a stable read with a consistent tag ID, not an intermittent or dropped
## signal". So a single read is not a detection: it takes this many reads of the *same*
## tag inside the window. At the edge of range the simulator produces exactly the ragged
## stream this is here to reject.
const CONFIRM_READS := 3
const CONFIRM_WINDOW_SEC := 1.5

## No read of the locked tag for this long and the meter is no longer telling the truth.
const SIGNAL_LOST_SEC := 1.2

## How long the meter takes to fall back to the floor once reads stop. Falling rather
## than freezing matters: a held-up bar would read as "still on target" while the
## operator drives away.
const DECAY_SEC := 0.8

const COLOR_READY := Color("3ddc84")
const COLOR_WARN := Color("ffb300")
const COLOR_FAULT := Color("ff5252")
const COLOR_IDLE := Color("78909c")

const LOG_ROWS_SHOWN := 5

@onready var _status: Label = $Column/Signal/Status
@onready var _bar: ProgressBar = $Column/Signal/Bar
@onready var _signal_value: Label = $Column/Signal/Value
@onready var _rock_name: Label = $Column/Report/RockName
@onready var _rock_class: Label = $Column/Report/RockClass
@onready var _elements: VBoxContainer = $Column/Report/Elements
@onready var _log_list: VBoxContainer = $Column/Log/List
@onready var _log_heading: Label = $Column/Log/Heading

var _table := CompositionTable.new()

var _display_rssi := SIGNAL_FLOOR_DBM
var _last_read_t := -999.0
var _last_tag := ""

## Recent read times for the tag currently being acquired, for the CONFIRM_READS rule.
var _acquiring := ""
var _acquire_times: Array[float] = []

var _locked := ""
## tag_id -> {name, class, first_seen, count, best_rssi}. Insertion order is the log order.
var _session: Dictionary = {}

## Own copy of the fill style, so the bar's colour can carry strength as well as its
## length. Colour is doing real work here: the operator is watching the camera monitor,
## not this panel, and a green bar in peripheral vision means "stop and read".
var _fill: StyleBoxFlat


func _ready() -> void:
	_table.load_default()

	var base := _bar.get_theme_stylebox("fill")
	_fill = (base.duplicate() if base is StyleBoxFlat else StyleBoxFlat.new())
	_bar.add_theme_stylebox_override("fill", _fill)

	_clear_report()
	_render_log()
	set_process(true)


## Called by the console for every `tag` frame. `rssi` is the reader's, not the Wi-Fi link's.
func on_tag_read(tag_id: String, rssi: int, _rover_ts_ms: int) -> void:
	var now := _now()
	_last_read_t = now
	_last_tag = tag_id

	# The meter follows the strongest thing being heard; with two tags in range the
	# nearer one wins, which is the answer the operator wants while closing in.
	_display_rssi = maxf(_display_rssi, float(rssi))

	if tag_id != _acquiring:
		_acquiring = tag_id
		_acquire_times.clear()

	_acquire_times.append(now)
	while not _acquire_times.is_empty() and now - _acquire_times[0] > CONFIRM_WINDOW_SEC:
		_acquire_times.pop_front()

	if _acquire_times.size() >= CONFIRM_READS and _locked != tag_id:
		_confirm(tag_id, rssi, now)
	elif _locked == tag_id:
		_note_repeat(tag_id, rssi)


func _process(delta: float) -> void:
	var now := _now()
	var since := now - _last_read_t

	# Decay once reads stop. Held rather than snapped to zero for a moment first, so a
	# single dropped read in a good stream does not make the bar flicker.
	if since > 0.35:
		var fall := (SIGNAL_CEILING_DBM - SIGNAL_FLOOR_DBM) * delta / DECAY_SEC
		_display_rssi = maxf(SIGNAL_FLOOR_DBM, _display_rssi - fall)

	var fraction := (_display_rssi - SIGNAL_FLOOR_DBM) / (SIGNAL_CEILING_DBM - SIGNAL_FLOOR_DBM)
	_bar.value = clampf(fraction, 0.0, 1.0) * 100.0

	if since <= SIGNAL_LOST_SEC:
		var strong := _bar.value > 55.0
		_signal_value.text = "%d dBm" % int(round(_display_rssi))
		_signal_value.modulate = COLOR_READY if strong else COLOR_WARN
		_fill.bg_color = COLOR_READY if strong else COLOR_WARN
	else:
		_signal_value.text = "--- dBm"
		_signal_value.modulate = COLOR_IDLE
		_fill.bg_color = COLOR_IDLE

	_update_status(since)


func _update_status(since: float) -> void:
	if since <= SIGNAL_LOST_SEC:
		if _locked != "":
			_status.text = "TAG DETECTED  %s" % _short(_locked)
			_status.modulate = COLOR_READY
		else:
			_status.text = "SIGNAL  acquiring %s" % _short(_last_tag)
			_status.modulate = COLOR_WARN
	elif _locked != "":
		# The science result stays on screen; only the live meter goes quiet. Clearing
		# the report the instant the rover rolls away would throw away Scene 7.
		_status.text = "SIGNAL LOST  ·  last result held"
		_status.modulate = COLOR_WARN
	else:
		_status.text = "NO TAG IN RANGE"
		_status.modulate = COLOR_IDLE


# --- Detection ------------------------------------------------------------------------

func _confirm(tag_id: String, rssi: int, now: float) -> void:
	_locked = tag_id
	var entry: Variant = _table.lookup(tag_id)

	if entry is Dictionary:
		_show_report(entry as Dictionary, tag_id)
		_log(tag_id, str((entry as Dictionary).get("name", "?")), rssi, now)
	else:
		# A rock whose ID was never entered in the table. Reported plainly rather than
		# hidden — it means the arena register and the console have drifted apart, and
		# the operator is the only one who can fix that.
		_show_unknown(tag_id)
		_log(tag_id, "", rssi, now)

	print("Analysis: confirmed %s after %d reads" % [tag_id, _acquire_times.size()])


func _note_repeat(tag_id: String, rssi: int) -> void:
	var record: Variant = _session.get(tag_id)
	if record is Dictionary:
		(record as Dictionary)["best_rssi"] = maxi(int((record as Dictionary)["best_rssi"]), rssi)


# --- Report card ----------------------------------------------------------------------

func _clear_report() -> void:
	_rock_name.text = "—"
	_rock_name.modulate = COLOR_IDLE
	_rock_class.text = "awaiting a tag read"
	_rock_class.modulate = COLOR_IDLE
	for child in _elements.get_children():
		child.queue_free()


func _show_report(entry: Dictionary, tag_id: String) -> void:
	_clear_report()
	_rock_name.text = str(entry.get("name", "Unnamed sample"))
	_rock_name.modulate = COLOR_READY
	_rock_class.text = "%s  ·  %s" % [str(entry.get("class", "unclassified")), _short(tag_id)]
	_rock_class.modulate = COLOR_IDLE

	for element in CompositionTable.ranked_elements(entry):
		var row := HBoxContainer.new()
		var symbol := Label.new()
		symbol.text = str(element["symbol"])
		symbol.custom_minimum_size.x = 56
		var percent := Label.new()
		percent.text = "%5.1f %%" % float(element["percent"])
		percent.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		percent.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		row.add_child(symbol)
		row.add_child(percent)
		_elements.add_child(row)


func _show_unknown(tag_id: String) -> void:
	_clear_report()
	_rock_name.text = "UNKNOWN TAG"
	_rock_name.modulate = COLOR_FAULT
	_rock_class.text = "%s is not in the composition table" % _short(tag_id)
	_rock_class.modulate = COLOR_WARN


# --- Session log ----------------------------------------------------------------------

func _log(tag_id: String, name: String, rssi: int, now: float) -> void:
	if _session.has(tag_id):
		# Re-reading a rock is normal — the operator may pass it again on the way back.
		# It is the same finding, so the entry is updated rather than duplicated.
		var record: Dictionary = _session[tag_id]
		record["count"] = int(record["count"]) + 1
		record["best_rssi"] = maxi(int(record["best_rssi"]), rssi)
	else:
		_session[tag_id] = {
			"name": name,
			"count": 1,
			"best_rssi": rssi,
			"at": Time.get_time_string_from_system(),
			"seq": _session.size() + 1,
		}
	_render_log()


func _render_log() -> void:
	for child in _log_list.get_children():
		child.queue_free()

	_log_heading.text = "SESSION LOG  ·  %d identified" % _session.size()

	var ids := _session.keys()
	ids.reverse()  # most recent first
	for tag_id in ids.slice(0, LOG_ROWS_SHOWN):
		var record: Dictionary = _session[tag_id]
		var label := Label.new()
		var name: String = str(record["name"])
		if name == "":
			name = "unknown tag"
		var repeats := ""
		if int(record["count"]) > 1:
			repeats = "  x%d" % int(record["count"])
		label.text = "%d. %s  %s%s" % [int(record["seq"]), str(record["at"]), name, repeats]
		label.modulate = COLOR_IDLE if name == "unknown tag" else COLOR_READY
		_log_list.add_child(label)


## Exposed for the console's mission state and for testing.
func session_count() -> int:
	return _session.size()


func reset_session() -> void:
	_session.clear()
	_locked = ""
	_acquiring = ""
	_acquire_times.clear()
	_clear_report()
	_render_log()


func table_summary() -> String:
	if _table.entry_count == 0:
		return "no composition table (%s)" % _table.load_error
	return "%d entries from %s" % [_table.entry_count, _table.source_path]


func _short(tag_id: String) -> String:
	return tag_id.substr(maxi(0, tag_id.length() - 6))


func _now() -> float:
	return Time.get_ticks_msec() / 1000.0
