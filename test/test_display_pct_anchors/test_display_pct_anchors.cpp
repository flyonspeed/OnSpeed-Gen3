// test_display_pct_anchors.cpp — Pin the lever-aware display percent anchors.
//
// Contract under test (see DisplayPctAnchors.h for the design rule):
//   - tonesOnPctLift (L/Dmax pip)         INTERPOLATES across the bracket
//   - flapsDeg        (numeric readout)   INTERPOLATES across the bracket
//   - onSpeedFastPctLift                  SNAPS to active detent
//   - onSpeedSlowPctLift                  SNAPS to active detent
//   - stallWarnPctLift                    SNAPS to active detent
//
// Per Vac's design rule: visual aerodynamic references interpolate;
// operational thresholds snap.  The OnSpeed band edges and stall-warn
// anchor pair with audio cues that fire at those same calibrated
// thresholds, so they snap together to keep visual and audio in
// lockstep.  Only the L/Dmax pip is purely visual.
//
// The audio path itself is unchanged — audio compares g_Sensors.AOA
// directly to g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA.  The
// `test_audio_path_setpoints_unchanged` test pins that
// ComputeDisplayPctAnchors does not modify any input flap entry.

#include <unity.h>

#include <algorithm>
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
// Fixtures.  Three RV-10-style detents calibrated with different
// alpha_0 / alpha_stall so each setpoint name lands at a different
// percent in each detent.  Pot positions span 1000 (clean) to 3000
// (full) with takeoff in between at 2000; ascending wiring.
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

static SuFlaps makeTakeoffDetent()
{
    SuFlaps f;
    f.iDegrees        = 16;
    f.iPotPosition    = 2000;
    f.fLDMAXAOA       = 0.5f;
    f.fONSPEEDFASTAOA = 3.5f;
    f.fONSPEEDSLOWAOA = 5.5f;
    f.fSTALLWARNAOA   = 8.5f;
    f.fSTALLAOA       = 0.0f;
    f.fMANAOA         = 0.0f;
    f.fAlpha0         = -5.5f;
    f.fAlphaStall     = 11.0f;
    f.fKFit           = 0.0f;
    return f;
}

// ============================================================================
// Helpers
// ============================================================================

// Convenience: compute the canonical percent for a body angle against
// a flap config.  Just an alias for the sake of readable assertions.
static int Pct(float aoaDeg, const SuFlaps& f, bool iasValid = true)
{
    return ComputePercentLift(aoaDeg, f, iasValid);
}

// ============================================================================
// At a detent's pot position, all anchors equal that detent's percents
// when the lever exactly matches the active detent.
// ============================================================================

void test_at_detent_a_returns_a_percents(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors a = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[0].iPotPosition), flaps, 2,
        /*activeIndex*/ 0, true);

    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fLDMAXAOA,       flaps[0]), a.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fONSPEEDFASTAOA, flaps[0]), a.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fONSPEEDSLOWAOA, flaps[0]), a.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fSTALLWARNAOA,   flaps[0]), a.stallWarnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[0].iDegrees, a.flapsDeg);
}

void test_at_detent_b_returns_b_percents(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors b = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[1].iPotPosition), flaps, 2,
        /*activeIndex*/ 1, true);

    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fLDMAXAOA,       flaps[1]), b.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDFASTAOA, flaps[1]), b.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDSLOWAOA, flaps[1]), b.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fSTALLWARNAOA,   flaps[1]), b.stallWarnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[1].iDegrees, b.flapsDeg);
}

// ============================================================================
// Midpoint behavior — the load-bearing assertion of the snap-vs-interpolate
// split.  Lever at lambda=0.5 between two detents:
//   - tonesOnPctLift / flapsDeg INTERPOLATE -> arithmetic mean
//   - band edges + stall-warn   SNAP        -> exactly the active detent's
// ============================================================================

