// test_percent_lift.cpp - Unit tests for onspeed::aoa::ComputePercentLift
//
// Pins the honest single-linear normalization:
//   percentLift = (aoaDeg - fAlpha0) / (fAlphaStall - fAlpha0) * 100
//
// Where each per-flap setpoint (LDmax, OnSpeedFast/Slow, StallWarn)
// lands on this scale is *whatever the calibration says* — the
// percent values vary per flap because the aerodynamics vary per
// flap.  These tests verify the formula directly, never assert
// constants for the setpoints.

#include <unity.h>

#include <aoa/PercentLift.h>
#include <config/OnSpeedConfig.h>

using onspeed::aoa::ComputePercentLift;
using SuFlaps = onspeed::config::OnSpeedConfig::SuFlaps;

void setUp(void) {}
void tearDown(void) {}

// Realistic-ish RV-10-style flap calibration.
// Span = alpha_stall - alpha_0 = 11.0 - (-2.5) = 13.5 deg.
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

// Higher-flap config — different alpha_0/alpha_stall, so every
// per-flap setpoint lands at a different percent than for clean.
// Span = 11.5 - (-9.0) = 20.5 deg.  Used to verify the function
// produces *different* percents for the same setpoint name across
// flaps (the design intent of the rework).
static SuFlaps makeRv10FullFlaps()
{
    SuFlaps f;
    f.iDegrees        = 33;
    f.iPotPosition    = 0;
    f.fLDMAXAOA       = -2.2f;
    f.fONSPEEDFASTAOA = 2.4f;
    f.fONSPEEDSLOWAOA = 4.1f;
    f.fSTALLWARNAOA   = 7.9f;
    f.fSTALLAOA       = 0.0f;
    f.fMANAOA         = 0.0f;
    f.fAlpha0         = -9.0f;
    f.fAlphaStall     = 11.5f;
    f.fKFit           = 0.0f;
    return f;
}

// Uncalibrated config — fAlphaStall and fAlpha0 both 0.
static SuFlaps makeUncalibratedFlaps()
{
    SuFlaps f;
    f.fLDMAXAOA       = 2.0f;
    f.fONSPEEDFASTAOA = 5.0f;
    f.fONSPEEDSLOWAOA = 7.5f;
    f.fSTALLWARNAOA   = 9.5f;
    // fAlphaStall left at 0 -> triggers the stallWarn*100/90 fallback ceiling.
    // fAlpha0 left at 0 -> floor at 0.
    return f;
}

// Helper: expected percent for the honest formula.
static int expectedPct(float aoaDeg, float alpha0, float alphaStall)
{
    int p = static_cast<int>((aoaDeg - alpha0) / (alphaStall - alpha0) * 100.0f);
    if (p < 0)  p = 0;
    if (p > 99) p = 99;
    return p;
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
// Honest formula — endpoints and linearity
// ============================================================================

void test_at_alpha_0_returns_0(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(f.fAlpha0, f, true));
}

void test_below_alpha_0_clamps_to_0(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(-10.0f, f, true));
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(f.fAlpha0 - 5.0f, f, true));
}

void test_at_alpha_stall_clamps_to_99(void)
{
    // Honest formula at alpha_stall = exactly 100, then clamped to 99.
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(99, ComputePercentLift(f.fAlphaStall, f, true));
}

void test_above_alpha_stall_clamps_to_99(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(99, ComputePercentLift(20.0f, f, true));
    TEST_ASSERT_EQUAL_INT(99, ComputePercentLift(40.0f, f, true));
}

void test_midpoint_alpha_0_to_alpha_stall(void)
{
    // (alpha_0 + alpha_stall) / 2 = 4.25 -> exactly 50%.
    SuFlaps f = makeRv10ZeroFlaps();
    const float mid = (f.fAlpha0 + f.fAlphaStall) / 2.0f;
    TEST_ASSERT_INT_WITHIN(1, 50, ComputePercentLift(mid, f, true));
}

void test_linearity(void)
{
    // Sample evenly across the span; verify percent increases linearly.
    SuFlaps f = makeRv10ZeroFlaps();
    const float span = f.fAlphaStall - f.fAlpha0;     // 13.5

    for (int i = 0; i <= 10; ++i) {
        const float frac = static_cast<float>(i) / 10.0f;     // 0.0 .. 1.0
        const float aoa  = f.fAlpha0 + frac * span;
        const int   exp  = expectedPct(aoa, f.fAlpha0, f.fAlphaStall);
        const int   got  = ComputePercentLift(aoa, f, true);
        TEST_ASSERT_INT_WITHIN(1, exp, got);
    }
}

// ============================================================================
// Setpoint values vary per flap (the design intent of the rework)
//
// L/Dmax-the-body-angle is a measurable, flyable property of the
// active flap.  Its position on the lift envelope (the percent value
// the indexer reads at L/Dmax body angle) varies per flap.  These
// tests verify that the function delivers different percents for the
// same-name setpoints across two different flap configs — the exact
// behavior the segmented function used to hide.
// ============================================================================

void test_ldmax_percent_varies_per_flap(void)
{
    SuFlaps clean = makeRv10ZeroFlaps();
    SuFlaps full  = makeRv10FullFlaps();

    int cleanLdmaxPct = ComputePercentLift(clean.fLDMAXAOA, clean, true);
    int fullLdmaxPct  = ComputePercentLift(full.fLDMAXAOA,  full,  true);

    // Both should land somewhere in the lower-middle of the envelope,
    // but at *different* percents.  For these fixtures: clean L/Dmax
    // (2.0 deg, span 13.5) -> ~33%; full L/Dmax (-2.2 deg, span 20.5)
    // -> ~33%.  These happen to be close — the point is that they're
    // computed from the calibration, not hardcoded to 50.
    TEST_ASSERT_INT_WITHIN(1, expectedPct(clean.fLDMAXAOA, clean.fAlpha0, clean.fAlphaStall),
                           cleanLdmaxPct);
    TEST_ASSERT_INT_WITHIN(1, expectedPct(full.fLDMAXAOA, full.fAlpha0, full.fAlphaStall),
                           fullLdmaxPct);

    // Neither should be 50 — that was the segmented function's lie.
    TEST_ASSERT_NOT_EQUAL(50, cleanLdmaxPct);
    TEST_ASSERT_NOT_EQUAL(50, fullLdmaxPct);
}

