extends PanelContainer
## Scenes 5 through 8, camera edition: an object in view, the operator's analysis of it,
## the composition that analysis reveals, and the session log.
##
## Fed by the host-side detector (vision/detect_stream.py) through detection_api.gd, never
## by the rover — detection stays off the control channel (proposal §5.5, risk R1):
##
## - `on_view` a few times a second with every confident detection in the current frame,
##   possibly none. That drives the live readout and is the detector's heartbeat.
## - `on_detection` is accepted and ignored; see there.
##
## Detect → analyse → reveal. The detector only knows what an object *looks* like, so the
## panel never shows its class label: it says OBJECT DETECTED, the operator presses
## ANALYSE, the object has to stay in view for ANALYSIS_SEC, and only then does the report
## card name the sample and give its composition. That is the order a real rover works in
## — find a target, put the instrument on it, read the result — and it gives Scene 6 an
## operator action rather than a result that appears on its own.
##
## The mission identifies rocks by camera since 2026-09-26; the RFID signal meter this
## panel used to carry went with the reader.

signal detector_state_changed(online: bool)

const CompositionTable := preload("res://scripts/composition_table.gd")

## No snapshot for this long and the detector is treated as gone. The detector sends
## about four a second, so this is several missed in a row, not one slow post.
const VIEW_STALE_SEC := 2.0

## Scene 6 wants "a stable read … not an intermittent or dropped signal". A single frame
## is not a target: ANALYSE only lights after this many consecutive snapshots agree on the
## same label (~0.75 s at the detector's default rate).
const CONFIRM_VIEWS := 3

## How long an analysis takes. Simulated instrument integration time — the console has no
## spectrometer — long enough that the operator has to hold the rover on the object.
const ANALYSIS_SEC := 3.0

## The object left view this long ago: the live line says so, and a running analysis is
## abandoned. The report stays.
const LOST_SEC := 0.75

## How long "ANALYSIS INTERRUPTED" stays on the status line before it falls back to the
## live readout.
const INTERRUPTED_SHOWN_SEC := 3.0

## Placeholder name for an object class the composition table has no entry for.
const UNKNOWN_NAME := "Unclassified sample"

## How long the confidence bar takes to fall to empty once nothing is in view. Falling
## rather than freezing: a held-up bar would read as "still on target" while the operator
## drives away.
const DECAY_SEC := 0.8

const COLOR_READY := Color("3ddc84")
const COLOR_WARN := Color("ffb300")
const COLOR_FAULT := Color("ff5252")
const COLOR_IDLE := Color("78909c")

const LOG_ROWS_SHOWN := 5

@onready var _status: Label = $Column/Signal/Status
@onready var _bar: ProgressBar = $Column/Signal/Bar
@onready var _signal_value: Label = $Column/Signal/Value
@onready var _analyse: Button = $Column/Analyse
@onready var _rock_name: Label = $Column/Report/RockName
@onready var _rock_class: Label = $Column/Report/RockClass
@onready var _elements: VBoxContainer = $Column/Report/Elements
@onready var _log_list: VBoxContainer = $Column/Log/List
@onready var _log_heading: Label = $Column/Log/Heading

var _table := CompositionTable.new()

var _last_view_t := -999.0
var _online := false

## Best detection in the latest snapshot, {label, element, confidence}, or empty.
var _in_view: Dictionary = {}
var _last_seen_t := -999.0
var _display_confidence := 0.0

## The label the last snapshots agree on, and how many in a row.
var _acquiring := ""
var _acquire_count := 0

## The label under analysis, or empty; when it started and when it was last in a snapshot.
var _analysing := ""
var _analysis_t0 := 0.0
var _analysing_seen_t := -999.0
var _interrupted_t := -999.0

## The label on the report card.
var _locked := ""
var _locked_name := ""

## lower-cased display name -> {name, at, seq}. Insertion order is the log order.
var _session: Dictionary = {}

## Own copy of the fill style, so the bar's colour can carry state as well as its length.
## Colour is doing real work here: the operator is watching the camera monitor, not this
## panel, and a green bar in peripheral vision means "stop, there is something to analyse".
var _fill: StyleBoxFlat


func _ready() -> void:
	_table.load_default()

	var base := _bar.get_theme_stylebox("fill")
	_fill = (base.duplicate() if base is StyleBoxFlat else StyleBoxFlat.new())
	_bar.add_theme_stylebox_override("fill", _fill)

	_analyse.pressed.connect(_on_analyse_pressed)

	_clear_report()
	_render_log()
	set_process(true)


