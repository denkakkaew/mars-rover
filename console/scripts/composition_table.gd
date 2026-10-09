extends RefCounted
## Detector class label -> simulated elemental composition (proposal §5.4).
##
## The camera detector (vision/detect_stream.py) reports a class label and nothing else.
## Turning that into a composition happens here, on the console, so the mapping can be
## edited per arena setup without retraining or reflashing anything. Keyed by label since
## 2026-09-26 (format 2); the RFID tag IDs it was keyed by before are gone with the reader.
##
## Lookup order, first hit wins:
##   1. $COMPOSITION_TABLE   — an explicit path, for a bench run or a test
##   2. user://compositions.json — the per-installation copy an operator can edit
##   3. res://data/compositions.json — the version shipped with the console
##
## Loading is deliberately forgiving about *missing* files and strict about malformed
## ones: no table at all is a normal state during Phase S, but a table that silently
## half-loads would show the wrong rock's chemistry, which is worse than showing none.

const SHIPPED_PATH := "res://data/compositions.json"
const USER_PATH := "user://compositions.json"

var source_path := ""
var entry_count := 0
var load_error := ""

var _labels: Dictionary = {}


func load_default() -> bool:
	for candidate in [OS.get_environment("COMPOSITION_TABLE"), USER_PATH, SHIPPED_PATH]:
		if candidate == "" or not FileAccess.file_exists(candidate):
			continue
		return load_from(candidate)

	load_error = "no composition table found"
	push_warning("Composition table: %s" % load_error)
	return false


func load_from(path: String) -> bool:
	_labels = {}
	entry_count = 0
	source_path = path
	load_error = ""

	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		load_error = "cannot open %s (%d)" % [path, FileAccess.get_open_error()]
		push_warning("Composition table: %s" % load_error)
		return false

	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()

	if not (parsed is Dictionary) or not (parsed as Dictionary).has("labels"):
		# Said specifically for a format 1 file, which an old user:// copy may still be.
		var hint := " — a format 1 (RFID tag) table, re-key it by detector label" \
				if parsed is Dictionary and (parsed as Dictionary).has("tags") else ""
		load_error = "%s is not a composition table (no `labels` object)%s" % [path, hint]
		push_warning("Composition table: %s" % load_error)
		return false

	var labels: Variant = (parsed as Dictionary)["labels"]
	if not (labels is Dictionary):
		load_error = "`labels` is not an object in %s" % path
		push_warning("Composition table: %s" % load_error)
		return false

	for key in (labels as Dictionary):
		var value: Variant = (labels as Dictionary)[key]
		if value is Dictionary:
			# Lower-cased on the way in so a hand-edited "Green-Sharp" still matches.
			_labels[_key(str(key))] = value

	entry_count = _labels.size()
	print("Composition table: %d entries from %s" % [entry_count, path])
	return true


## Returns null when the label is not in the table. That is a real operating case — a
## class trained after the table was last edited — and the console reports it rather
## than inventing a result.
func lookup(label: String) -> Variant:
	return _labels.get(_key(label))


func has(label: String) -> bool:
	return _labels.has(_key(label))


static func _key(label: String) -> String:
	return label.strip_edges().to_lower()


## Elements as an array of {symbol, percent}, heaviest fraction first, so the report card
## reads top-down in order of significance.
static func ranked_elements(entry: Dictionary) -> Array:
	var raw: Variant = entry.get("elements", {})
	if not (raw is Dictionary):
		return []

	var out: Array = []
	for symbol in (raw as Dictionary):
		out.append({"symbol": str(symbol), "percent": float((raw as Dictionary)[symbol])})
	out.sort_custom(func(a, b): return a["percent"] > b["percent"])
	return out
