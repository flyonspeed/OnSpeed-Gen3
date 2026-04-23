// test_audio_mixer.cpp — Unit tests for the AudioMixer
// (stereo gain/pan/pulse-gate composition).

#include <unity.h>

#include <audio/AudioMixer.h>
#include <audio/ToneSynth.h>

#include <cstdint>
#include <vector>

using onspeed::audio::Mix;
using onspeed::audio::MixerInputs;
using onspeed::audio::MixerState;
using onspeed::audio::PackStereoI16;
using onspeed::audio::PulseGateSpec;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// PackStereoI16
// ============================================================================

void test_pack_stereo_zero(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, PackStereoI16(0, 0));
}

void test_pack_stereo_left_only(void)
{
    // low 16 bits should hold the left sample
    std::uint32_t w = PackStereoI16(0x1234, 0);
    TEST_ASSERT_EQUAL_UINT32(0x00001234u, w);
}

void test_pack_stereo_right_only(void)
{
    std::uint32_t w = PackStereoI16(0, 0x5678);
    TEST_ASSERT_EQUAL_UINT32(0x56780000u, w);
}

void test_pack_stereo_both(void)
{
    std::uint32_t w = PackStereoI16(0x0001, -1);
    // -1 as int16 is 0xFFFF as uint16.
    TEST_ASSERT_EQUAL_UINT32(0xFFFF0001u, w);
}

// ============================================================================
// Mix — basic sanity
// ============================================================================

void test_mix_null_input_is_noop(void)
{
    std::vector<std::int16_t> out(8, 99);
    MixerInputs inp;
    inp.in = nullptr;
    MixerState st;
    Mix(inp, out.data(), 4, st);
    for (auto v : out) TEST_ASSERT_EQUAL_INT16(99, v);
}

void test_mix_null_output_is_noop(void)
{
    std::int16_t in[4] = {1, 2, 3, 4};
    MixerInputs inp;
    inp.in = in;
    MixerState st;
    // Should not crash
    Mix(inp, nullptr, 4, st);
    TEST_PASS();
}

void test_mix_zero_frames_is_noop(void)
{
    std::int16_t in[4] = {1, 2, 3, 4};
    std::vector<std::int16_t> out(8, 42);
    MixerInputs inp;
    inp.in = in;
    MixerState st;
    Mix(inp, out.data(), 0, st);
    for (auto v : out) TEST_ASSERT_EQUAL_INT16(42, v);
}

void test_mix_unity_gain_passthrough(void)
{
    std::int16_t in[4] = { 1000, -500, 32000, -32000 };
    std::vector<std::int16_t> out(8, 0);

    MixerInputs inp;
    inp.in = in;
    inp.leftScale  = 1.0f;
    inp.rightScale = 1.0f;

    MixerState st;
    Mix(inp, out.data(), 4, st);

    for (std::size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_INT16(in[i], out[2 * i + 0]);
        TEST_ASSERT_EQUAL_INT16(in[i], out[2 * i + 1]);
    }
}

void test_mix_zero_gain_produces_silence(void)
{
    std::int16_t in[4] = { 1000, -500, 32000, -32000 };
    std::vector<std::int16_t> out(8, 99);

    MixerInputs inp;
    inp.in = in;
    inp.leftScale  = 0.0f;
    inp.rightScale = 0.0f;

    MixerState st;
    Mix(inp, out.data(), 4, st);

    for (auto v : out) TEST_ASSERT_EQUAL_INT16(0, v);
}

// ============================================================================
// Mix — pan
// ============================================================================

void test_mix_pan_full_left(void)
{
    std::int16_t in[4] = { 10000, 10000, 10000, 10000 };
    std::vector<std::int16_t> out(8, 0);

    MixerInputs inp;
    inp.in = in;
    inp.leftScale  = 1.0f;
    inp.rightScale = 0.0f;

    MixerState st;
    Mix(inp, out.data(), 4, st);

    for (std::size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_INT16(10000, out[2 * i + 0]);
        TEST_ASSERT_EQUAL_INT16(0,     out[2 * i + 1]);
    }
}

