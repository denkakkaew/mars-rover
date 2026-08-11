// Host tests for lib/Protocol — the wire format in docs/protocol.md.
//
// These run on a PC with no ESP32 attached (IMPLEMENTATION_PLAN.md S.4):
//   python -m platformio test -e native
//
// The point of this file is the rejection cases. A parser that accepts a malformed
// frame and applies half of it is how a rover ends up driving into the glass, so the
// bad input gets more coverage here than the good input does.

#include <string.h>
#include <unity.h>

#include <string>

#include <Protocol.h>

namespace {

protocol::Command parseText(const char *json) {
  return protocol::parse(json, strlen(json));
}

}  // namespace

// Enum comparisons as macros, not helpers, so a failure reports the caller's line.
#define ASSERT_TYPE(expected, cmd) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>((cmd).type))
#define ASSERT_ERROR(expected, cmd) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>((cmd).error))

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------------
// drive — the command that moves the rover
// ---------------------------------------------------------------------------------

void test_drive_valid(void) {
  const protocol::Command cmd = parseText("{\"cmd\":\"drive\",\"l\":0.6,\"r\":-0.6}");
  ASSERT_TYPE(protocol::CommandType::Drive, cmd);
  ASSERT_ERROR(protocol::ParseError::None, cmd);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.6f, cmd.left);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.6f, cmd.right);
}

void test_drive_accepts_integer_throttle(void) {
  // JSON has one number type; 1 and 1.0 must mean the same thing.
  const protocol::Command cmd = parseText("{\"cmd\":\"drive\",\"l\":1,\"r\":-1}");
  ASSERT_TYPE(protocol::CommandType::Drive, cmd);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, cmd.left);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, cmd.right);
}

void test_drive_clamps_out_of_range(void) {
  // Clamped, not rejected (protocol.md 3.2) — a console bug must not stop the rover
  // responding, it must just not make it go faster than full.
  const protocol::Command cmd = parseText("{\"cmd\":\"drive\",\"l\":2.5,\"r\":-9.0}");
  ASSERT_TYPE(protocol::CommandType::Drive, cmd);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, cmd.left);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, cmd.right);
}

void test_drive_missing_fields_default_to_zero(void) {
  const protocol::Command cmd = parseText("{\"cmd\":\"drive\"}");
  ASSERT_TYPE(protocol::CommandType::Drive, cmd);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, cmd.left);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, cmd.right);
}

void test_drive_one_side_only(void) {
  const protocol::Command cmd = parseText("{\"cmd\":\"drive\",\"l\":0.5}");
  ASSERT_TYPE(protocol::CommandType::Drive, cmd);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, cmd.left);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, cmd.right);
}

void test_drive_rejects_string_throttle(void) {
  // The F4 case: "0.6" must not be coerced into 0.6 (protocol.md 2.5).
  const protocol::Command cmd = parseText("{\"cmd\":\"drive\",\"l\":\"0.6\"}");
  ASSERT_TYPE(protocol::CommandType::Malformed, cmd);
  ASSERT_ERROR(protocol::ParseError::BadField, cmd);
}

void test_drive_rejects_bool_throttle(void) {
  // true is not 1 (protocol.md 2.7).
  const protocol::Command cmd = parseText("{\"cmd\":\"drive\",\"l\":true}");
  ASSERT_TYPE(protocol::CommandType::Malformed, cmd);
  ASSERT_ERROR(protocol::ParseError::BadField, cmd);
}

void test_drive_rejects_object_throttle(void) {
  const protocol::Command cmd = parseText("{\"cmd\":\"drive\",\"r\":{\"v\":1}}");
  ASSERT_TYPE(protocol::CommandType::Malformed, cmd);
  ASSERT_ERROR(protocol::ParseError::BadField, cmd);
}

void test_drive_null_field_is_absent_not_invalid(void) {
  const protocol::Command cmd = parseText("{\"cmd\":\"drive\",\"l\":null,\"r\":0.4}");
  ASSERT_TYPE(protocol::CommandType::Drive, cmd);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, cmd.left);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.4f, cmd.right);
}

void test_drive_ignores_unknown_fields(void) {
  // Additive fields must stay backward compatible (protocol.md 2.3).
  const protocol::Command cmd =
      parseText("{\"cmd\":\"drive\",\"l\":0.2,\"r\":0.2,\"future\":42}");
  ASSERT_TYPE(protocol::CommandType::Drive, cmd);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.2f, cmd.left);
}

