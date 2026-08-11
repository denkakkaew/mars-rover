#pragma once
#include <stddef.h>
#include <stdint.h>

/// Wire format for the console <-> rover control channel.
///
/// The authoritative definition is docs/protocol.md; this header implements it and
/// section numbers below refer to it. Parsing and serialisation live here, apart from
/// the transport, so the safety-critical decisions can be unit-tested on a PC with no
/// ESP32 attached (IMPLEMENTATION_PLAN.md S.3/S.4).
///
/// **No Arduino headers.** Nothing in here may reach for `String`, `millis()`, or any
/// board API — that constraint is what keeps it host-compilable.
namespace protocol {

/// Bumped only for a breaking change; additive fields do not bump it (protocol.md 5).
constexpr int kVersion = 1;

/// Frames larger than this are dropped whole, never partially applied (protocol.md 1).
constexpr size_t kMaxFrameBytes = 512;

/// Longest tag EPC we will carry, in hex characters (protocol.md 4.4). Generous: a 96-bit
/// EPC is 24 characters. The real reader's format is confirmed at step 2.1.
constexpr size_t kMaxTagIdChars = 64;

enum class CommandType : uint8_t {
  Malformed,  ///< Did not parse, or a field was the wrong type. Dropped whole.
  Unknown,    ///< Parsed, but `cmd` is not one we know. Ignored, not an error (protocol.md 2.2).
  Hello,
  Drive,
  Stop,
  Mast,
  Ping,
};

/// Why a frame was rejected. Diagnostic only — every value means "dropped".
enum class ParseError : uint8_t {
  None,
  TooLarge,     ///< Over kMaxFrameBytes
  BadJson,      ///< Not valid JSON
  NotAnObject,  ///< Valid JSON, but not an object
  MissingCmd,   ///< No `cmd`, or `cmd` was not a string
  BadField,     ///< A known field carried the wrong type (protocol.md 2.5)
};

/// One decoded console -> rover frame. Fields are only meaningful for their own `type`.
struct Command {
  CommandType type = CommandType::Malformed;
  ParseError error = ParseError::BadJson;

  int version = 0;  ///< Hello: protocol version the console speaks

  float left = 0.0f;   ///< Drive: left-side throttle, already clamped to -1..1
  float right = 0.0f;  ///< Drive: right-side throttle, already clamped to -1..1

  bool has_pan = false;   ///< Mast: false means "hold this axis" (protocol.md 3.4)
  bool has_tilt = false;
  float pan_deg = 0.0f;
  float tilt_deg = 0.0f;

  int64_t ts = 0;  ///< Ping: opaque console timestamp, echoed back unmodified
};

/// Decodes one frame. Never throws, never allocates beyond its own scratch document,
/// and never returns a partially applied Command — a bad field yields Malformed.
Command parse(const char *frame, size_t length);

/// True for the commands that count as proof of a live console: drive, stop, mast.
/// Deliberately false for ping and hello — a console that can ping but not drive is not
/// a console that should keep the motors live (protocol.md 6.2).
bool refreshesFailsafe(CommandType type);

/// Stable lowercase name, for logs. Never null.
const char *name(CommandType type);

/// Rover mode as reported in telemetry (protocol.md 4.2).
enum class Mode : uint8_t {
  Safe,          ///< Motors cut, awaiting a fresh command
  Drive,         ///< Armed, acting on commands
  Incompatible,  ///< Protocol version mismatch; will not arm at all
};

const char *modeName(Mode mode);

/// RFID reader state, reported in every telemetry frame (protocol.md 4.2). Scene 1 needs
/// the reader to go green before the mission starts, so "not fitted" and "fitted but
/// broken" have to be distinguishable.
enum class ReaderState : uint8_t {
  Absent,    ///< No reader on this build
  Ready,     ///< Up and answering, no tag in range
  Scanning,  ///< A tag is being read right now
  Fault,     ///< Fitted but not responding, or reporting an error
};

const char *readerStateName(ReaderState state);

struct Telemetry {
  float battery_v = 0.0f;  ///< Pack volts. Uncalibrated until step 1.7.
  Mode mode = Mode::Safe;
  int rssi = 0;  ///< Wi-Fi link strength, dBm. Not the RFID RSSI.
  ReaderState reader = ReaderState::Absent;
};

/// One RFID tag read (protocol.md 4.4).
struct TagRead {
  const char *id = "";  ///< EPC as uppercase hex. Opaque — never interpreted here.
  int rssi = 0;         ///< Reader signal strength for this read, dBm. The proximity cue.
  uint32_t ts_ms = 0;   ///< Rover uptime at the read
};

/// Each writes a complete frame plus a null terminator into `out` and returns the byte
/// count excluding the terminator, or 0 if `capacity` was too small.
size_t serializeTelemetry(const Telemetry &telemetry, char *out, size_t capacity);
size_t serializeHello(int version, const char *firmware, const char *const *caps,
                      size_t cap_count, char *out, size_t capacity);
size_t serializePong(int64_t ts, char *out, size_t capacity);

/// Returns 0 for an empty or over-long `id` as well as for a short buffer — a tag frame
/// with no usable ID is worse than no frame, since the console would log a read it cannot
/// attribute to a rock.
size_t serializeTagRead(const TagRead &read, char *out, size_t capacity);

}  // namespace protocol
