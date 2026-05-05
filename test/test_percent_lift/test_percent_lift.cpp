// test_percent_lift.cpp - Unit tests for onspeed::aoa::ComputePercentLift
//
// Pins the honest single-linear normalization:
//   percentLift = (aoaDeg - fAlpha0) / (fAlphaStall - fAlpha0) * 100
//
// The function returns whole-percent units as a float (0.0..99.9) since
// the v4.24 unification.  The wire encoder in BuildDisplayFrame scales
// ×10 and truncates to int for the `%03u` tenths-of-a-percent field.
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
using SuFlaps = onspeed::config::OnSpeedConfig::SuFlaps;

void setUp(void) {}
void tearDown(void) {}

// Tolerance for float-percent comparisons: 0.05% is well below the
// minimum the wire's tenths field can resolve (0.1%) and well below
// any flight-meaningful change.
static constexpr float kPctEps = 0.05f;

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

// Helper: expected percent for the honest formula (whole-percent float).
static float expectedPctF(float aoaDeg, float alpha0, float alphaStall)
{
    float p = (aoaDeg - alpha0) / (alphaStall - alpha0) * 100.0f;
    if (p < 0.0f)  p = 0.0f;
    if (p > 99.9f) p = 99.9f;
    return p;
}

// ============================================================================
// IAS gating
// ============================================================================

void test_ias_invalid_returns_zero(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    // Even at stall AOA, with iasValid=false we should still get 0.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ComputePercentLift(11.0f, f, false));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ComputePercentLift(0.0f,  f, false));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ComputePercentLift(-5.0f, f, false));
}

// ============================================================================
// Honest formula — endpoints and linearity
// ============================================================================

void test_at_alpha_0_returns_0(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ComputePercentLift(f.fAlpha0, f, true));
}

void test_below_alpha_0_clamps_to_0(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ComputePercentLift(-10.0f, f, true));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ComputePercentLift(f.fAlpha0 - 5.0f, f, true));
}

void test_at_alpha_stall_clamps_to_99_9(void)
{
    // Honest formula at alpha_stall = exactly 100, then clamped to 99.9
    // (saturation convention — never reads 100, the wire encoder's
    // tenths field never emits 1000).
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, 99.9f, ComputePercentLift(f.fAlphaStall, f, true));
}

void test_above_alpha_stall_clamps_to_99_9(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, 99.9f, ComputePercentLift(20.0f, f, true));
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, 99.9f, ComputePercentLift(40.0f, f, true));
}

void test_midpoint_alpha_0_to_alpha_stall(void)
{
    // (alpha_0 + alpha_stall) / 2 = 4.25 -> exactly 50%.
    SuFlaps f = makeRv10ZeroFlaps();
    const float mid = (f.fAlpha0 + f.fAlphaStall) / 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, 50.0f, ComputePercentLift(mid, f, true));
}

void test_linearity(void)
{
    // Sample evenly across the span; verify percent increases linearly.
    SuFlaps f = makeRv10ZeroFlaps();
    const float span = f.fAlphaStall - f.fAlpha0;     // 13.5

    for (int i = 0; i <= 10; ++i) {
        const float frac = static_cast<float>(i) / 10.0f;     // 0.0 .. 1.0
        const float aoa  = f.fAlpha0 + frac * span;
        const float exp  = expectedPctF(aoa, f.fAlpha0, f.fAlphaStall);
        const float got  = ComputePercentLift(aoa, f, true);
        TEST_ASSERT_FLOAT_WITHIN(kPctEps, exp, got);
    }
}

// ============================================================================
// Sub-percent fidelity (the wire's tenths-of-a-percent resolution
// surfaces here as float fractional values).
// ============================================================================

void test_sub_percent_fidelity_carries_through(void)
{
    // span = 13.5 deg; 1% = 0.135 deg of body angle.
    // Step from 0.0 deg to 0.0135 deg in tiny increments and confirm
    // the float return distinguishes them.
    SuFlaps f = makeRv10ZeroFlaps();
    const float span = f.fAlphaStall - f.fAlpha0;    // 13.5
    const float onePct = span / 100.0f;              // 0.135 deg

    const float p0 = ComputePercentLift(f.fAlpha0 + 0.5f * onePct, f, true);
    const float p1 = ComputePercentLift(f.fAlpha0 + 0.7f * onePct, f, true);
    // Both should land below 1.0% but be distinct (sub-percent fidelity).
    TEST_ASSERT_TRUE(p0 < 1.0f);
    TEST_ASSERT_TRUE(p1 < 1.0f);
    TEST_ASSERT_TRUE(p1 > p0);
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, 0.5f, p0);
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, 0.7f, p1);
}