void test_at_midpoint_LDmax_interpolates_band_edges_snap_to_active(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };
    const uint16_t midAdc = static_cast<uint16_t>(
        (flaps[0].iPotPosition + flaps[1].iPotPosition) / 2);   // 2000

    // Active = clean (detent 0): band edges should snap to clean's
    // calibration; L/Dmax should interpolate to the average.
    DisplayPctAnchors midActiveA =
        ComputeDisplayPctAnchors(midAdc, flaps, 2, /*activeIndex*/ 0, true);

    const int aTones = Pct(flaps[0].fLDMAXAOA, flaps[0]);
    const int bTones = Pct(flaps[1].fLDMAXAOA, flaps[1]);
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, (aTones + bTones) / 2,
                                   midActiveA.tonesOnPctLift,
                                   "tonesOnPctLift should interpolate to the mean");

    // Band edges == active detent's (clean).
    TEST_ASSERT_EQUAL_INT_MESSAGE(Pct(flaps[0].fONSPEEDFASTAOA, flaps[0]),
                                  midActiveA.onSpeedFastPctLift,
                                  "onSpeedFastPctLift should snap to active detent");
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fONSPEEDSLOWAOA, flaps[0]),
                          midActiveA.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fSTALLWARNAOA, flaps[0]),
                          midActiveA.stallWarnPctLift);

    // flapsDeg interpolates: (0 + 33) / 2 = 16 or 17 depending on rounding.
    const int expDeg = static_cast<int>(std::lround(
        (static_cast<float>(flaps[0].iDegrees) + static_cast<float>(flaps[1].iDegrees)) * 0.5f));
    TEST_ASSERT_EQUAL_INT(expDeg, midActiveA.flapsDeg);

    // Now the same lever ADC but active = full (detent 1).  Band edges
    // change to full's calibration; interpolated values do NOT change
    // (they depend on bracket position, not on activeIdx).
    DisplayPctAnchors midActiveB =
        ComputeDisplayPctAnchors(midAdc, flaps, 2, /*activeIndex*/ 1, true);

    TEST_ASSERT_INT_WITHIN(1, (aTones + bTones) / 2, midActiveB.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT_MESSAGE(Pct(flaps[1].fONSPEEDFASTAOA, flaps[1]),
                                  midActiveB.onSpeedFastPctLift,
                                  "onSpeedFastPctLift should now snap to full's calibration");
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDSLOWAOA, flaps[1]),
                          midActiveB.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fSTALLWARNAOA, flaps[1]),
                          midActiveB.stallWarnPctLift);
    TEST_ASSERT_EQUAL_INT(expDeg, midActiveB.flapsDeg);

    // The L/Dmax pip is at the same screen percent regardless of
    // activeIdx — only the band edges change.
    TEST_ASSERT_EQUAL_INT(midActiveA.tonesOnPctLift, midActiveB.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(midActiveA.flapsDeg,       midActiveB.flapsDeg);
}

// ============================================================================
// L/Dmax pip continuity through the snap moment.
//
// At the moment Flaps::Update() advances iIndex from a to b, the
// active detent flips.  Band edges step (intentional — they snap with
// the audio cues).  But the L/Dmax pip must remain continuous across
// that instant because it interpolates in lever-ADC space.  Pinned
// here by reading midpoint−1 / midpoint / midpoint+1 with each side's
// notion of active.
// ============================================================================

