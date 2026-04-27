// test_spin_detect.cpp — unit tests for SpinDetector.
//
// Mirror the case list in local-plans/SPEC_SPIN_RECOVERY_CUE.md §6.1.
// Each case constructs a fresh detector, drives it with a synthetic
// time series, and asserts on cue values (−1 / 0 / +1).

#include <unity.h>
#include <sensors/SpinDetector.h>
#include <cmath>
#include <limits>

using onspeed::SpinDetector;

void setUp(void) {}
void tearDown(void) {}

// 50 ms tick (20 Hz wire-rate cadence).  At τ = 1.0 s, alpha ≈ 0.0476 per
// tick — enough samples in 2 s to clear the filter threshold from 30°/s.
static constexpr float kDt        = 0.05f;
static constexpr float kStallAoa  = 18.0f;   // arbitrary; tests pass aoa
                                              // above/below explicitly.
static constexpr float kBelowStallAoa = 10.0f;
static constexpr float kAboveStallAoa = 25.0f;

// Helper: drive the detector for n ticks at constant inputs, return the
// final cue.
static int DriveSteady(SpinDetector& d, int n, float yawDps, float aoaDeg)
{
    int cue = 0;
    for (int i = 0; i < n; ++i)
        cue = d.Update(kDt, yawDps, aoaDeg, kStallAoa);
    return cue;
}

// ============================================================================
// Case 1: AOA gate alone — wing not stalled, fast yaw → cue 0.
// ============================================================================
void test_aoa_gate_blocks_when_wing_not_stalled()
{
    SpinDetector d;
    const int cue = DriveSteady(d, 100, 100.0f, kBelowStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);
    TEST_ASSERT_FALSE(d.IsActive());
}

// ============================================================================
// Case 2: yaw gate alone — wing stalled, slow yaw → cue 0.
// ============================================================================
void test_yaw_gate_blocks_when_yaw_below_threshold()
{
    SpinDetector d;
    const int cue = DriveSteady(d, 100, 5.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);
    TEST_ASSERT_FALSE(d.IsActive());
}