void test_saturation_below_100(void)
{
    // The clamp at 99.9 means the wire encoder's truncation
    // `static_cast<int>(99.9f * 10.0f)` lands at 999 — never 1000.
    // This is the load-bearing invariant for the wire.
    SuFlaps f = makeRv10ZeroFlaps();
    const float pct = ComputePercentLift(1000.0f, f, true);   // way above stall
    TEST_ASSERT_TRUE(pct < 100.0f);
    TEST_ASSERT_TRUE(pct >= 99.0f);
    // Wire encoding sanity check (mirrors BuildDisplayFrame's truncation).
    const int wireTenths = static_cast<int>(pct * 10.0f);
    TEST_ASSERT_TRUE(wireTenths <= 999);
    TEST_ASSERT_TRUE(wireTenths >= 990);
}

// ============================================================================
// Setpoint values vary per flap (the design intent of the rework)
//
// L/Dmax-the-body-angle is a measurable, flyable property of the
// active flap.  Its position on the lift envelope (the percent value
// the indexer reads at L/Dmax body angle) varies per flap.
// ============================================================================

void test_ldmax_percent_varies_per_flap(void)
{
    SuFlaps clean = makeRv10ZeroFlaps();
    SuFlaps full  = makeRv10FullFlaps();

    const float cleanLdmaxPct = ComputePercentLift(clean.fLDMAXAOA, clean, true);
    const float fullLdmaxPct  = ComputePercentLift(full.fLDMAXAOA,  full,  true);

    // Both should land somewhere in the lower-middle of the envelope,
    // but per the formula on each detent's own (alpha_0, alpha_stall).
    TEST_ASSERT_FLOAT_WITHIN(kPctEps,
        expectedPctF(clean.fLDMAXAOA, clean.fAlpha0, clean.fAlphaStall), cleanLdmaxPct);
    TEST_ASSERT_FLOAT_WITHIN(kPctEps,
        expectedPctF(full.fLDMAXAOA, full.fAlpha0, full.fAlphaStall), fullLdmaxPct);

    // Neither should be 50 — the segmented function used to lie at 50%.
    TEST_ASSERT_TRUE(std::fabs(cleanLdmaxPct - 50.0f) > 1.0f);
    TEST_ASSERT_TRUE(std::fabs(fullLdmaxPct  - 50.0f) > 1.0f);
}

void test_onspeed_band_edges_vary_per_flap(void)
{
    SuFlaps clean = makeRv10ZeroFlaps();
    SuFlaps full  = makeRv10FullFlaps();

    const float cleanFast = ComputePercentLift(clean.fONSPEEDFASTAOA, clean, true);
    const float fullFast  = ComputePercentLift(full.fONSPEEDFASTAOA,  full,  true);
    const float cleanSlow = ComputePercentLift(clean.fONSPEEDSLOWAOA, clean, true);
    const float fullSlow  = ComputePercentLift(full.fONSPEEDSLOWAOA,  full,  true);

    TEST_ASSERT_FLOAT_WITHIN(kPctEps, expectedPctF(clean.fONSPEEDFASTAOA, clean.fAlpha0, clean.fAlphaStall), cleanFast);
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, expectedPctF(full.fONSPEEDFASTAOA,  full.fAlpha0,  full.fAlphaStall),  fullFast);
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, expectedPctF(clean.fONSPEEDSLOWAOA, clean.fAlpha0, clean.fAlphaStall), cleanSlow);
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, expectedPctF(full.fONSPEEDSLOWAOA,  full.fAlpha0,  full.fAlphaStall),  fullSlow);

    // Full-flap slow should not be 66 (that was the segmented function's lie).
    TEST_ASSERT_TRUE(std::fabs(fullSlow - 66.0f) > 1.0f);
}

void test_stall_warn_percent_varies_per_flap(void)
{
    SuFlaps clean = makeRv10ZeroFlaps();
    SuFlaps full  = makeRv10FullFlaps();

    const float cleanWarn = ComputePercentLift(clean.fSTALLWARNAOA, clean, true);
    const float fullWarn  = ComputePercentLift(full.fSTALLWARNAOA,  full,  true);

    TEST_ASSERT_FLOAT_WITHIN(kPctEps, expectedPctF(clean.fSTALLWARNAOA, clean.fAlpha0, clean.fAlphaStall), cleanWarn);
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, expectedPctF(full.fSTALLWARNAOA,  full.fAlpha0,  full.fAlphaStall),  fullWarn);

    // Neither should be 90 — the segmented function's lie.
    TEST_ASSERT_TRUE(std::fabs(cleanWarn - 90.0f) > 1.0f);
    TEST_ASSERT_TRUE(std::fabs(fullWarn  - 90.0f) > 1.0f);
}

// ============================================================================
// Defensive fallbacks
// ============================================================================

void test_uncalibrated_alpha_stall_uses_fallback(void)
{
    // fAlphaStall = 0 (uncalibrated) -> function falls back to
    // fSTALLWARNAOA * 100 / 90 = 9.5 * 100/90 = 10.555 as the synthetic
    // upper anchor.  At that AOA the formula reads 100, clamped to 99.9.
    SuFlaps f = makeUncalibratedFlaps();   // alpha_0 = 0, alpha_stall = 0
    const float top = ComputePercentLift(9.5f * 100.0f / 90.0f, f, true);
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, 99.9f, top);
}

