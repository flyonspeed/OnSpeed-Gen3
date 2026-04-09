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
// Stall volume ramp — fVolumeMult
//
// Ported from Gen2 (Tones.ino): cruise/on-speed tones play at STALL_VOL_MIN
// (0.25), stall warning plays at STALL_VOL_MAX (1.0), with a linear ramp
// between OnSpeedSlow and StallWarn in the pulsed-high region.
// ============================================================================

void test_volmult_below_ldmax_default()
{
    // No tone plays, but fVolumeMult should default to 1.0 (safe value)
    ToneResult r = calculateTone(5.0f, kClean);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, r.fVolumeMult);
}

void test_volmult_at_ldmax_floor()
{
    // Pulsed-low floor: 0.25
    ToneResult r = calculateTone(kClean.fLDMAXAOA, kClean);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, STALL_VOL_MIN, r.fVolumeMult);
}

void test_volmult_midway_ldmax_to_onspeedfast_floor()
{
    // Pulsed-low mid: still 0.25 (floor is flat across all low-tone regions)
    float fMid = (kClean.fLDMAXAOA + kClean.fONSPEEDFASTAOA) / 2.0f;
    ToneResult r = calculateTone(fMid, kClean);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, STALL_VOL_MIN, r.fVolumeMult);
}

void test_volmult_at_onspeedfast_floor()
{
    // Solid-low at OnSpeedFast: 0.25
    ToneResult r = calculateTone(kClean.fONSPEEDFASTAOA, kClean);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, STALL_VOL_MIN, r.fVolumeMult);
}

void test_volmult_midway_onspeed_region_floor()
{
    // Solid-low mid-on-speed: 0.25
    float fMid = (kClean.fONSPEEDFASTAOA + kClean.fONSPEEDSLOWAOA) / 2.0f;
    ToneResult r = calculateTone(fMid, kClean);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, STALL_VOL_MIN, r.fVolumeMult);
}

void test_volmult_at_onspeedslow_floor()
{
    // At exactly OnSpeedSlow, still in solid-low branch: 0.25
    ToneResult r = calculateTone(kClean.fONSPEEDSLOWAOA, kClean);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, STALL_VOL_MIN, r.fVolumeMult);
}

void test_volmult_just_above_onspeedslow_ramp_start()
{
    // First AOA in pulsed-high ramp: just above floor, strictly less than ceiling
    float fJustAbove = kClean.fONSPEEDSLOWAOA + 0.01f;
    ToneResult r = calculateTone(fJustAbove, kClean);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    TEST_ASSERT_TRUE(r.fVolumeMult > STALL_VOL_MIN);
    TEST_ASSERT_TRUE(r.fVolumeMult < STALL_VOL_MAX);
    // Ramp is linear from 0.25 to 1.0 over (OnSpeedSlow, StallWarn); close to floor.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, STALL_VOL_MIN, r.fVolumeMult);
}

void test_volmult_midway_ramp()
{
    // Midpoint of the ramp: halfway between 0.25 and 1.0 = 0.625
    float fMid = (kClean.fONSPEEDSLOWAOA + kClean.fSTALLWARNAOA) / 2.0f;
    ToneResult r = calculateTone(fMid, kClean);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    float fExpected = (STALL_VOL_MIN + STALL_VOL_MAX) / 2.0f;  // 0.625
    TEST_ASSERT_FLOAT_WITHIN(0.001f, fExpected, r.fVolumeMult);
}

void test_volmult_just_below_stallwarn_ramp_end()
{
    // Last AOA in ramp: just below ceiling, strictly greater than floor
    float fJustBelow = kClean.fSTALLWARNAOA - 0.01f;
    ToneResult r = calculateTone(fJustBelow, kClean);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    TEST_ASSERT_TRUE(r.fVolumeMult > STALL_VOL_MIN);
    TEST_ASSERT_TRUE(r.fVolumeMult < STALL_VOL_MAX);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, STALL_VOL_MAX, r.fVolumeMult);
}

void test_volmult_at_stallwarn_ceiling()
{
    // At stall warning: exactly 1.0
    ToneResult r = calculateTone(kClean.fSTALLWARNAOA, kClean);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, STALL_VOL_MAX, r.fVolumeMult);
}

void test_volmult_far_above_stallwarn_ceiling()
{
    // Well above stall warning: still 1.0 (clamped)
    ToneResult r = calculateTone(25.0f, kClean);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, STALL_VOL_MAX, r.fVolumeMult);
}

void test_volmult_full_flaps_solid_low_floor()
{
    // Full-flaps edge case: pulsed-low region collapses, goes straight to solid low.
    // Volume mult should still be 0.25 (floor) in solid-low.
    float fBetween = 9.5f;  // Between OnSpeedFast(9) and LDmax(10)
    ToneResult r = calculateTone(fBetween, kFullFlaps);
    TEST_ASSERT_EQUAL(EnToneType::Low, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, STALL_VOL_MIN, r.fVolumeMult);
}

void test_volmult_full_flaps_ramp_midpoint()
{
    // Full-flaps pulsed-high ramp mid: 0.625 (same formula, different thresholds)
    float fMid = (kFullFlaps.fONSPEEDSLOWAOA + kFullFlaps.fSTALLWARNAOA) / 2.0f;  // 13.5
    ToneResult r = calculateTone(fMid, kFullFlaps);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    float fExpected = (STALL_VOL_MIN + STALL_VOL_MAX) / 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, fExpected, r.fVolumeMult);
}

void test_volmult_muted_stall_firing_ceiling()
{
    // Muted mode with stall firing: volume mult at ceiling
    ToneResult r = calculateToneMuted(17.0f, 80.0f, 16.48f, 25);
    TEST_ASSERT_EQUAL(EnToneType::High, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, STALL_VOL_MAX, r.fVolumeMult);
}

void test_volmult_muted_silent_default()
{
    // Muted mode silent path: fVolumeMult defaults to 1.0 (no tone plays anyway)
    ToneResult r = calculateToneMuted(14.0f, 80.0f, 16.48f, 25);
    TEST_ASSERT_EQUAL(EnToneType::None, r.enTone);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, r.fVolumeMult);
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

    // Stall volume ramp
    RUN_TEST(test_volmult_below_ldmax_default);
    RUN_TEST(test_volmult_at_ldmax_floor);
    RUN_TEST(test_volmult_midway_ldmax_to_onspeedfast_floor);
    RUN_TEST(test_volmult_at_onspeedfast_floor);
    RUN_TEST(test_volmult_midway_onspeed_region_floor);
    RUN_TEST(test_volmult_at_onspeedslow_floor);
    RUN_TEST(test_volmult_just_above_onspeedslow_ramp_start);
    RUN_TEST(test_volmult_midway_ramp);
    RUN_TEST(test_volmult_just_below_stallwarn_ramp_end);
    RUN_TEST(test_volmult_at_stallwarn_ceiling);
    RUN_TEST(test_volmult_far_above_stallwarn_ceiling);
    RUN_TEST(test_volmult_full_flaps_solid_low_floor);
    RUN_TEST(test_volmult_full_flaps_ramp_midpoint);
    RUN_TEST(test_volmult_muted_stall_firing_ceiling);
    RUN_TEST(test_volmult_muted_silent_default);

    return UNITY_END();
}
