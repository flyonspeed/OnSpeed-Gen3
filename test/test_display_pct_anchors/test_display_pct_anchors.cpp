// test_display_pct_anchors.cpp — Pin the lever-interpolated display
// percent anchors that drive the M5 indexer pip and band edges.
//
// What we're pinning:
//   - At a detent's pot position, return that detent's percents exactly.
//   - At the midpoint between two detents (lambda=0.5), return the
//     average of their percents (within rounding).
//   - Continuity through the snap: the value at the detection-midpoint
//     ADC reading does not jump as iIndex advances — bracket-based
//     interpolation depends only on lever ADC, not on iIndex.
//   - Endpoint clamping: lever below the lowest pot or above the highest
//     returns the corresponding endpoint detent's percents (no
//     extrapolation).
//   - Audio path stays untouched: the active-detent's `fLDMAXAOA` is
//     still the body-angle setpoint audio compares against.

#include <unity.h>

#include <cmath>

#include <aoa/DisplayPctAnchors.h>
#include <aoa/PercentLift.h>
#include <config/OnSpeedConfig.h>

using onspeed::aoa::ComputeDisplayPctAnchors;
using onspeed::aoa::ComputePercentLift;
using onspeed::aoa::DisplayPctAnchors;
using SuFlaps = onspeed::config::OnSpeedConfig::SuFlaps;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Fixtures
//
// Two RV-10-style detents calibrated with different alpha_0 / alpha_stall
// so each setpoint name lands at a different percent in each detent —
// the design intent that makes interpolation meaningful.  Pot positions
// span 1000 (clean) to 3000 (full); ascending wiring.
// ============================================================================

static SuFlaps makeCleanDetent()
{
    SuFlaps f;
    f.iDegrees        = 0;
    f.iPotPosition    = 1000;
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

static SuFlaps makeFullDetent()
{
    SuFlaps f;
    f.iDegrees        = 33;
    f.iPotPosition    = 3000;
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

// Three-detent fixture: clean at 1000, takeoff at 2000, full at 3000.
// Used to pin continuity through the take->full snap boundary.
static SuFlaps makeTakeoffDetent()
{
    SuFlaps f;
    f.iDegrees        = 15;
    f.iPotPosition    = 2000;
    f.fLDMAXAOA       = 0.5f;
    f.fONSPEEDFASTAOA = 3.5f;
    f.fONSPEEDSLOWAOA = 5.5f;
    f.fSTALLWARNAOA   = 8.5f;
    f.fSTALLAOA       = 0.0f;
    f.fMANAOA         = 0.0f;
    f.fAlpha0         = -5.0f;
    f.fAlphaStall     = 11.0f;
    f.fKFit           = 0.0f;
    return f;
}

// ============================================================================
// At a detent's pot position, anchors equal that detent's percents.
// ============================================================================

void test_at_detent_a_returns_a_percents(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors a = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[0].iPotPosition), flaps, 2, true);

    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[0].fLDMAXAOA,       flaps[0], true),
                          a.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[0].fONSPEEDFASTAOA, flaps[0], true),
                          a.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[0].fONSPEEDSLOWAOA, flaps[0], true),
                          a.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[0].fSTALLWARNAOA,   flaps[0], true),
                          a.stallWarnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[0].iDegrees, a.flapsDeg);
}

void test_at_detent_b_returns_b_percents(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors b = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[1].iPotPosition), flaps, 2, true);

    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[1].fLDMAXAOA,       flaps[1], true),
                          b.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[1].fONSPEEDFASTAOA, flaps[1], true),
                          b.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[1].fONSPEEDSLOWAOA, flaps[1], true),
                          b.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[1].fSTALLWARNAOA,   flaps[1], true),
                          b.stallWarnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[1].iDegrees, b.flapsDeg);
}

// ============================================================================
// Midpoint: lambda=0.5 -> arithmetic mean of detent endpoints (within
// rounding tolerance).
// ============================================================================

