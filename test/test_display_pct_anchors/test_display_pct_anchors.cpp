// test_display_pct_anchors.cpp — Pin the lever-aware display percent anchors.
//
// Contract under test (see DisplayPctAnchors.h for the design rule):
//
//   tonesOnPctLift     SNAPPED to active detent's L/Dmax — operational,
//                      mirrors the audio low-tone gate.  Bottom chevron
//                      gates on this value.
//   pipPctLift         INTERPOLATED across the entire pot range from the
//                      cleanest detent's L/Dmax percent to the most-
//                      deployed detent's bottom-half-of-donut target
//                      ((3*fast + slow) / 4).  Visual aerodynamic
//                      reference; intermediate detents are ignored for
//                      the pip.
//   onSpeedFastPctLift SNAPPED to active detent
//   onSpeedSlowPctLift SNAPPED to active detent
//   stallWarnPctLift   SNAPPED to active detent
//   flapsDeg           INTERPOLATED across the active bracket; visits
//                      every detent's iDegrees exactly when the lever
//                      pot equals that detent's iPotPosition.
//
// Per Vac's design rule (ld_max.pdf §8): "L/Dmax pips are aerodynamic
// references. Fast tone is an operational limit cue. They must remain
// independent."  The pip and the chevron edge coincide visually only
// at the cleanest detent.  This file pins that property and the related
// invariants in the spec.

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
    f.fLDMAXAOA       = 4.0f;       // pct ≈ 48
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

// ComputePercentLift returns float (whole percent, 0.0..99.9); these
// tests work in integer percent because they assert on the snapped
// per-detent band-edge values (the same int rounding the production
// DisplayPctAnchors fields use).  Truncation here matches the
// `static_cast<int>(ComputePercentLift(...))` callsite in
// DisplayPctAnchors.cpp.
static int Pct(float aoaDeg, const SuFlaps& f, bool iasValid = true)
{
    return static_cast<int>(ComputePercentLift(aoaDeg, f, iasValid));
}

// Bottom-half-of-donut target for the pip at full flaps: one quarter of
// the way from the fast edge to the slow edge of the most-deployed
// detent's OnSpeed band, in percent space.  Mirrors the production
// `FullFlapPipTarget` formula in DisplayPctAnchors.cpp (Vac §6 + spec
// §"Behavior contract").
static int FullFlapPipTargetPct(const SuFlaps& f, bool iasValid = true)
{
    const int fast = Pct(f.fONSPEEDFASTAOA, f, iasValid);
    const int slow = Pct(f.fONSPEEDSLOWAOA, f, iasValid);
    return static_cast<int>(std::lround(
        (3.0f * static_cast<float>(fast) + static_cast<float>(slow)) / 4.0f));
}

// ============================================================================
// At a detent's pot position, operational anchors equal that detent's
// percents, the pip lerps toward it from the configured endpoints.
// ============================================================================

void test_at_clean_detent_all_anchors_at_clean_calibration(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors a = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[0].iPotPosition), flaps, 2,
        /*activeIndex*/ 0, true);

    // Operational: snapped to clean.
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fLDMAXAOA,       flaps[0]), a.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fONSPEEDFASTAOA, flaps[0]), a.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fONSPEEDSLOWAOA, flaps[0]), a.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fSTALLWARNAOA,   flaps[0]), a.stallWarnPctLift);

    // Pip: at the clean endpoint of its lerp -> equals clean's L/Dmax pct.
    // (Spec invariant 6: pip == tonesOn at the cleanest detent.)
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fLDMAXAOA, flaps[0]), a.pipPctLift);
    TEST_ASSERT_EQUAL_INT_MESSAGE(a.tonesOnPctLift, a.pipPctLift,
        "pip and tonesOn must coincide at the cleanest detent");

    TEST_ASSERT_EQUAL_INT(flaps[0].iDegrees, a.flapsDeg);
}