void test_LDmax_pip_continuous_through_snap(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };
    const uint16_t midAdc = static_cast<uint16_t>(
        (flaps[0].iPotPosition + flaps[1].iPotPosition) / 2);   // 2000

    // Just before the snap: active still = clean (0).
    DisplayPctAnchors before =
        ComputeDisplayPctAnchors(midAdc - 1, flaps, 2, /*activeIndex*/ 0, true);
    // Exactly at the snap: active flips to full (1).  Same lever ADC.
    DisplayPctAnchors at =
        ComputeDisplayPctAnchors(midAdc,     flaps, 2, /*activeIndex*/ 1, true);
    // Just after: active = full (1).
    DisplayPctAnchors after =
        ComputeDisplayPctAnchors(midAdc + 1, flaps, 2, /*activeIndex*/ 1, true);

    // L/Dmax pip moves smoothly through all three samples.
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, at.tonesOnPctLift, before.tonesOnPctLift,
                                   "L/Dmax pip jumped at the snap moment");
    TEST_ASSERT_INT_WITHIN(1, at.tonesOnPctLift, after.tonesOnPctLift);

    // flapsDeg also continuous.
    TEST_ASSERT_INT_WITHIN(1, at.flapsDeg, before.flapsDeg);
    TEST_ASSERT_INT_WITHIN(1, at.flapsDeg, after.flapsDeg);

    // Band edges step at the snap — that's intentional (snap with
    // audio).  Verify the step matches the difference between detent
    // a's calibration and detent b's calibration.
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fONSPEEDFASTAOA, flaps[0]),
                          before.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDFASTAOA, flaps[1]),
                          at.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDFASTAOA, flaps[1]),
                          after.onSpeedFastPctLift);
}

// Three-detent variant: the pip is continuous across both snap
// boundaries (clean->take and take->full).
void test_LDmax_pip_continuous_through_three_detent_snaps(void)
{
    SuFlaps flaps[3] = { makeCleanDetent(), makeTakeoffDetent(), makeFullDetent() };

    // First boundary: midpoint of clean (1000) and takeoff (2000) = 1500.
    {
        const uint16_t mid = 1500u;
        DisplayPctAnchors before = ComputeDisplayPctAnchors(mid - 1, flaps, 3, 0, true);
        DisplayPctAnchors at     = ComputeDisplayPctAnchors(mid,     flaps, 3, 1, true);
        DisplayPctAnchors after  = ComputeDisplayPctAnchors(mid + 1, flaps, 3, 1, true);
        // L/Dmax pip continuous through the snap.
        TEST_ASSERT_INT_WITHIN(1, at.tonesOnPctLift, before.tonesOnPctLift);
        TEST_ASSERT_INT_WITHIN(1, at.tonesOnPctLift, after.tonesOnPctLift);
        // Band edges step from clean (detent 0) to takeoff (detent 1)
        // at the snap — intentional, matches the audio cue threshold.
        TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fONSPEEDFASTAOA, flaps[0]),
                              before.onSpeedFastPctLift);
        TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDFASTAOA, flaps[1]),
                              at.onSpeedFastPctLift);
        TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDFASTAOA, flaps[1]),
                              after.onSpeedFastPctLift);
    }

    // Second boundary: midpoint of takeoff (2000) and full (3000) = 2500.
    {
        const uint16_t mid = 2500u;
        DisplayPctAnchors before = ComputeDisplayPctAnchors(mid - 1, flaps, 3, 1, true);
        DisplayPctAnchors at     = ComputeDisplayPctAnchors(mid,     flaps, 3, 2, true);
        DisplayPctAnchors after  = ComputeDisplayPctAnchors(mid + 1, flaps, 3, 2, true);
        // L/Dmax pip continuous through the snap.
        TEST_ASSERT_INT_WITHIN(1, at.tonesOnPctLift, before.tonesOnPctLift);
        TEST_ASSERT_INT_WITHIN(1, at.tonesOnPctLift, after.tonesOnPctLift);
        // Band edges step from takeoff (detent 1) to full (detent 2)
        // at the snap.
        TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDFASTAOA, flaps[1]),
                              before.onSpeedFastPctLift);
        TEST_ASSERT_EQUAL_INT(Pct(flaps[2].fONSPEEDFASTAOA, flaps[2]),
                              at.onSpeedFastPctLift);
        TEST_ASSERT_EQUAL_INT(Pct(flaps[2].fONSPEEDFASTAOA, flaps[2]),
                              after.onSpeedFastPctLift);
    }
}

// ============================================================================
// Endpoint clamping.  Below the lowest pot or above the highest, all
// four percents (and flapsDeg) snap to the endpoint detent.
// ============================================================================

