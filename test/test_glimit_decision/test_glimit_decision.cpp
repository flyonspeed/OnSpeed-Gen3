// test_glimit_decision.cpp — Unit tests for GLimitDetector
//
// Covers: threshold boundaries, asymmetric-flight limit reduction, debounce
// repeat-timeout, Reset() behavior, and first-trigger edge cases.

#include <unity.h>
#include <audio/GLimitDecision.h>

using namespace onspeed::audio;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Standard config matching the firmware defaults.
static GLimitConfig makeDefaultConfig()
{
    GLimitConfig cfg;
    cfg.positiveLimitG      =  4.0f;
    cfg.negativeLimitG      = -2.0f;
    cfg.asymmetricGyroDps   = 15.0f;
    cfg.asymmetricReduction = 2.0f / 3.0f;   // 0.666...
    cfg.repeatTimeoutMs     = 3000;
    return cfg;
}

// Inputs at 1 G in coordinated (symmetric) flight.
static GLimitInputs makeNominalInputs(uint32_t tickMs = 0)
{
    GLimitInputs in;
    in.verticalG   =  1.0f;
    in.rollRateDps =  0.0f;
    in.yawRateDps  =  0.0f;
    in.tickMs      = tickMs;
    return in;
}

// ---------------------------------------------------------------------------
// Test 1: below both limits → no trigger
// ---------------------------------------------------------------------------

void test_below_both_limits_no_trigger()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    GLimitInputs   in  = makeNominalInputs(1000);
    in.verticalG = 1.0f;   // well below +4 G

    TEST_ASSERT_FALSE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 2: strictly above positive limit → trigger
// ---------------------------------------------------------------------------

void test_above_positive_limit_triggers()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    GLimitInputs   in  = makeNominalInputs(1000);
    in.verticalG = 4.01f;   // just above +4 G

    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 3: strictly below negative limit → trigger
// ---------------------------------------------------------------------------

void test_below_negative_limit_triggers()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    GLimitInputs   in  = makeNominalInputs(1000);
    in.verticalG = -2.01f;   // just below -2 G

    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 4: exactly at positive limit → triggers (inclusive inequality)
// ---------------------------------------------------------------------------

void test_exactly_at_positive_limit_triggers()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    GLimitInputs   in  = makeNominalInputs(1000);
    in.verticalG = 4.0f;   // exactly at limit — matches original firmware

    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 5: exactly at negative limit → triggers (inclusive inequality)
// ---------------------------------------------------------------------------

void test_exactly_at_negative_limit_triggers()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    GLimitInputs   in  = makeNominalInputs(1000);
    in.verticalG = -2.0f;   // exactly at limit — matches original firmware

    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 6: high roll rate reduces positive limit → triggers at lower G
// ---------------------------------------------------------------------------

void test_asymmetric_roll_reduces_positive_limit()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    // cfg.positiveLimitG = 4.0, asymmetricReduction = 2/3 → reduced limit ≈ 2.667
    GLimitInputs in  = makeNominalInputs(1000);
    in.rollRateDps = 20.0f;   // exceeds asymmetricGyroDps=15
    in.verticalG   =  3.0f;   // above reduced limit (2.667) but below full limit (4.0)

    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 7: high yaw rate reduces negative limit → triggers at less-negative G
// ---------------------------------------------------------------------------

void test_asymmetric_yaw_reduces_negative_limit()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    // cfg.negativeLimitG = -2.0, reduction = 2/3 → reduced limit ≈ -1.333
    GLimitInputs in  = makeNominalInputs(1000);
    in.yawRateDps  = -20.0f;   // abs exceeds asymmetricGyroDps=15
    in.verticalG   =  -1.5f;   // below reduced limit (-1.333) but above full limit (-2.0)

    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 8: below repeat-timeout → chime suppressed
// ---------------------------------------------------------------------------

void test_debounce_suppresses_rapid_retrigger()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    GLimitInputs   in  = makeNominalInputs(1000);
    in.verticalG = 5.0f;   // above limit

    // First trigger
    TEST_ASSERT_TRUE(det.Update(in, cfg));

    // Immediately again — 0 ms elapsed, within 3000 ms timeout
    TEST_ASSERT_FALSE(det.Update(in, cfg));

    // 2999 ms elapsed — still within timeout
    in.tickMs = 1000 + 2999;
    TEST_ASSERT_FALSE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 9: after repeat-timeout elapses → re-triggers
// ---------------------------------------------------------------------------