func detector_online() -> bool:
	return _online


## One snapshot of the detector's current frame. `detections` entries are
## {label, element, confidence}; an empty array means "looking, nothing there".
func on_view(detections: Array) -> void:
	var now := _now()
	_last_view_t = now
	_set_online(true)

	var best: Dictionary = {}
	for entry: Dictionary in detections:
		# A running analysis follows its own object, not whichever is most confident: a
		# second rock drifting into frame must not interrupt the one being analysed.
		if _analysing != "" and str(entry["label"]).to_lower() == _analysing:
			_analysing_seen_t = now
		if best.is_empty() or float(entry["confidence"]) > float(best["confidence"]):
			best = entry

	if best.is_empty():
		_in_view = {}
		_acquiring = ""
		_acquire_count = 0
		return

	_in_view = best
	_last_seen_t = now
	_display_confidence = float(best["confidence"])

	var label: String = str(best["label"]).to_lower()
	if label == _acquiring:
		_acquire_count += 1
	else:
		_acquiring = label
		_acquire_count = 1


## POST /detection, kept so the detector's API is unchanged. Ignored on purpose: the log
## records what the operator analysed, and the detector merely seeing something is not
## that. Always false — nothing was added.
func on_detection(_element: String, _label: String, _confidence: float) -> bool:
	return false


func _process(delta: float) -> void:
	var now := _now()
	if now - _last_view_t > VIEW_STALE_SEC:
		_set_online(false)
		_in_view = {}
		_acquiring = ""
		_acquire_count = 0

	if _analysing != "":
		if not _online or now - _analysing_seen_t > LOST_SEC:
			_interrupt()
		elif now - _analysis_t0 >= ANALYSIS_SEC:
			_complete()

	var since_seen := now - _last_seen_t
	if since_seen > 0.3:
		_display_confidence = maxf(0.0, _display_confidence - delta / DECAY_SEC)

	var visible := _online and not _in_view.is_empty() and since_seen <= LOST_SEC
	var steady := visible and _acquire_count >= CONFIRM_VIEWS

	var colour := COLOR_IDLE
	if _analysing != "":
		# The bar measures the analysis while one runs, and says so in words below it.
		var progress := clampf((now - _analysis_t0) / ANALYSIS_SEC, 0.0, 1.0)
		_bar.value = progress * 100.0
		_signal_value.text = "analysis %d %%" % int(progress * 100.0)
		colour = COLOR_WARN
	else:
		_bar.value = clampf(_display_confidence, 0.0, 1.0) * 100.0
		if visible:
			_signal_value.text = "%d %% confidence" % int(round(float(_in_view["confidence"]) * 100.0))
			colour = COLOR_READY if steady else COLOR_WARN
		else:
			_signal_value.text = "--- %"
	_signal_value.modulate = colour
	_fill.bg_color = colour

	_analyse.disabled = not steady or _analysing != ""
	_analyse.text = "ANALYSING …" if _analysing != "" else "ANALYSE"

	_update_status(now, visible, steady)


func _update_status(now: float, visible: bool, steady: bool) -> void:
	if not _online:
		_status.text = "DETECTOR OFFLINE" + ("  ·  last result held" if _locked != "" else "")
		_status.modulate = COLOR_WARN if _locked != "" else COLOR_IDLE
	elif _analysing != "":
		_status.text = "ANALYSING OBJECT  ·  hold position"
		_status.modulate = COLOR_WARN
	elif now - _interrupted_t < INTERRUPTED_SHOWN_SEC:
		_status.text = "ANALYSIS INTERRUPTED  ·  object left view"
		_status.modulate = COLOR_WARN
	elif steady and _acquiring == _locked:
		_status.text = "ANALYSED  ·  %s" % _locked_name.to_upper()
		_status.modulate = COLOR_READY
	elif steady:
		_status.text = "OBJECT DETECTED  ·  ready to analyse"
		_status.modulate = COLOR_READY
	elif visible:
		_status.text = "OBJECT DETECTED  ·  acquiring"
		_status.modulate = COLOR_WARN
	elif _locked != "":
		# The science result stays on screen; only the live line goes quiet. Clearing
		# the report the instant the rover rolls away would throw away Scene 7.
		_status.text = "OUT OF VIEW  ·  last result held"
		_status.modulate = COLOR_WARN
	else:
		_status.text = "NO OBJECT IN VIEW"
		_status.modulate = COLOR_IDLE


