// test_tone_synth.cpp — Unit tests for ToneSynth (PCM tone generator).
//
// Spectral tests use a Goertzel single-bin DFT to validate that the
// synthesized sample sequence has its energy concentrated at the expected
// frequency bin.  Structural tests cover the silence/zero-amplitude/empty
// edge cases and the pulse-gating state machine.

#include <unity.h>

#include <audio/ToneSynth.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

using onspeed::audio::ApplyPulseGate;
using onspeed::audio::kSampleRateHz;
using onspeed::audio::PulseGateSpec;
using onspeed::audio::PulseGateState;
using onspeed::audio::Synthesize;
using onspeed::audio::SynthesizeLegacyCosine;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Goertzel single-bin magnitude.  Returns the (unnormalized) power at the
// target frequency.
// ============================================================================

static double GoertzelPower(const int16_t* samples, std::size_t n,
                            double targetHz, double sampleRateHz)
{
    if (n == 0) return 0.0;

    const double w     = 2.0 * M_PI * targetHz / sampleRateHz;
    const double coeff = 2.0 * std::cos(w);

    double q1 = 0.0;
    double q2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double q0 = coeff * q1 - q2 + static_cast<double>(samples[i]);
        q2 = q1;
        q1 = q0;
    }
    // magnitude^2 = q1^2 + q2^2 - q1*q2*coeff
    return q1 * q1 + q2 * q2 - q1 * q2 * coeff;
}

static double TotalEnergy(const int16_t* samples, std::size_t n)
{
    double e = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = static_cast<double>(samples[i]);
        e += s * s;
    }
    return e;
}

// ============================================================================
// Structural tests
// ============================================================================

void test_synthesize_null_buffer_is_noop(void)
{
    // Must not crash when out==nullptr
    float p = Synthesize(1000.0f, 25000, 16000, nullptr, 0, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, p);
}

void test_synthesize_zero_frequency_produces_silence(void)
{
    std::vector<int16_t> buf(128, 12345);  // preload garbage
    float p = Synthesize(0.0f, 25000, 16000, buf.data(), buf.size(), 0.7f);
    for (auto s : buf) TEST_ASSERT_EQUAL_INT16(0, s);
    // phase unchanged
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.7f, p);
}

void test_synthesize_zero_amplitude_produces_silence(void)
{
    std::vector<int16_t> buf(128, 12345);
    float p = Synthesize(400.0f, 0, 16000, buf.data(), buf.size(), 0.0f);
    for (auto s : buf) TEST_ASSERT_EQUAL_INT16(0, s);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, p);
}

void test_synthesize_bad_sample_rate_produces_silence(void)
{
    std::vector<int16_t> buf(32, 999);
    float p = Synthesize(400.0f, 25000, 0, buf.data(), buf.size(), 0.0f);
    for (auto s : buf) TEST_ASSERT_EQUAL_INT16(0, s);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, p);
}

void test_synthesize_phase_continuous_across_calls(void)
{
    // Two 400 Hz runs concatenated should produce the same output as one
    // run of twice the length.
    const std::size_t kN = 1024;
    std::vector<int16_t> single(2 * kN);
    Synthesize(400.0f, 25000, 16000, single.data(), single.size(), 0.0f);

    std::vector<int16_t> chainedA(kN), chainedB(kN);
    float ph = Synthesize(400.0f, 25000, 16000, chainedA.data(), kN, 0.0f);
    Synthesize(400.0f, 25000, 16000, chainedB.data(), kN, ph);

    // Expect exact match at the seam (next sample predictable)
    // Allow ±2 counts of slop for float roundoff in the phase handoff.
    for (std::size_t i = 0; i < kN; ++i) {
        TEST_ASSERT_INT16_WITHIN(2, single[i], chainedA[i]);
        TEST_ASSERT_INT16_WITHIN(2, single[kN + i], chainedB[i]);
    }
}

void test_legacy_cosine_starts_at_positive_peak(void)
{
    std::vector<int16_t> buf(128);
    SynthesizeLegacyCosine(400.0f, 16000, buf.data(), buf.size());
    // cos(0) = 1, so first sample should be near +25000.
    TEST_ASSERT_INT16_WITHIN(10, 25000, buf[0]);
}

void test_legacy_cosine_silent_on_bad_rate(void)
{
    std::vector<int16_t> buf(16, 77);
    SynthesizeLegacyCosine(400.0f, -1, buf.data(), buf.size());
    // Unchanged (early return doesn't wipe the buffer)
    TEST_ASSERT_EQUAL_INT16(77, buf[0]);
}

// ============================================================================
// Spectral tests: Goertzel on synthesized tone buffers
// ============================================================================