void test_mix_pan_full_right(void)
{
    std::int16_t in[4] = { 10000, 10000, 10000, 10000 };
    std::vector<std::int16_t> out(8, 0);

    MixerInputs inp;
    inp.in = in;
    inp.leftScale  = 0.0f;
    inp.rightScale = 1.0f;

    MixerState st;
    Mix(inp, out.data(), 4, st);

    for (std::size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_INT16(0,     out[2 * i + 0]);
        TEST_ASSERT_EQUAL_INT16(10000, out[2 * i + 1]);
    }
}

void test_mix_pan_center_half_volume(void)
{
    std::int16_t in[4] = { 10000, 10000, 10000, 10000 };
    std::vector<std::int16_t> out(8, 0);

    MixerInputs inp;
    inp.in = in;
    inp.leftScale  = 0.5f;
    inp.rightScale = 0.5f;

    MixerState st;
    Mix(inp, out.data(), 4, st);

    for (std::size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_INT16_WITHIN(1, 5000, out[2 * i + 0]);
        TEST_ASSERT_INT16_WITHIN(1, 5000, out[2 * i + 1]);
    }
}

void test_mix_asymmetric_pan(void)
{
    // Matches the left_speaker / right_speaker cal-voice pattern:
    // left = full, right = 0.25x.
    std::int16_t in[4] = { 20000, 20000, 20000, 20000 };
    std::vector<std::int16_t> out(8, 0);

    MixerInputs inp;
    inp.in = in;
    inp.leftScale  = 1.0f;
    inp.rightScale = 0.25f;

    MixerState st;
    Mix(inp, out.data(), 4, st);

    for (std::size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_INT16_WITHIN(1, 20000, out[2 * i + 0]);
        TEST_ASSERT_INT16_WITHIN(1,  5000, out[2 * i + 1]);
    }
}

// ============================================================================
// Mix — clamping (audit #019 companion)
// ============================================================================

void test_mix_clamps_positive_overflow(void)
{
    std::int16_t in[4] = { 30000, 30000, 30000, 30000 };
    std::vector<std::int16_t> out(8, 0);

    MixerInputs inp;
    inp.in = in;
    inp.leftScale  = 2.0f;   // 30000 * 2 = 60000 → clamp
    inp.rightScale = 2.0f;

    MixerState st;
    Mix(inp, out.data(), 4, st);

    for (std::size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_INT16(32767, out[2 * i + 0]);
        TEST_ASSERT_EQUAL_INT16(32767, out[2 * i + 1]);
    }
}

void test_mix_clamps_negative_overflow(void)
{
    std::int16_t in[4] = { -30000, -30000, -30000, -30000 };
    std::vector<std::int16_t> out(8, 0);

    MixerInputs inp;
    inp.in = in;
    inp.leftScale  = 2.0f;
    inp.rightScale = 2.0f;

    MixerState st;
    Mix(inp, out.data(), 4, st);

    for (std::size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_INT16(-32768, out[2 * i + 0]);
        TEST_ASSERT_EQUAL_INT16(-32768, out[2 * i + 1]);
    }
}

// ============================================================================
// Mix — pulse gate
// ============================================================================

void test_mix_pulse_disabled_is_straight_scaling(void)
{
    std::int16_t in[8];
    for (int i = 0; i < 8; ++i) in[i] = 10000;
    std::vector<std::int16_t> out(16, 0);

    MixerInputs inp;
    inp.in = in;
    inp.leftScale  = 1.0f;
    inp.rightScale = 1.0f;
    inp.pulseSpec.halfPeriodSamples = 0.0f;  // disabled

    MixerState st;
    Mix(inp, out.data(), 8, st);

    for (std::size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_INT16(10000, out[2 * i + 0]);
        TEST_ASSERT_EQUAL_INT16(10000, out[2 * i + 1]);
    }
}