void test_below_lowest_pot_clamps_to_lowest(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors clamped = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[0].iPotPosition - 500), flaps, 2, 0, true);

    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fLDMAXAOA,       flaps[0]), clamped.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fONSPEEDFASTAOA, flaps[0]), clamped.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fONSPEEDSLOWAOA, flaps[0]), clamped.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fSTALLWARNAOA,   flaps[0]), clamped.stallWarnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[0].iDegrees, clamped.flapsDeg);
}

void test_above_highest_pot_clamps_to_highest(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors clamped = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[1].iPotPosition + 500), flaps, 2, 1, true);

    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fLDMAXAOA,       flaps[1]), clamped.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDFASTAOA, flaps[1]), clamped.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDSLOWAOA, flaps[1]), clamped.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fSTALLWARNAOA,   flaps[1]), clamped.stallWarnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[1].iDegrees, clamped.flapsDeg);
}

// Descending wiring: pot value decreases from clean -> full.
void test_descending_wiring_endpoint_clamp(void)
{
    SuFlaps clean = makeCleanDetent();   clean.iPotPosition = 3000;
    SuFlaps full  = makeFullDetent();    full.iPotPosition  = 1000;
    SuFlaps flaps[2] = { clean, full };

    // Below the lowest reading (pot 1000 — full) -> clamp to full.
    DisplayPctAnchors lowClamp = ComputeDisplayPctAnchors(500u, flaps, 2, 1, true);
    TEST_ASSERT_EQUAL_INT(Pct(full.fLDMAXAOA, full), lowClamp.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(full.iDegrees,             lowClamp.flapsDeg);

    // Above the highest reading (pot 3000 — clean) -> clamp to clean.
    DisplayPctAnchors hiClamp = ComputeDisplayPctAnchors(4000u, flaps, 2, 0, true);
    TEST_ASSERT_EQUAL_INT(Pct(clean.fLDMAXAOA, clean), hiClamp.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(clean.iDegrees,              hiClamp.flapsDeg);
}

// Descending wiring midpoint: L/Dmax interpolates to the mean.
void test_descending_wiring_midpoint(void)
{
    SuFlaps clean = makeCleanDetent();   clean.iPotPosition = 3000;
    SuFlaps full  = makeFullDetent();    full.iPotPosition  = 1000;
    SuFlaps flaps[2] = { clean, full };

    // At pot=2000 with active=clean.
    DisplayPctAnchors mid = ComputeDisplayPctAnchors(2000u, flaps, 2, 0, true);

    const int cleanTones = Pct(clean.fLDMAXAOA, clean);
    const int fullTones  = Pct(full.fLDMAXAOA,  full);
    TEST_ASSERT_INT_WITHIN(1, (cleanTones + fullTones) / 2, mid.tonesOnPctLift);

    // Band edges snap to the active (clean).
    TEST_ASSERT_EQUAL_INT(Pct(clean.fONSPEEDFASTAOA, clean), mid.onSpeedFastPctLift);
}

// ============================================================================
// Degenerate / edge cases.
// ============================================================================

void test_empty_flap_vector_returns_zeros(void)
{
    DisplayPctAnchors a = ComputeDisplayPctAnchors(1500u, nullptr, 0, 0, true);
    TEST_ASSERT_EQUAL_INT(0, a.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.stallWarnPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.flapsDeg);
}

void test_single_detent_returns_that_detent(void)
{
    SuFlaps flaps[1] = { makeCleanDetent() };

    DisplayPctAnchors a = ComputeDisplayPctAnchors(500u,  flaps, 1, 0, true);
    DisplayPctAnchors b = ComputeDisplayPctAnchors(2500u, flaps, 1, 0, true);

    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fLDMAXAOA,       flaps[0]), a.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fONSPEEDFASTAOA, flaps[0]), a.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fLDMAXAOA,       flaps[0]), b.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[0].iDegrees, a.flapsDeg);
    TEST_ASSERT_EQUAL_INT(flaps[0].iDegrees, b.flapsDeg);
}