// ---------------------------------------------------------------------------------
// Frame-level rejection
// ---------------------------------------------------------------------------------

void test_malformed_json(void) {
  const protocol::Command cmd = parseText("{\"cmd\":\"drive\",\"l\":");
  ASSERT_TYPE(protocol::CommandType::Malformed, cmd);
  ASSERT_ERROR(protocol::ParseError::BadJson, cmd);
}

void test_empty_frame(void) {
  const protocol::Command cmd = parseText("");
  ASSERT_TYPE(protocol::CommandType::Malformed, cmd);
  ASSERT_ERROR(protocol::ParseError::BadJson, cmd);
}

void test_null_pointer(void) {
  const protocol::Command cmd = protocol::parse(nullptr, 0);
  ASSERT_TYPE(protocol::CommandType::Malformed, cmd);
}

void test_json_array_is_not_a_command(void) {
  const protocol::Command cmd = parseText("[1,2,3]");
  ASSERT_TYPE(protocol::CommandType::Malformed, cmd);
  ASSERT_ERROR(protocol::ParseError::NotAnObject, cmd);
}

void test_missing_cmd(void) {
  const protocol::Command cmd = parseText("{\"l\":1.0,\"r\":1.0}");
  ASSERT_TYPE(protocol::CommandType::Malformed, cmd);
  ASSERT_ERROR(protocol::ParseError::MissingCmd, cmd);
}

void test_cmd_must_be_a_string(void) {
  const protocol::Command cmd = parseText("{\"cmd\":7}");
  ASSERT_TYPE(protocol::CommandType::Malformed, cmd);
  ASSERT_ERROR(protocol::ParseError::MissingCmd, cmd);
}

void test_unknown_cmd_is_ignored_not_malformed(void) {
  // A verb we do not know is dropped, not fatal — that is what lets a newer console
  // talk to an older rover without the link collapsing (protocol.md 2.2).
  const protocol::Command cmd = parseText("{\"cmd\":\"launch\",\"thrust\":1}");
  ASSERT_TYPE(protocol::CommandType::Unknown, cmd);
  ASSERT_ERROR(protocol::ParseError::None, cmd);
  TEST_ASSERT_FALSE(protocol::refreshesFailsafe(cmd.type));
}

void test_oversize_frame_rejected(void) {
  std::string frame = "{\"cmd\":\"drive\",\"l\":1.0,\"r\":1.0,\"pad\":\"";
  frame.append(600, 'x');
  frame += "\"}";
  TEST_ASSERT_GREATER_THAN_UINT32(protocol::kMaxFrameBytes, frame.size());

  const protocol::Command cmd = protocol::parse(frame.c_str(), frame.size());
  ASSERT_TYPE(protocol::CommandType::Malformed, cmd);
  ASSERT_ERROR(protocol::ParseError::TooLarge, cmd);
}

void test_frame_at_size_limit_is_accepted(void) {
  // The boundary itself is legal; only *over* the limit is rejected.
  std::string frame = "{\"cmd\":\"drive\",\"l\":1.0,\"r\":1.0,\"pad\":\"";
  const std::string tail = "\"}";
  frame.append(protocol::kMaxFrameBytes - frame.size() - tail.size(), 'x');
  frame += tail;
  TEST_ASSERT_EQUAL_UINT32(protocol::kMaxFrameBytes, frame.size());

  const protocol::Command cmd = protocol::parse(frame.c_str(), frame.size());
  ASSERT_TYPE(protocol::CommandType::Drive, cmd);
}

// ---------------------------------------------------------------------------------
// stop, hello, ping
// ---------------------------------------------------------------------------------

void test_stop(void) {
  const protocol::Command cmd = parseText("{\"cmd\":\"stop\"}");
  ASSERT_TYPE(protocol::CommandType::Stop, cmd);
  TEST_ASSERT_TRUE(protocol::refreshesFailsafe(cmd.type));
}

void test_hello_valid(void) {
  const protocol::Command cmd = parseText("{\"cmd\":\"hello\",\"v\":1}");
  ASSERT_TYPE(protocol::CommandType::Hello, cmd);
  TEST_ASSERT_EQUAL_INT(1, cmd.version);
}

void test_hello_version_mismatch_still_parses(void) {
  // Parsing succeeds; refusing to arm is the state machine's job, not the parser's.
  const protocol::Command cmd = parseText("{\"cmd\":\"hello\",\"v\":99}");
  ASSERT_TYPE(protocol::CommandType::Hello, cmd);
  TEST_ASSERT_EQUAL_INT(99, cmd.version);
}