void test_mix_pulse_gates_between_on_and_off(void)
{
    // 10 PPS at 16 kHz: halfPeriod = 800 samples.
    const std::size_t kN = 1000;
    std::vector<std::int16_t> in(kN, 10000);
    std::vector<std::int16_t> out(2 * kN, 0);

    MixerInputs inp;
    inp.in = in.data();
    inp.leftScale  = 1.0f;
    inp.rightScale = 1.0f;
    inp.pulseSpec.halfPeriodSamples = 800.0f;
    inp.pulseSpec.offScale          = 0.2f;

    MixerState st;
    Mix(inp, out.data(), kN, st);

    // Sample 0 is on phase → full 10000.
    TEST_ASSERT_EQUAL_INT16(10000, out[0]);
    TEST_ASSERT_EQUAL_INT16(10000, out[1]);
    // Still on before the half-period.
    TEST_ASSERT_EQUAL_INT16(10000, out[2 * 400]);
    // After the half-period: off phase at 0.2 × input = 2000.
    TEST_ASSERT_INT16_WITHIN(2, 2000, out[2 * 900]);
    TEST_ASSERT_INT16_WITHIN(2, 2000, out[2 * 900 + 1]);
}

void test_mix_pulse_state_continuous_across_calls(void)
{
    // Two consecutive Mix calls with the same state should produce the
    // same pulse gating pattern as one Mix over the concatenated input.
    const std::size_t kN = 1600;
    std::vector<std::int16_t> in(kN, 10000);
    std::vector<std::int16_t> out1(2 * kN, 0), out2A(2 * (kN / 2), 0), out2B(2 * (kN / 2), 0);

    MixerInputs inp;
    inp.in = in.data();
    inp.leftScale = 1.0f;
    inp.rightScale = 1.0f;
    inp.pulseSpec.halfPeriodSamples = 400.0f;
    inp.pulseSpec.offScale = 0.0f;

    MixerState s1;
    Mix(inp, out1.data(), kN, s1);

    MixerState s2;
    MixerInputs inpA = inp; inpA.in = in.data();
    MixerInputs inpB = inp; inpB.in = in.data() + kN / 2;
    Mix(inpA, out2A.data(), kN / 2, s2);
    Mix(inpB, out2B.data(), kN / 2, s2);

    for (std::size_t i = 0; i < kN; ++i) {
        const std::int16_t* expected = out1.data() + 2 * i;
        const std::int16_t* actual   =
            (i < kN / 2)
                ? out2A.data() + 2 * i
                : out2B.data() + 2 * (i - kN / 2);
        TEST_ASSERT_EQUAL_INT16(expected[0], actual[0]);
        TEST_ASSERT_EQUAL_INT16(expected[1], actual[1]);
    }
}

void test_mix_pulse_respects_pan(void)
{
    // Pulse-gated AND panned: left should be full amplitude, right should
    // be 0 regardless of gate phase.
    const std::size_t kN = 1000;
    std::vector<std::int16_t> in(kN, 10000);
    std::vector<std::int16_t> out(2 * kN, 0);

    MixerInputs inp;
    inp.in = in.data();
    inp.leftScale  = 1.0f;
    inp.rightScale = 0.0f;
    inp.pulseSpec.halfPeriodSamples = 300.0f;
    inp.pulseSpec.offScale          = 0.5f;

    MixerState st;
    Mix(inp, out.data(), kN, st);

    // Right channel is 0 throughout.
    for (std::size_t i = 0; i < kN; ++i) {
        TEST_ASSERT_EQUAL_INT16(0, out[2 * i + 1]);
    }
    // Left channel has both on (10000) and off (5000) samples.
    bool sawOn = false, sawOff = false;
    for (std::size_t i = 0; i < kN; ++i) {
        if (out[2 * i + 0] == 10000) sawOn = true;
        else if (out[2 * i + 0] >= 4998 && out[2 * i + 0] <= 5002) sawOff = true;
    }
    TEST_ASSERT_TRUE(sawOn);
    TEST_ASSERT_TRUE(sawOff);
}