void test_uncalibrated_zero_floor(void)
{
    // alpha_0 = 0 (uncalibrated) -> AOA exactly at 0 reads 0%.
    SuFlaps f = makeUncalibratedFlaps();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ComputePercentLift(0.0f, f, true));
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
    const float aliveLdmax    = ComputePercentLift(f.fLDMAXAOA,       f, true);
    const float aliveFast     = ComputePercentLift(f.fONSPEEDFASTAOA, f, true);
    const float aliveSlow     = ComputePercentLift(f.fONSPEEDSLOWAOA, f, true);
    const float aliveWarn     = ComputePercentLift(f.fSTALLWARNAOA,   f, true);

    // Producer always passes iasValid=true for setpoint percents — but
    // even if a future caller passed false, the only effect should be
    // returning 0, never producing a different non-zero value.  This
    // pins the contract that the function cannot silently drift on
    // the iasValid axis.
    const float deadLdmax = ComputePercentLift(f.fLDMAXAOA,       f, false);
    const float deadFast  = ComputePercentLift(f.fONSPEEDFASTAOA, f, false);
    const float deadSlow  = ComputePercentLift(f.fONSPEEDSLOWAOA, f, false);
    const float deadWarn  = ComputePercentLift(f.fSTALLWARNAOA,   f, false);

    TEST_ASSERT_EQUAL_FLOAT(0.0f, deadLdmax);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, deadFast);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, deadSlow);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, deadWarn);

    // Sanity: alive values are nonzero (the test is meaningful only
    // because the alive case actually computes per the formula).
    TEST_ASSERT_TRUE(aliveLdmax > 0.0f);
    TEST_ASSERT_TRUE(aliveFast  > 0.0f);
    TEST_ASSERT_TRUE(aliveSlow  > 0.0f);
    TEST_ASSERT_TRUE(aliveWarn  > 0.0f);
}

// ============================================================================
// NaN / Inf handling
// ============================================================================

void test_nan_input_returns_zero(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ComputePercentLift(NAN, f, true));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ComputePercentLift(INFINITY, f, true));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ComputePercentLift(-INFINITY, f, true));
}

void test_degenerate_calibration_returns_zero(void)
{
    // alpha_0 >= alpha_stall (impossible aerodynamically but possible
    // via misconfigured XML) -> return 0 instead of NaN/divide-by-zero.
    SuFlaps f;
    f.fAlpha0       = 5.0f;
    f.fAlphaStall   = 4.0f;
    f.fSTALLWARNAOA = 3.5f;
    const float pct = ComputePercentLift(4.5f, f, true);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pct);
}

// ============================================================================
// Wire encoding round-trip (matches BuildDisplayFrame's truncation
// `int(pct * 10.0f)` clamped to [0, 999]).  Pins that the producer's
// output is byte-identical to v4.23 master for the same source AOA.
// ============================================================================

void test_wire_tenths_truncation(void)
{
    SuFlaps f = makeRv10ZeroFlaps();
    const float span = f.fAlphaStall - f.fAlpha0;    // 13.5

    // At the midpoint, formula gives exactly 50.0% → wire 500 tenths.
    const float midPct = ComputePercentLift(f.fAlpha0 + span * 0.5f, f, true);
    TEST_ASSERT_FLOAT_WITHIN(kPctEps, 50.0f, midPct);
    TEST_ASSERT_EQUAL_INT(500, static_cast<int>(midPct * 10.0f));

    // At alpha_0, formula gives 0.0 → wire 0.
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(ComputePercentLift(f.fAlpha0, f, true) * 10.0f));

    // Above alpha_stall, formula clamps to 99.9 → wire 999.
    TEST_ASSERT_EQUAL_INT(999, static_cast<int>(ComputePercentLift(20.0f, f, true) * 10.0f));
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
    RUN_TEST(test_at_alpha_stall_clamps_to_99_9);
    RUN_TEST(test_above_alpha_stall_clamps_to_99_9);
    RUN_TEST(test_midpoint_alpha_0_to_alpha_stall);
    RUN_TEST(test_linearity);
    RUN_TEST(test_sub_percent_fidelity_carries_through);
    RUN_TEST(test_saturation_below_100);

    RUN_TEST(test_ldmax_percent_varies_per_flap);
    RUN_TEST(test_onspeed_band_edges_vary_per_flap);
    RUN_TEST(test_stall_warn_percent_varies_per_flap);

    RUN_TEST(test_uncalibrated_alpha_stall_uses_fallback);
    RUN_TEST(test_uncalibrated_zero_floor);
    RUN_TEST(test_degenerate_calibration_returns_zero);

    RUN_TEST(test_anchors_stable_across_ias_validity);
    RUN_TEST(test_nan_input_returns_zero);

    RUN_TEST(test_wire_tenths_truncation);

    return UNITY_END();
}
