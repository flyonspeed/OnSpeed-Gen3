// test_ias_alive.cpp — tests for the pressure deadband and hysteretic
// IAS-display state machine in sensors/IasAlive.h.
//
// Both helpers are shared between the sketch driver (SensorIO.cpp) and
// the regression harness (tools/regression/host_main.cpp).  Pinning the
// thresholds + hysteresis here keeps both call sites mechanically in
// sync — if the rising/falling thresholds ever need to change, this
// test has to change too and every other consumer will pick up the new
// constants automatically.

#include <unity.h>
#include <sensors/IasAlive.h>

using namespace onspeed::sensors;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// UpdateIasDisplayable — hysteretic state machine
// ============================================================================

void test_ias_alive_rising_edge_crosses_threshold(void)
{
    // Starts false; must only flip at or above the rising threshold.
    constexpr float kRising = 20.0f;
    TEST_ASSERT_FALSE(UpdateIasDisplayable(false, 0.0f,           kRising));
    TEST_ASSERT_FALSE(UpdateIasDisplayable(false, kRising - 0.01f, kRising));
    TEST_ASSERT_TRUE (UpdateIasDisplayable(false, kRising,         kRising));
    TEST_ASSERT_TRUE (UpdateIasDisplayable(false, kRising + 50.0f, kRising));
}

void test_ias_alive_falling_edge_requires_hysteresis(void)
{
    // Starts true; must NOT flip back until strictly below the falling
    // threshold (rising - kIasDisplayHysteresisKt).  At exactly the
    // falling threshold, stays true.
    constexpr float kRising  = 20.0f;
    const     float kFalling = kRising - kIasDisplayHysteresisKt;
    TEST_ASSERT_TRUE (UpdateIasDisplayable(true, 200.0f,          kRising));
    TEST_ASSERT_TRUE (UpdateIasDisplayable(true, kFalling,         kRising));
    TEST_ASSERT_FALSE(UpdateIasDisplayable(true, kFalling - 0.01f, kRising));
    TEST_ASSERT_FALSE(UpdateIasDisplayable(true, 0.0f,             kRising));
}

void test_ias_alive_hysteresis_band_holds_state(void)
{
    // In the hysteresis band (between falling and rising), state is
    // preserved — this is what prevents chatter.
    constexpr float kRising  = 20.0f;
    const     float kFalling = kRising - kIasDisplayHysteresisKt;
    const     float kMidband = 0.5f * (kRising + kFalling);
    TEST_ASSERT_FALSE(UpdateIasDisplayable(false, kMidband, kRising));
    TEST_ASSERT_TRUE (UpdateIasDisplayable(true,  kMidband, kRising));
}

void test_ias_alive_hysteresis_band_is_positive(void)
{
    // Structural: the hysteresis constant must be strictly positive so
    // the falling edge sits below the rising edge.  A zero or negative
    // value would reintroduce the chatter this is designed to prevent.
    TEST_ASSERT_TRUE(kIasDisplayHysteresisKt > 0.0f);
}

void test_ias_alive_custom_threshold(void)
{
    // The threshold is pilot-tunable (OnSpeedConfig::
    // iIasDisplayThresholdKt). Verify the function honours an
    // arbitrary rising-edge value.
    constexpr float kCustomRising  = 30.0f;
    const     float kCustomFalling = kCustomRising - kIasDisplayHysteresisKt;
    TEST_ASSERT_FALSE(UpdateIasDisplayable(false, kCustomRising - 0.01f,  kCustomRising));
    TEST_ASSERT_TRUE (UpdateIasDisplayable(false, kCustomRising,          kCustomRising));
    TEST_ASSERT_TRUE (UpdateIasDisplayable(true,  kCustomFalling,         kCustomRising));
    TEST_ASSERT_FALSE(UpdateIasDisplayable(true,  kCustomFalling - 0.01f, kCustomRising));
}

void test_ias_alive_sentinel_zero_means_always_displayable(void)
{
    // Sentinel: rising == 0 means "never blank" — every call returns
    // true regardless of input IAS or previous state.  Matches the
    // iMuteAudioUnderIAS == 0 always-on convention.
    TEST_ASSERT_TRUE(UpdateIasDisplayable(false, 0.0f,    0.0f));
    TEST_ASSERT_TRUE(UpdateIasDisplayable(false, 100.0f, 0.0f));
    TEST_ASSERT_TRUE(UpdateIasDisplayable(true,  0.0f,    0.0f));
    TEST_ASSERT_TRUE(UpdateIasDisplayable(true,  100.0f, 0.0f));
}

// ============================================================================
// ApplyPfwdDeadband — pressure deadband
// ============================================================================

void test_pfwd_deadband_passes_through_positive_values(void)
{
    TEST_ASSERT_EQUAL_FLOAT(10.0f, ApplyPfwdDeadband(10.0f));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, ApplyPfwdDeadband(100.0f));
}

void test_pfwd_deadband_passes_through_negative_values(void)
{
    // Pitot differential can go negative briefly during gusts or
    // maneuvers.  The deadband only kills values near zero, not sign.
    TEST_ASSERT_EQUAL_FLOAT(-10.0f,  ApplyPfwdDeadband(-10.0f));
    TEST_ASSERT_EQUAL_FLOAT(-100.0f, ApplyPfwdDeadband(-100.0f));
}

void test_pfwd_deadband_clamps_noise_band_to_zero(void)
{
    // Values within ±deadband go to exactly 0 so PitotPsiToIasKt(0)==0
    // cascades cleanly (no sqrt-amplified phantom IAS).
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ApplyPfwdDeadband(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ApplyPfwdDeadband(kPfwdDeadbandCounts - 0.01f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ApplyPfwdDeadband(-(kPfwdDeadbandCounts - 0.01f)));
}

void test_pfwd_deadband_preserves_values_at_the_edge(void)
{
    // At exactly ±deadband, the value passes through — the comparison
    // is strictly-less-than.  Just-above-deadband also passes through
    // (so small but real signal is not suppressed).
    TEST_ASSERT_EQUAL_FLOAT( kPfwdDeadbandCounts,  ApplyPfwdDeadband(kPfwdDeadbandCounts));
    TEST_ASSERT_EQUAL_FLOAT(-kPfwdDeadbandCounts,  ApplyPfwdDeadband(-kPfwdDeadbandCounts));
    TEST_ASSERT_EQUAL_FLOAT( kPfwdDeadbandCounts + 0.5f, ApplyPfwdDeadband(kPfwdDeadbandCounts + 0.5f));
}

// ============================================================================
// Main
// ============================================================================

int main(int, char**)
{
    UNITY_BEGIN();

    RUN_TEST(test_ias_alive_rising_edge_crosses_threshold);
    RUN_TEST(test_ias_alive_falling_edge_requires_hysteresis);
    RUN_TEST(test_ias_alive_hysteresis_band_holds_state);
    RUN_TEST(test_ias_alive_hysteresis_band_is_positive);
    RUN_TEST(test_ias_alive_custom_threshold);
    RUN_TEST(test_ias_alive_sentinel_zero_means_always_displayable);

    RUN_TEST(test_pfwd_deadband_passes_through_positive_values);
    RUN_TEST(test_pfwd_deadband_passes_through_negative_values);
    RUN_TEST(test_pfwd_deadband_clamps_noise_band_to_zero);
    RUN_TEST(test_pfwd_deadband_preserves_values_at_the_edge);

    return UNITY_END();
}