void test_at_full_detent_pip_at_bottom_half_donut_tones_at_full_LDmax(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors b = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[1].iPotPosition), flaps, 2,
        /*activeIndex*/ 1, true);

    // Operational: snapped to full.
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fLDMAXAOA,       flaps[1]), b.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDFASTAOA, flaps[1]), b.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fONSPEEDSLOWAOA, flaps[1]), b.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fSTALLWARNAOA,   flaps[1]), b.stallWarnPctLift);

    // Pip: at the full-flap endpoint -> bottom-half-of-donut target of
    // the most-deployed detent (spec §"Behavior contract", invariant 2).
    TEST_ASSERT_EQUAL_INT(FullFlapPipTargetPct(flaps[1]), b.pipPctLift);

    // Spec invariant 7: pip > tonesOn at full flaps for any sane calibration.
    TEST_ASSERT_GREATER_THAN_INT(b.tonesOnPctLift, b.pipPctLift);

    TEST_ASSERT_EQUAL_INT(flaps[1].iDegrees, b.flapsDeg);
}

// ============================================================================
// Pip ignores intermediate detents.
//
// Three-detent config (clean/16/33).  At the 16° detent's pot position
// the pip should be at the 2-endpoint lerp value, NOT at the 16°
// detent's calibrated L/Dmax percent.  This is the load-bearing
// "ignore intermediate detents for the pip" property from the spec.
// ============================================================================

void test_pip_ignores_intermediate_detent(void)
{
    SuFlaps flaps[3] = { makeCleanDetent(), makeTakeoffDetent(), makeFullDetent() };

    // Lever exactly at the 16° detent.  Active = 16° detent (1).
    DisplayPctAnchors at16 = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[1].iPotPosition), flaps, 3,
        /*activeIndex*/ 1, true);

    // Operational tonesOn snaps to detent 1's L/Dmax pct.
    const int tones16Expected = Pct(flaps[1].fLDMAXAOA, flaps[1]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(tones16Expected, at16.tonesOnPctLift,
        "tonesOnPctLift must snap to active detent's L/Dmax (operational cue)");

    // Pip lerps using the cleanest+fullflap endpoints.  At pot=2000
    // (midway between clean=1000 and full=3000), lambda=0.5, pip is
    // halfway between pipClean and pipFullFlap.
    const int pipClean    = Pct(flaps[0].fLDMAXAOA, flaps[0]);
    const int pipFullFlap = FullFlapPipTargetPct(flaps[2]);
    const int pipExpected = static_cast<int>(std::lround(
        (static_cast<float>(pipClean) + static_cast<float>(pipFullFlap)) / 2.0f));
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, pipExpected, at16.pipPctLift,
        "pipPctLift must lerp between cleanest and fullflap endpoints, "
        "ignoring the intermediate 16° detent");

    // The pip is NOT at the 16° detent's calibrated L/Dmax percent
    // (unless coincidentally — but for these fixtures, it isn't).
    const int tones16IfPip = tones16Expected;
    if (std::abs(pipExpected - tones16IfPip) >= 2) {
        TEST_ASSERT_NOT_EQUAL_INT(tones16IfPip, at16.pipPctLift);
    }
}

// ============================================================================
// Pip continuity in lever-pot space — single lerp, monotone, no jumps.
// (Spec invariant 1.)
// ============================================================================

void test_pip_continuous_in_rawAdc_full_sweep(void)
{
    SuFlaps flaps[3] = { makeCleanDetent(), makeTakeoffDetent(), makeFullDetent() };

    int prev   = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[0].iPotPosition), flaps, 3, 0, true).pipPctLift;
    int maxJump = 0;
    for (uint16_t adc = static_cast<uint16_t>(flaps[0].iPotPosition + 1);
         adc <= static_cast<uint16_t>(flaps[2].iPotPosition);
         ++adc)
    {
        // Active index follows Flaps::Update() midpoint rule.
        size_t aidx;
        if      (adc < 1500) aidx = 0;
        else if (adc < 2500) aidx = 1;
        else                 aidx = 2;
        int now = ComputeDisplayPctAnchors(adc, flaps, 3, aidx, true).pipPctLift;
        int jump = std::abs(now - prev);
        if (jump > maxJump) maxJump = jump;
        prev = now;
    }
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(1, maxJump,
        "pipPctLift jumped by more than 1 between adjacent ADC samples");
}

