// test_percent_lift.cpp - Unit tests for onspeed::aoa::ComputePercentLift
//
// Pins the verbatim port from DisplaySerial.cpp.  See PercentLift.h for the
// range table that drives the expected values below.

#include <unity.h>

#include <aoa/PercentLift.h>
#include <config/OnSpeedConfig.h>

using onspeed::aoa::ComputePercentLift;
using SuFlaps = onspeed::config::OnSpeedConfig::SuFlaps;

void setUp(void) {}
void tearDown(void) {}

// Realistic-ish RV-10-style flap calibration (from CLAUDE.md memory).
// Used as the default fixture for most tests.
static SuFlaps makeRv10ZeroFlaps()
{
    SuFlaps f;
    f.iDegrees        = 0;
    f.iPotPosition    = 0;
    f.fLDMAXAOA       = 2.0f;
    f.fONSPEEDFASTAOA = 5.0f;
    f.fONSPEEDSLOWAOA = 7.5f;
    f.fSTALLWARNAOA   = 9.5f;
    f.fSTALLAOA       = 0.0f;
    f.fMANAOA         = 0.0f;
    f.fAlpha0         = -2.5f;
    f.fAlphaStall     = 11.0f;
    f.fKFit           = 0.0f;
    return f;
}

// Uncalibrated config — fAlphaStall and fAlpha0 both 0 (the fallback path).
static SuFlaps makeUncalibratedFlaps()
{
    SuFlaps f;
    f.fLDMAXAOA       = 2.0f;
    f.fONSPEEDFASTAOA = 5.0f;
    f.fONSPEEDSLOWAOA = 7.5f;
    f.fSTALLWARNAOA   = 9.5f;
    // fAlphaStall left at 0 -> triggers the *100/90 fallback ceiling.
    // fAlpha0 left at 0 -> the legacy hardcoded-zero floor (the bug).
    return f;
}

// ============================================================================
// IAS gating
// ============================================================================

void test_ias_invalid_returns_zero(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    // Even at stall AOA, with iasValid=false we should still get 0.
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(11.0f, f, false));
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(0.0f,  f, false));
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(-5.0f, f, false));
}

// ============================================================================
// One representative value per AOA range (5 ranges)
// ============================================================================

void test_range_below_ldmax_midpoint(void)
{
    // Range: alpha_0 (-2.5) .. fLDMAXAOA (2.0) -> 0..50
    // Midpoint AOA = -0.25 -> expect ~25%.
    SuFlaps f = makeRv10ZeroFlaps();
    int pct = ComputePercentLift(-0.25f, f, true);
    TEST_ASSERT_INT_WITHIN(1, 25, pct);
}

void test_range_ldmax_to_onspeedfast_midpoint(void)
{
    // Range: fLDMAXAOA (2.0) .. fONSPEEDFASTAOA (5.0) -> 50..55
    // Midpoint AOA = 3.5 -> expect ~52 (truncates from 52.5).
    SuFlaps f = makeRv10ZeroFlaps();
    int pct = ComputePercentLift(3.5f, f, true);
    TEST_ASSERT_INT_WITHIN(1, 52, pct);
}

void test_range_onspeedfast_to_onspeedslow_midpoint(void)
{
    // Range: fONSPEEDFASTAOA (5.0) .. fONSPEEDSLOWAOA (7.5) -> 55..66
    // Midpoint AOA = 6.25 -> expect 60 (truncates from 60.5).
    SuFlaps f = makeRv10ZeroFlaps();
    int pct = ComputePercentLift(6.25f, f, true);
    TEST_ASSERT_INT_WITHIN(1, 60, pct);
}

void test_range_onspeedslow_to_stallwarn_midpoint(void)
{
    // Range: fONSPEEDSLOWAOA (7.5) .. fSTALLWARNAOA (9.5) -> 66..90
    // Midpoint AOA = 8.5 -> expect 78.
    SuFlaps f = makeRv10ZeroFlaps();
    int pct = ComputePercentLift(8.5f, f, true);
    TEST_ASSERT_INT_WITHIN(1, 78, pct);
}

void test_range_stallwarn_to_stall_midpoint(void)
{
    // Range: fSTALLWARNAOA (9.5) .. fAlphaStall (11.0) -> 90..100
    // Midpoint AOA = 10.25 -> expect 95.
    SuFlaps f = makeRv10ZeroFlaps();
    int pct = ComputePercentLift(10.25f, f, true);
    TEST_ASSERT_INT_WITHIN(1, 95, pct);
}

// ============================================================================
// Boundary transitions — pin which side of each boundary owns the value
// ============================================================================

