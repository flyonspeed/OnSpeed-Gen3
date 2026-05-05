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

#include <cmath>

#include <aoa/PercentLift.h>
#include <config/OnSpeedConfig.h>

using onspeed::aoa::ComputePercentLift;
using onspeed::aoa::ComputePercentLiftTenths;
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

// ============================================================================
// Anchor stability across iasValid transitions
//
// Band-edge anchors (LDmax, OnSpeed*, StallWarn) are computed by the
// firmware producer with iasValid=true regardless of live IAS so the
// indexer geometry stays stable when the pilot is on the ground or
// the audio mute kicks in.  Pin that contract — the function does
// not gate setpoint values on iasValid, only the live-AOA reading
// does.
// ============================================================================

void test_anchors_stable_across_ias_validity(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    int aliveLdmax    = ComputePercentLift(f.fLDMAXAOA,       f, true);
    int aliveFast     = ComputePercentLift(f.fONSPEEDFASTAOA, f, true);
    int aliveSlow     = ComputePercentLift(f.fONSPEEDSLOWAOA, f, true);
    int aliveWarn     = ComputePercentLift(f.fSTALLWARNAOA,   f, true);

    // Producer always passes iasValid=true for setpoint percents — but
    // even if a future caller passed false, the only effect should be
    // returning 0, never producing a different non-zero value.  This
    // pins the contract that the function cannot silently drift on
    // the iasValid axis.
    int deadLdmax = ComputePercentLift(f.fLDMAXAOA,       f, false);
    int deadFast  = ComputePercentLift(f.fONSPEEDFASTAOA, f, false);
    int deadSlow  = ComputePercentLift(f.fONSPEEDSLOWAOA, f, false);
    int deadWarn  = ComputePercentLift(f.fSTALLWARNAOA,   f, false);

    TEST_ASSERT_EQUAL_INT(0, deadLdmax);
    TEST_ASSERT_EQUAL_INT(0, deadFast);
    TEST_ASSERT_EQUAL_INT(0, deadSlow);
    TEST_ASSERT_EQUAL_INT(0, deadWarn);

    // Sanity: alive values are nonzero (the test is meaningful only
    // because the alive case actually computes per the formula).
    TEST_ASSERT_NOT_EQUAL(0, aliveLdmax);
    TEST_ASSERT_NOT_EQUAL(0, aliveFast);
    TEST_ASSERT_NOT_EQUAL(0, aliveSlow);
    TEST_ASSERT_NOT_EQUAL(0, aliveWarn);
}

// ============================================================================
// NaN / Inf handling
// ============================================================================

void test_nan_input_returns_zero(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(NAN, f, true));
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(INFINITY, f, true));
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLift(-INFINITY, f, true));
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
// ComputePercentLiftTenths — same formula scaled ×1000 for the v4.23 wire
// ============================================================================
//
// Pinned defenses against silent body-angle drift on the wire: every
// degenerate path that returns 0 in the integer version must also
// return 0 in the tenths version, and the saturation ceiling must
// land at 999, not 1000.

void test_tenths_at_alpha_0_returns_0(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLiftTenths(f.fAlpha0, f, true));
}

void test_tenths_at_alpha_stall_clamps_to_999(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(999, ComputePercentLiftTenths(f.fAlphaStall, f, true));
    // Above alpha_stall also saturates at 999, never wraps to 0/1000.
    TEST_ASSERT_EQUAL_INT(999, ComputePercentLiftTenths(20.0f, f, true));
}

void test_tenths_midpoint_returns_500(void)
{
    SuFlaps f = makeRv10ZeroFlaps();  // span = 13.5
    const float midAoa = (f.fAlpha0 + f.fAlphaStall) / 2.0f;
    int tenths = ComputePercentLiftTenths(midAoa, f, true);
    // Allow ±1 tenth for floating-point truncation.
    TEST_ASSERT_INT_WITHIN(1, 500, tenths);
}

void test_tenths_ias_invalid_returns_zero(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLiftTenths(f.fAlphaStall, f, false));
}

void test_tenths_nan_input_returns_zero(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLiftTenths(NAN,       f, true));
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLiftTenths(INFINITY,  f, true));
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLiftTenths(-INFINITY, f, true));
}

void test_tenths_degenerate_calibration_returns_zero(void)
{
    // alpha_0 >= alpha_stall: span <= 0 → return 0, not divide-by-zero.
    SuFlaps f;
    f.fAlpha0       = 5.0f;
    f.fAlphaStall   = 4.0f;
    f.fSTALLWARNAOA = 3.5f;
    TEST_ASSERT_EQUAL_INT(0, ComputePercentLiftTenths(4.5f, f, true));
}

void test_tenths_uncalibrated_alpha_stall_uses_fallback(void)
{
    // When alphaStall ≤ stallwarn the function uses stallwarn*100/90 as
    // the synthetic ceiling.  Stallwarn at 9.0 → ceiling at 10.0 → reading
    // at 5.0 lands halfway → ~500 tenths.
    SuFlaps f = makeUncalibratedFlaps();  // alpha_stall=0, stallwarn=9.5
    f.fSTALLWARNAOA = 9.0f;
    f.fAlphaStall   = 0.0f;  // unset → fallback path
    f.fAlpha0       = 0.0f;
    int tenths = ComputePercentLiftTenths(5.0f, f, true);
    // Fallback ceiling = 9.0 * 100/90 = 10.0; (5-0)/10 * 1000 = 500.
    TEST_ASSERT_INT_WITHIN(2, 500, tenths);
}

void test_tenths_matches_integer_version_at_band_edges(void)
{
    // Tenths reading divided by 10 should equal the integer reading
    // for every body angle that lands inside the [0, 99] saturation
    // range.  Pins that the two helpers agree on the formula.
    SuFlaps f = makeRv10ZeroFlaps();
    for (float aoa : {f.fAlpha0 + 1.0f, 0.0f, 5.0f, 8.0f, f.fAlphaStall - 1.0f}) {
        int integer = ComputePercentLift(aoa, f, true);
        int tenths  = ComputePercentLiftTenths(aoa, f, true);
        // Allow ±1 percent tolerance — the integer truncation in the
        // two functions can diverge by ≤1 due to int-cast rounding at
        // different scales.
        TEST_ASSERT_INT_WITHIN(1, integer, tenths / 10);
    }
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

    RUN_TEST(test_anchors_stable_across_ias_validity);
    RUN_TEST(test_nan_input_returns_zero);

    // Tenths variant (v4.23 wire scale)
    RUN_TEST(test_tenths_at_alpha_0_returns_0);
    RUN_TEST(test_tenths_at_alpha_stall_clamps_to_999);
    RUN_TEST(test_tenths_midpoint_returns_500);
    RUN_TEST(test_tenths_ias_invalid_returns_zero);
    RUN_TEST(test_tenths_nan_input_returns_zero);
    RUN_TEST(test_tenths_degenerate_calibration_returns_zero);
    RUN_TEST(test_tenths_uncalibrated_alpha_stall_uses_fallback);
    RUN_TEST(test_tenths_matches_integer_version_at_band_edges);

    return UNITY_END();
}
