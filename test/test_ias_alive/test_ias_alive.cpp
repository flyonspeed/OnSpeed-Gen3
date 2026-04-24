// test_ias_alive.cpp — tests for the pressure deadband and hysteretic
// IAS-alive state machine in sensors/IasAlive.h.
//
// Both helpers are shared between the sketch driver (SensorIO.cpp) and
// the regression harness (tools/regression/host_main.cpp).  Pinning the
// thresholds here keeps both call sites mechanically in sync — if the
// rising/falling thresholds ever need to change, this test has to change
// too and every other consumer will pick up the new constants
// automatically.

#include <unity.h>
#include <sensors/IasAlive.h>

using namespace onspeed::sensors;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// UpdateIasAlive — hysteretic state machine
// ============================================================================

void test_ias_alive_rising_edge_crosses_threshold(void)
{
    // Starts false; must only flip at or above the rising threshold.
    TEST_ASSERT_FALSE(UpdateIasAlive(false, 0.0f));
    TEST_ASSERT_FALSE(UpdateIasAlive(false, kIasAliveRisingKt - 0.01f));
    TEST_ASSERT_TRUE (UpdateIasAlive(false, kIasAliveRisingKt));
    TEST_ASSERT_TRUE (UpdateIasAlive(false, kIasAliveRisingKt + 50.0f));
}

void test_ias_alive_falling_edge_requires_hysteresis(void)
{
    // Starts true; must NOT flip back until strictly below the falling
    // threshold.  At exactly the falling threshold, stays true.
    TEST_ASSERT_TRUE (UpdateIasAlive(true, 200.0f));
    TEST_ASSERT_TRUE (UpdateIasAlive(true, kIasAliveFallingKt));
    TEST_ASSERT_FALSE(UpdateIasAlive(true, kIasAliveFallingKt - 0.01f));
    TEST_ASSERT_FALSE(UpdateIasAlive(true, 0.0f));
}

void test_ias_alive_hysteresis_band_holds_state(void)
{
    // In the hysteresis band (between falling and rising), state is
    // preserved — this is what prevents chatter.
    const float kMidband = 0.5f * (kIasAliveRisingKt + kIasAliveFallingKt);
    TEST_ASSERT_FALSE(UpdateIasAlive(false, kMidband));
    TEST_ASSERT_TRUE (UpdateIasAlive(true,  kMidband));
}

void test_ias_alive_thresholds_provide_real_hysteresis(void)
{
    // Structural: falling must be strictly below rising.  A single-
    // threshold design (rising == falling) would reintroduce the
    // chatter this is designed to prevent.
    TEST_ASSERT_TRUE(kIasAliveFallingKt < kIasAliveRisingKt);
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
    RUN_TEST(test_ias_alive_thresholds_provide_real_hysteresis);

    RUN_TEST(test_pfwd_deadband_passes_through_positive_values);
    RUN_TEST(test_pfwd_deadband_passes_through_negative_values);
    RUN_TEST(test_pfwd_deadband_clamps_noise_band_to_zero);
    RUN_TEST(test_pfwd_deadband_preserves_values_at_the_edge);

    return UNITY_END();
}
