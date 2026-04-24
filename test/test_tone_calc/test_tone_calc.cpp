// test_tone_calc.cpp - Unit tests for ToneCalc (extracted from Audio.cpp)

#include <unity.h>
#include <audio/ToneCalc.h>

using namespace onspeed;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Test fixture: typical RV-4 clean (flaps 0) thresholds
// ============================================================================

static const ToneThresholds kClean = {
    .fLDMAXAOA       =  8.03f,
    .fONSPEEDFASTAOA = 11.25f,
    .fONSPEEDSLOWAOA = 13.84f,
    .fSTALLWARNAOA   = 16.48f,
};

// Full flaps: LDmax >= OnSpeedFast (pulsed low region collapses)
static const ToneThresholds kFullFlaps = {
    .fLDMAXAOA       = 10.0f,
    .fONSPEEDFASTAOA =  9.0f,   // LDmax > OnSpeedFast
    .fONSPEEDSLOWAOA = 12.0f,
    .fSTALLWARNAOA   = 15.0f,
};

// ============================================================================
// calculateTone — AOA region tests
// ============================================================================

void test_below_ldmax_no_tone()
{
    ToneResult r = calculateTone(5.0f, kClean);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.fPulseFreq);
}

void test_at_ldmax_pulsed_low()
{
    ToneResult r = calculateTone(kClean.fLDMAXAOA, kClean);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    // At the bottom of the range → LOW_TONE_PPS_MIN
    TEST_ASSERT_FLOAT_WITHIN(0.01f, LOW_TONE_PPS_MIN, r.fPulseFreq);
}

void test_midway_ldmax_to_onspeedfast_pulsed_low()
{
    float fMid = (kClean.fLDMAXAOA + kClean.fONSPEEDFASTAOA) / 2.0f;
    ToneResult r = calculateTone(fMid, kClean);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    float fExpected = (LOW_TONE_PPS_MIN + LOW_TONE_PPS_MAX) / 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, fExpected, r.fPulseFreq);
}

void test_at_onspeedfast_solid_low()
{
    ToneResult r = calculateTone(kClean.fONSPEEDFASTAOA, kClean);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.fPulseFreq);
}

void test_onspeed_region_solid_low()
{
    float fMid = (kClean.fONSPEEDFASTAOA + kClean.fONSPEEDSLOWAOA) / 2.0f;
    ToneResult r = calculateTone(fMid, kClean);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.fPulseFreq);
}

void test_at_onspeeslow_solid_low()
{
    // >= OnSpeedFast and <= OnSpeedSlow → solid low
    ToneResult r = calculateTone(kClean.fONSPEEDSLOWAOA, kClean);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.fPulseFreq);
}

void test_above_onspeedslow_pulsed_high()
{
    // Just above OnSpeedSlow → pulsed high, near PPS_MIN
    float fJustAbove = kClean.fONSPEEDSLOWAOA + 0.01f;
    ToneResult r = calculateTone(fJustAbove, kClean);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    TEST_ASSERT_TRUE(r.fPulseFreq >= HIGH_TONE_PPS_MIN);
    TEST_ASSERT_TRUE(r.fPulseFreq < HIGH_TONE_PPS_MAX);
}

void test_midway_onspeedslow_to_stallwarn_pulsed_high()
{
    float fMid = (kClean.fONSPEEDSLOWAOA + kClean.fSTALLWARNAOA) / 2.0f;
    ToneResult r = calculateTone(fMid, kClean);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    float fExpected = (HIGH_TONE_PPS_MIN + HIGH_TONE_PPS_MAX) / 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, fExpected, r.fPulseFreq);
}

void test_at_stallwarn_stall_tone()
{
    ToneResult r = calculateTone(kClean.fSTALLWARNAOA, kClean);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, HIGH_TONE_STALL_PPS, r.fPulseFreq);
}

void test_above_stallwarn_stall_tone()
{
    ToneResult r = calculateTone(25.0f, kClean);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, HIGH_TONE_STALL_PPS, r.fPulseFreq);
}

