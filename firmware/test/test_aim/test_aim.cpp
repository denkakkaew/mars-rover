// Host tests for lib/Aim — the camera pan/tilt head's travel limits and angle-to-pulse
// mapping (docs/servo-wiring.md, docs/protocol.md 3.4).
//
//   python -m platformio test -e native
//
// A wrong limit here drives the camera into its own cable; a wrong sign points it the
// opposite way to the button the operator pressed. Both are cheaper to catch on the PC.

#include <math.h>
#include <unity.h>

#include <Aim.h>

namespace {

// A plain forward-mounted axis. The mapping tests use this, so they stay put when the
// real head's servos get re-mounted and change sign.
const aim::Axis kFwd{-45.0f, 45.0f, 1520, 10.0f, 920, 2120};

// The real head, as config.h has it after the servos were re-positioned on 2026-09-20:
// **both axes reversed**, so protocol-positive is the shorter pulse on pan and the longer
// one on tilt. Restated here so a change there is a deliberate change in these tests too.
const aim::Axis kPan{-45.0f, 45.0f, 1520, -10.0f, 920, 2120};
const aim::Axis kTilt{-60.0f, 45.0f, 1520, 10.0f, 920, 2120};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_centre_is_the_centre_pulse(void) {
  TEST_ASSERT_EQUAL_UINT16(1520, aim::pulseUs(kFwd, 0.0f));
  // Centre is centre whichever way the servo is mounted.
  TEST_ASSERT_EQUAL_UINT16(1520, aim::pulseUs(kPan, 0.0f));
  TEST_ASSERT_EQUAL_UINT16(1520, aim::pulseUs(kTilt, 0.0f));
}

void test_positive_is_longer_pulse(void) {
  TEST_ASSERT_EQUAL_UINT16(1820, aim::pulseUs(kFwd, 30.0f));
  TEST_ASSERT_EQUAL_UINT16(1220, aim::pulseUs(kFwd, -30.0f));
}

void test_angle_clamped_to_travel(void) {
  // 90 degrees is inside the protocol's range but past this head's travel.
  TEST_ASSERT_EQUAL_UINT16(1970, aim::pulseUs(kFwd, 90.0f));
  TEST_ASSERT_EQUAL_UINT16(1070, aim::pulseUs(kFwd, -90.0f));
  // Tilt's full down is the servo's shortest pulse on the current mounting, and goes no
  // further however far it is asked.
  TEST_ASSERT_EQUAL_UINT16(920, aim::pulseUs(kTilt, -60.0f));
  TEST_ASSERT_EQUAL_UINT16(920, aim::pulseUs(kTilt, -90.0f));
  TEST_ASSERT_EQUAL_UINT16(1970, aim::pulseUs(kTilt, 45.0f));
}

void test_head_axes_are_both_reversed(void) {
  // The mounting as it stands: protocol-positive pan (right) is the SHORTER pulse, and
  // protocol-positive tilt (up) the longer one. If a re-mount flips either of these, the
  // sign in config.h flips with it — and this test is the one that should fail first.
  TEST_ASSERT_TRUE(aim::pulseUs(kPan, 30.0f) < aim::pulseUs(kPan, -30.0f));
  TEST_ASSERT_TRUE(aim::pulseUs(kTilt, 30.0f) > aim::pulseUs(kTilt, -30.0f));
}

void test_pulse_clamped_to_electrical_limits(void) {
  // A scale mis-set to 30 us/deg would ask for 2870 us at the travel limit. The servo's
  // electrical range still wins.
  const aim::Axis steep{-45.0f, 45.0f, 1520, 30.0f, 1000, 2000};
  TEST_ASSERT_EQUAL_UINT16(2000, aim::pulseUs(steep, 45.0f));
  TEST_ASSERT_EQUAL_UINT16(1000, aim::pulseUs(steep, -45.0f));
}

void test_negative_scale_reverses_the_axis(void) {
  // A servo mounted the other way round: protocol "right" must still turn right, which
  // for this servo means a shorter pulse.
  const aim::Axis reversed{-45.0f, 45.0f, 1520, -10.0f, 1000, 2000};
  TEST_ASSERT_EQUAL_UINT16(1220, aim::pulseUs(reversed, 30.0f));
  TEST_ASSERT_EQUAL_UINT16(1820, aim::pulseUs(reversed, -30.0f));
}

void test_non_finite_angle_holds(void) {
  TEST_ASSERT_EQUAL_FLOAT(12.0f, aim::clampAngle(kFwd, NAN, 12.0f));
  TEST_ASSERT_EQUAL_FLOAT(12.0f, aim::clampAngle(kFwd, INFINITY, 12.0f));
}

void test_duty_counts_at_50hz_16bit(void) {
  // 0.305 us per count: the figures in docs/servo-wiring.md.
  TEST_ASSERT_EQUAL_UINT32(3277, aim::dutyCounts(1000, 50, 16));
  TEST_ASSERT_EQUAL_UINT32(4981, aim::dutyCounts(1520, 50, 16));
  TEST_ASSERT_EQUAL_UINT32(6554, aim::dutyCounts(2000, 50, 16));
}

void test_duty_never_exceeds_full_scale(void) {
  // A pulse longer than the period cannot be represented; it saturates rather than wraps.
  TEST_ASSERT_EQUAL_UINT32(65535, aim::dutyCounts(30000, 50, 16));
}

void test_apply_both_axes(void) {
  const aim::Pose next = aim::apply(aim::Pose{}, true, 20.0f, true, -10.0f, kPan, kTilt);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, next.pan_deg);
  TEST_ASSERT_EQUAL_FLOAT(-10.0f, next.tilt_deg);
}

