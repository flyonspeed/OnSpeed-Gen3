// test_tone_calc.cpp - Unit tests for ToneCalc (extracted from Audio.cpp)

#include <unity.h>
#include <ToneCalc.h>

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
// Main
// ============================================================================

int main(int argc, char **argv)
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

    return UNITY_END();
}