// ============================================================================
// Edge case: full flaps (LDmax >= OnSpeedFast — pulsed low region collapses)
// ============================================================================

void test_full_flaps_skips_pulsed_low()
{
    // AOA between OnSpeedFast and LDmax → no pulsed low, goes straight to solid low
    // (because LDmax >= OnSpeedFast, the guard `th.fLDMAXAOA < th.fONSPEEDFASTAOA` is false)
    float fBetween = 9.5f;  // Between OnSpeedFast(9) and LDmax(10)
    ToneResult r = calculateTone(fBetween, kFullFlaps);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.fPulseFreq);
}

void test_full_flaps_below_both_no_tone()
{
    ToneResult r = calculateTone(7.0f, kFullFlaps);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
}

// ============================================================================
// calculateToneMuted
// ============================================================================

void test_muted_stall_warning_fires()
{
    ToneResult r = calculateToneMuted(17.0f, 80.0f, 16.48f, 25);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, HIGH_TONE_STALL_PPS, r.fPulseFreq);
}

void test_muted_below_stallwarn_silent()
{
    ToneResult r = calculateToneMuted(14.0f, 80.0f, 16.48f, 25);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
}

void test_muted_low_ias_silent()
{
    // Even with high AOA, if IAS is below mute threshold → silent
    ToneResult r = calculateToneMuted(17.0f, 20.0f, 16.48f, 25);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
}

// ============================================================================
// Uncalibrated gate — fSTALLWARNAOA <= 0 means "factory/unconfigured",
// and the tone logic must stay silent.  Without this gate, the default
// all-zero config would produce a constant stall warning above the
// mute-under-IAS threshold: AOA (any non-negative) >= fSTALLWARNAOA (0)
// is trivially true.  Silence is the right answer when the user hasn't
// told us what the stall is.
// ============================================================================

static const ToneThresholds kUncalibrated = {
    .fLDMAXAOA       = 0.0f,
    .fONSPEEDFASTAOA = 0.0f,
    .fONSPEEDSLOWAOA = 0.0f,
    .fSTALLWARNAOA   = 0.0f,
};

void test_uncalibrated_stall_warn_produces_no_tone_at_zero_aoa()
{
    ToneResult r = calculateTone(0.0f, kUncalibrated);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.fPulseFreq);
}

void test_uncalibrated_stall_warn_produces_no_tone_at_positive_aoa()
{
    // Would otherwise fire the `AOA >= fSTALLWARNAOA` branch with 5.0 >= 0.
    ToneResult r = calculateTone(5.0f, kUncalibrated);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
}

void test_uncalibrated_stall_warn_produces_no_tone_even_at_high_aoa()
{
    // Anywhere in the flight envelope — uncalibrated stays silent.
    ToneResult r = calculateTone(25.0f, kUncalibrated);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
}

void test_uncalibrated_stall_warn_produces_no_tone_at_negative_aoa()
{
    // Defensive: a negative AOA (e.g. during a push-over) must not
    // accidentally trip the gate because we used > 0 and the AOA
    // slipped past a subtle sign test.
    ToneResult r = calculateTone(-3.0f, kUncalibrated);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
}

void test_muted_uncalibrated_stays_silent_above_ias_threshold()
{
    // Muted mode only lets the stall warning through — but with
    // fSTALLWARNAOA == 0, there's no meaningful stall threshold to
    // pass, so it must stay silent at any AOA.
    ToneResult r = calculateToneMuted(17.0f, 80.0f, 0.0f, 25);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
}

void test_partial_calibration_silent_at_intermediate_aoa()
{
    // Partial calibration: only StallWarn is set, the rest at zero.
    // Without a stricter gate, the fAOA > fONSPEEDSLOWAOA (0) branch
    // would fire a pulsed high tone for any positive AOA — spurious
    // "approaching stall" cues for a plane flying in cruise.  The
    // gate treats any zeroed threshold as "uncalibrated" and stays
    // silent.
    ToneThresholds partial = kUncalibrated;
    partial.fSTALLWARNAOA = 16.0f;
    ToneResult r = calculateTone(5.0f, partial);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
}

