// test_audio_test_sweep.cpp — Unit tests for AudioTestSweep.
//
// Pins the bench-test sweep behaviour: endpoint formulas, region
// coverage, gate behaviour, and step-count math.  The actual audio
// pumping (FreeRTOS vTaskDelay, stop-button, global mutation) lives in
// the sketch and is not covered here.

#include <unity.h>
#include <audio/AudioTestSweep.h>
#include <audio/ToneCalc.h>

#include <cstdint>

using onspeed::EnToneType;
using onspeed::ToneResult;
using onspeed::ToneThresholds;
using onspeed::audio::AudioTestSweepConfig;
using onspeed::audio::AudioTestSweepStep;
using onspeed::audio::AudioTestSweepStepCount;
using onspeed::audio::GetAudioTestSweepStep;
using onspeed::audio::ShouldRunAudioTestSweep;

void setUp(void) {}
void tearDown(void) {}

// Representative RV-10-style flap setpoints used across most tests.
// LDmax 4°, OnSpeedFast 8°, OnSpeedSlow 12°, StallWarn 15°.  These are
// in the calibration neighborhood the box was designed around.
static ToneThresholds DefaultThresholds()
{
    ToneThresholds th{};
    th.fLDMAXAOA       = 4.0f;
    th.fONSPEEDFASTAOA = 8.0f;
    th.fONSPEEDSLOWAOA = 12.0f;
    th.fSTALLWARNAOA   = 15.0f;
    return th;
}

// =============================================================================
// ShouldRunAudioTestSweep — gate behaviour
// =============================================================================

void test_gate_passes_on_fully_calibrated_config(void)
{
    const ToneThresholds th = DefaultThresholds();
    TEST_ASSERT_TRUE(ShouldRunAudioTestSweep(th));
}

void test_gate_blocks_on_zero_stallwarn(void)
{
    ToneThresholds th = DefaultThresholds();
    th.fSTALLWARNAOA = 0.0f;
    TEST_ASSERT_FALSE(ShouldRunAudioTestSweep(th));
}

void test_gate_blocks_on_zero_onspeed_fast(void)
{
    // The bug PR #335's first reviewer caught: a config with StallWarn
    // set but OnSpeed setpoints zero must skip the sweep.  calculateTone
    // would otherwise return silent for the entire pulsed-low and
    // solid-low regions, leaving only the saturated stall warning at
    // the very top.
    ToneThresholds th = DefaultThresholds();
    th.fONSPEEDFASTAOA = 0.0f;
    TEST_ASSERT_FALSE(ShouldRunAudioTestSweep(th));
}

void test_gate_blocks_on_zero_onspeed_slow(void)
{
    ToneThresholds th = DefaultThresholds();
    th.fONSPEEDSLOWAOA = 0.0f;
    TEST_ASSERT_FALSE(ShouldRunAudioTestSweep(th));
}

void test_gate_blocks_on_negative_threshold(void)
{
    ToneThresholds th = DefaultThresholds();
    th.fSTALLWARNAOA = -1.0f;
    TEST_ASSERT_FALSE(ShouldRunAudioTestSweep(th));
}

void test_gate_blocks_on_all_zero_thresholds(void)
{
    ToneThresholds th{};
    TEST_ASSERT_FALSE(ShouldRunAudioTestSweep(th));
}

// LDmax can legitimately be negative (body-angle convention with high
// flap settings — see CLAUDE.md).  The gate must not block on that.
void test_gate_passes_on_negative_ldmax(void)
{
    ToneThresholds th = DefaultThresholds();
    th.fLDMAXAOA = -1.12f;
    TEST_ASSERT_TRUE(ShouldRunAudioTestSweep(th));
}

// =============================================================================
// AudioTestSweepStepCount
// =============================================================================

void test_step_count_default_is_400(void)
{
    AudioTestSweepConfig cfg{};
    TEST_ASSERT_EQUAL_UINT32(400u, AudioTestSweepStepCount(cfg));
}

void test_step_count_scales_with_duration(void)
{
    AudioTestSweepConfig cfg{};
    cfg.durationMs = 10000;
    cfg.stepMs     = 50;
    TEST_ASSERT_EQUAL_UINT32(200u, AudioTestSweepStepCount(cfg));
}

void test_step_count_zero_step_returns_zero(void)
{
    AudioTestSweepConfig cfg{};
    cfg.stepMs = 0;
    TEST_ASSERT_EQUAL_UINT32(0u, AudioTestSweepStepCount(cfg));
}

// =============================================================================
// Endpoint formulas
// =============================================================================

void test_first_step_is_below_ldmax_by_bottom_margin(void)
{
    const ToneThresholds th = DefaultThresholds();
    AudioTestSweepConfig cfg{};

    AudioTestSweepStep step = GetAudioTestSweepStep(th, cfg, 0);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, th.fLDMAXAOA - cfg.bottomMargin, step.aoaDeg);
}

