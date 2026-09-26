extends Node
## Inbound API for the host-side rock detector (vision/detect_stream.py, step 2.0e).
##
## Detection moved off the rover onto the operator PC, so the rover sends no `tag` frame
## and there is nothing on the control channel to carry a result. This is the other way
## in: a deliberately tiny HTTP listener that accepts one verb.
##
##     POST /detection
##     {"element": "Helium", "label": "green-sharp", "confidence": 0.83}
##
## It answers 200 with {"added": true|false} — false meaning the name was already on the
## display list — 400 on a body it cannot use, and 404 on anything else. It binds to
## loopback unless DETECTION_API_BIND says otherwise, because nothing off this machine has
## any business announcing rocks to the operator. It never touches the rover link: video
## and detection stay off the control channel (proposal §5.5, risk R1).

const DEFAULT_PORT := 8765
const DEFAULT_BIND := "127.0.0.1"

## A request that has not finished arriving in this long is dropped, so a half-open
## client cannot hold a slot for ever.
const REQUEST_TIMEOUT_MSEC := 2000
const MAX_REQUEST_BYTES := 16384

## func(element: String, label: String, confidence: float) -> bool, true when the entry
## was new. Set by the console; the API itself holds no opinion on what is a duplicate.
var handler: Callable

var _server := TCPServer.new()
## Each entry: {peer: StreamPeerTCP, buf: PackedByteArray, started: int}
var _clients: Array[Dictionary] = []


func _ready() -> void:
	var port := DEFAULT_PORT
	var port_env := OS.get_environment("DETECTION_API_PORT")
	if port_env.is_valid_int():
		port = port_env.to_int()
	var bind := OS.get_environment("DETECTION_API_BIND")
	if bind == "":
		bind = DEFAULT_BIND

	var err := _server.listen(port, bind)
	if err != OK:
		# Not fatal: the console still drives without it. Said loudly, because the
		# detector's only symptom would otherwise be "connection refused".
		push_warning("Detection API: could not listen on %s:%d (error %d)" % [bind, port, err])
		set_process(false)
		return
	print("Detection API: listening on http://%s:%d/detection" % [bind, port])


func _process(_delta: float) -> void:
	while _server.is_connection_available():
		var peer := _server.take_connection()
		if peer != null:
			_clients.append({"peer": peer, "buf": PackedByteArray(), "started": Time.get_ticks_msec()})

	for i in range(_clients.size() - 1, -1, -1):
		if _service(_clients[i]):
			(_clients[i]["peer"] as StreamPeerTCP).disconnect_from_host()
			_clients.remove_at(i)


## Returns true when the client is finished with, one way or another.
func _service(client: Dictionary) -> bool:
	var peer: StreamPeerTCP = client["peer"]
	peer.poll()
	if peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		return true
	if Time.get_ticks_msec() - int(client["started"]) > REQUEST_TIMEOUT_MSEC:
		return true

	var available := peer.get_available_bytes()
	if available > 0:
		var got: Array = peer.get_partial_data(available)
		if int(got[0]) == OK:
			# Packed arrays are values, not references: append to a copy and store it back.
			var grown: PackedByteArray = client["buf"]
			grown.append_array(got[1])
			client["buf"] = grown

	var buf: PackedByteArray = client["buf"]
	if buf.size() > MAX_REQUEST_BYTES:
		_respond(peer, 413, {"error": "request too large"})
		return true

	var text := buf.get_string_from_utf8()
	var header_end := text.find("\r\n\r\n")
	if header_end == -1:
		return false

	var lines := text.substr(0, header_end).split("\r\n")
	var request_line := lines[0].split(" ")
	var content_length := 0
	for line in lines.slice(1):
		var colon := line.find(":")
		if colon > 0 and line.substr(0, colon).strip_edges().to_lower() == "content-length":
			content_length = line.substr(colon + 1).strip_edges().to_int()

	# Content-Length counts bytes, so compare against the byte buffer, not the string.
	var body_start := text.substr(0, header_end + 4).to_utf8_buffer().size()
	if buf.size() - body_start < content_length:
		return false

	var method := request_line[0] if request_line.size() > 0 else ""
	var path := request_line[1] if request_line.size() > 1 else ""
	if method != "POST" or path.split("?")[0] != "/detection":
		_respond(peer, 404, {"error": "only POST /detection is served"})
		return true

	var body := buf.slice(body_start, body_start + content_length).get_string_from_utf8()
	var result := _handle(body)
	_respond(peer, int(result[0]), result[1])
	return true


## Returns [status, reply].
func _handle(body: String) -> Array:
	# A JSON instance rather than JSON.parse_string, which logs an engine error for every
	# malformed body; a bad post is the client's problem and gets a 400, not console noise.
	var json := JSON.new()
	var data: Variant = json.data if json.parse(body) == OK else null
	if not (data is Dictionary):
		return [400, {"error": "body must be a JSON object"}]

	var label := str((data as Dictionary).get("label", "")).strip_edges()
	var element := str((data as Dictionary).get("element", "")).strip_edges()
	if element == "":
		element = label
	if element == "":
		return [400, {"error": "element or label is required"}]

	var confidence := float((data as Dictionary).get("confidence", 0.0))
	var added := false
	if handler.is_valid():
		added = bool(handler.call(element, label, confidence))
	return [200, {"added": added, "element": element}]


func _respond(peer: StreamPeerTCP, status: int, reply: Dictionary) -> void:
	var reasons := {200: "OK", 400: "Bad Request", 404: "Not Found", 413: "Payload Too Large"}
	var body := JSON.stringify(reply).to_utf8_buffer()
	var head := "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" \
			% [status, reasons.get(status, "Error"), body.size()]
	peer.put_data(head.to_utf8_buffer())
	peer.put_data(body)


func _exit_tree() -> void:
	for client in _clients:
		(client["peer"] as StreamPeerTCP).disconnect_from_host()
	_clients.clear()
	_server.stop()