void test_retrigger_after_timeout()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    GLimitInputs   in  = makeNominalInputs(1000);
    in.verticalG = 5.0f;

    // First trigger at t=1000
    TEST_ASSERT_TRUE(det.Update(in, cfg));

    // t=1000+2999=3999 ms — one ms before timeout boundary: still suppressed
    in.tickMs = 3999;
    TEST_ASSERT_FALSE(det.Update(in, cfg));

    // t=4000 — exactly at timeout (elapsed==repeatTimeoutMs) → should trigger
    // Condition: elapsed < repeatTimeoutMs → false when equal → chime fires
    in.tickMs = 4000;
    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 10: Reset() clears debounce — next trigger fires immediately
// ---------------------------------------------------------------------------

void test_reset_clears_debounce()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    GLimitInputs   in  = makeNominalInputs(1000);
    in.verticalG = 5.0f;

    // First trigger
    TEST_ASSERT_TRUE(det.Update(in, cfg));

    // Within timeout — suppressed
    TEST_ASSERT_FALSE(det.Update(in, cfg));

    // After Reset, should trigger again even at the same tick
    det.Reset();
    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 11: high gyro rate but G still within limits → no trigger
// ---------------------------------------------------------------------------

void test_high_gyro_rate_but_below_reduced_limit_no_trigger()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    // reduced posLimit = 4.0 * (2/3) ≈ 2.667
    GLimitInputs in  = makeNominalInputs(1000);
    in.rollRateDps = 20.0f;
    in.verticalG   =  2.5f;   // below reduced positive limit (2.667)

    TEST_ASSERT_FALSE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 12: asymmetricReduction=1.0 (disabled) → full limits used
// ---------------------------------------------------------------------------

void test_asymmetric_reduction_of_one_uses_full_limits()
{
    GLimitDetector det;
    GLimitConfig   cfg      = makeDefaultConfig();
    cfg.asymmetricReduction = 1.0f;   // no reduction
    // posLimit stays at 4.0 even with high gyro rate
    GLimitInputs in  = makeNominalInputs(1000);
    in.rollRateDps = 30.0f;   // well above asymmetricGyroDps
    in.verticalG   =  3.5f;   // would exceed reduced limit but not full limit

    TEST_ASSERT_FALSE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 13: first call at tickMs=0 with G exceeded → triggers (no false suppression)
// ---------------------------------------------------------------------------

void test_first_trigger_at_tick_zero()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    GLimitInputs   in  = makeNominalInputs(0);   // tickMs=0
    in.verticalG = 5.0f;

    // haveTriggered_=false on fresh instance → should trigger regardless of tick
    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test: re-enabling bOverGWarning after it was off mid-cooldown
//
// Housekeeping.cpp gates this detector on g_Config.bOverGWarning. When the
// pilot toggles the setting off via the web UI, Update() is skipped and
// lastTriggerMs_ freezes. When the setting is toggled back on, elapsed is
// measured from the last real chime, not from the moment the detector was
// re-enabled. Two cases:
//   1. Toggled back on within the 3000 ms window → still cooldown, no chime.
//   2. Toggled back on after 3000 ms → chime allowed.
//
// This matches master's behavior. Master used an integer tick counter that
// decremented every 100 ms regardless of the config flag; the count reaches
// zero at the same wall-clock time. The semantic is the same.
// ---------------------------------------------------------------------------

void test_cooldown_outlives_skipped_ticks()
{
    GLimitDetector det;
    GLimitConfig   cfg = makeDefaultConfig();
    GLimitInputs   in  = makeNominalInputs(1000);
    in.verticalG = 5.0f;

    // Fire at t=1000.
    TEST_ASSERT_TRUE(det.Update(in, cfg));

    // Config toggled off: caller skips Update() for 500 ms, then back on.
    // Cooldown from the fire at t=1000 (3000 ms total) is still active.
    in.tickMs = 1500;
    TEST_ASSERT_FALSE(det.Update(in, cfg));

    // Toggled off again, back on at t=4500. Elapsed = 3500 ms > 3000 ms
    // cooldown → trigger allowed.
    in.tickMs = 4500;
    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int /*argc*/, char** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_below_both_limits_no_trigger);
    RUN_TEST(test_above_positive_limit_triggers);
    RUN_TEST(test_below_negative_limit_triggers);
    RUN_TEST(test_exactly_at_positive_limit_triggers);
    RUN_TEST(test_exactly_at_negative_limit_triggers);
    RUN_TEST(test_asymmetric_roll_reduces_positive_limit);
    RUN_TEST(test_asymmetric_yaw_reduces_negative_limit);
    RUN_TEST(test_debounce_suppresses_rapid_retrigger);
    RUN_TEST(test_retrigger_after_timeout);
    RUN_TEST(test_reset_clears_debounce);
    RUN_TEST(test_high_gyro_rate_but_below_reduced_limit_no_trigger);
    RUN_TEST(test_asymmetric_reduction_of_one_uses_full_limits);
    RUN_TEST(test_first_trigger_at_tick_zero);
    RUN_TEST(test_cooldown_outlives_skipped_ticks);

    return UNITY_END();
}