void test_last_step_lands_just_below_top(void)
{
    // The sweep loop runs i = 0..nSteps-1, so the final emitted AOA is
    // (top - oneStep), not (top).  Pin that exactly so a future change
    // to the loop bounds is a deliberate decision.
    const ToneThresholds th = DefaultThresholds();
    AudioTestSweepConfig cfg{};
    const std::uint32_t nSteps = AudioTestSweepStepCount(cfg);

    const float fStartAoa = th.fLDMAXAOA    - cfg.bottomMargin;
    const float fEndAoa   = th.fSTALLWARNAOA + cfg.topMargin;
    const float fOneStep  = (fEndAoa - fStartAoa) / static_cast<float>(nSteps);

    AudioTestSweepStep step = GetAudioTestSweepStep(th, cfg, nSteps - 1);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, fEndAoa - fOneStep, step.aoaDeg);
}

void test_aoa_ramp_is_linear(void)
{
    // Step (i+1) - step (i) is constant across the sweep.
    const ToneThresholds th = DefaultThresholds();
    AudioTestSweepConfig cfg{};
    const std::uint32_t nSteps = AudioTestSweepStepCount(cfg);

    const float fStep0  = GetAudioTestSweepStep(th, cfg, 0).aoaDeg;
    const float fStep1  = GetAudioTestSweepStep(th, cfg, 1).aoaDeg;
    const float fStep10 = GetAudioTestSweepStep(th, cfg, 10).aoaDeg;
    const float fStepN  = GetAudioTestSweepStep(th, cfg, nSteps - 1).aoaDeg;

    const float fDelta = fStep1 - fStep0;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, fDelta, (fStep10 - fStep0) / 10.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, fDelta,
        (fStepN - fStep0) / static_cast<float>(nSteps - 1));
}

void test_endpoints_obey_negative_ldmax(void)
{
    // Body-angle calibrations can put LDmax below zero.  Sweep must
    // start below that, not at zero.
    ToneThresholds th = DefaultThresholds();
    th.fLDMAXAOA = -1.12f;
    AudioTestSweepConfig cfg{};

    AudioTestSweepStep step = GetAudioTestSweepStep(th, cfg, 0);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.12f - cfg.bottomMargin, step.aoaDeg);
}

// =============================================================================
// Region coverage — every tone-map region must be hit at default config
// =============================================================================

void test_sweep_covers_every_tone_region(void)
{
    const ToneThresholds th = DefaultThresholds();
    AudioTestSweepConfig cfg{};
    const std::uint32_t nSteps = AudioTestSweepStepCount(cfg);

    bool sawSilent       = false;
    bool sawPulsedLow    = false;
    bool sawSolidLow     = false;
    bool sawPulsedHigh   = false;
    bool sawSolidHigh    = false;

    for (std::uint32_t i = 0; i < nSteps; ++i)
    {
        const ToneResult r = GetAudioTestSweepStep(th, cfg, i).tone;
        if (r.enTone == EnToneType::None)
            sawSilent = true;
        else if (r.enTone == EnToneType::Low && r.fPulseFreq > 0.0f)
            sawPulsedLow = true;
        else if (r.enTone == EnToneType::Low && r.fPulseFreq == 0.0f)
            sawSolidLow = true;
        else if (r.enTone == EnToneType::High &&
                 r.fPulseFreq < onspeed::HIGH_TONE_STALL_PPS - 0.001f)
            sawPulsedHigh = true;
        else if (r.enTone == EnToneType::High &&
                 r.fPulseFreq >= onspeed::HIGH_TONE_STALL_PPS - 0.001f)
            sawSolidHigh = true;
    }

    TEST_ASSERT_TRUE_MESSAGE(sawSilent,     "expected silent region (below LDmax)");
    TEST_ASSERT_TRUE_MESSAGE(sawPulsedLow,  "expected pulsed-low region");
    TEST_ASSERT_TRUE_MESSAGE(sawSolidLow,   "expected solid-low (on-speed) region");
    TEST_ASSERT_TRUE_MESSAGE(sawPulsedHigh, "expected pulsed-high region");
    TEST_ASSERT_TRUE_MESSAGE(sawSolidHigh,  "expected solid-high (stall) region");
}

// =============================================================================
// Pulse-rate ramp monotonicity
// =============================================================================

