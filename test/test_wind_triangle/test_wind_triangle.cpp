// test_wind_triangle.cpp
//
// Unit tests for onspeed::aero::ComputeWind.
//
// Conventions:
//   - All velocities in m/s; 1 kt = 0.514444 m/s.
//   - yawDeg / pitchDeg in degrees, body-frame Tait-Bryan (3-2-1).
//   - GnssVelNed is true-north NED; positive-down on the third component.
//   - Returned windDirDeg is the "from" direction in [0, 360).
//   - Returned windVerticalMps is positive for an updraft.

#include <unity.h>
#include <aero/WindTriangle.h>

#include <cmath>

using onspeed::aero::ComputeWind;
using onspeed::aero::WindNed;

void setUp(void) {}
void tearDown(void) {}

static constexpr float kKtToMps = 0.5144444f;

// -----------------------------------------------------------------------------

// 1. Pure headwind from north. Aircraft on heading 0 at 100 KIAS, GPS reports
//    80 kt ground speed northbound -> 20 kt headwind from 000.
void test_pure_headwind(void)
{
    auto w = ComputeWind(80.0f * kKtToMps, 0.0f, 0.0f,
                         /*yaw*/0.0f, /*pitch*/0.0f,
                         100.0f * kKtToMps);
    TEST_ASSERT_TRUE(w.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f * kKtToMps, w->windSpeedMps);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, w->windDirDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, w->windVerticalMps);
}

// 2. Pure tailwind. Heading 0, TAS 100 kt, ground speed 120 kt northbound ->
//    20 kt tailwind from south (180).
void test_pure_tailwind(void)
{
    auto w = ComputeWind(120.0f * kKtToMps, 0.0f, 0.0f,
                         0.0f, 0.0f,
                         100.0f * kKtToMps);
    TEST_ASSERT_TRUE(w.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f * kKtToMps, w->windSpeedMps);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 180.0f, w->windDirDeg);
}

// 3. Pure crosswind. Aircraft heading 0 at 100 kt TAS, GPS shows 100 kt N and
//    100 kt E -> wind = ground - air = (0, 100, 0) kt = wind going east =
//    wind from 270 (west), 100 kt.
void test_pure_crosswind_from_west(void)
{
    auto w = ComputeWind(100.0f * kKtToMps, 100.0f * kKtToMps, 0.0f,
                         0.0f, 0.0f,
                         100.0f * kKtToMps);
    TEST_ASSERT_TRUE(w.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f * kKtToMps, w->windSpeedMps);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 270.0f, w->windDirDeg);
}

// 4. Updraft only, no horizontal wind. Heading 0, pitch 0, TAS 100kt.
//    Ground velocity matches air horizontally but has a -5 kt down (rising).
void test_pure_updraft(void)
{
    auto w = ComputeWind(100.0f * kKtToMps, 0.0f, /*down*/ -5.0f * kKtToMps,
                         0.0f, 0.0f,
                         100.0f * kKtToMps);
    TEST_ASSERT_TRUE(w.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, w->windSpeedMps);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.0f * kKtToMps, w->windVerticalMps);
}

// 5. Pitched climb in still air. Heading 0, pitch +10 deg, TAS 100 kt.
//    Ground velocity = TAS rotated by pitch: (cos10*100 N, 0, -sin10*100 D).
//    Expect wind ~= 0 in all three axes.
void test_pitched_climb_still_air(void)
{
    const float tasMps = 100.0f * kKtToMps;
    const float pitchDeg = 10.0f;
    const float pitchRad = pitchDeg * 3.14159265f / 180.0f;
    auto w = ComputeWind(std::cos(pitchRad) * tasMps,
                         0.0f,
                         -std::sin(pitchRad) * tasMps,
                         0.0f, pitchDeg, tasMps);
    TEST_ASSERT_TRUE(w.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, w->windSpeedMps);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, w->windVerticalMps);
}

// 6. TAS below threshold -> nullopt (taxiing, not flying).
void test_tas_below_threshold_returns_nullopt(void)
{
    auto w = ComputeWind(2.0f, 0.0f, 0.0f, 0.0f, 0.0f, /*tas*/ 5.0f);
    TEST_ASSERT_FALSE(w.has_value());
}