// ============================================================================
// Case 3: both gates met, positive yaw — cue latches to −1 (left rudder
// against the +nose-right yaw).
// ============================================================================
void test_latch_left_rudder_on_right_yaw()
{
    SpinDetector d;
    // Drive at +30°/s for 2 s to fully populate the filter.
    const int cue = DriveSteady(d, 40, 30.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(-1, cue);
    TEST_ASSERT_TRUE(d.IsActive());
}

// ============================================================================
// Case 4: both gates met, negative yaw — cue latches to +1 (right rudder).
// ============================================================================
void test_latch_right_rudder_on_left_yaw()
{
    SpinDetector d;
    const int cue = DriveSteady(d, 40, -30.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(+1, cue);
    TEST_ASSERT_TRUE(d.IsActive());
}

// ============================================================================
// Case 5: filter rejects single-tick spike.  Steady benign yaw with a
// one-tick excursion above threshold should not latch.
// ============================================================================
void test_filter_rejects_single_tick_spike()
{
    SpinDetector d;
    // Seed and let filter settle at 5°/s for half a second.
    DriveSteady(d, 10, 5.0f, kAboveStallAoa);

    // One-tick spike to 50°/s.  Instantaneous gate passes, but the
    // filtered value barely budges (5 + alpha*(50-5) ≈ 7°/s), so the
    // filtered gate fails — no latch.
    int cue = d.Update(kDt, 50.0f, kAboveStallAoa, kStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);
    TEST_ASSERT_FALSE(d.IsActive());

    // Back to 5°/s; still no latch as filter decays.
    cue = DriveSteady(d, 30, 5.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);
    TEST_ASSERT_FALSE(d.IsActive());
}

// ============================================================================
// Case 6: hysteresis on yaw drop.  Latched at +30; drop to 17 (above
// threshold-hysteresis = 15) → stays latched; drop to 14 → clears.
// ============================================================================
void test_hysteresis_on_yaw_drop()
{
    SpinDetector d;
    // Latch first.
    int cue = DriveSteady(d, 40, 30.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(-1, cue);

    // Drop yaw to 17°/s; the filter takes time to follow, but eventually
    // settles around 17.  At settling, |filt| > 15 ⇒ stays latched.
    cue = DriveSteady(d, 80, 17.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(-1, cue);
    TEST_ASSERT_TRUE(d.IsActive());

    // Drop yaw to 14°/s; once filter falls below 15, latch clears.
    cue = DriveSteady(d, 80, 14.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);
    TEST_ASSERT_FALSE(d.IsActive());
}

// ============================================================================
// Case 7: hysteresis on AOA un-stall.  Latched at +30°/s, AOA drops below
// stall — clears immediately regardless of yaw rate.
// ============================================================================
void test_clear_on_aoa_unstall()
{
    SpinDetector d;
    int cue = DriveSteady(d, 40, 30.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(-1, cue);

    // Wing un-stalls; yaw still +30°/s but cue should clear immediately.
    cue = d.Update(kDt, 30.0f, kBelowStallAoa, kStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);
    TEST_ASSERT_FALSE(d.IsActive());
}

// ============================================================================
// Case 8: no mid-event direction flip.  Latched at −1 (right yaw), then
// over-rudder reverses filtered yaw to negative — the cue must clear
// (returns 0) but must never flip to +1 while the wing is still stalled.
//
// This is the GA equivalent of the F/A-18 OFP v10.7 "chasing arrows"
// rule: even though filter sign now points the opposite way, the
// detector stays disarmed until the wing actually un-stalls, so a
// flicker through 0/back to opposite direction cannot occur.
// ============================================================================
void test_latched_direction_holds_through_over_rudder()
{
    SpinDetector d;
    // Latch right yaw first.
    int cue = DriveSteady(d, 40, 30.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(-1, cue);

    // Pilot stomps left rudder hard — yaw input reverses to -30°/s.
    // Filter transits through zero on the way to -30.  The detector
    // must NOT emit +1 at any point in this transit while the wing
    // remains stalled.
    bool sawPositiveCue = false;
    for (int i = 0; i < 80; ++i) {
        cue = d.Update(kDt, -30.0f, kAboveStallAoa, kStallAoa);
        if (cue == +1) sawPositiveCue = true;
    }
    TEST_ASSERT_FALSE_MESSAGE(sawPositiveCue,
        "cue must never flip from -1 to +1 within a single latched event");

    // After the transit, cue is 0 and detector is disarmed.
    TEST_ASSERT_FALSE(d.IsActive());
    TEST_ASSERT_FALSE(d.IsArmed());
}

// ============================================================================
// Case 8b: yaw-hysteresis exit alone does not re-arm.  Drive yaw past
// threshold (latch), then drop to zero with wing still stalled (clears),
// then drive opposite direction (must NOT latch — detector is disarmed).
// ============================================================================
void test_yaw_hysteresis_exit_does_not_re_arm()
{
    SpinDetector d;
    int cue = DriveSteady(d, 40, 30.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(-1, cue);

    // Yaw recovers fully toward 0, wing still stalled.
    cue = DriveSteady(d, 80, 0.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);
    TEST_ASSERT_FALSE(d.IsArmed());

    // Now opposite-direction yaw spike with wing still stalled —
    // detector must stay silent because we never had a clean exit.
    cue = DriveSteady(d, 80, -30.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);
    TEST_ASSERT_FALSE(d.IsActive());
    TEST_ASSERT_FALSE(d.IsArmed());
}

// ============================================================================
// Case 9: re-arm after clean exit.  Latch right, clear via AOA, re-enter
// with opposite yaw — direction must recompute fresh.
// ============================================================================
void test_re_arm_with_opposite_direction()
{
    SpinDetector d;

    // Latch right yaw → cue −1.
    int cue = DriveSteady(d, 40, 30.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(-1, cue);

    // Clear via AOA un-stall.
    cue = d.Update(kDt, 30.0f, kBelowStallAoa, kStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);

    // Spend a few ticks at zero yaw, wing not stalled, to flush the
    // filter back toward zero.
    cue = DriveSteady(d, 60, 0.0f, kBelowStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);

    // Now re-enter with left yaw → cue should latch to +1.
    cue = DriveSteady(d, 40, -30.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(+1, cue);
    TEST_ASSERT_TRUE(d.IsActive());
}

// ============================================================================
// Case 10: first-tick initialization — even with stall + extreme yaw, the
// first call returns 0 because the filter is unseeded.  This is the
// F/A-18 OFP v10.7 transient-rejection rule.
// ============================================================================
void test_first_tick_returns_zero()
{
    SpinDetector d;
    const int cue = d.Update(kDt, 100.0f, kAboveStallAoa, kStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);
    TEST_ASSERT_FALSE(d.IsActive());
}

// ============================================================================
// Bonus: NaN/Inf inputs are no-ops; state holds.
// ============================================================================
void test_nan_inputs_are_noop()
{
    SpinDetector d;
    // Latch first so we have a non-trivial state to preserve.
    DriveSteady(d, 40, 30.0f, kAboveStallAoa);
    TEST_ASSERT_TRUE(d.IsActive());
    const int latched = d.Get();

    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    const float infVal = std::numeric_limits<float>::infinity();

    TEST_ASSERT_EQUAL_INT(latched, d.Update(kDt, nanVal, kAboveStallAoa, kStallAoa));
    TEST_ASSERT_EQUAL_INT(latched, d.Update(kDt, 30.0f, nanVal, kStallAoa));
    TEST_ASSERT_EQUAL_INT(latched, d.Update(kDt, 30.0f, kAboveStallAoa, nanVal));
    TEST_ASSERT_EQUAL_INT(latched, d.Update(0.0f, 30.0f, kAboveStallAoa, kStallAoa));
    TEST_ASSERT_EQUAL_INT(latched, d.Update(-0.05f, 30.0f, kAboveStallAoa, kStallAoa));
    TEST_ASSERT_EQUAL_INT(latched, d.Update(kDt, infVal, kAboveStallAoa, kStallAoa));

    // After all the no-ops, state still latched.
    TEST_ASSERT_TRUE(d.IsActive());
}

// ============================================================================
// Bonus: Reset() returns the detector to fresh state.
// ============================================================================
void test_reset_clears_state()
{
    SpinDetector d;
    DriveSteady(d, 40, 30.0f, kAboveStallAoa);
    TEST_ASSERT_TRUE(d.IsActive());

    d.Reset();
    TEST_ASSERT_FALSE(d.IsActive());
    TEST_ASSERT_EQUAL_INT(0, d.Get());

    // Post-reset: first tick is again a seed (returns 0) even with
    // valid latch-worthy inputs.
    const int cue = d.Update(kDt, 100.0f, kAboveStallAoa, kStallAoa);
    TEST_ASSERT_EQUAL_INT(0, cue);
}

// ============================================================================
// Bonus: Configure() honours custom thresholds.
// ============================================================================
void test_configure_overrides_defaults()
{
    SpinDetector d;
    // Lower the threshold to 10°/s — now a 12°/s yaw should latch.
    d.Configure(10.0f, 2.0f, 0.5f);

    const int cue = DriveSteady(d, 40, 12.0f, kAboveStallAoa);
    TEST_ASSERT_EQUAL_INT(-1, cue);
    TEST_ASSERT_TRUE(d.IsActive());
}

// ============================================================================

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_aoa_gate_blocks_when_wing_not_stalled);
    RUN_TEST(test_yaw_gate_blocks_when_yaw_below_threshold);
    RUN_TEST(test_latch_left_rudder_on_right_yaw);
    RUN_TEST(test_latch_right_rudder_on_left_yaw);
    RUN_TEST(test_filter_rejects_single_tick_spike);
    RUN_TEST(test_hysteresis_on_yaw_drop);
    RUN_TEST(test_clear_on_aoa_unstall);
    RUN_TEST(test_latched_direction_holds_through_over_rudder);
    RUN_TEST(test_yaw_hysteresis_exit_does_not_re_arm);
    RUN_TEST(test_re_arm_with_opposite_direction);
    RUN_TEST(test_first_tick_returns_zero);
    RUN_TEST(test_nan_inputs_are_noop);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_configure_overrides_defaults);

    return UNITY_END();
}