void test_partial_calibration_silent_above_stallwarn()
{
    // Above the configured stall-warning threshold but with other
    // setpoints still at zero: silent, because we don't trust a
    // half-configured calibration enough to fire a stall tone.  The
    // user sees the SetpointOrderError warning on the config page
    // instead.
    ToneThresholds partial = kUncalibrated;
    partial.fSTALLWARNAOA = 16.0f;
    ToneResult r = calculateTone(18.0f, partial);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
}

void test_partial_calibration_missing_ldmax_only()
{
    // The subtle case: three of four setpoints are sensibly
    // configured, only LDmax is still at zero.  Without the stricter
    // gate, AOA > fONSPEEDSLOWAOA still fires correctly and
    // AOA < fONSPEEDFASTAOA falls into the `fAOA >= fLDMAXAOA (0)`
    // branch, producing a pulsed-low tone for any positive AOA.
    // The gate silences this until the user fills in LDmax too.
    ToneThresholds partial = {
        .fLDMAXAOA       =  0.0f,   // missing
        .fONSPEEDFASTAOA = 11.0f,
        .fONSPEEDSLOWAOA = 14.0f,
        .fSTALLWARNAOA   = 16.0f,
    };
    ToneResult r = calculateTone(5.0f, partial);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
}

void test_muted_mode_fires_with_only_stallwarn_configured()
{
    // Muted mode is a narrower gate: it only needs a stall-warning
    // threshold to cut through user-muted audio.  The other
    // setpoints aren't used in this path, so a partial config with
    // just StallWarn is enough for muted-mode stall warning to fire
    // above the IAS threshold.
    ToneResult r = calculateToneMuted(18.0f, 80.0f, 16.0f, 25);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    UNITY_BEGIN();

    // Normal AOA regions
    RUN_TEST(test_below_ldmax_no_tone);
    RUN_TEST(test_at_ldmax_pulsed_low);
    RUN_TEST(test_midway_ldmax_to_onspeedfast_pulsed_low);
    RUN_TEST(test_at_onspeedfast_solid_low);
    RUN_TEST(test_onspeed_region_solid_low);
    RUN_TEST(test_at_onspeeslow_solid_low);
    RUN_TEST(test_above_onspeedslow_pulsed_high);
    RUN_TEST(test_midway_onspeedslow_to_stallwarn_pulsed_high);
    RUN_TEST(test_at_stallwarn_stall_tone);
    RUN_TEST(test_above_stallwarn_stall_tone);

    // Full flaps edge case
    RUN_TEST(test_full_flaps_skips_pulsed_low);
    RUN_TEST(test_full_flaps_below_both_no_tone);

    // Muted mode
    RUN_TEST(test_muted_stall_warning_fires);
    RUN_TEST(test_muted_below_stallwarn_silent);
    RUN_TEST(test_muted_low_ias_silent);

    // Uncalibrated gate
    RUN_TEST(test_uncalibrated_stall_warn_produces_no_tone_at_zero_aoa);
    RUN_TEST(test_uncalibrated_stall_warn_produces_no_tone_at_positive_aoa);
    RUN_TEST(test_uncalibrated_stall_warn_produces_no_tone_even_at_high_aoa);
    RUN_TEST(test_uncalibrated_stall_warn_produces_no_tone_at_negative_aoa);
    RUN_TEST(test_muted_uncalibrated_stays_silent_above_ias_threshold);
    RUN_TEST(test_partial_calibration_silent_at_intermediate_aoa);
    RUN_TEST(test_partial_calibration_silent_above_stallwarn);
    RUN_TEST(test_partial_calibration_missing_ldmax_only);
    RUN_TEST(test_muted_mode_fires_with_only_stallwarn_configured);

    return UNITY_END();
}