void test_at_midpoint_returns_average(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    const uint16_t midAdc = static_cast<uint16_t>(
        (flaps[0].iPotPosition + flaps[1].iPotPosition) / 2);   // 2000

    DisplayPctAnchors mid = ComputeDisplayPctAnchors(midAdc, flaps, 2, true);

    const int aTones = ComputePercentLift(flaps[0].fLDMAXAOA,       flaps[0], true);
    const int bTones = ComputePercentLift(flaps[1].fLDMAXAOA,       flaps[1], true);
    const int aFast  = ComputePercentLift(flaps[0].fONSPEEDFASTAOA, flaps[0], true);
    const int bFast  = ComputePercentLift(flaps[1].fONSPEEDFASTAOA, flaps[1], true);
    const int aSlow  = ComputePercentLift(flaps[0].fONSPEEDSLOWAOA, flaps[0], true);
    const int bSlow  = ComputePercentLift(flaps[1].fONSPEEDSLOWAOA, flaps[1], true);
    const int aWarn  = ComputePercentLift(flaps[0].fSTALLWARNAOA,   flaps[0], true);
    const int bWarn  = ComputePercentLift(flaps[1].fSTALLWARNAOA,   flaps[1], true);

    // Within 1% rounding tolerance — the intermediate computation
    // rounds twice (each endpoint percent gets rounded via
    // ComputePercentLift's truncate-to-int, then the lerp rounds to
    // nearest integer).
    TEST_ASSERT_INT_WITHIN(1, (aTones + bTones) / 2, mid.tonesOnPctLift);
    TEST_ASSERT_INT_WITHIN(1, (aFast  + bFast)  / 2, mid.onSpeedFastPctLift);
    TEST_ASSERT_INT_WITHIN(1, (aSlow  + bSlow)  / 2, mid.onSpeedSlowPctLift);
    TEST_ASSERT_INT_WITHIN(1, (aWarn  + bWarn)  / 2, mid.stallWarnPctLift);

    // flapsDeg averaged: (0 + 33) / 2 = 16 or 17 depending on rounding.
    const int expDeg = static_cast<int>(std::lround(
        (static_cast<float>(flaps[0].iDegrees) + static_cast<float>(flaps[1].iDegrees)) * 0.5f));
    TEST_ASSERT_EQUAL_INT(expDeg, mid.flapsDeg);
}

// ============================================================================
// Continuity through the snap boundary.
//
// In `Flaps::Update()`, iIndex advances at the midpoint of two adjacent
// pot positions.  The display anchors must NOT jump at that boundary.
// We don't depend on iIndex here — the snapshot helper looks at the
// raw lever ADC and finds the bracket directly, so the value at the
// midpoint ADC reading is the same regardless of which iIndex was
// snapped.  Pin that with an ADC-only sweep: read at midpoint − 1,
// midpoint, midpoint + 1 and verify the values are tightly clustered.
// ============================================================================

void test_continuity_through_snap_two_detent(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    const uint16_t midAdc = static_cast<uint16_t>(
        (flaps[0].iPotPosition + flaps[1].iPotPosition) / 2);   // 2000

    DisplayPctAnchors below = ComputeDisplayPctAnchors(midAdc - 1, flaps, 2, true);
    DisplayPctAnchors at    = ComputeDisplayPctAnchors(midAdc,     flaps, 2, true);
    DisplayPctAnchors above = ComputeDisplayPctAnchors(midAdc + 1, flaps, 2, true);

    // Each percent should change by at most 1 across two adjacent ADC
    // counts at the boundary — there's no discontinuity to introduce a
    // larger jump.
    TEST_ASSERT_INT_WITHIN(1, at.tonesOnPctLift,     below.tonesOnPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.tonesOnPctLift,     above.tonesOnPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.onSpeedFastPctLift, below.onSpeedFastPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.onSpeedFastPctLift, above.onSpeedFastPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.onSpeedSlowPctLift, below.onSpeedSlowPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.onSpeedSlowPctLift, above.onSpeedSlowPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.stallWarnPctLift,   below.stallWarnPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.stallWarnPctLift,   above.stallWarnPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.flapsDeg,           below.flapsDeg);
    TEST_ASSERT_INT_WITHIN(1, at.flapsDeg,           above.flapsDeg);
}

