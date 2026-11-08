// Host tests for lib/RcMap — the RC-car drivetrain's demand -> pulse mapping
// (docs/esc-wiring.md, docs/protocol.md 3.2.1).
//
//   python -m platformio test -e native
//
// A wrong sign here drives the car the opposite way to the stick; a missing cap lets a
// 75 km/h car loose in a 3 m arena. Both are cheaper to catch on the PC.

#include <math.h>
#include <unity.h>

#include <RcMap.h>

namespace {

// The ESC as config.h has it on 2026-11-07: car-ESC convention, capped at 75% of the
// original top speed both ways.
const rcmap::ThrottleMap kEsc{1500, 2000, 1000, 0.05f, 0.75f, 0.75f, 1500, 1500};

// The same ESC uncapped, so the mapping tests do not depend on the cap.
const rcmap::ThrottleMap kFull{1500, 2000, 1000, 0.05f, 1.0f, 1.0f, 1500, 1500};

// A plain +/-300 us steering map about 1500. Fixed here so the mapping tests do not move
// when config.h's measured end stops do.
const rcmap::SteerMap kSteer{1500, 1200, 1800};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// ---- Throttle -----------------------------------------------------------------------

void test_zero_throttle_is_neutral(void) {
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::throttlePulseUs(kEsc, 0.0f));
}

void test_deadband_is_neutral(void) {
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::throttlePulseUs(kFull, 0.04f));
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::throttlePulseUs(kFull, -0.04f));
  // At the edge it moves.
  TEST_ASSERT_EQUAL_UINT16(1525, rcmap::throttlePulseUs(kFull, 0.05f));
}

void test_forward_is_longer_reverse_shorter(void) {
  TEST_ASSERT_EQUAL_UINT16(2000, rcmap::throttlePulseUs(kFull, 1.0f));
  TEST_ASSERT_EQUAL_UINT16(1750, rcmap::throttlePulseUs(kFull, 0.5f));
  TEST_ASSERT_EQUAL_UINT16(1000, rcmap::throttlePulseUs(kFull, -1.0f));
  TEST_ASSERT_EQUAL_UINT16(1250, rcmap::throttlePulseUs(kFull, -0.5f));
}

void test_cap_scales_rather_than_clips(void) {
  // Full stick reaches the cap and no further...
  TEST_ASSERT_EQUAL_UINT16(1875, rcmap::throttlePulseUs(kEsc, 1.0f));
  TEST_ASSERT_EQUAL_UINT16(1125, rcmap::throttlePulseUs(kEsc, -1.0f));
  // ...and half stick is half the cap, so the console's speed modes still mean something.
  TEST_ASSERT_EQUAL_UINT16(1688, rcmap::throttlePulseUs(kEsc, 0.5f));
}

void test_out_of_range_throttle_clamps_to_the_cap(void) {
  TEST_ASSERT_EQUAL_UINT16(1875, rcmap::throttlePulseUs(kEsc, 5.0f));
  TEST_ASSERT_EQUAL_UINT16(1125, rcmap::throttlePulseUs(kEsc, -5.0f));
}

void test_non_finite_throttle_is_neutral(void) {
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::throttlePulseUs(kEsc, NAN));
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::throttlePulseUs(kEsc, INFINITY));
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::throttlePulseUs(kEsc, -INFINITY));
}

void test_zero_cap_never_moves(void) {
  const rcmap::ThrottleMap locked{1500, 2000, 1000, 0.05f, 0.0f, 0.0f, 1500, 1500};
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::throttlePulseUs(locked, 1.0f));
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::throttlePulseUs(locked, -1.0f));
}

void test_cap_above_one_is_treated_as_one(void) {
  const rcmap::ThrottleMap wild{1500, 2000, 1000, 0.05f, 3.0f, 3.0f, 1500, 1500};
  TEST_ASSERT_EQUAL_UINT16(2000, rcmap::throttlePulseUs(wild, 1.0f));
  TEST_ASSERT_EQUAL_UINT16(1000, rcmap::throttlePulseUs(wild, -1.0f));
}

void test_swapped_endpoints_reverse_the_motor(void) {
  // The fix config.h prescribes when FORWARD drives backwards.
  const rcmap::ThrottleMap swapped{1500, 1000, 2000, 0.05f, 0.75f, 0.75f, 1500, 1500};
  TEST_ASSERT_EQUAL_UINT16(1125, rcmap::throttlePulseUs(swapped, 1.0f));
  TEST_ASSERT_EQUAL_UINT16(1875, rcmap::throttlePulseUs(swapped, -1.0f));
}