void test_apply_missing_axis_holds(void) {
  aim::Pose current;
  current.pan_deg = 15.0f;
  current.tilt_deg = 25.0f;
  const aim::Pose pan_only = aim::apply(current, true, -5.0f, false, 0.0f, kPan, kTilt);
  TEST_ASSERT_EQUAL_FLOAT(-5.0f, pan_only.pan_deg);
  TEST_ASSERT_EQUAL_FLOAT(25.0f, pan_only.tilt_deg);

  const aim::Pose neither = aim::apply(current, false, 0.0f, false, 0.0f, kPan, kTilt);
  TEST_ASSERT_EQUAL_FLOAT(15.0f, neither.pan_deg);
  TEST_ASSERT_EQUAL_FLOAT(25.0f, neither.tilt_deg);
}

void test_apply_clamps_each_axis_to_its_own_travel(void) {
  const aim::Pose next = aim::apply(aim::Pose{}, true, 80.0f, true, -80.0f, kPan, kTilt);
  TEST_ASSERT_EQUAL_FLOAT(45.0f, next.pan_deg);
  TEST_ASSERT_EQUAL_FLOAT(-60.0f, next.tilt_deg);
}

void test_apply_is_idempotent(void) {
  // protocol.md 3.4: absolute, so sending the same frame twice changes nothing.
  const aim::Pose once = aim::apply(aim::Pose{}, true, 10.0f, true, 5.0f, kPan, kTilt);
  const aim::Pose twice = aim::apply(once, true, 10.0f, true, 5.0f, kPan, kTilt);
  TEST_ASSERT_EQUAL_FLOAT(once.pan_deg, twice.pan_deg);
  TEST_ASSERT_EQUAL_FLOAT(once.tilt_deg, twice.tilt_deg);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_centre_is_the_centre_pulse);
  RUN_TEST(test_positive_is_longer_pulse);
  RUN_TEST(test_angle_clamped_to_travel);
  RUN_TEST(test_head_axes_are_both_reversed);
  RUN_TEST(test_pulse_clamped_to_electrical_limits);
  RUN_TEST(test_negative_scale_reverses_the_axis);
  RUN_TEST(test_non_finite_angle_holds);
  RUN_TEST(test_duty_counts_at_50hz_16bit);
  RUN_TEST(test_duty_never_exceeds_full_scale);
  RUN_TEST(test_apply_both_axes);
  RUN_TEST(test_apply_missing_axis_holds);
  RUN_TEST(test_apply_clamps_each_axis_to_its_own_travel);
  RUN_TEST(test_apply_is_idempotent);
  return UNITY_END();
}
