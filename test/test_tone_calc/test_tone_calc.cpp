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

void test_ldmax_zero_is_a_valid_floor_not_an_uncalibrated_signal()
{
    // LDmax is the only setpoint that lives near or below the
    // body-angle reference (the wing produces L/D-max lift at low
    // fuselage angles, especially with flaps).  fLDMAXAOA == 0 is a
    // valid floor — it means "L/D-max body angle is at the wind line"
    // — not an "uncalibrated" signal.  The other three setpoints
    // (FAST/SLOW/StallWarn) sit between zero-lift and stall and are
    // always positive on a real calibration; the gate keys off them.
    //
    // With LDmax=0, FAST=11, AOA=5 falls in the pulsed-low region,
    // so a tone must fire.
    ToneThresholds cfg = {
        .fLDMAXAOA       =  0.0f,
        .fONSPEEDFASTAOA = 11.0f,
        .fONSPEEDSLOWAOA = 14.0f,
        .fSTALLWARNAOA   = 16.0f,
    };
    ToneResult r = calculateTone(5.0f, cfg);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
}

// ============================================================================
// Body-angle convention: LDmax can be negative at high flap settings.
// Reference data: N720AK (RV-10) `2_20_26_config.cfg`, the calibration
// that surfaced this bug.  AHRS=EKF6, body-angle convention throughout.
// ============================================================================

// Flaps 0° (clean): all positive, narrow LDmax-to-OnSpeedFast band.
static const ToneThresholds kN720AK_Flaps0 = {
    .fLDMAXAOA       = 4.10f,
    .fONSPEEDFASTAOA = 4.08f,   // narrow band; pulsed-low collapses
    .fONSPEEDSLOWAOA = 4.95f,
    .fSTALLWARNAOA   = 7.98f,
};

// Flaps 16°: all positive, normal ordering.
static const ToneThresholds kN720AK_Flaps16 = {
    .fLDMAXAOA       = 2.30f,
    .fONSPEEDFASTAOA = 2.69f,
    .fONSPEEDSLOWAOA = 4.10f,
    .fSTALLWARNAOA   = 7.60f,
};

// Flaps 33° (full): LDmax NEGATIVE under body-angle convention.
// This is the configuration that was silenced by the all-four > 0 gate.
static const ToneThresholds kN720AK_Flaps33 = {
    .fLDMAXAOA       = -1.12f,
    .fONSPEEDFASTAOA =  1.80f,
    .fONSPEEDSLOWAOA =  4.00f,
    .fSTALLWARNAOA   =  9.24f,
};

void test_n720ak_flaps0_stall_warning_fires()
{
    ToneResult r = calculateTone(8.5f, kN720AK_Flaps0);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, HIGH_TONE_STALL_PPS, r.fPulseFreq);
}

void test_n720ak_flaps16_onspeed_region_solid_low()
{
    // AOA between OnSpeedFast (2.69) and OnSpeedSlow (4.10).
    ToneResult r = calculateTone(3.5f, kN720AK_Flaps16);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.fPulseFreq);
}

void test_n720ak_flaps33_negative_ldmax_does_not_silence_pulsed_low()
{
    // The bug: with the all-four > 0 gate, this returned None for
    // every AOA.  AOA = 0.5° sits in the pulsed-low region between
    // LDmax (-1.12) and OnSpeedFast (1.80) and must produce a Low
    // tone.  Negative LDmax is correct under the body-angle
    // convention at full flaps.
    ToneResult r = calculateTone(0.5f, kN720AK_Flaps33);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_TRUE(r.fPulseFreq >= LOW_TONE_PPS_MIN);
    TEST_ASSERT_TRUE(r.fPulseFreq <= LOW_TONE_PPS_MAX);
}

void test_n720ak_flaps33_negative_ldmax_onspeed_region_solid_low()
{
    // Approach AOA between OnSpeedFast (1.80) and OnSpeedSlow (4.00).
    ToneResult r = calculateTone(3.0f, kN720AK_Flaps33);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.fPulseFreq);
}

void test_n720ak_flaps33_negative_ldmax_stall_warning_fires()
{
    // The most safety-critical case: at full flaps with negative
    // LDmax, the stall warning at AOA >= 9.24° must still fire.
    ToneResult r = calculateTone(10.0f, kN720AK_Flaps33);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, HIGH_TONE_STALL_PPS, r.fPulseFreq);
}

void test_n720ak_flaps33_below_ldmax_silent()
{
    // Below LDmax (-1.12) is the genuinely-too-fast region; no tone.
    ToneResult r = calculateTone(-2.0f, kN720AK_Flaps33);
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
    RUN_TEST(test_ldmax_zero_is_a_valid_floor_not_an_uncalibrated_signal);
    RUN_TEST(test_muted_mode_fires_with_only_stallwarn_configured);

    // Body-angle convention: LDmax can be negative at full flaps
    // (N720AK reference calibration)
    RUN_TEST(test_n720ak_flaps0_stall_warning_fires);
    RUN_TEST(test_n720ak_flaps16_onspeed_region_solid_low);
    RUN_TEST(test_n720ak_flaps33_negative_ldmax_does_not_silence_pulsed_low);
    RUN_TEST(test_n720ak_flaps33_negative_ldmax_onspeed_region_solid_low);
    RUN_TEST(test_n720ak_flaps33_negative_ldmax_stall_warning_fires);
    RUN_TEST(test_n720ak_flaps33_below_ldmax_silent);

    return UNITY_END();
}
