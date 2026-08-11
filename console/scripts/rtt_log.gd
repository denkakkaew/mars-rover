extends RefCounted
## Appends round-trip samples to a CSV, one row per measurement.
##
## The point is comparability across time. Step 1.8 measures control latency on real
## hardware with an idle link; step 3.3 measures it again with both camera feeds live,
## and R1 is settled by putting those two distributions side by side. That only works if
## both runs were recorded the same way, so the format is fixed here and every run since
## S.7 — simulator included — writes it.
##
## Each row carries the rover context alongside the number, because "180 ms" means one
## thing at -50 dBm and something quite different at -80 dBm.

const HEADER := "iso_time,uptime_ms,rtt_ms,p95_ms,loss_pct,mode,rssi_dbm,battery_v"

## Rows between flushes. A crashed console should still leave a usable log behind, and
## the write rate is only four rows a second.
const FLUSH_EVERY := 20

var path := ""

var _file: FileAccess = null
var _rows := 0
var _since_flush := 0


## Opens the log. `RTT_LOG` in the environment overrides the location, which is how a
## bench run gets its samples somewhere findable rather than into the user data folder.
func open(override_path := "") -> bool:
	var target := override_path
	if target == "":
		target = OS.get_environment("RTT_LOG")
	if target == "":
		var stamp := Time.get_datetime_string_from_system(true).replace(":", "-")
		target = "user://rtt_%s.csv" % stamp

	_file = FileAccess.open(target, FileAccess.WRITE)
	if _file == null:
		push_warning("RTT log: cannot write %s (%d)" % [target, FileAccess.get_open_error()])
		return false

	path = ProjectSettings.globalize_path(target)
	_file.store_line(HEADER)
	print("RTT log: %s" % path)
	return true


func append(rtt_ms: int, p95_ms: int, loss_pct: float, telemetry: Dictionary) -> void:
	if _file == null:
		return
	_file.store_line("%s,%d,%d,%d,%.2f,%s,%s,%s" % [
		Time.get_datetime_string_from_system(true),
		Time.get_ticks_msec(),
		rtt_ms,
		p95_ms,
		loss_pct,
		str(telemetry.get("mode", "")),
		str(telemetry.get("rssi", "")),
		str(telemetry.get("battery_v", "")),
	])
	_rows += 1
	_since_flush += 1
	if _since_flush >= FLUSH_EVERY:
		_since_flush = 0
		_file.flush()


func row_count() -> int:
	return _rows


func close() -> void:
	if _file != null:
		_file.flush()
		_file.close()
		_file = null