// ---- Motor start point (the minimum speed) ------------------------------------------

namespace {
// Capped at 75%, motor starting 60 us either side of neutral — config.h's starting guess.
const rcmap::ThrottleMap kStart{1500, 2000, 1000, 0.05f, 0.75f, 0.75f, 1560, 1440};
}  // namespace

void test_slowest_demand_is_at_least_the_start(void) {
  // The whole point: the first demand past the deadband already turns the motor.
  TEST_ASSERT_TRUE(rcmap::throttlePulseUs(kStart, 0.05f) >= 1560);
  TEST_ASSERT_TRUE(rcmap::throttlePulseUs(kStart, -0.05f) <= 1440);
  // 0.16 of the way (the start) + 0.84 x 0.05 = 0.202 of 375 us.
  TEST_ASSERT_EQUAL_UINT16(1576, rcmap::throttlePulseUs(kStart, 0.05f));
  TEST_ASSERT_EQUAL_UINT16(1424, rcmap::throttlePulseUs(kStart, -0.05f));
}

void test_start_does_not_touch_the_deadband_or_the_cap(void) {
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::throttlePulseUs(kStart, 0.0f));
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::throttlePulseUs(kStart, 0.04f));
  TEST_ASSERT_EQUAL_UINT16(1875, rcmap::throttlePulseUs(kStart, 1.0f));
  TEST_ASSERT_EQUAL_UINT16(1125, rcmap::throttlePulseUs(kStart, -1.0f));
}

void test_start_keeps_throttle_monotonic(void) {
  uint16_t previous = rcmap::throttlePulseUs(kStart, 0.05f);
  for (int i = 6; i <= 100; ++i) {
    const uint16_t pulse = rcmap::throttlePulseUs(kStart, i / 100.0f);
    TEST_ASSERT_TRUE(pulse > previous);
    previous = pulse;
  }
}

void test_start_beyond_the_cap_is_the_cap(void) {
  // The cap always wins: a start of 1950 with a 1875 cap never exceeds 1875.
  const rcmap::ThrottleMap over{1500, 2000, 1000, 0.05f, 0.75f, 0.75f, 1950, 1050};
  TEST_ASSERT_EQUAL_UINT16(1875, rcmap::throttlePulseUs(over, 0.05f));
  TEST_ASSERT_EQUAL_UINT16(1875, rcmap::throttlePulseUs(over, 1.0f));
  TEST_ASSERT_EQUAL_UINT16(1125, rcmap::throttlePulseUs(over, -0.05f));
}

void test_start_on_the_wrong_side_is_no_offset(void) {
  const rcmap::ThrottleMap wrong{1500, 2000, 1000, 0.05f, 0.75f, 0.75f, 1450, 1550};
  TEST_ASSERT_EQUAL_UINT16(1688, rcmap::throttlePulseUs(wrong, 0.5f));
  TEST_ASSERT_EQUAL_UINT16(1313, rcmap::throttlePulseUs(wrong, -0.5f));  // 1312.5 rounds away
}

void test_start_follows_swapped_endpoints(void) {
  // FORWARD fixed by swapping endpoints: the starts swap with them.
  const rcmap::ThrottleMap swapped{1500, 1000, 2000, 0.05f, 0.75f, 0.75f, 1440, 1560};
  TEST_ASSERT_EQUAL_UINT16(1424, rcmap::throttlePulseUs(swapped, 0.05f));
  TEST_ASSERT_EQUAL_UINT16(1576, rcmap::throttlePulseUs(swapped, -0.05f));
}

// ---- Steering -----------------------------------------------------------------------

void test_zero_steer_is_centre(void) {
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::steerPulseUs(kSteer, 0.0f));
}

void test_steering_is_fine_grained(void) {
  // The console sends steer at 0.01 resolution. Every one of those steps must reach the
  // servo as its own pulse — no rounding back onto a few positions, which is what the
  // old three-position axle did. 201 demands, 201 distinct, monotonic pulses... at least
  // until the 600 us span runs out of whole microseconds, which it does not at 0.01.
  uint16_t previous = rcmap::steerPulseUs(kSteer, -1.0f);
  for (int i = -99; i <= 100; ++i) {
    const uint16_t pulse = rcmap::steerPulseUs(kSteer, i / 100.0f);
    TEST_ASSERT_TRUE(pulse > previous);
    previous = pulse;
  }
}