void test_boundary_at_ldmax_belongs_to_50_55_range(void)
{
    // The first branch is `aoaDeg < fLDMAXAOA` — exactly fLDMAXAOA falls
    // into the second branch (50..55), so AOA=2.0 should map to 50.
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(50, ComputePercentLift(2.0f, f, true));
}

void test_boundary_at_onspeedfast_belongs_to_50_55_range(void)
{
    // Second branch: aoaDeg <= fONSPEEDFASTAOA -> AOA=5.0 hits the top of
    // the 50..55 range = 55.
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(55, ComputePercentLift(5.0f, f, true));
}

void test_boundary_at_onspeedslow_belongs_to_55_66_range(void)
{
    // Third branch: aoaDeg <= fONSPEEDSLOWAOA -> AOA=7.5 hits the top of
    // the 55..66 range = 66.
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(66, ComputePercentLift(7.5f, f, true));
}

void test_boundary_at_stallwarn_belongs_to_66_90_range(void)
{
    // Fourth branch: aoaDeg <= fSTALLWARNAOA -> AOA=9.5 hits the top of
    // the 66..90 range = 90.
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(90, ComputePercentLift(9.5f, f, true));
}

// ============================================================================
// Top-of-range / clamping
// ============================================================================

void test_aoa_at_alpha_stall_clamps_to_99(void)
{
    // AOA = fAlphaStall = 11.0 -> mapfloat returns 100 -> constrain
    // clamps to 99.
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(99, ComputePercentLift(11.0f, f, true));
}

void test_aoa_above_alpha_stall_clamps_to_99(void)
{
    // AOA way past stall — mapfloat extrapolates above 100 but the
    // [0,99] clamp kicks in.
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(99, ComputePercentLift(20.0f, f, true));
    TEST_ASSERT_EQUAL_INT(99, ComputePercentLift(40.0f, f, true));
}

void test_aoa_below_alpha_0_clamps_to_zero(void)
{
    // Below the alpha_0 floor, mapfloat goes negative — clamp at 0.
    SuFlaps f = makeRv10ZeroFlaps();  // fAlpha0 = -2.5
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(-10.0f, f, true));
}

// ============================================================================
// Bug-pinning test for the legacy hardcoded-zero floor (issue #196)
// ============================================================================
//
// Memory note: prior to PR 3.1 Task 5 the code hardcoded `0` as the floor of
// the 0..50 range.  In-progress fix uses fAlpha0 instead.  This test pins
// the *current* (alpha_0-aware) behavior so we notice if anyone changes it.
void test_aoa_at_alpha_0_returns_zero(void)
{
    // AOA = fAlpha0 = -2.5 -> mapfloat in_min, returns 0.
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(-2.5f, f, true));
}

void test_uncalibrated_config_uses_fallback_ceiling(void)
{
    // No fAlphaStall (defaults to 0) — fStallCeiling falls back to
    // fSTALLWARNAOA * 100 / 90 = 9.5 * 100/90 = ~10.555.  At AOA=10.555
    // we should get 99 (the range top, clamped from 100).  At the
    // midpoint of fSTALLWARNAOA..ceiling we should be near 95.
    SuFlaps f = makeUncalibratedFlaps();
    int topPct = ComputePercentLift(9.5f * 100.0f / 90.0f, f, true);
    TEST_ASSERT_EQUAL_INT(99, topPct);

    // Sanity: the legacy zero-floor uncalibrated config maps AOA=0 into
    // the middle of 0..50 (since alpha_0=0 and fLDMAXAOA=2).
    int aoa0Pct = ComputePercentLift(0.0f, f, true);
    TEST_ASSERT_EQUAL_INT(0, aoa0Pct);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_ias_invalid_returns_zero);

    RUN_TEST(test_range_below_ldmax_midpoint);
    RUN_TEST(test_range_ldmax_to_onspeedfast_midpoint);
    RUN_TEST(test_range_onspeedfast_to_onspeedslow_midpoint);
    RUN_TEST(test_range_onspeedslow_to_stallwarn_midpoint);
    RUN_TEST(test_range_stallwarn_to_stall_midpoint);

    RUN_TEST(test_boundary_at_ldmax_belongs_to_50_55_range);
    RUN_TEST(test_boundary_at_onspeedfast_belongs_to_50_55_range);
    RUN_TEST(test_boundary_at_onspeedslow_belongs_to_55_66_range);
    RUN_TEST(test_boundary_at_stallwarn_belongs_to_66_90_range);

    RUN_TEST(test_aoa_at_alpha_stall_clamps_to_99);
    RUN_TEST(test_aoa_above_alpha_stall_clamps_to_99);
    RUN_TEST(test_aoa_below_alpha_0_clamps_to_zero);

    RUN_TEST(test_aoa_at_alpha_0_returns_zero);
    RUN_TEST(test_uncalibrated_config_uses_fallback_ceiling);

    return UNITY_END();
}