// Pip is independent of activeIndex (spec invariant 3).  Same rawAdc,
// different activeIdx values, same pip.
void test_pip_independent_of_activeIndex(void)
{
    SuFlaps flaps[3] = { makeCleanDetent(), makeTakeoffDetent(), makeFullDetent() };
    const uint16_t midAdc = 1750;

    DisplayPctAnchors a0 = ComputeDisplayPctAnchors(midAdc, flaps, 3, 0, true);
    DisplayPctAnchors a1 = ComputeDisplayPctAnchors(midAdc, flaps, 3, 1, true);
    DisplayPctAnchors a2 = ComputeDisplayPctAnchors(midAdc, flaps, 3, 2, true);

    TEST_ASSERT_EQUAL_INT(a0.pipPctLift, a1.pipPctLift);
    TEST_ASSERT_EQUAL_INT(a1.pipPctLift, a2.pipPctLift);

    // Operational anchors DO change with activeIdx — make sure the
    // fixture distinguishes them, otherwise this test proves nothing.
    TEST_ASSERT_NOT_EQUAL_INT(a0.tonesOnPctLift, a1.tonesOnPctLift);
    TEST_ASSERT_NOT_EQUAL_INT(a1.tonesOnPctLift, a2.tonesOnPctLift);
}

// ============================================================================
// tonesOnPctLift snaps at iIndex transitions (spec invariant 4).
// At the same rawAdc but different activeIdx, the tones-on threshold
// changes by exactly the difference between the two detents' L/Dmax pcts.
// ============================================================================

void test_tonesOn_snaps_at_iIndex_advance(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };
    const uint16_t midAdc = static_cast<uint16_t>(
        (flaps[0].iPotPosition + flaps[1].iPotPosition) / 2);   // 2000

    DisplayPctAnchors before = ComputeDisplayPctAnchors(midAdc, flaps, 2, 0, true);
    DisplayPctAnchors after  = ComputeDisplayPctAnchors(midAdc, flaps, 2, 1, true);

    const int beforeExpected = Pct(flaps[0].fLDMAXAOA, flaps[0]);
    const int afterExpected  = Pct(flaps[1].fLDMAXAOA, flaps[1]);

    TEST_ASSERT_EQUAL_INT(beforeExpected, before.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(afterExpected,  after.tonesOnPctLift);
    TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(before.tonesOnPctLift, after.tonesOnPctLift,
        "tonesOnPctLift must change when iIndex advances "
        "(snap matches audio gate)");

    // But the pip is unchanged — visual continuity through the snap.
    TEST_ASSERT_EQUAL_INT(before.pipPctLift, after.pipPctLift);
}

// ============================================================================
// tonesOnPctLift matches the audio gate exactly (spec invariant 5).
// For every flap entry, tonesOnPctLift produced when active = that
// entry equals ComputePercentLift of that entry's fLDMAXAOA.
// ============================================================================

void test_tonesOn_matches_audio_gate_per_detent(void)
{
    SuFlaps flaps[3] = { makeCleanDetent(), makeTakeoffDetent(), makeFullDetent() };

    for (size_t i = 0; i < 3; ++i) {
        DisplayPctAnchors a = ComputeDisplayPctAnchors(
            static_cast<uint16_t>(flaps[i].iPotPosition), flaps, 3, i, true);
        const int expected = Pct(flaps[i].fLDMAXAOA, flaps[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(expected, a.tonesOnPctLift,
            "tonesOnPctLift must equal ComputePercentLift of active "
            "detent's fLDMAXAOA — same value the audio gate uses");
    }
}

// ============================================================================
// Endpoint clamping.
// ============================================================================

void test_below_lowest_pot_clamps_pip_to_clean_endpoint(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors clamped = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[0].iPotPosition - 500), flaps, 2, 0, true);

    // Pip at the clean endpoint = clean's L/Dmax pct (lambda=0).
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fLDMAXAOA, flaps[0]), clamped.pipPctLift);
    // Operational anchors snap to active (clean).
    TEST_ASSERT_EQUAL_INT(Pct(flaps[0].fLDMAXAOA, flaps[0]), clamped.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[0].iDegrees, clamped.flapsDeg);
}

void test_above_highest_pot_clamps_pip_to_full_endpoint(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors clamped = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(flaps[1].iPotPosition + 500), flaps, 2, 1, true);

    // Pip at the full-flap endpoint = bottom-half-of-donut target.
    TEST_ASSERT_EQUAL_INT(FullFlapPipTargetPct(flaps[1]), clamped.pipPctLift);
    // Operational anchors snap to active (full).
    TEST_ASSERT_EQUAL_INT(Pct(flaps[1].fLDMAXAOA, flaps[1]), clamped.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(flaps[1].iDegrees, clamped.flapsDeg);
}