// Three-detent variant: continuity at the takeoff->full midpoint, where
// `iIndex` advances from 1->2.  The bracket changes from (clean,take)
// to (take,full) when the lever crosses pot=2000 (the takeoff detent).
// At pot=2500 the lever is in the (take,full) bracket regardless of
// iIndex, so the percents flow continuously through the take->full
// snap boundary at pot=2500.
void test_continuity_through_snap_three_detent(void)
{
    SuFlaps flaps[3] = { makeCleanDetent(), makeTakeoffDetent(), makeFullDetent() };

    // Midpoint of takeoff (2000) and full (3000) = 2500.
    const uint16_t midAdc = 2500u;

    DisplayPctAnchors below = ComputeDisplayPctAnchors(midAdc - 1, flaps, 3, true);
    DisplayPctAnchors at    = ComputeDisplayPctAnchors(midAdc,     flaps, 3, true);
    DisplayPctAnchors above = ComputeDisplayPctAnchors(midAdc + 1, flaps, 3, true);

    TEST_ASSERT_INT_WITHIN(1, at.tonesOnPctLift,     below.tonesOnPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.tonesOnPctLift,     above.tonesOnPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.onSpeedFastPctLift, below.onSpeedFastPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.onSpeedFastPctLift, above.onSpeedFastPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.onSpeedSlowPctLift, below.onSpeedSlowPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.onSpeedSlowPctLift, above.onSpeedSlowPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.stallWarnPctLift,   below.stallWarnPctLift);
    TEST_ASSERT_INT_WITHIN(1, at.stallWarnPctLift,   above.stallWarnPctLift);

    // At the lever's exact crossing of a detent's pot position (the
    // takeoff detent at 2000 in this fixture), the value should equal
    // that detent's percents exactly — pin a separate point.
    DisplayPctAnchors atTake = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[1].iPotPosition), flaps, 3, true);
    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[1].fLDMAXAOA, flaps[1], true),
                          atTake.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[1].iDegrees, atTake.flapsDeg);
}

// ============================================================================
// Endpoint clamping.
// ============================================================================

void test_below_lowest_pot_clamps_to_lowest(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors clamped = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[0].iPotPosition - 500), flaps, 2, true);

    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[0].fLDMAXAOA, flaps[0], true),
                          clamped.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[0].iDegrees, clamped.flapsDeg);
}

void test_above_highest_pot_clamps_to_highest(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors clamped = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[1].iPotPosition + 500), flaps, 2, true);

    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[1].fLDMAXAOA, flaps[1], true),
                          clamped.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[1].iDegrees, clamped.flapsDeg);
}