// activeIndex out of range -> clamped to last entry.  No out-of-bounds
// read of the flap array.
void test_out_of_range_active_index_clamps_safely(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    // activeIndex == entryCount: should clamp to entryCount-1 (full).
    DisplayPctAnchors a = ComputeDisplayPctAnchors(2000u, flaps, 2, 2, true);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDFASTAOA, flaps[1]), a.onSpeedFastPctLift);

    // activeIndex way out of range.
    DisplayPctAnchors b = ComputeDisplayPctAnchors(2000u, flaps, 2, 99, true);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDFASTAOA, flaps[1]), b.onSpeedFastPctLift);
}

// iasValid forwarded.  When false, all percent fields go to 0
// (ComputePercentLift's contract).  flapsDeg is mechanical and stays
// interpolated.
void test_ias_invalid_forwarded(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors a = ComputeDisplayPctAnchors(2000u, flaps, 2, 0, false);
    TEST_ASSERT_EQUAL_INT(0, a.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.stallWarnPctLift);

    const int expDeg = static_cast<int>(std::lround(
        (static_cast<float>(flaps[0].iDegrees) + static_cast<float>(flaps[1].iDegrees)) * 0.5f));
    TEST_ASSERT_EQUAL_INT(expDeg, a.flapsDeg);
}

// ============================================================================
// Audio path invariant.  ComputeDisplayPctAnchors must not modify the
// input flap entries — the audio path reads g_Config.aFlaps[iIndex]
// directly and depends on those values being untouched.
// ============================================================================

void test_audio_path_setpoints_unchanged(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    const float origCleanLDMax = flaps[0].fLDMAXAOA;
    const float origFullLDMax  = flaps[1].fLDMAXAOA;
    const float origCleanWarn  = flaps[0].fSTALLWARNAOA;
    const float origFullWarn   = flaps[1].fSTALLWARNAOA;

    for (uint16_t adc = 500; adc <= 3500; adc += 100) {
        for (size_t aidx = 0; aidx < 2; ++aidx) {
            (void)ComputeDisplayPctAnchors(adc, flaps, 2, aidx, true);
        }
    }

    TEST_ASSERT_EQUAL_FLOAT(origCleanLDMax, flaps[0].fLDMAXAOA);
    TEST_ASSERT_EQUAL_FLOAT(origFullLDMax,  flaps[1].fLDMAXAOA);
    TEST_ASSERT_EQUAL_FLOAT(origCleanWarn,  flaps[0].fSTALLWARNAOA);
    TEST_ASSERT_EQUAL_FLOAT(origFullWarn,   flaps[1].fSTALLWARNAOA);
}

// ============================================================================
// Slope continuity for the L/Dmax pip across a full ADC sweep.
// Catches off-by-one errors in bracket selection.  Band edges snap so
// they're not part of this test.
// ============================================================================

void test_LDmax_slope_continuity_full_sweep(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };
    const uint16_t lo = 1000;
    const uint16_t hi = 3000;

    int prev = ComputeDisplayPctAnchors(lo, flaps, 2, 0, true).tonesOnPctLift;
    int maxJump = 0;
    for (uint16_t adc = lo + 1; adc <= hi; ++adc) {
        // Choose activeIdx using the same midpoint rule Flaps::Update
        // would: switch from 0 to 1 at the configured midpoint (2000).
        const size_t aidx = (adc < 2000) ? 0 : 1;
        int now = ComputeDisplayPctAnchors(adc, flaps, 2, aidx, true).tonesOnPctLift;
        int jump = (now > prev) ? (now - prev) : (prev - now);
        if (jump > maxJump) maxJump = jump;
        prev = now;
    }
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(
        1, maxJump,
        "tonesOnPctLift jumped by more than 1 between adjacent ADC samples");
}