// ============================================================================
// Descending-wiring symmetry.  Pip math operates on signed pot deltas;
// reversing the wiring (clean = high pot, full = low pot, like the
// user's RV-10 config) must produce equivalent behavior.
// (Spec open-question 2 + invariant 1 across both wirings.)
// ============================================================================

void test_descending_wiring_pip_endpoints(void)
{
    SuFlaps clean = makeCleanDetent();   clean.iPotPosition = 1462;
    SuFlaps full  = makeFullDetent();    full.iPotPosition  = 2;
    SuFlaps flaps[2] = { clean, full };

    // At the clean detent's pot — pip at clean's L/Dmax.
    DisplayPctAnchors atClean = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(clean.iPotPosition), flaps, 2, 0, true);
    TEST_ASSERT_EQUAL_INT(Pct(clean.fLDMAXAOA, clean), atClean.pipPctLift);

    // At the full detent's pot — pip at bottom-half-of-donut target of full.
    DisplayPctAnchors atFull = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(full.iPotPosition), flaps, 2, 1, true);
    TEST_ASSERT_EQUAL_INT(FullFlapPipTargetPct(full), atFull.pipPctLift);
}

void test_descending_wiring_pip_midpoint(void)
{
    SuFlaps clean = makeCleanDetent();   clean.iPotPosition = 3000;
    SuFlaps full  = makeFullDetent();    full.iPotPosition  = 1000;
    SuFlaps flaps[2] = { clean, full };

    // At pot=2000 (geometric midpoint of the descending range).
    DisplayPctAnchors mid = ComputeDisplayPctAnchors(2000u, flaps, 2, 0, true);

    const int pipClean    = Pct(clean.fLDMAXAOA, clean);
    const int pipFullFlap = FullFlapPipTargetPct(full);
    const int pipExpected = static_cast<int>(std::lround(
        (static_cast<float>(pipClean) + static_cast<float>(pipFullFlap)) / 2.0f));
    TEST_ASSERT_INT_WITHIN(1, pipExpected, mid.pipPctLift);
}

// Endpoint clamp on descending wiring: out-of-range rawAdc still
// produces sensible endpoints.
void test_descending_wiring_endpoint_clamp(void)
{
    SuFlaps clean = makeCleanDetent();   clean.iPotPosition = 3000;
    SuFlaps full  = makeFullDetent();    full.iPotPosition  = 1000;
    SuFlaps flaps[2] = { clean, full };

    // Above the clean detent's pot (out of range) -> pip clamps to clean's
    // L/Dmax (lambda=0).
    DisplayPctAnchors hi = ComputeDisplayPctAnchors(4000u, flaps, 2, 0, true);
    TEST_ASSERT_EQUAL_INT(Pct(clean.fLDMAXAOA, clean), hi.pipPctLift);

    // Below the full detent's pot (out of range) -> pip clamps to full's
    // bottom-half-of-donut target (lambda=1).
    DisplayPctAnchors lo = ComputeDisplayPctAnchors(500u, flaps, 2, 1, true);
    TEST_ASSERT_EQUAL_INT(FullFlapPipTargetPct(full), lo.pipPctLift);
}

// ============================================================================
// Degenerate / edge cases.
// ============================================================================

void test_empty_flap_vector_returns_zeros(void)
{
    DisplayPctAnchors a = ComputeDisplayPctAnchors(1500u, nullptr, 0, 0, true);
    TEST_ASSERT_EQUAL_INT(0, a.pipPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.stallWarnPctLift);
    TEST_ASSERT_EQUAL_INT(0, a.flapsDeg);
}