func _set_online(online: bool) -> void:
	if online == _online:
		return
	_online = online
	print("Analysis: detector %s" % ("online" if online else "offline"))
	detector_state_changed.emit(online)


# --- Analysis -------------------------------------------------------------------------

func _on_analyse_pressed() -> void:
	# The button is disabled unless this holds; checked again because a snapshot may have
	# landed between the frame that enabled it and the press.
	if _analysing != "" or _acquiring == "" or _acquire_count < CONFIRM_VIEWS:
		return
	_analysing = _acquiring
	_analysis_t0 = _now()
	_analysing_seen_t = _analysis_t0
	_interrupted_t = -999.0
	_locked = ""
	_locked_name = ""
	_clear_report()
	_rock_name.text = "…"
	_rock_class.text = "analysis in progress"
	print("Analysis: started on %s" % _analysing)


func _interrupt() -> void:
	print("Analysis: interrupted on %s, %.1f s in" % [_analysing, _now() - _analysis_t0])
	_analysing = ""
	_interrupted_t = _now()
	_clear_report()
	_rock_class.text = "analysis interrupted — no result"
	_rock_class.modulate = COLOR_WARN


func _complete() -> void:
	var label := _analysing
	_analysing = ""
	_locked = label

	var entry: Variant = _table.lookup(label)
	if entry is Dictionary:
		_locked_name = str((entry as Dictionary).get("name", "")).strip_edges()
		if _locked_name == "":
			_locked_name = UNKNOWN_NAME
		_show_report(entry as Dictionary)
	else:
		# A class the table has no entry for — trained after the table was last edited.
		# Flagged on screen; the label goes to the output, not the panel, because the
		# operator's screen never shows detector labels and whoever edits the table needs it.
		_locked_name = UNKNOWN_NAME
		_show_unknown()
		push_warning("Analysis: detector label \"%s\" is not in the composition table" % label)
	_log(_locked_name)
	print("Analysis: %s analysed as %s" % [label, _locked_name])


# --- Report card ----------------------------------------------------------------------

func _clear_report() -> void:
	_rock_name.text = "—"
	_rock_name.modulate = COLOR_IDLE
	_rock_class.text = "no analysis yet"
	_rock_class.modulate = COLOR_IDLE
	for child in _elements.get_children():
		child.queue_free()


func _show_report(entry: Dictionary) -> void:
	_clear_report()
	_rock_name.text = _locked_name
	_rock_name.modulate = COLOR_READY
	_rock_class.text = str(entry.get("class", "unclassified"))
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


func _show_unknown() -> void:
	_clear_report()
	_rock_name.text = UNKNOWN_NAME
	_rock_name.modulate = COLOR_WARN
	_rock_class.text = "no composition table entry for this object"
	_rock_class.modulate = COLOR_WARN


# --- Session log ----------------------------------------------------------------------

## Returns true when the name was new. The operator may analyse the same rock again on
## the way back — it is the same finding.
func _log(name: String) -> bool:
	var key := name.strip_edges().to_lower()
	if key == "" or _session.has(key):
		return false
	_session[key] = {
		"name": name,
		"at": Time.get_time_string_from_system(),
		"seq": _session.size() + 1,
	}
	_render_log()
	return true


func _render_log() -> void:
	for child in _log_list.get_children():
		child.queue_free()

	_log_heading.text = "SESSION LOG  ·  %d analysed" % _session.size()

	var keys := _session.keys()
	keys.reverse()  # most recent first
	for key in keys.slice(0, LOG_ROWS_SHOWN):
		var record: Dictionary = _session[key]
		var row := Label.new()
		row.text = "%d. %s  %s" % [int(record["seq"]), str(record["at"]), str(record["name"])]
		row.modulate = COLOR_READY
		_log_list.add_child(row)


## Exposed for the console's mission state and for testing.
func session_count() -> int:
	return _session.size()


func reset_session() -> void:
	_session.clear()
	_locked = ""
	_locked_name = ""
	_acquiring = ""
	_acquire_count = 0
	_analysing = ""
	_interrupted_t = -999.0
	_clear_report()
	_render_log()


func table_summary() -> String:
	if _table.entry_count == 0:
		return "no composition table (%s)" % _table.load_error
	return "%d entries from %s" % [_table.entry_count, _table.source_path]


func _now() -> float:
	return Time.get_ticks_msec() / 1000.0