// ============================================================================
// Mix — integration with ToneSynth
// ============================================================================

void test_mix_of_synthesized_tone_preserves_peak(void)
{
    // Synthesize 1 kHz at amplitude 25000, pan center, half volume.
    const std::size_t kN = 2048;
    std::vector<std::int16_t> mono(kN);
    onspeed::audio::Synthesize(1000.0f, 25000, 16000, mono.data(), kN, 0.0f);

    std::vector<std::int16_t> stereo(2 * kN);

    MixerInputs inp;
    inp.in = mono.data();
    inp.leftScale  = 0.5f;
    inp.rightScale = 0.5f;

    MixerState st;
    Mix(inp, stereo.data(), kN, st);

    // After 0.5x gain, peak left/right amplitude is ~12500.
    std::int16_t maxL = 0, maxR = 0;
    for (std::size_t i = 0; i < kN; ++i) {
        if (stereo[2 * i + 0] > maxL) maxL = stereo[2 * i + 0];
        if (stereo[2 * i + 1] > maxR) maxR = stereo[2 * i + 1];
    }
    TEST_ASSERT_INT16_WITHIN(50, 12500, maxL);
    TEST_ASSERT_INT16_WITHIN(50, 12500, maxR);
}

// ============================================================================
// Mix — envelope path (DAHDR gate)
// ============================================================================

void test_mix_envelope_idle_produces_silence(void)
{
    // Envelope in idle state should produce zero output even with full gain.
    const std::size_t kN = 64;
    std::vector<std::int16_t> mono(kN, 25000);
    std::vector<std::int16_t> out(2 * kN);

    onspeed::audio::Envelope env;  // Idle by default
    MixerInputs inp;
    inp.in = mono.data();
    inp.leftScale  = 1.0f;
    inp.rightScale = 1.0f;
    inp.envelope   = &env;

    MixerState st;
    Mix(inp, out.data(), kN, st);

    for (std::size_t i = 0; i < kN; ++i) {
        TEST_ASSERT_EQUAL_INT16(0, out[2 * i + 0]);
        TEST_ASSERT_EQUAL_INT16(0, out[2 * i + 1]);
    }
}

void test_mix_envelope_pulse_silent_during_delay(void)
{
    // First N samples of a pulse should be silent (Delay phase).
    const std::size_t kN = 100;
    std::vector<std::int16_t> mono(kN, 20000);
    std::vector<std::int16_t> out(2 * kN);

    onspeed::audio::Envelope env;
    onspeed::audio::EnvelopeSpec spec;
    spec.delaySamples   = 50.0f;
    spec.attackSamples  = 10.0f;
    spec.holdSamples    = 30.0f;
    spec.decaySamples   = 10.0f;
    spec.releaseSamples = 10.0f;
    env.NoteOn(spec);

    MixerInputs inp;
    inp.in = mono.data();
    inp.leftScale  = 1.0f;
    inp.rightScale = 1.0f;
    inp.envelope   = &env;

    MixerState st;
    Mix(inp, out.data(), kN, st);

    // First 50 samples (delay phase) should be exactly 0
    for (std::size_t i = 0; i < 50; ++i) {
        TEST_ASSERT_EQUAL_INT16(0, out[2 * i + 0]);
        TEST_ASSERT_EQUAL_INT16(0, out[2 * i + 1]);
    }
    // Sample 50+ should have non-zero values (attack phase)
    bool sawNonZero = false;
    for (std::size_t i = 51; i < kN; ++i) {
        if (out[2 * i + 0] != 0) sawNonZero = true;
    }
    TEST_ASSERT_TRUE(sawNonZero);
}