void test_single_detent_pip_locked_at_LDmax_pct(void)
{
    SuFlaps flaps[1] = { makeCleanDetent() };

    DisplayPctAnchors a = ComputeDisplayPctAnchors(500u,  flaps, 1, 0, true);
    DisplayPctAnchors b = ComputeDisplayPctAnchors(2500u, flaps, 1, 0, true);

    const int expected = Pct(flaps[0].fLDMAXAOA, flaps[0]);

    // Pip locked at the only detent's L/Dmax — no slide possible.
    TEST_ASSERT_EQUAL_INT(expected, a.pipPctLift);
    TEST_ASSERT_EQUAL_INT(expected, b.pipPctLift);

    // Pip == tonesOn (both at the same single detent).
    TEST_ASSERT_EQUAL_INT(a.tonesOnPctLift, a.pipPctLift);

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
//
// NOTE: This test pins the function's behavior when called with
// iasValid=false on anchor computations.  No production caller does
// this — see DisplayPctAnchors.h's producer note "the producer always
// passes true for the band-edge / L/Dmax anchors" (lines 97-100),
// matched by sketch_common DisplaySerial.cpp / DataServer.cpp and
// (post-PR #432) the X-Plane plugin's PercentLiftFill.cpp.  The test
// exists because the function still has documented behavior in the
// iasValid=false case, even though that case is now an anti-pattern.
void test_ias_invalid_forwarded(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    DisplayPctAnchors a = ComputeDisplayPctAnchors(2000u, flaps, 2, 0, false);
    TEST_ASSERT_EQUAL_INT(0, a.pipPctLift);
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
// Pip monotone in lever ADC for both wirings.
// ============================================================================

void test_pip_monotone_ascending_wiring(void)
{
    SuFlaps flaps[2] = { makeCleanDetent(), makeFullDetent() };

    const int pipClean    = Pct(flaps[0].fLDMAXAOA, flaps[0]);
    const int pipFullFlap = FullFlapPipTargetPct(flaps[1]);
    const bool ascending  = (pipFullFlap >= pipClean);

    int prev = pipClean;
    for (uint16_t adc = static_cast<uint16_t>(flaps[0].iPotPosition);
         adc <= static_cast<uint16_t>(flaps[1].iPotPosition);
         adc += 25)
    {
        const size_t aidx = (adc < 2000) ? 0 : 1;
        int now = ComputeDisplayPctAnchors(adc, flaps, 2, aidx, true).pipPctLift;
        if (ascending) {
            TEST_ASSERT_TRUE_MESSAGE(now + 1 >= prev,
                "pipPctLift not monotone (ascending wiring, ascending pip)");
        } else {
            TEST_ASSERT_TRUE_MESSAGE(now - 1 <= prev,
                "pipPctLift not monotone (ascending wiring, descending pip)");
        }
        prev = now;
    }
}

// ============================================================================
// Zero-span bracket — degenerate config (two detents at the same pot).
// ============================================================================

void test_zero_span_endpoints_no_divide_by_zero(void)
{
    SuFlaps fixture[2];
    fixture[0]              = makeCleanDetent();
    fixture[1]              = makeFullDetent();
    fixture[1].iPotPosition = fixture[0].iPotPosition;   // collide ADCs

    DisplayPctAnchors a = ComputeDisplayPctAnchors(
        fixture[0].iPotPosition, fixture, 2, 0, true);

    // Pip locks at the clean endpoint when the lerp span is zero.
    TEST_ASSERT_EQUAL_INT(Pct(fixture[0].fLDMAXAOA, fixture[0]), a.pipPctLift);
    TEST_ASSERT_TRUE(a.pipPctLift         >= 0 && a.pipPctLift         <= 99);
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

    RUN_TEST(test_at_clean_detent_all_anchors_at_clean_calibration);
    RUN_TEST(test_at_full_detent_pip_at_bottom_half_donut_tones_at_full_LDmax);

    RUN_TEST(test_pip_ignores_intermediate_detent);
    RUN_TEST(test_pip_continuous_in_rawAdc_full_sweep);
    RUN_TEST(test_pip_independent_of_activeIndex);

    RUN_TEST(test_tonesOn_snaps_at_iIndex_advance);
    RUN_TEST(test_tonesOn_matches_audio_gate_per_detent);

    RUN_TEST(test_below_lowest_pot_clamps_pip_to_clean_endpoint);
    RUN_TEST(test_above_highest_pot_clamps_pip_to_full_endpoint);

    RUN_TEST(test_descending_wiring_pip_endpoints);
    RUN_TEST(test_descending_wiring_pip_midpoint);
    RUN_TEST(test_descending_wiring_endpoint_clamp);

    RUN_TEST(test_empty_flap_vector_returns_zeros);
    RUN_TEST(test_single_detent_pip_locked_at_LDmax_pct);
    RUN_TEST(test_out_of_range_active_index_clamps_safely);
    RUN_TEST(test_ias_invalid_forwarded);

    RUN_TEST(test_audio_path_setpoints_unchanged);
    RUN_TEST(test_pip_monotone_ascending_wiring);
    RUN_TEST(test_zero_span_endpoints_no_divide_by_zero);

    return UNITY_END();
}