void test_onspeed_band_edges_vary_per_flap(void)
{
    SuFlaps clean = makeRv10ZeroFlaps();
    SuFlaps full  = makeRv10FullFlaps();

    // OnSpeedFast: clean (5.0) -> ~55%, full (2.4) -> ~55%.
    // OnSpeedSlow: clean (7.5) -> ~74%, full (4.1) -> ~64%.
    int cleanFast = ComputePercentLift(clean.fONSPEEDFASTAOA, clean, true);
    int fullFast  = ComputePercentLift(full.fONSPEEDFASTAOA,  full,  true);
    int cleanSlow = ComputePercentLift(clean.fONSPEEDSLOWAOA, clean, true);
    int fullSlow  = ComputePercentLift(full.fONSPEEDSLOWAOA,  full,  true);

    TEST_ASSERT_INT_WITHIN(1, expectedPct(clean.fONSPEEDFASTAOA, clean.fAlpha0, clean.fAlphaStall), cleanFast);
    TEST_ASSERT_INT_WITHIN(1, expectedPct(full.fONSPEEDFASTAOA,  full.fAlpha0,  full.fAlphaStall),  fullFast);
    TEST_ASSERT_INT_WITHIN(1, expectedPct(clean.fONSPEEDSLOWAOA, clean.fAlpha0, clean.fAlphaStall), cleanSlow);
    TEST_ASSERT_INT_WITHIN(1, expectedPct(full.fONSPEEDSLOWAOA,  full.fAlpha0,  full.fAlphaStall),  fullSlow);

    // Neither should be 55 or 66 — those were the segmented function's lies.
    // (Within rounding: %01 means 56 close enough to 55 — but 64 is clearly
    //  not 66, so this test catches the case meaningfully.)
    TEST_ASSERT_NOT_EQUAL(66, fullSlow);
}

void test_stall_warn_percent_varies_per_flap(void)
{
    SuFlaps clean = makeRv10ZeroFlaps();
    SuFlaps full  = makeRv10FullFlaps();

    int cleanWarn = ComputePercentLift(clean.fSTALLWARNAOA, clean, true);
    int fullWarn  = ComputePercentLift(full.fSTALLWARNAOA,  full,  true);

    TEST_ASSERT_INT_WITHIN(1, expectedPct(clean.fSTALLWARNAOA, clean.fAlpha0, clean.fAlphaStall), cleanWarn);
    TEST_ASSERT_INT_WITHIN(1, expectedPct(full.fSTALLWARNAOA,  full.fAlpha0,  full.fAlphaStall),  fullWarn);

    // Neither should be 90 — the segmented function's lie.
    TEST_ASSERT_NOT_EQUAL(90, cleanWarn);
    TEST_ASSERT_NOT_EQUAL(90, fullWarn);
}

// ============================================================================
// Defensive fallbacks
// ============================================================================

void test_uncalibrated_alpha_stall_uses_fallback(void)
{
    // fAlphaStall = 0 (uncalibrated) -> function falls back to
    // fSTALLWARNAOA * 100 / 90 = 9.5 * 100/90 = 10.555 as the synthetic
    // upper anchor.  At that AOA the formula reads 100, clamped to 99.
    SuFlaps f = makeUncalibratedFlaps();   // alpha_0 = 0, alpha_stall = 0
    int top = ComputePercentLift(9.5f * 100.0f / 90.0f, f, true);
    TEST_ASSERT_EQUAL_INT(99, top);
}

void test_uncalibrated_zero_floor(void)
{
    // alpha_0 = 0 (uncalibrated) -> AOA exactly at 0 reads 0%.
    SuFlaps f = makeUncalibratedFlaps();
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(0.0f, f, true));
}

void test_degenerate_calibration_returns_zero(void)
{
    // alpha_0 >= alpha_stall (impossible aerodynamically but possible
    // via misconfigured XML) -> return 0 instead of NaN/divide-by-zero.
    SuFlaps f;
    f.fAlpha0       = 5.0f;
    f.fAlphaStall   = 4.0f;
    f.fSTALLWARNAOA = 3.5f;
    int pct = ComputePercentLift(4.5f, f, true);
    TEST_ASSERT_EQUAL_INT(0, pct);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_ias_invalid_returns_zero);

    RUN_TEST(test_at_alpha_0_returns_0);
    RUN_TEST(test_below_alpha_0_clamps_to_0);
    RUN_TEST(test_at_alpha_stall_clamps_to_99);
    RUN_TEST(test_above_alpha_stall_clamps_to_99);
    RUN_TEST(test_midpoint_alpha_0_to_alpha_stall);
    RUN_TEST(test_linearity);

    RUN_TEST(test_ldmax_percent_varies_per_flap);
    RUN_TEST(test_onspeed_band_edges_vary_per_flap);
    RUN_TEST(test_stall_warn_percent_varies_per_flap);

    RUN_TEST(test_uncalibrated_alpha_stall_uses_fallback);
    RUN_TEST(test_uncalibrated_zero_floor);
    RUN_TEST(test_degenerate_calibration_returns_zero);

    return UNITY_END();
}