// Within the pulsed-low region, PPS should ramp monotonically from
// LOW_TONE_PPS_MIN to LOW_TONE_PPS_MAX as AOA crosses [LDmax, OnSpeedFast].
void test_pulsed_low_pps_ramp_is_monotonic(void)
{
    const ToneThresholds th = DefaultThresholds();
    AudioTestSweepConfig cfg{};
    const std::uint32_t nSteps = AudioTestSweepStepCount(cfg);

    float prevPps = -1.0f;
    bool inRegion = false;

    for (std::uint32_t i = 0; i < nSteps; ++i)
    {
        const AudioTestSweepStep step = GetAudioTestSweepStep(th, cfg, i);
        const bool isPulsedLow =
            step.tone.enTone == EnToneType::Low && step.tone.fPulseFreq > 0.0f;

        if (isPulsedLow)
        {
            if (inRegion)
            {
                TEST_ASSERT_TRUE_MESSAGE(step.tone.fPulseFreq >= prevPps - 1e-4f,
                    "pulsed-low PPS must not decrease across the region");
            }
            prevPps  = step.tone.fPulseFreq;
            inRegion = true;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(inRegion, "expected pulsed-low region to be entered");
}

// Within the pulsed-high region, PPS ramps from HIGH_TONE_PPS_MIN to
// HIGH_TONE_PPS_MAX, AND fVolumeMult ramps from STALL_VOL_MIN to
// STALL_VOL_MAX (the Gen2 amplitude ramp).  Both must be monotonic.
void test_pulsed_high_ramps_are_monotonic(void)
{
    const ToneThresholds th = DefaultThresholds();
    AudioTestSweepConfig cfg{};
    const std::uint32_t nSteps = AudioTestSweepStepCount(cfg);

    float prevPps = -1.0f;
    float prevVol = -1.0f;
    bool inRegion = false;

    for (std::uint32_t i = 0; i < nSteps; ++i)
    {
        const AudioTestSweepStep step = GetAudioTestSweepStep(th, cfg, i);
        const bool isPulsedHigh =
            step.tone.enTone == EnToneType::High &&
            step.tone.fPulseFreq < onspeed::HIGH_TONE_STALL_PPS - 0.001f;

        if (isPulsedHigh)
        {
            if (inRegion)
            {
                TEST_ASSERT_TRUE_MESSAGE(step.tone.fPulseFreq >= prevPps - 1e-4f,
                    "pulsed-high PPS must not decrease");
                TEST_ASSERT_TRUE_MESSAGE(step.tone.fVolumeMult >= prevVol - 1e-4f,
                    "stall-volume ramp must not decrease");
            }
            prevPps  = step.tone.fPulseFreq;
            prevVol  = step.tone.fVolumeMult;
            inRegion = true;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(inRegion, "expected pulsed-high region to be entered");
}

// =============================================================================
// Final region must be saturated stall
// =============================================================================

void test_last_step_is_solid_stall_warning(void)
{
    // Top margin 1.5° puts the final step well into solid-high — the
    // test pins this so a smaller top margin is a deliberate choice.
    const ToneThresholds th = DefaultThresholds();
    AudioTestSweepConfig cfg{};
    const std::uint32_t nSteps = AudioTestSweepStepCount(cfg);

    AudioTestSweepStep last = GetAudioTestSweepStep(th, cfg, nSteps - 1);
    TEST_ASSERT_EQUAL(static_cast<int>(EnToneType::High),
                      static_cast<int>(last.tone.enTone));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, onspeed::HIGH_TONE_STALL_PPS, last.tone.fPulseFreq);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, onspeed::STALL_VOL_MAX, last.tone.fVolumeMult);
}

// =============================================================================
// main
// =============================================================================

int main(int /*argc*/, char** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_gate_passes_on_fully_calibrated_config);
    RUN_TEST(test_gate_blocks_on_zero_stallwarn);
    RUN_TEST(test_gate_blocks_on_zero_onspeed_fast);
    RUN_TEST(test_gate_blocks_on_zero_onspeed_slow);
    RUN_TEST(test_gate_blocks_on_negative_threshold);
    RUN_TEST(test_gate_blocks_on_all_zero_thresholds);
    RUN_TEST(test_gate_passes_on_negative_ldmax);

    RUN_TEST(test_step_count_default_is_400);
    RUN_TEST(test_step_count_scales_with_duration);
    RUN_TEST(test_step_count_zero_step_returns_zero);

    RUN_TEST(test_first_step_is_below_ldmax_by_bottom_margin);
    RUN_TEST(test_last_step_lands_just_below_top);
    RUN_TEST(test_aoa_ramp_is_linear);
    RUN_TEST(test_endpoints_obey_negative_ldmax);

    RUN_TEST(test_sweep_covers_every_tone_region);
    RUN_TEST(test_pulsed_low_pps_ramp_is_monotonic);
    RUN_TEST(test_pulsed_high_ramps_are_monotonic);
    RUN_TEST(test_last_step_is_solid_stall_warning);

    return UNITY_END();
}