// Descending wiring: pot value decreases from clean -> full.  Endpoint
// clamping must still apply at the actual lowest/highest readings, not
// at the array endpoints.
void test_descending_wiring_endpoint_clamp(void)
{
    SuFlaps clean = makeCleanDetent();   clean.iPotPosition = 3000;
    SuFlaps full  = makeFullDetent();    full.iPotPosition  = 1000;
    SuFlaps flaps[2] = { clean, full };

    // Below the lowest reading (pot 1000 — full flaps in this wiring)
    // -> clamp to the full-flaps detent.
    DisplayPctAnchors lowClamp = ComputeDisplayPctAnchors(500u, flaps, 2, true);
    TEST_ASSERT_EQUAL_INT(ComputePercentLift(full.fLDMAXAOA, full, true),
                          lowClamp.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(full.iDegrees, lowClamp.flapsDeg);

    // Above the highest reading (pot 3000 — clean) -> clamp to clean.
    DisplayPctAnchors hiClamp = ComputeDisplayPctAnchors(4000u, flaps, 2, true);
    TEST_ASSERT_EQUAL_INT(ComputePercentLift(clean.fLDMAXAOA, clean, true),
                          hiClamp.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(clean.iDegrees, hiClamp.flapsDeg);
}

// Descending wiring: lever inside the bracket lerps in the same
// direction as ascending wiring.  At the midpoint pot value the
// returned percents match the average of the two endpoint percents.
void test_descending_wiring_midpoint(void)
{
    SuFlaps clean = makeCleanDetent();   clean.iPotPosition = 3000;
    SuFlaps full  = makeFullDetent();    full.iPotPosition  = 1000;
    SuFlaps flaps[2] = { clean, full };

    DisplayPctAnchors mid = ComputeDisplayPctAnchors(2000u, flaps, 2, true);

    const int cleanTones = ComputePercentLift(clean.fLDMAXAOA, clean, true);
    const int fullTones  = ComputePercentLift(full.fLDMAXAOA,  full,  true);
    TEST_ASSERT_INT_WITHIN(1, (cleanTones + fullTones) / 2, mid.tonesOnPctLift);
}

// ============================================================================
// Degenerate and edge cases.
// ============================================================================

void test_empty_flap_vector_returns_zeros(void)
{
    DisplayPctAnchors a = ComputeDisplayPctAnchors(1500u, nullptr, 0, true);
    TEST_ASSERT_EQUAL_INT(0, a.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.stallWarnPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.flapsDeg);
}

void test_single_detent_returns_that_detent(void)
{
    SuFlaps flaps[1] = { makeCleanDetent() };

    // Lever at any value -> single-detent's percents (no interpolation).
    DisplayPctAnchors a = ComputeDisplayPctAnchors(500u, flaps, 1, true);
    DisplayPctAnchors b = ComputeDisplayPctAnchors(2500u, flaps, 1, true);

    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[0].fLDMAXAOA, flaps[0], true),
                          a.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(ComputePercentLift(flaps[0].fLDMAXAOA, flaps[0], true),
                          b.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[0].iDegrees, a.flapsDeg);
    TEST_ASSERT_EQUAL_INT(flaps[0].iDegrees, b.flapsDeg);
}

// ias_invalid: anchors are computed with iasValid forwarded.  When the
// producer passes false the anchors all collapse to zero.  Producer
// always passes true today (the anchors are calibration shape, not
// gated on live IAS); this test pins the contract that the function
// honors the iasValid argument as-is and doesn't silently override.
void test_ias_invalid_forwarded(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors a = ComputeDisplayPctAnchors(2000u, flaps, 2, false);
    TEST_ASSERT_EQUAL_INT(0, a.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.stallWarnPctLift);
    // flapsDeg is mechanical and stays interpolated even with iasValid=false.
    // Document that explicitly: the lever angle is observable regardless
    // of airspeed.
    const int expDeg = static_cast<int>(std::lround(
        (static_cast<float>(flaps[0].iDegrees) + static_cast<float>(flaps[1].iDegrees)) * 0.5f));
    TEST_ASSERT_EQUAL_INT(expDeg, a.flapsDeg);
}

// ============================================================================
// Audio-path invariant.
//
// The audio path compares g_Sensors.AOA to the active detent's
// fLDMAXAOA.  ComputeDisplayPctAnchors does not change which detent
// is "active" — that's still controlled by Flaps::Update() snapping
// iIndex.  Pin that the active detent's body-angle setpoints are
// untouched by going through the snapshot helper: the helper outputs
// percents (display values), never modifies the input flap entries.
// ============================================================================

void test_audio_path_setpoints_unchanged(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    // Snapshot the original body-angle setpoints.
    const float origCleanLDMax = flaps[0].fLDMAXAOA;
    const float origFullLDMax  = flaps[1].fLDMAXAOA;
    const float origCleanWarn  = flaps[0].fSTALLWARNAOA;
    const float origFullWarn   = flaps[1].fSTALLWARNAOA;

    // Run the helper through a sweep — should not modify any input.
    for (uint16_t adc = 500; adc <= 3500; adc += 100) {
        (void)ComputeDisplayPctAnchors(adc, flaps, 2, true);
    }

    TEST_ASSERT_EQUAL_FLOAT(origCleanLDMax, flaps[0].fLDMAXAOA);
    TEST_ASSERT_EQUAL_FLOAT(origFullLDMax,  flaps[1].fLDMAXAOA);
    TEST_ASSERT_EQUAL_FLOAT(origCleanWarn,  flaps[0].fSTALLWARNAOA);
    TEST_ASSERT_EQUAL_FLOAT(origFullWarn,   flaps[1].fSTALLWARNAOA);
}

// ============================================================================
// Linearity sweep across the full lever travel.
//
// As the lever sweeps from clean (1000) to full (3000), each percent
// should change monotonically — no zigzags from a buggy bracket
// search.  This is the integrative check that catches off-by-one
// errors in the bracket-detection loop.
// ============================================================================

void test_monotonic_sweep(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    const int aTones = ComputePercentLift(flaps[0].fLDMAXAOA, flaps[0], true);
    const int bTones = ComputePercentLift(flaps[1].fLDMAXAOA, flaps[1], true);
    const int aDeg   = flaps[0].iDegrees;
    const int bDeg   = flaps[1].iDegrees;

    int prevTones = -1;
    int prevWarn  = -1;
    int prevDeg   = -1000;

    for (uint16_t adc = static_cast<uint16_t>(flaps[0].iPotPosition);
         adc <= static_cast<uint16_t>(flaps[1].iPotPosition);
         adc += 50)
    {
        DisplayPctAnchors a = ComputeDisplayPctAnchors(adc, flaps, 2, true);

        // Each percent should be between the two endpoints (within 1 for rounding).
        const int tonesLo = std::min(aTones, bTones) - 1;
        const int tonesHi = std::max(aTones, bTones) + 1;
        TEST_ASSERT_TRUE(a.tonesOnPctLift >= tonesLo && a.tonesOnPctLift <= tonesHi);
        TEST_ASSERT_TRUE(a.flapsDeg >= std::min(aDeg, bDeg) && a.flapsDeg <= std::max(aDeg, bDeg));

        // Monotonic in the direction (a -> b).  Because a is
        // ascending in this fixture, percents increase or stay equal
        // (within 1 for rounding).  Only test direction, not strict
        // increase, because integer rounding produces flat segments.
        if (prevTones >= 0) {
            TEST_ASSERT_TRUE_MESSAGE(a.tonesOnPctLift + 1 >= prevTones,
                                     "tonesOnPctLift not monotonic");
            TEST_ASSERT_TRUE_MESSAGE(a.stallWarnPctLift + 1 >= prevWarn,
                                     "stallWarnPctLift not monotonic");
            TEST_ASSERT_TRUE_MESSAGE(a.flapsDeg + 0 >= prevDeg,
                                     "flapsDeg not monotonic");
        }
        prevTones = a.tonesOnPctLift;
        prevWarn  = a.stallWarnPctLift;
        prevDeg   = a.flapsDeg;
    }
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_at_detent_a_returns_a_percents);
    RUN_TEST(test_at_detent_b_returns_b_percents);
    RUN_TEST(test_at_midpoint_returns_average);

    RUN_TEST(test_continuity_through_snap_two_detent);
    RUN_TEST(test_continuity_through_snap_three_detent);

    RUN_TEST(test_below_lowest_pot_clamps_to_lowest);
    RUN_TEST(test_above_highest_pot_clamps_to_highest);
    RUN_TEST(test_descending_wiring_endpoint_clamp);
    RUN_TEST(test_descending_wiring_midpoint);

    RUN_TEST(test_empty_flap_vector_returns_zeros);
    RUN_TEST(test_single_detent_returns_that_detent);
    RUN_TEST(test_ias_invalid_forwarded);

    RUN_TEST(test_audio_path_setpoints_unchanged);
    RUN_TEST(test_monotonic_sweep);

    return UNITY_END();
}