void test_hello_requires_version(void) {
  ASSERT_TYPE(protocol::CommandType::Malformed, parseText("{\"cmd\":\"hello\"}"));
  ASSERT_TYPE(protocol::CommandType::Malformed, parseText("{\"cmd\":\"hello\",\"v\":\"1\"}"));
  ASSERT_TYPE(protocol::CommandType::Malformed, parseText("{\"cmd\":\"hello\",\"v\":1.5}"));
}

void test_hello_does_not_refresh_failsafe(void) {
  TEST_ASSERT_FALSE(protocol::refreshesFailsafe(protocol::CommandType::Hello));
}

void test_ping_preserves_timestamp(void) {
  // Well beyond 32 bits — a ms epoch does not fit in an int (protocol.md 3.6).
  const protocol::Command cmd = parseText("{\"cmd\":\"ping\",\"ts\":1734001234567}");
  ASSERT_TYPE(protocol::CommandType::Ping, cmd);
  TEST_ASSERT_EQUAL_INT64(1734001234567LL, cmd.ts);
}

void test_ping_requires_timestamp(void) {
  ASSERT_TYPE(protocol::CommandType::Malformed, parseText("{\"cmd\":\"ping\"}"));
  ASSERT_TYPE(protocol::CommandType::Malformed, parseText("{\"cmd\":\"ping\",\"ts\":\"x\"}"));
}

void test_ping_does_not_refresh_failsafe(void) {
  // A console that can ping but not drive is not a console that should keep the
  // motors live (protocol.md 6.2).
  TEST_ASSERT_FALSE(protocol::refreshesFailsafe(protocol::CommandType::Ping));
}

// ---------------------------------------------------------------------------------
// mast
// ---------------------------------------------------------------------------------

void test_mast_both_axes(void) {
  const protocol::Command cmd = parseText("{\"cmd\":\"mast\",\"pan\":-30,\"tilt\":15}");
  ASSERT_TYPE(protocol::CommandType::Mast, cmd);
  TEST_ASSERT_TRUE(cmd.has_pan);
  TEST_ASSERT_TRUE(cmd.has_tilt);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -30.0f, cmd.pan_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 15.0f, cmd.tilt_deg);
}

void test_mast_single_axis_holds_the_other(void) {
  // A missing axis means "hold", not "go to zero" (protocol.md 3.4).
  const protocol::Command cmd = parseText("{\"cmd\":\"mast\",\"pan\":45}");
  ASSERT_TYPE(protocol::CommandType::Mast, cmd);
  TEST_ASSERT_TRUE(cmd.has_pan);
  TEST_ASSERT_FALSE(cmd.has_tilt);
}

void test_mast_rejects_wrong_type(void) {
  const protocol::Command cmd = parseText("{\"cmd\":\"mast\",\"tilt\":\"up\"}");
  ASSERT_TYPE(protocol::CommandType::Malformed, cmd);
  ASSERT_ERROR(protocol::ParseError::BadField, cmd);
}

// ---------------------------------------------------------------------------------
// arm — retired with the manipulator in proposal Revision 2 (step S.9)
// ---------------------------------------------------------------------------------

void test_arm_is_now_an_unknown_verb(void) {
  // Dropping a command must not become a *breaking* change: §2.2 says an unrecognised
  // `cmd` is logged and ignored, never fatal. That is the whole reason removing the arm
  // needed no protocol version bump — an old console still sending `arm` is tolerated,
  // it just gets nothing. This test is what keeps that promise true.
  const protocol::Command cmd =
      parseText("{\"cmd\":\"arm\",\"joints\":[0,45,90],\"grip\":true}");
  ASSERT_TYPE(protocol::CommandType::Unknown, cmd);
  ASSERT_ERROR(protocol::ParseError::None, cmd);
  TEST_ASSERT_FALSE(protocol::refreshesFailsafe(cmd.type));
}

// ---------------------------------------------------------------------------------
// Which commands prove the console is alive
// ---------------------------------------------------------------------------------