static void AssertSpectralPeak(const std::vector<int16_t>& samples,
                               double expectedHz,
                               double sampleRateHz,
                               double snrDbThreshold)
{
    const double atExpected = GoertzelPower(samples.data(), samples.size(),
                                            expectedHz, sampleRateHz);
    const double totalEnergy = TotalEnergy(samples.data(), samples.size());

    // For a pure cosine, Goertzel magnitude^2 ≈ (N/2 * A)^2
    // Parseval says total energy = N * A^2 / 2.
    // Ratio magnitude^2 / totalEnergy ≈ N/2 — so for N=8192, ratio ≈ 4096.
    // Call it "SNR" in dB: 10*log10(ratio).
    TEST_ASSERT_TRUE(totalEnergy > 0.0);
    const double snrDb = 10.0 * std::log10(atExpected / totalEnergy);

    // Peak should be concentrated at the expected bin.  For a synthesized
    // tone this is typically ~35 dB; require the caller's threshold.
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "Expected spectral peak at %.1f Hz (got %.1f dB, threshold %.1f dB)",
                  expectedHz, snrDb, snrDbThreshold);
    TEST_ASSERT_TRUE_MESSAGE(snrDb > snrDbThreshold, msg);
}

void test_synthesize_400hz_spectral_peak(void)
{
    const std::size_t kN = 8192;
    std::vector<int16_t> buf(kN);
    Synthesize(400.0f, 25000, 16000, buf.data(), kN, 0.0f);
    AssertSpectralPeak(buf, 400.0, 16000.0, 30.0);
}

void test_synthesize_1000hz_spectral_peak(void)
{
    const std::size_t kN = 8192;
    std::vector<int16_t> buf(kN);
    Synthesize(1000.0f, 25000, 16000, buf.data(), kN, 0.0f);
    AssertSpectralPeak(buf, 1000.0, 16000.0, 30.0);
}

void test_synthesize_1600hz_spectral_peak(void)
{
    const std::size_t kN = 8192;
    std::vector<int16_t> buf(kN);
    Synthesize(1600.0f, 25000, 16000, buf.data(), kN, 0.0f);
    AssertSpectralPeak(buf, 1600.0, 16000.0, 30.0);
}

void test_synthesize_wrong_bin_is_weaker(void)
{
    const std::size_t kN = 8192;
    std::vector<int16_t> buf(kN);
    Synthesize(400.0f, 25000, 16000, buf.data(), kN, 0.0f);
    const double atTarget = GoertzelPower(buf.data(), kN, 400.0, 16000.0);
    const double atOff    = GoertzelPower(buf.data(), kN, 1000.0, 16000.0);
    // 600 Hz away, power should be *vastly* smaller.
    TEST_ASSERT_TRUE(atTarget > 100.0 * atOff);
}

void test_legacy_cosine_400hz_spectral_peak(void)
{
    const std::size_t kN = 8192;
    std::vector<int16_t> buf(kN);
    SynthesizeLegacyCosine(400.0f, 16000, buf.data(), kN);
    AssertSpectralPeak(buf, 400.0, 16000.0, 30.0);
}

void test_legacy_cosine_1600hz_spectral_peak(void)
{
    const std::size_t kN = 8192;
    std::vector<int16_t> buf(kN);
    SynthesizeLegacyCosine(1600.0f, 16000, buf.data(), kN);
    AssertSpectralPeak(buf, 1600.0, 16000.0, 30.0);
}

// ============================================================================
// Pulse-gate tests
// ============================================================================

void test_pulse_gate_passthrough_when_disabled(void)
{
    std::vector<int16_t> src(256, 10000);
    std::vector<int16_t> dst(256, 0);

    PulseGateSpec spec{};  // halfPeriodSamples = 0 disables
    PulseGateState state{};

    ApplyPulseGate(src.data(), dst.data(), src.size(), 1.0f, spec, state);
    for (auto v : dst) TEST_ASSERT_EQUAL_INT16(10000, v);
}

void test_pulse_gate_base_scale_applied_when_disabled(void)
{
    std::vector<int16_t> src(64, 10000);
    std::vector<int16_t> dst(64, 0);

    PulseGateSpec spec{};
    PulseGateState state{};

    ApplyPulseGate(src.data(), dst.data(), src.size(), 0.5f, spec, state);
    for (auto v : dst) TEST_ASSERT_EQUAL_INT16(5000, v);
}

void test_pulse_gate_toggles_after_half_period(void)
{
    // 10 PPS at 16 kHz → half-period = 16000 / (10 * 2) = 800 samples.
    // Span 1600 samples to cross both the on->off and off->on edges.
    const std::size_t kN = 1600;
    std::vector<int16_t> src(kN, 10000);
    std::vector<int16_t> dst(kN, 0);

    PulseGateSpec spec;
    spec.halfPeriodSamples = 800.0f;
    spec.offScale          = 0.0f;

    PulseGateState state{};
    ApplyPulseGate(src.data(), dst.data(), kN, 1.0f, spec, state);

    // First sample: on phase.
    TEST_ASSERT_EQUAL_INT16(10000, dst[0]);
    // Somewhere before 800, still on.
    TEST_ASSERT_EQUAL_INT16(10000, dst[400]);
    // After crossing the half-period (toggles after sample 800 is written),
    // sample 801 is off.
    TEST_ASSERT_EQUAL_INT16(0, dst[1000]);
    // Sample 1599 is the last sample in the off half (toggle would happen
    // after sample 1600's output).
    TEST_ASSERT_EQUAL_INT16(0, dst[1599]);
}

