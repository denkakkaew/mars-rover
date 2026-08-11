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

/// Upper bound on the arm's joint count. The real count is fixed in Phase 0 and is
/// checked by the Arm module, not here — this is only the buffer size.
constexpr uint8_t kMaxArmJoints = 4;

enum class CommandType : uint8_t {
  Malformed,  ///< Did not parse, or a field was the wrong type. Dropped whole.
  Unknown,    ///< Parsed, but `cmd` is not one we know. Ignored, not an error (protocol.md 2.2).
  Hello,
  Drive,
  Stop,
  Mast,
  Arm,
  Ping,
};

/// Why a frame was rejected. Diagnostic only — every value means "dropped".
enum class ParseError : uint8_t {
  None,
  TooLarge,       ///< Over kMaxFrameBytes
  BadJson,        ///< Not valid JSON
  NotAnObject,    ///< Valid JSON, but not an object
  MissingCmd,     ///< No `cmd`, or `cmd` was not a string
  BadField,       ///< A known field carried the wrong type (protocol.md 2.5)
  BadJointCount,  ///< `joints` was empty or longer than kMaxArmJoints
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

  uint8_t joint_count = 0;  ///< Arm: 0 means "hold the joints"
  float joints[kMaxArmJoints] = {};
  bool has_grip = false;
  bool grip_closed = false;

  int64_t ts = 0;  ///< Ping: opaque console timestamp, echoed back unmodified
};

/// Decodes one frame. Never throws, never allocates beyond its own scratch document,
/// and never returns a partially applied Command — a bad field yields Malformed.
Command parse(const char *frame, size_t length);

/// True for the commands that count as proof of a live console: drive, stop, mast, arm.
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

struct Telemetry {
  float battery_v = 0.0f;  ///< Pack volts. Uncalibrated until step 1.7.
  Mode mode = Mode::Safe;
  int rssi = 0;  ///< dBm
};

/// Each writes a complete frame plus a null terminator into `out` and returns the byte
/// count excluding the terminator, or 0 if `capacity` was too small.
size_t serializeTelemetry(const Telemetry &telemetry, char *out, size_t capacity);
size_t serializeHello(int version, const char *firmware, const char *const *caps,
                      size_t cap_count, char *out, size_t capacity);
size_t serializePong(int64_t ts, char *out, size_t capacity);

}  // namespace protocol
