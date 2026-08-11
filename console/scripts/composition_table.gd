extends RefCounted
## Tag ID -> simulated elemental composition (proposal §5.4).
##
## The rover reports a tag ID and nothing else. Turning that into a composition happens
## here, on the console, so the mapping can be edited per arena setup without reflashing
## anything — which matters because the tags are sealed inside rocks and the arena gets
## re-dressed between sessions.
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

var _tags: Dictionary = {}


func load_default() -> bool:
	for candidate in [OS.get_environment("COMPOSITION_TABLE"), USER_PATH, SHIPPED_PATH]:
		if candidate == "" or not FileAccess.file_exists(candidate):
			continue
		return load_from(candidate)

	load_error = "no composition table found"
	push_warning("Composition table: %s" % load_error)
	return false


func load_from(path: String) -> bool:
	_tags = {}
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

	if not (parsed is Dictionary) or not (parsed as Dictionary).has("tags"):
		load_error = "%s is not a composition table (no `tags` object)" % path
		push_warning("Composition table: %s" % load_error)
		return false

	var tags: Variant = (parsed as Dictionary)["tags"]
	if not (tags is Dictionary):
		load_error = "`tags` is not an object in %s" % path
		push_warning("Composition table: %s" % load_error)
		return false

	for key in (tags as Dictionary):
		var value: Variant = (tags as Dictionary)[key]
		if value is Dictionary:
			# Uppercased on the way in so a lower-case entry in a hand-edited file still
			# matches what the reader reports.
			_tags[str(key).to_upper()] = value

	entry_count = _tags.size()
	print("Composition table: %d entries from %s" % [entry_count, path])
	return true


## Returns null when the tag is not in the table. That is a real operating case — a rock
## whose ID was never entered — and the console reports it rather than inventing a result.
func lookup(tag_id: String) -> Variant:
	return _tags.get(tag_id.to_upper())


func has(tag_id: String) -> bool:
	return _tags.has(tag_id.to_upper())


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