// Linear sweep: tonesOnPctLift stays between the two endpoint detents'
// values across the whole travel and progresses monotonically.
void test_LDmax_monotonic_sweep(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    const int aTones = Pct(flaps[0].fLDMAXAOA, flaps[0]);
    const int bTones = Pct(flaps[1].fLDMAXAOA, flaps[1]);
    const int tonesLo = std::min(aTones, bTones) - 1;
    const int tonesHi = std::max(aTones, bTones) + 1;

    int prevTones = -1000;
    for (uint16_t adc = static_cast<uint16_t>(flaps[0].iPotPosition);
         adc <= static_cast<uint16_t>(flaps[1].iPotPosition);
         adc += 50)
    {
        const size_t aidx = (adc < 2000) ? 0 : 1;
        DisplayPctAnchors a = ComputeDisplayPctAnchors(adc, flaps, 2, aidx, true);

        TEST_ASSERT_TRUE(a.tonesOnPctLift >= tonesLo && a.tonesOnPctLift <= tonesHi);
        if (prevTones > -1000) {
            // For ascending wiring, both endpoints could go up or down
            // depending on which has the higher percent.  Direction is
            // monotone toward bTones; allow rounding tolerance ±1.
            const bool ascending = (bTones >= aTones);
            if (ascending) {
                TEST_ASSERT_TRUE_MESSAGE(a.tonesOnPctLift + 1 >= prevTones,
                                         "tonesOnPctLift not monotonic (ascending)");
            } else {
                TEST_ASSERT_TRUE_MESSAGE(a.tonesOnPctLift - 1 <= prevTones,
                                         "tonesOnPctLift not monotonic (descending)");
            }
        }
        prevTones = a.tonesOnPctLift;
    }
}

// ============================================================================
// Edge cases flagged by code review.
// ============================================================================

// Two adjacent detents with identical iPotPosition (degenerate config,
// reachable via misconfigured XML).  The bracket math has zero span;
// verify the function returns finite percents without dividing by
// zero.
void test_zero_span_bracket_no_divide_by_zero(void)
{
    SuFlaps fixture[2];
    fixture[0]              = makeCleanDetent();
    fixture[1]              = makeFullDetent();
    fixture[1].iPotPosition = fixture[0].iPotPosition;   // collide ADCs

    DisplayPctAnchors a = ComputeDisplayPctAnchors(
        fixture[0].iPotPosition, fixture, 2, 0, true);

    int expected = Pct(fixture[0].fLDMAXAOA, fixture[0]);
    TEST_ASSERT_INT_WITHIN(1, expected, a.tonesOnPctLift);
    TEST_ASSERT_TRUE(a.tonesOnPctLift     >= 0 && a.tonesOnPctLift     <= 99);
    TEST_ASSERT_TRUE(a.onSpeedFastPctLift >= 0 && a.onSpeedFastPctLift <= 99);
    TEST_ASSERT_TRUE(a.onSpeedSlowPctLift >= 0 && a.onSpeedSlowPctLift <= 99);
    TEST_ASSERT_TRUE(a.stallWarnPctLift   >= 0 && a.stallWarnPctLift   <= 99);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_at_detent_a_returns_a_percents);
    RUN_TEST(test_at_detent_b_returns_b_percents);

    RUN_TEST(test_at_midpoint_LDmax_interpolates_band_edges_snap_to_active);
    RUN_TEST(test_LDmax_pip_continuous_through_snap);
    RUN_TEST(test_LDmax_pip_continuous_through_three_detent_snaps);

    RUN_TEST(test_below_lowest_pot_clamps_to_lowest);
    RUN_TEST(test_above_highest_pot_clamps_to_highest);
    RUN_TEST(test_descending_wiring_endpoint_clamp);
    RUN_TEST(test_descending_wiring_midpoint);

    RUN_TEST(test_empty_flap_vector_returns_zeros);
    RUN_TEST(test_single_detent_returns_that_detent);
    RUN_TEST(test_out_of_range_active_index_clamps_safely);
    RUN_TEST(test_ias_invalid_forwarded);

    RUN_TEST(test_audio_path_setpoints_unchanged);
    RUN_TEST(test_LDmax_slope_continuity_full_sweep);
    RUN_TEST(test_LDmax_monotonic_sweep);

    RUN_TEST(test_zero_span_bracket_no_divide_by_zero);

    return UNITY_END();
}
