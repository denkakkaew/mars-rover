#include "Protocol.h"

#include <ArduinoJson.h>
#include <string.h>

namespace protocol {
namespace {

Command rejected(ParseError error) {
  Command out;
  out.type = CommandType::Malformed;
  out.error = error;
  return out;
}

float clamp(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

/// Strict numeric read: a JSON string is never coerced into a number, and a bool is
/// never 0/1 (protocol.md 2.5, 2.7). Integers are accepted where a number is expected.
bool readNumber(JsonVariantConst value, float &out) {
  if (!value.is<float>()) return false;
  out = value.as<float>();
  return true;
}

/// Reads an optional field. Absent leaves `out` alone and reports success; present but
/// wrong-typed is a failure, because a typo must not silently become a default.
bool readOptionalNumber(JsonVariantConst value, float &out, bool &present) {
  if (value.isNull()) {
    present = false;
    return true;
  }
  present = true;
  return readNumber(value, out);
}

Command parseDrive(JsonObjectConst doc) {
  Command out;
  out.type = CommandType::Drive;
  out.error = ParseError::None;

  bool present = false;
  if (!readOptionalNumber(doc["l"], out.left, present)) return rejected(ParseError::BadField);
  if (!readOptionalNumber(doc["r"], out.right, present)) return rejected(ParseError::BadField);

  // Out of range is clamped, not rejected — the console clamps too, and both are
  // required (protocol.md 3.2).
  out.left = clamp(out.left, -1.0f, 1.0f);
  out.right = clamp(out.right, -1.0f, 1.0f);
  return out;
}

Command parseMast(JsonObjectConst doc) {
  Command out;
  out.type = CommandType::Mast;
  out.error = ParseError::None;

  if (!readOptionalNumber(doc["pan"], out.pan_deg, out.has_pan)) {
    return rejected(ParseError::BadField);
  }
  if (!readOptionalNumber(doc["tilt"], out.tilt_deg, out.has_tilt)) {
    return rejected(ParseError::BadField);
  }
  // Not clamped here: the travel limits are mechanical and get measured in step 2.2.
  // The Mast module owns them, so this stays a pure decode.
  return out;
}

Command parseArm(JsonObjectConst doc) {
  Command out;
  out.type = CommandType::Arm;
  out.error = ParseError::None;

  JsonVariantConst joints = doc["joints"];
  if (!joints.isNull()) {
    if (!joints.is<JsonArrayConst>()) return rejected(ParseError::BadField);

    JsonArrayConst list = joints.as<JsonArrayConst>();
    const size_t count = list.size();
    if (count == 0 || count > kMaxArmJoints) return rejected(ParseError::BadJointCount);

    // Decoded into a scratch array first: a wrong-typed angle halfway along must not
    // leave the first half applied (protocol.md 2.4, 3.5).
    float decoded[kMaxArmJoints] = {};
    size_t index = 0;
    for (JsonVariantConst angle : list) {
      if (!readNumber(angle, decoded[index])) return rejected(ParseError::BadField);
      ++index;
    }
    for (size_t i = 0; i < count; ++i) out.joints[i] = decoded[i];
    out.joint_count = static_cast<uint8_t>(count);
  }

  JsonVariantConst grip = doc["grip"];
  if (!grip.isNull()) {
    if (!grip.is<bool>()) return rejected(ParseError::BadField);
    out.has_grip = true;
    out.grip_closed = grip.as<bool>();
  }
  return out;
}

/// Writes `doc` only if the whole frame fits.
///
/// `serializeJson` into a fixed buffer truncates and reports what it managed to write,
/// which would put a half-finished JSON object on the wire — worse than sending nothing,
/// because the console would log a parse error instead of a missing frame. So the length
/// is measured first and a frame that will not fit is refused outright.
size_t emit(const JsonDocument &doc, char *out, size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;

  if (measureJson(doc) + 1 > capacity) {
    out[0] = '\0';
    return 0;
  }
  return serializeJson(doc, out, capacity);
}

}  // namespace

Command parse(const char *frame, size_t length) {
  if (frame == nullptr) return rejected(ParseError::BadJson);
  if (length > kMaxFrameBytes) return rejected(ParseError::TooLarge);

  JsonDocument doc;
  if (deserializeJson(doc, frame, length) != DeserializationError::Ok) {
    return rejected(ParseError::BadJson);
  }
  if (!doc.is<JsonObjectConst>()) return rejected(ParseError::NotAnObject);

  JsonObjectConst root = doc.as<JsonObjectConst>();
  JsonVariantConst cmd = root["cmd"];
  if (!cmd.is<const char *>()) return rejected(ParseError::MissingCmd);
  const char *verb = cmd.as<const char *>();

  if (strcmp(verb, "drive") == 0) return parseDrive(root);
  if (strcmp(verb, "mast") == 0) return parseMast(root);
  if (strcmp(verb, "arm") == 0) return parseArm(root);

  if (strcmp(verb, "stop") == 0) {
    Command out;
    out.type = CommandType::Stop;
    out.error = ParseError::None;
    return out;
  }

  if (strcmp(verb, "hello") == 0) {
    if (!root["v"].is<int>()) return rejected(ParseError::BadField);
    Command out;
    out.type = CommandType::Hello;
    out.error = ParseError::None;
    out.version = root["v"].as<int>();
    return out;
  }

  if (strcmp(verb, "ping") == 0) {
    if (!root["ts"].is<long long>()) return rejected(ParseError::BadField);
    Command out;
    out.type = CommandType::Ping;
    out.error = ParseError::None;
    out.ts = root["ts"].as<long long>();
    return out;
  }

  // A `cmd` we do not know is dropped and logged, not treated as an error — that is
  // what lets a newer console talk to an older rover without the link collapsing.
  Command out;
  out.type = CommandType::Unknown;
  out.error = ParseError::None;
  return out;
}

bool refreshesFailsafe(CommandType type) {
  switch (type) {
    case CommandType::Drive:
    case CommandType::Stop:
    case CommandType::Mast:
    case CommandType::Arm:
      return true;
    default:
      return false;
  }
}

const char *name(CommandType type) {
  switch (type) {
    case CommandType::Malformed: return "malformed";
    case CommandType::Unknown: return "unknown";
    case CommandType::Hello: return "hello";
    case CommandType::Drive: return "drive";
    case CommandType::Stop: return "stop";
    case CommandType::Mast: return "mast";
    case CommandType::Arm: return "arm";
    case CommandType::Ping: return "ping";
  }
  return "unknown";
}

const char *modeName(Mode mode) {
  switch (mode) {
    case Mode::Drive: return "drive";
    case Mode::Incompatible: return "incompatible";
    case Mode::Safe: break;
  }
  return "safe";
}

size_t serializeTelemetry(const Telemetry &telemetry, char *out, size_t capacity) {
  JsonDocument doc;
  doc["t"] = "tlm";
  doc["battery_v"] = telemetry.battery_v;
  doc["mode"] = modeName(telemetry.mode);
  doc["rssi"] = telemetry.rssi;
  return emit(doc, out, capacity);
}

size_t serializeHello(int version, const char *firmware, const char *const *caps,
                      size_t cap_count, char *out, size_t capacity) {
  JsonDocument doc;
  doc["t"] = "hello";
  doc["v"] = version;
  doc["fw"] = firmware;
  JsonArray list = doc["caps"].to<JsonArray>();
  for (size_t i = 0; i < cap_count; ++i) list.add(caps[i]);
  return emit(doc, out, capacity);
}

size_t serializePong(int64_t ts, char *out, size_t capacity) {
  JsonDocument doc;
  doc["t"] = "pong";
  doc["ts"] = ts;
  return emit(doc, out, capacity);
}

}  // namespace protocol