void test_refreshes_failsafe_membership(void) {
  TEST_ASSERT_TRUE(protocol::refreshesFailsafe(protocol::CommandType::Drive));
  TEST_ASSERT_TRUE(protocol::refreshesFailsafe(protocol::CommandType::Stop));
  TEST_ASSERT_TRUE(protocol::refreshesFailsafe(protocol::CommandType::Mast));

  TEST_ASSERT_FALSE(protocol::refreshesFailsafe(protocol::CommandType::Hello));
  TEST_ASSERT_FALSE(protocol::refreshesFailsafe(protocol::CommandType::Ping));
  TEST_ASSERT_FALSE(protocol::refreshesFailsafe(protocol::CommandType::Unknown));
  TEST_ASSERT_FALSE(protocol::refreshesFailsafe(protocol::CommandType::Malformed));
}

// ---------------------------------------------------------------------------------
// Serialisation — rover to console
// ---------------------------------------------------------------------------------

void test_telemetry_is_tagged(void) {
  protocol::Telemetry telemetry;
  telemetry.battery_v = 12.5f;
  telemetry.mode = protocol::Mode::Safe;
  telemetry.rssi = -58;
  telemetry.reader = protocol::ReaderState::Ready;

  char out[protocol::kMaxFrameBytes];
  const size_t length = protocol::serializeTelemetry(telemetry, out, sizeof(out));

  TEST_ASSERT_GREATER_THAN_UINT32(0, length);
  TEST_ASSERT_EQUAL_UINT32(length, strlen(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"t\":\"tlm\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"mode\":\"safe\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"rssi\":-58"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"battery_v\":12.5"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"rfid\":\"ready\""));
}

void test_reader_state_names(void) {
  // Scene 1 gates the mission on the reader reporting green, so "not fitted" and
  // "fitted but broken" must never collapse into one value.
  TEST_ASSERT_EQUAL_STRING("absent", protocol::readerStateName(protocol::ReaderState::Absent));
  TEST_ASSERT_EQUAL_STRING("ready", protocol::readerStateName(protocol::ReaderState::Ready));
  TEST_ASSERT_EQUAL_STRING("scanning",
                           protocol::readerStateName(protocol::ReaderState::Scanning));
  TEST_ASSERT_EQUAL_STRING("fault", protocol::readerStateName(protocol::ReaderState::Fault));
}

// ---------------------------------------------------------------------------------
// tag reads — the payload of the revised mission
// ---------------------------------------------------------------------------------

void test_tag_read_frame(void) {
  protocol::TagRead read;
  read.id = "E280116060000208A1B2C3D4";
  read.rssi = -47;
  read.ts_ms = 184320;

  char out[protocol::kMaxFrameBytes];
  const size_t length = protocol::serializeTagRead(read, out, sizeof(out));

  TEST_ASSERT_GREATER_THAN_UINT32(0, length);
  TEST_ASSERT_EQUAL_STRING(
      "{\"t\":\"tag\",\"id\":\"E280116060000208A1B2C3D4\",\"rssi\":-47,\"ts\":184320}", out);
}

void test_tag_read_rejects_empty_id(void) {
  // A read the console cannot attribute to a rock is worse than a dropped one.
  protocol::TagRead read;
  read.id = "";
  read.rssi = -47;

  char out[protocol::kMaxFrameBytes];
  TEST_ASSERT_EQUAL_UINT32(0, protocol::serializeTagRead(read, out, sizeof(out)));
}

void test_tag_read_rejects_null_id(void) {
  protocol::TagRead read;
  read.id = nullptr;

  char out[protocol::kMaxFrameBytes];
  TEST_ASSERT_EQUAL_UINT32(0, protocol::serializeTagRead(read, out, sizeof(out)));
}

void test_tag_read_rejects_oversize_id(void) {
  std::string huge(protocol::kMaxTagIdChars + 1, 'A');
  protocol::TagRead read;
  read.id = huge.c_str();

  char out[protocol::kMaxFrameBytes];
  TEST_ASSERT_EQUAL_UINT32(0, protocol::serializeTagRead(read, out, sizeof(out)));
}

void test_tag_read_accepts_id_at_the_limit(void) {
  std::string limit(protocol::kMaxTagIdChars, 'A');
  protocol::TagRead read;
  read.id = limit.c_str();

  char out[protocol::kMaxFrameBytes];
  TEST_ASSERT_GREATER_THAN_UINT32(0, protocol::serializeTagRead(read, out, sizeof(out)));
}