void test_mix_envelope_overrides_pulse_gate(void)
{
    // When envelope is provided, pulseSpec is ignored.
    const std::size_t kN = 32;
    std::vector<std::int16_t> mono(kN, 10000);
    std::vector<std::int16_t> out(2 * kN);

    onspeed::audio::Envelope env;
    // Solid spec, immediate attack so envelope reaches full quickly
    onspeed::audio::EnvelopeSpec spec;
    spec.delaySamples = 0.0f;
    spec.attackSamples = 0.0f;
    spec.isSolid = true;
    env.NoteOn(spec);

    MixerInputs inp;
    inp.in = mono.data();
    inp.leftScale  = 1.0f;
    inp.rightScale = 1.0f;
    inp.envelope   = &env;
    // pulseSpec is set to a normally-active state — should be ignored
    inp.pulseSpec.halfPeriodSamples = 8.0f;
    inp.pulseSpec.offScale          = 0.0f;

    MixerState st;
    Mix(inp, out.data(), kN, st);

    // All samples should be at full amplitude (envelope sustains at 1.0).
    // If pulse-gate were active they'd alternate to 0.
    for (std::size_t i = 0; i < kN; ++i) {
        TEST_ASSERT_EQUAL_INT16(10000, out[2 * i + 0]);
    }
}

void test_mix_envelope_per_channel_scale_composes(void)
{
    // Envelope multiplies both channels equally by the same gate, on top of
    // independent per-channel scales.
    const std::size_t kN = 32;
    std::vector<std::int16_t> mono(kN, 20000);
    std::vector<std::int16_t> out(2 * kN);

    onspeed::audio::Envelope env;
    onspeed::audio::EnvelopeSpec spec;
    spec.delaySamples = 0.0f;
    spec.attackSamples = 0.0f;
    spec.isSolid = true;
    env.NoteOn(spec);

    MixerInputs inp;
    inp.in = mono.data();
    inp.leftScale  = 0.25f;   // simulates STALL_VOL_MIN
    inp.rightScale = 1.0f;    // simulates STALL_VOL_MAX (after pan)
    inp.envelope   = &env;

    MixerState st;
    Mix(inp, out.data(), kN, st);

    // Each channel should be scaled by its scale times gate (1.0).
    for (std::size_t i = 0; i < kN; ++i) {
        TEST_ASSERT_INT16_WITHIN(2, 5000, out[2 * i + 0]);
        TEST_ASSERT_INT16_WITHIN(2, 20000, out[2 * i + 1]);
    }
}

// ============================================================================

int main(int, char**)
{
    UNITY_BEGIN();

    RUN_TEST(test_pack_stereo_zero);
    RUN_TEST(test_pack_stereo_left_only);
    RUN_TEST(test_pack_stereo_right_only);
    RUN_TEST(test_pack_stereo_both);

    RUN_TEST(test_mix_null_input_is_noop);
    RUN_TEST(test_mix_null_output_is_noop);
    RUN_TEST(test_mix_zero_frames_is_noop);

    RUN_TEST(test_mix_unity_gain_passthrough);
    RUN_TEST(test_mix_zero_gain_produces_silence);

    RUN_TEST(test_mix_pan_full_left);
    RUN_TEST(test_mix_pan_full_right);
    RUN_TEST(test_mix_pan_center_half_volume);
    RUN_TEST(test_mix_asymmetric_pan);

    RUN_TEST(test_mix_clamps_positive_overflow);
    RUN_TEST(test_mix_clamps_negative_overflow);

    RUN_TEST(test_mix_pulse_disabled_is_straight_scaling);
    RUN_TEST(test_mix_pulse_gates_between_on_and_off);
    RUN_TEST(test_mix_pulse_state_continuous_across_calls);
    RUN_TEST(test_mix_pulse_respects_pan);

    RUN_TEST(test_mix_of_synthesized_tone_preserves_peak);

    RUN_TEST(test_mix_envelope_idle_produces_silence);
    RUN_TEST(test_mix_envelope_pulse_silent_during_delay);
    RUN_TEST(test_mix_envelope_overrides_pulse_gate);
    RUN_TEST(test_mix_envelope_per_channel_scale_composes);

    return UNITY_END();
}
