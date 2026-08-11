// Host tests for lib/Safety â€” the failsafe contract in docs/protocol.md section 6.
//
//   python -m platformio test -e native
//
// This is the logic that stops the rover driving into the glass when the link degrades
// (risk R1). Step 1.6 proves it on real hardware and is the one gate the plan does not
// negotiate past; these tests are what make that step a confirmation rather than a
// discovery.

#include <unity.h>

#include <Safety.h>

namespace {

constexpr uint32_t kTimeout = 500;

#define ASSERT_STATE(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

/// A connected, handshaken rover that was commanded at t=1000, evaluated at `now`.
safety::Inputs driving(uint32_t now) {
  safety::Inputs in;
  in.connected = true;
  in.handshake_ok = true;
  in.commanded_since_connect = true;
  in.last_command_ms = 1000;
  in.now_ms = now;
  in.timeout_ms = kTimeout;
  return in;
}

/// A connected and handshaken rover, ready to be armed by a command.
safety::Failsafe ready() {
  safety::Failsafe failsafe(kTimeout);
  failsafe.onConnect();
  failsafe.setHandshakeOk(true);
  return failsafe;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------------
// The pure state machine
// ---------------------------------------------------------------------------------

void test_boot_is_safe(void) {
  const safety::Inputs in;  // nothing connected, nothing commanded
  ASSERT_STATE(safety::State::Safe, safety::evaluate(in));
}

void test_connected_but_uncommanded_is_safe(void) {
  safety::Inputs in;
  in.connected = true;
  in.handshake_ok = true;
  in.now_ms = 10000;
  ASSERT_STATE(safety::State::Safe, safety::evaluate(in));
}

void test_unidentified_console_never_arms(void) {
  // A console that drives without ever saying hello gets nothing, however valid its
  // commands look (protocol.md 5).
  safety::Inputs in = driving(1000);
  in.handshake_ok = false;
  ASSERT_STATE(safety::State::Safe, safety::evaluate(in));
}

void test_arms_on_first_command(void) {
  ASSERT_STATE(safety::State::Armed, safety::evaluate(driving(1000)));
}

void test_armed_just_before_the_boundary(void) {
  ASSERT_STATE(safety::State::Armed, safety::evaluate(driving(1000 + kTimeout - 1)));
}

void test_trips_exactly_at_the_boundary(void) {
  // 500 ms of silence *is* a timeout, not 501 (protocol.md 6.2). This is the test the
  // step S.4 verification deliberately breaks to prove the suite bites.
  ASSERT_STATE(safety::State::Safe, safety::evaluate(driving(1000 + kTimeout)));
}

void test_trips_well_past_the_boundary(void) {
  ASSERT_STATE(safety::State::Safe, safety::evaluate(driving(1000 + kTimeout * 10)));
}

void test_disconnect_is_safe_even_with_a_recent_command(void) {
  safety::Inputs in = driving(1000);
  in.connected = false;
  ASSERT_STATE(safety::State::Safe, safety::evaluate(in));
}

void test_incompatible_outranks_a_valid_command_stream(void) {
  safety::Inputs in = driving(1000);
  in.peer_compatible = false;
  ASSERT_STATE(safety::State::Incompatible, safety::evaluate(in));
}

void test_incompatible_outranks_disconnect(void) {
  safety::Inputs in = driving(1000);
  in.peer_compatible = false;
  in.connected = false;
  ASSERT_STATE(safety::State::Incompatible, safety::evaluate(in));
}

void test_evaluate_is_pure(void) {
  const safety::Inputs in = driving(1200);
  const safety::State first = safety::evaluate(in);
  const safety::State second = safety::evaluate(in);
  ASSERT_STATE(first, second);
}

void test_zero_timeout_never_arms(void) {
  safety::Inputs in = driving(1000);
  in.timeout_ms = 0;
  ASSERT_STATE(safety::State::Safe, safety::evaluate(in));
}

// ---------------------------------------------------------------------------------
// millis() wraparound â€” the rover runs for weeks between reflashes
// ---------------------------------------------------------------------------------

void test_armed_across_millis_wraparound(void) {
  // Commanded at 0xFFFFFF00, now 100 ms past the wrap: 356 ms elapsed, still armed.
  safety::Inputs in;
  in.connected = true;
  in.handshake_ok = true;
  in.commanded_since_connect = true;
  in.timeout_ms = kTimeout;
  in.last_command_ms = 0xFFFFFF00u;
  in.now_ms = 100u;
  ASSERT_STATE(safety::State::Armed, safety::evaluate(in));
}

void test_trips_across_millis_wraparound(void) {
  // Exactly 500 ms elapsed, straddling the wrap.
  safety::Inputs in;
  in.connected = true;
  in.handshake_ok = true;
  in.commanded_since_connect = true;
  in.timeout_ms = kTimeout;
  in.last_command_ms = 0xFFFFFF00u;
  in.now_ms = 244u;
  ASSERT_STATE(safety::State::Safe, safety::evaluate(in));
}

void test_timed_out_helper_directly(void) {
  TEST_ASSERT_FALSE(safety::timedOut(1000, 1000, kTimeout));
  TEST_ASSERT_FALSE(safety::timedOut(1000, 1499, kTimeout));
  TEST_ASSERT_TRUE(safety::timedOut(1000, 1500, kTimeout));
  TEST_ASSERT_TRUE(safety::timedOut(0xFFFFFF00u, 244u, kTimeout));
}

// ---------------------------------------------------------------------------------
// The Failsafe holder â€” event sequences
// ---------------------------------------------------------------------------------

void test_holder_starts_safe(void) {
  const safety::Failsafe failsafe(kTimeout);
  ASSERT_STATE(safety::State::Safe, failsafe.state(0));
}

void test_holder_connect_alone_does_not_arm(void) {
  safety::Failsafe failsafe = ready();
  ASSERT_STATE(safety::State::Safe, failsafe.state(1000));
}

void test_holder_arms_then_times_out(void) {
  safety::Failsafe failsafe = ready();
  failsafe.onCommand(1000);
  ASSERT_STATE(safety::State::Armed, failsafe.state(1000));
  ASSERT_STATE(safety::State::Armed, failsafe.state(1499));
  ASSERT_STATE(safety::State::Safe, failsafe.state(1500));
}

void test_holder_re_arms_on_a_fresh_command(void) {
  safety::Failsafe failsafe = ready();
  failsafe.onCommand(1000);
  ASSERT_STATE(safety::State::Safe, failsafe.state(2000));  // tripped

  failsafe.onCommand(2000);
  ASSERT_STATE(safety::State::Armed, failsafe.state(2000));
}

void test_holder_repeated_commands_hold_the_arm(void) {
  // The 150 ms console repeat of protocol.md 6.5, simulated: the rover stays armed
  // across a sustained hold and never trips mid-press.
  safety::Failsafe failsafe = ready();
  for (uint32_t t = 1000; t <= 5000; t += 150) {
    failsafe.onCommand(t);
    ASSERT_STATE(safety::State::Armed, failsafe.state(t + 149));
  }
}

void test_holder_survives_two_dropped_repeats(void) {
  // 150 ms cadence absorbs two consecutive losses (450 ms < 500 ms) â€” the margin the
  // repeat rate was chosen for.
  safety::Failsafe failsafe = ready();
  failsafe.onCommand(1000);
  ASSERT_STATE(safety::State::Armed, failsafe.state(1450));  // two repeats lost
  ASSERT_STATE(safety::State::Safe, failsafe.state(1600));   // a third: gone
}

void test_holder_disconnect_trips_immediately(void) {
  safety::Failsafe failsafe = ready();
  failsafe.onCommand(1000);
  ASSERT_STATE(safety::State::Armed, failsafe.state(1000));

  failsafe.onDisconnect();
  ASSERT_STATE(safety::State::Safe, failsafe.state(1000));
}

void test_holder_reconnect_inside_the_window_does_not_re_arm(void) {
  // The lurch-on-reconnect case, and the reason the armed flag is scoped to the
  // connection (protocol.md 6.3). Reconnecting 10 ms after the last command must not
  // resume driving with nobody touching the screen.
  safety::Failsafe failsafe = ready();
  failsafe.onCommand(1000);

  failsafe.onDisconnect();
  failsafe.onConnect();
  failsafe.setHandshakeOk(true);
  ASSERT_STATE(safety::State::Safe, failsafe.state(1010));

  // ...and only a fresh command brings it back.
  failsafe.onCommand(1010);
  ASSERT_STATE(safety::State::Armed, failsafe.state(1010));
}

void test_holder_handshake_is_required_before_arming(void) {
  // Connected and commanded, but the console never identified itself.
  safety::Failsafe failsafe(kTimeout);
  failsafe.onConnect();
  failsafe.onCommand(1000);
  ASSERT_STATE(safety::State::Safe, failsafe.state(1000));

  failsafe.setHandshakeOk(true);
  failsafe.onCommand(1000);
  ASSERT_STATE(safety::State::Armed, failsafe.state(1000));
}

void test_holder_reconnect_requires_a_fresh_handshake(void) {
  // Every new connection identifies itself again; the previous one's handshake does
  // not carry over.
  safety::Failsafe failsafe = ready();
  failsafe.onCommand(1000);
  ASSERT_STATE(safety::State::Armed, failsafe.state(1000));

  failsafe.onDisconnect();
  failsafe.onConnect();
  failsafe.onCommand(1010);
  ASSERT_STATE(safety::State::Safe, failsafe.state(1010));
}

void test_holder_version_mismatch_refuses_to_arm(void) {
  safety::Failsafe failsafe = ready();
  failsafe.setPeerCompatible(false);

  failsafe.onCommand(1000);
  ASSERT_STATE(safety::State::Incompatible, failsafe.state(1000));
}

void test_holder_mismatch_survives_commands_and_time(void) {
  // Safe mode clears on the next command; an incompatible peer does not. Neither
  // driving at it nor waiting it out changes anything (protocol.md 5).
  safety::Failsafe failsafe = ready();
  failsafe.setPeerCompatible(false);

  failsafe.onCommand(1000);
  ASSERT_STATE(safety::State::Incompatible, failsafe.state(1000));
  ASSERT_STATE(safety::State::Incompatible, failsafe.state(99000));

  failsafe.setPeerCompatible(true);
  ASSERT_STATE(safety::State::Armed, failsafe.state(1000));
}

void test_holder_a_new_connection_clears_a_mismatch(void) {
  // A different console must be able to take over without power-cycling the rover.
  // This is not a loophole: the new connection still has to handshake before anything
  // moves, which test_holder_reconnect_requires_a_fresh_handshake covers.
  safety::Failsafe failsafe = ready();
  failsafe.setPeerCompatible(false);
  ASSERT_STATE(safety::State::Incompatible, failsafe.state(1000));

  failsafe.onDisconnect();
  failsafe.onConnect();
  ASSERT_STATE(safety::State::Safe, failsafe.state(1000));

  failsafe.setHandshakeOk(true);
  failsafe.onCommand(1000);
  ASSERT_STATE(safety::State::Armed, failsafe.state(1000));
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_boot_is_safe);
  RUN_TEST(test_connected_but_uncommanded_is_safe);
  RUN_TEST(test_unidentified_console_never_arms);
  RUN_TEST(test_arms_on_first_command);
  RUN_TEST(test_armed_just_before_the_boundary);
  RUN_TEST(test_trips_exactly_at_the_boundary);
  RUN_TEST(test_trips_well_past_the_boundary);
  RUN_TEST(test_disconnect_is_safe_even_with_a_recent_command);
  RUN_TEST(test_incompatible_outranks_a_valid_command_stream);
  RUN_TEST(test_incompatible_outranks_disconnect);
  RUN_TEST(test_evaluate_is_pure);
  RUN_TEST(test_zero_timeout_never_arms);

  RUN_TEST(test_armed_across_millis_wraparound);
  RUN_TEST(test_trips_across_millis_wraparound);
  RUN_TEST(test_timed_out_helper_directly);

  RUN_TEST(test_holder_starts_safe);
  RUN_TEST(test_holder_connect_alone_does_not_arm);
  RUN_TEST(test_holder_arms_then_times_out);
  RUN_TEST(test_holder_re_arms_on_a_fresh_command);
  RUN_TEST(test_holder_repeated_commands_hold_the_arm);
  RUN_TEST(test_holder_survives_two_dropped_repeats);
  RUN_TEST(test_holder_disconnect_trips_immediately);
  RUN_TEST(test_holder_reconnect_inside_the_window_does_not_re_arm);
  RUN_TEST(test_holder_handshake_is_required_before_arming);
  RUN_TEST(test_holder_reconnect_requires_a_fresh_handshake);
  RUN_TEST(test_holder_version_mismatch_refuses_to_arm);
  RUN_TEST(test_holder_mismatch_survives_commands_and_time);
  RUN_TEST(test_holder_a_new_connection_clears_a_mismatch);

  return UNITY_END();
}