// 7. NaN inputs reject cleanly.
void test_nan_inputs_return_nullopt(void)
{
    const float nan = std::nanf("");
    TEST_ASSERT_FALSE(ComputeWind(nan, 0, 0, 0, 0, 30).has_value());
    TEST_ASSERT_FALSE(ComputeWind(0, nan, 0, 0, 0, 30).has_value());
    TEST_ASSERT_FALSE(ComputeWind(0, 0, nan, 0, 0, 30).has_value());
    TEST_ASSERT_FALSE(ComputeWind(0, 0, 0, nan, 0, 30).has_value());
    TEST_ASSERT_FALSE(ComputeWind(0, 0, 0, 0, nan, 30).has_value());
    TEST_ASSERT_FALSE(ComputeWind(0, 0, 0, 0, 0, nan).has_value());
}

// 8. yaw=-45 and yaw=315 produce the same wind solution (input normalization
//    isn't needed because trig functions are 2pi-periodic, but verify).
void test_negative_yaw_equivalent_to_positive(void)
{
    const float tasMps = 50.0f;
    auto a = ComputeWind(40.0f, 5.0f, 0.0f, -45.0f, 0.0f, tasMps);
    auto b = ComputeWind(40.0f, 5.0f, 0.0f, 315.0f, 0.0f, tasMps);
    TEST_ASSERT_TRUE(a.has_value() && b.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, a->windSpeedMps, b->windSpeedMps);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, a->windDirDeg, b->windDirDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, a->windVerticalMps, b->windVerticalMps);
}

// 9. Wind exactly from north reports 0 deg, not 360 (range is [0, 360)).
void test_direction_range_zero_not_360(void)
{
    // TAS 100 kt heading 0, ground 90 kt N -> 10 kt headwind from north.
    auto w = ComputeWind(90.0f * kKtToMps, 0.0f, 0.0f,
                         0.0f, 0.0f,
                         100.0f * kKtToMps);
    TEST_ASSERT_TRUE(w.has_value());
    TEST_ASSERT_TRUE(w->windDirDeg >= 0.0f);
    TEST_ASSERT_TRUE(w->windDirDeg < 360.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, w->windDirDeg);
}

// 10. Eastbound aircraft with wind from south: yaw=90, TAS=100, ground=(0,100,-0)
//     plus a 10 kt push from the south means ground = (10N, 100E, 0).
//     Wind from 180 deg, 10 kt.
void test_eastbound_with_south_wind(void)
{
    auto w = ComputeWind(10.0f * kKtToMps, 100.0f * kKtToMps, 0.0f,
                         /*yaw*/ 90.0f, 0.0f,
                         100.0f * kKtToMps);
    TEST_ASSERT_TRUE(w.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 10.0f * kKtToMps, w->windSpeedMps);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 180.0f, w->windDirDeg);
}

// 11. Pitched descent in still air (approach geometry). Heading 0, pitch
//     -5 deg, TAS 100 kt. Ground velocity = TAS rotated by pitch:
//     (cos(-5)*100, 0, -sin(-5)*100) = (~99.6, 0, +8.72) — ground vector
//     descends. Wind should be ~0 in all axes. This guards against a sign
//     error in airD = -tas*sin(theta) for negative pitch.
void test_pitched_descent_still_air(void)
{
    const float tasMps = 100.0f * kKtToMps;
    const float pitchDeg = -5.0f;
    const float pitchRad = pitchDeg * 3.14159265f / 180.0f;
    auto w = ComputeWind(std::cos(pitchRad) * tasMps,
                         0.0f,
                         -std::sin(pitchRad) * tasMps,
                         0.0f, pitchDeg, tasMps);
    TEST_ASSERT_TRUE(w.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, w->windSpeedMps);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, w->windVerticalMps);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_pure_headwind);
    RUN_TEST(test_pure_tailwind);
    RUN_TEST(test_pure_crosswind_from_west);
    RUN_TEST(test_pure_updraft);
    RUN_TEST(test_pitched_climb_still_air);
    RUN_TEST(test_tas_below_threshold_returns_nullopt);
    RUN_TEST(test_nan_inputs_return_nullopt);
    RUN_TEST(test_negative_yaw_equivalent_to_positive);
    RUN_TEST(test_direction_range_zero_not_360);
    RUN_TEST(test_eastbound_with_south_wind);
    RUN_TEST(test_pitched_descent_still_air);
    return UNITY_END();
}