void test_pulse_gate_off_scale_half_power(void)
{
    // Verify the "off" half uses spec.offScale.
    const std::size_t kN = 1000;
    std::vector<int16_t> src(kN, 20000);
    std::vector<int16_t> dst(kN, 0);

    PulseGateSpec spec;
    spec.halfPeriodSamples = 500.0f;
    spec.offScale          = 0.2f;  // legacy "ducked" level

    PulseGateState state{};
    ApplyPulseGate(src.data(), dst.data(), kN, 1.0f, spec, state);

    // First sample: on
    TEST_ASSERT_EQUAL_INT16(20000, dst[0]);
    // After half-period: off at 0.2 × input = 4000
    TEST_ASSERT_INT16_WITHIN(5, 4000, dst[600]);
}

void test_pulse_gate_state_advances_across_calls(void)
{
    // Two halves of a buffer called separately should produce the same
    // gated output as a single-shot call.
    const std::size_t kN = 1600;
    std::vector<int16_t> src(kN, 10000);
    std::vector<int16_t> one(kN, 0), twoA(kN / 2, 0), twoB(kN / 2, 0);

    PulseGateSpec spec;
    spec.halfPeriodSamples = 800.0f;
    spec.offScale          = 0.0f;

    PulseGateState s1{}, s2{};
    ApplyPulseGate(src.data(), one.data(), kN, 1.0f, spec, s1);
    ApplyPulseGate(src.data(), twoA.data(), kN / 2, 1.0f, spec, s2);
    ApplyPulseGate(src.data() + kN / 2, twoB.data(), kN / 2, 1.0f, spec, s2);

    for (std::size_t i = 0; i < kN / 2; ++i) {
        TEST_ASSERT_EQUAL_INT16(one[i], twoA[i]);
        TEST_ASSERT_EQUAL_INT16(one[kN / 2 + i], twoB[i]);
    }
}

void test_pulse_gate_clamps_to_int16(void)
{
    // Scale above 1.0 that would push beyond int16 range gets clamped.
    std::vector<int16_t> src(16, 30000);
    std::vector<int16_t> dst(16, 0);

    PulseGateSpec spec{};   // pulsing disabled → straight scaling
    PulseGateState state{};

    ApplyPulseGate(src.data(), dst.data(), src.size(), 2.0f, spec, state);
    for (auto v : dst) TEST_ASSERT_EQUAL_INT16(32767, v);

    std::vector<int16_t> nsrc(16, -30000);
    ApplyPulseGate(nsrc.data(), dst.data(), nsrc.size(), 2.0f, spec, state);
    for (auto v : dst) TEST_ASSERT_EQUAL_INT16(-32768, v);
}

void test_pulse_gate_null_inputs_noop(void)
{
    // Must not crash
    PulseGateSpec spec{};
    PulseGateState state{};
    ApplyPulseGate(nullptr, nullptr, 0, 1.0f, spec, state);
    ApplyPulseGate(nullptr, nullptr, 10, 1.0f, spec, state);
    TEST_PASS();
}

// ============================================================================

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_synthesize_null_buffer_is_noop);
    RUN_TEST(test_synthesize_zero_frequency_produces_silence);
    RUN_TEST(test_synthesize_zero_amplitude_produces_silence);
    RUN_TEST(test_synthesize_bad_sample_rate_produces_silence);
    RUN_TEST(test_synthesize_phase_continuous_across_calls);
    RUN_TEST(test_legacy_cosine_starts_at_positive_peak);
    RUN_TEST(test_legacy_cosine_silent_on_bad_rate);

    RUN_TEST(test_synthesize_400hz_spectral_peak);
    RUN_TEST(test_synthesize_1000hz_spectral_peak);
    RUN_TEST(test_synthesize_1600hz_spectral_peak);
    RUN_TEST(test_synthesize_wrong_bin_is_weaker);
    RUN_TEST(test_legacy_cosine_400hz_spectral_peak);
    RUN_TEST(test_legacy_cosine_1600hz_spectral_peak);

    RUN_TEST(test_pulse_gate_passthrough_when_disabled);
    RUN_TEST(test_pulse_gate_base_scale_applied_when_disabled);
    RUN_TEST(test_pulse_gate_toggles_after_half_period);
    RUN_TEST(test_pulse_gate_off_scale_half_power);
    RUN_TEST(test_pulse_gate_state_advances_across_calls);
    RUN_TEST(test_pulse_gate_clamps_to_int16);
    RUN_TEST(test_pulse_gate_null_inputs_noop);

    return UNITY_END();
}