void test_telemetry_mode_names(void) {
  TEST_ASSERT_EQUAL_STRING("safe", protocol::modeName(protocol::Mode::Safe));
  TEST_ASSERT_EQUAL_STRING("drive", protocol::modeName(protocol::Mode::Drive));
  TEST_ASSERT_EQUAL_STRING("incompatible",
                           protocol::modeName(protocol::Mode::Incompatible));
}

void test_hello_reply(void) {
  const char *caps[] = {"drive"};
  char out[protocol::kMaxFrameBytes];
  const size_t length =
      protocol::serializeHello(protocol::kVersion, "0.1.0", caps, 1, out, sizeof(out));

  TEST_ASSERT_GREATER_THAN_UINT32(0, length);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"hello\",\"v\":1,\"fw\":\"0.1.0\",\"caps\":[\"drive\"]}",
                           out);
}

void test_pong_echoes_timestamp_exactly(void) {
  char out[protocol::kMaxFrameBytes];
  const size_t length = protocol::serializePong(1734001234567LL, out, sizeof(out));

  TEST_ASSERT_GREATER_THAN_UINT32(0, length);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"pong\",\"ts\":1734001234567}", out);
}

void test_round_trip_ping_to_pong(void) {
  // The measurement is only valid if ts survives decode and re-encode untouched.
  const protocol::Command cmd = parseText("{\"cmd\":\"ping\",\"ts\":9007199254740991}");
  ASSERT_TYPE(protocol::CommandType::Ping, cmd);

  char out[protocol::kMaxFrameBytes];
  protocol::serializePong(cmd.ts, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"pong\",\"ts\":9007199254740991}", out);
}

void test_serialize_reports_zero_when_buffer_too_small(void) {
  char tiny[8];
  protocol::Telemetry telemetry;
  TEST_ASSERT_EQUAL_UINT32(0, protocol::serializeTelemetry(telemetry, tiny, sizeof(tiny)));
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_drive_valid);
  RUN_TEST(test_drive_accepts_integer_throttle);
  RUN_TEST(test_drive_clamps_out_of_range);
  RUN_TEST(test_drive_missing_fields_default_to_zero);
  RUN_TEST(test_drive_one_side_only);
  RUN_TEST(test_drive_rejects_string_throttle);
  RUN_TEST(test_drive_rejects_bool_throttle);
  RUN_TEST(test_drive_rejects_object_throttle);
  RUN_TEST(test_drive_null_field_is_absent_not_invalid);
  RUN_TEST(test_drive_ignores_unknown_fields);

  RUN_TEST(test_malformed_json);
  RUN_TEST(test_empty_frame);
  RUN_TEST(test_null_pointer);
  RUN_TEST(test_json_array_is_not_a_command);
  RUN_TEST(test_missing_cmd);
  RUN_TEST(test_cmd_must_be_a_string);
  RUN_TEST(test_unknown_cmd_is_ignored_not_malformed);
  RUN_TEST(test_oversize_frame_rejected);
  RUN_TEST(test_frame_at_size_limit_is_accepted);

  RUN_TEST(test_stop);
  RUN_TEST(test_hello_valid);
  RUN_TEST(test_hello_version_mismatch_still_parses);
  RUN_TEST(test_hello_requires_version);
  RUN_TEST(test_hello_does_not_refresh_failsafe);
  RUN_TEST(test_ping_preserves_timestamp);
  RUN_TEST(test_ping_requires_timestamp);
  RUN_TEST(test_ping_does_not_refresh_failsafe);

  RUN_TEST(test_mast_both_axes);
  RUN_TEST(test_mast_single_axis_holds_the_other);
  RUN_TEST(test_mast_rejects_wrong_type);

  RUN_TEST(test_arm_is_now_an_unknown_verb);

  RUN_TEST(test_refreshes_failsafe_membership);

  RUN_TEST(test_telemetry_is_tagged);
  RUN_TEST(test_telemetry_mode_names);
  RUN_TEST(test_reader_state_names);
  RUN_TEST(test_tag_read_frame);
  RUN_TEST(test_tag_read_rejects_empty_id);
  RUN_TEST(test_tag_read_rejects_null_id);
  RUN_TEST(test_tag_read_rejects_oversize_id);
  RUN_TEST(test_tag_read_accepts_id_at_the_limit);
  RUN_TEST(test_hello_reply);
  RUN_TEST(test_pong_echoes_timestamp_exactly);
  RUN_TEST(test_round_trip_ping_to_pong);
  RUN_TEST(test_serialize_reports_zero_when_buffer_too_small);

  return UNITY_END();
}