void test_steering_is_proportional(void) {
  TEST_ASSERT_EQUAL_UINT16(1200, rcmap::steerPulseUs(kSteer, -1.0f));
  TEST_ASSERT_EQUAL_UINT16(1350, rcmap::steerPulseUs(kSteer, -0.5f));
  TEST_ASSERT_EQUAL_UINT16(1590, rcmap::steerPulseUs(kSteer, 0.3f));
  TEST_ASSERT_EQUAL_UINT16(1800, rcmap::steerPulseUs(kSteer, 1.0f));
}

void test_steering_clamps_to_the_end_stops(void) {
  TEST_ASSERT_EQUAL_UINT16(1200, rcmap::steerPulseUs(kSteer, -4.0f));
  TEST_ASSERT_EQUAL_UINT16(1800, rcmap::steerPulseUs(kSteer, 4.0f));
}

void test_non_finite_steer_is_centre(void) {
  TEST_ASSERT_EQUAL_UINT16(1500, rcmap::steerPulseUs(kSteer, NAN));
}

void test_asymmetric_and_trimmed_steering(void) {
  // Real linkages are rarely symmetric, and the trim moves centre off 1500.
  const rcmap::SteerMap real{1520, 1150, 1840};
  TEST_ASSERT_EQUAL_UINT16(1520, rcmap::steerPulseUs(real, 0.0f));
  TEST_ASSERT_EQUAL_UINT16(1150, rcmap::steerPulseUs(real, -1.0f));
  TEST_ASSERT_EQUAL_UINT16(1840, rcmap::steerPulseUs(real, 1.0f));
}

void test_swapped_steering_endpoints_reverse_it(void) {
  const rcmap::SteerMap swapped{1500, 1800, 1200};
  TEST_ASSERT_EQUAL_UINT16(1800, rcmap::steerPulseUs(swapped, -1.0f));
  TEST_ASSERT_EQUAL_UINT16(1200, rcmap::steerPulseUs(swapped, 1.0f));
}

// ---- Duty ---------------------------------------------------------------------------

void test_duty_counts_at_50hz_16bit(void) {
  // 1500 us of a 20 ms frame at 2^16 counts = 4915.2 -> 4915.
  TEST_ASSERT_EQUAL_UINT32(4915, rcmap::dutyCounts(1500, 50, 16));
  TEST_ASSERT_EQUAL_UINT32(3277, rcmap::dutyCounts(1000, 50, 16));
  TEST_ASSERT_EQUAL_UINT32(6554, rcmap::dutyCounts(2000, 50, 16));
}

void test_duty_never_exceeds_full_scale(void) {
  TEST_ASSERT_EQUAL_UINT32(65535, rcmap::dutyCounts(60000, 50, 16));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_zero_throttle_is_neutral);
  RUN_TEST(test_deadband_is_neutral);
  RUN_TEST(test_forward_is_longer_reverse_shorter);
  RUN_TEST(test_cap_scales_rather_than_clips);
  RUN_TEST(test_out_of_range_throttle_clamps_to_the_cap);
  RUN_TEST(test_non_finite_throttle_is_neutral);
  RUN_TEST(test_zero_cap_never_moves);
  RUN_TEST(test_cap_above_one_is_treated_as_one);
  RUN_TEST(test_swapped_endpoints_reverse_the_motor);
  RUN_TEST(test_slowest_demand_is_at_least_the_start);
  RUN_TEST(test_start_does_not_touch_the_deadband_or_the_cap);
  RUN_TEST(test_start_keeps_throttle_monotonic);
  RUN_TEST(test_start_beyond_the_cap_is_the_cap);
  RUN_TEST(test_start_on_the_wrong_side_is_no_offset);
  RUN_TEST(test_start_follows_swapped_endpoints);
  RUN_TEST(test_zero_steer_is_centre);
  RUN_TEST(test_steering_is_fine_grained);
  RUN_TEST(test_steering_is_proportional);
  RUN_TEST(test_steering_clamps_to_the_end_stops);
  RUN_TEST(test_non_finite_steer_is_centre);
  RUN_TEST(test_asymmetric_and_trimmed_steering);
  RUN_TEST(test_swapped_steering_endpoints_reverse_it);
  RUN_TEST(test_duty_counts_at_50hz_16bit);
  RUN_TEST(test_duty_never_exceeds_full_scale);
  return UNITY_END();
}
