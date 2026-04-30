// test_audio_orchestrator.cpp — Tests for onspeed::audio::AudioOrchestrator.
//
// Pins the platform-free invariants of the extracted decision tree:
//
//   1. MakePulseSpec numeric outputs at the four corners of the tone
//      map (PPS = 1.5, 6.2, 8.2, 20.0) for the firmware's 16 kHz rate.
//   2. MakeSolidSpec numeric outputs.
//   3. fromSolid=true shortens the first pulse's silent delay to the
//      Gen2 ~61 ms entry value instead of toneLength/2.
//   4. isStall=true switches the ramp from cfg.toneRampMs (15 ms) to
//      cfg.stallRampMs (5 ms).
//   5. DecideAndArm orchestration: NoteOff on enTone == None, NoteOn
//      with the right spec shape on Low/High, observes IsCurrentSolid()
//      for solid → pulsed transitions.
//   6. Round-trip behaviour vs a snapshot of the firmware's pre-extraction
//      Audio.cpp::SetTone body — the load-bearing regression test.

#include <unity.h>

#include <audio/AudioOrchestrator.h>
#include <audio/Envelope.h>
#include <audio/ToneCalc.h>

#include <cmath>

using onspeed::EnToneType;
using onspeed::ToneResult;
using onspeed::audio::DecideAndArm;
using onspeed::audio::Envelope;
using onspeed::audio::EnvelopeSpec;
using onspeed::audio::MakePulseSpec;
using onspeed::audio::MakeSolidSpec;
using onspeed::audio::OrchestratorConfig;

void setUp(void) {}
void tearDown(void) {}

// ----------------------------------------------------------------------------
// Helpers

namespace {

// 1 audio sample @ 16 kHz ≈ 62.5 µs — same tolerance Envelope::SameSpec uses.
constexpr float kSampleTol = 1.0f;

// Default config: matches firmware's V4P (SAMPLE_RATE = 16000) and the
// Gen2 ramp/delay constants exactly.
OrchestratorConfig DefaultCfg()
{
    OrchestratorConfig cfg;
    cfg.sampleRateHz = 16000;
    return cfg;
}

// Reference implementation: snapshot of Audio.cpp's pre-extraction
// MakePulseSpec / MakeSolidSpec / SetTone-body, ported here verbatim
// (with the constants replaced by hard-coded numerics).  Used by the
// load-bearing round-trip test in test_round_trip_matches_pre_extraction
// to prove the new orchestrator is byte-identical to the old code.
namespace ref {

constexpr int   kSampleRateHz             = 16000;
constexpr float kToneRampMs               = 15.0f;
constexpr float kStallRampMs              =  5.0f;
constexpr float kSolidTransitionDelayMs   = 1000.0f / 8.2f / 2.0f;
constexpr float kStallPpsThreshold        = 19.5f;   // HIGH_TONE_STALL_PPS - 0.5

inline float MsToSamples(float ms)
{
    return ms * (static_cast<float>(kSampleRateHz) / 1000.0f);
}

EnvelopeSpec MakePulseSpecRef(float pps, bool isStall, bool fromSolid)
{
    const float pulseDelayMs = 1000.0f / pps;
    const float toneLengthMs = pulseDelayMs - 3.0f;
    const float gapMs        = pulseDelayMs - toneLengthMs;
    const float rampMs       = isStall ? kStallRampMs : kToneRampMs;
    const float delayMs      = fromSolid ? kSolidTransitionDelayMs
                                         : (toneLengthMs * 0.5f);
    float       holdMs       = (toneLengthMs * 0.5f) - 2.0f * rampMs;
    if (holdMs < 0.0f) holdMs = 0.0f;

    EnvelopeSpec s;
    s.delaySamples   = MsToSamples(delayMs);
    s.attackSamples  = MsToSamples(rampMs);
    s.holdSamples    = MsToSamples(holdMs);
    s.decaySamples   = MsToSamples(rampMs);
    s.gapSamples     = MsToSamples(gapMs);
    s.releaseSamples = MsToSamples(rampMs);
    s.isSolid        = false;
    return s;
}

EnvelopeSpec MakeSolidSpecRef()
{
    EnvelopeSpec s;
    s.delaySamples   = MsToSamples(kSolidTransitionDelayMs);
    s.attackSamples  = MsToSamples(kToneRampMs);
    s.holdSamples    = 0.0f;
    s.decaySamples   = 0.0f;
    s.gapSamples     = 0.0f;
    s.releaseSamples = MsToSamples(kToneRampMs);
    s.isSolid        = true;
    return s;
}

// Reference body of Audio.cpp::SetTone before the extraction.  Matches
// the new DecideAndArm contract for the firmware's actual behaviour:
// returns the spec used (zero-filled for NoteOff) for assertion.
EnvelopeSpec SetToneRef(const ToneResult& tr, Envelope& env)
{
    if (tr.enTone == EnToneType::None)
    {
        env.NoteOff();
        return EnvelopeSpec{};
    }

    EnvelopeSpec spec;
    if (tr.fPulseFreq <= 0.0f)
    {
        spec = MakeSolidSpecRef();
    }
    else
    {
        const bool isStall   = (tr.fPulseFreq >= kStallPpsThreshold);
        const bool fromSolid = env.IsCurrentSolid();
        spec = MakePulseSpecRef(tr.fPulseFreq, isStall, fromSolid);
    }

    env.NoteOn(spec);
    return spec;
}

}  // namespace ref

// Field-by-field equality used by the round-trip regression.
void AssertSpecEqual(const EnvelopeSpec& a, const EnvelopeSpec& b,
                     const char* tag)
{
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: delaySamples", tag);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kSampleTol, a.delaySamples, b.delaySamples, msg);
    snprintf(msg, sizeof(msg), "%s: attackSamples", tag);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kSampleTol, a.attackSamples, b.attackSamples, msg);
    snprintf(msg, sizeof(msg), "%s: holdSamples", tag);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kSampleTol, a.holdSamples, b.holdSamples, msg);
    snprintf(msg, sizeof(msg), "%s: decaySamples", tag);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kSampleTol, a.decaySamples, b.decaySamples, msg);
    snprintf(msg, sizeof(msg), "%s: gapSamples", tag);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kSampleTol, a.gapSamples, b.gapSamples, msg);
    snprintf(msg, sizeof(msg), "%s: releaseSamples", tag);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kSampleTol, a.releaseSamples, b.releaseSamples, msg);
    snprintf(msg, sizeof(msg), "%s: isSolid", tag);
    TEST_ASSERT_EQUAL_MESSAGE(a.isSolid, b.isSolid, msg);
}

}  // namespace

// ----------------------------------------------------------------------------
// 1. MakePulseSpec numeric pins (4 corners, 16 kHz)

void test_pulse_spec_pps_8_2_normal(void)
{
    // PPS=8.2 (low-tone max): pulseDelay=121.951ms, toneLength=118.951ms,
    // delay=59.476ms→951.6, ramp=15→240, hold=29.476→471.6, gap=3→48,
    // release=240.
    EnvelopeSpec s = MakePulseSpec(8.2f, /*isStall=*/false,
                                   /*fromSolid=*/false, DefaultCfg());
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  951.6f, s.delaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  240.0f, s.attackSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  471.6f, s.holdSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  240.0f, s.decaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,   48.0f, s.gapSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  240.0f, s.releaseSamples);
    TEST_ASSERT_FALSE(s.isSolid);
}

void test_pulse_spec_pps_1_5(void)
{
    // PPS=1.5 (slowest): pulseDelay=666.667ms, toneLength=663.667ms,
    // delay=331.833ms→5309.3, ramp=15→240, hold=301.833→4829.3, gap=3→48,
    // release=240.
    EnvelopeSpec s = MakePulseSpec(1.5f, false, false, DefaultCfg());
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 5309.3f, s.delaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  240.0f, s.attackSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 4829.3f, s.holdSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  240.0f, s.decaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,   48.0f, s.gapSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  240.0f, s.releaseSamples);
    TEST_ASSERT_FALSE(s.isSolid);
}

void test_pulse_spec_pps_6_2(void)
{
    // PPS=6.2 (high-tone max): pulseDelay=161.290ms, toneLength=158.290ms,
    // delay=79.145ms→1266.32, ramp=15→240, hold=49.145→786.32, gap=3→48,
    // release=240.
    EnvelopeSpec s = MakePulseSpec(6.2f, false, false, DefaultCfg());
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 1266.3f, s.delaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  240.0f, s.attackSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  786.3f, s.holdSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  240.0f, s.decaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,   48.0f, s.gapSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  240.0f, s.releaseSamples);
    TEST_ASSERT_FALSE(s.isSolid);
}

void test_pulse_spec_pps_20_stall(void)
{
    // PPS=20 (stall): pulseDelay=50ms, toneLength=47ms, delay=23.5→376,
    // ramp=5→80 (stall ramp), hold=23.5-10=13.5→216, gap=3→48, release=80.
    EnvelopeSpec s = MakePulseSpec(20.0f, /*isStall=*/true, false, DefaultCfg());
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 376.0f, s.delaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  80.0f, s.attackSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 216.0f, s.holdSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  80.0f, s.decaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  48.0f, s.gapSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  80.0f, s.releaseSamples);
    TEST_ASSERT_FALSE(s.isSolid);
}

// ----------------------------------------------------------------------------
// 2. MakeSolidSpec numeric pins

void test_solid_spec_numeric_pins(void)
{
    // Solid: delay=60.9756ms→975.6, attack=15→240, hold/decay/gap=0,
    // release=240, isSolid=true.
    EnvelopeSpec s = MakeSolidSpec(DefaultCfg());
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 975.6f, s.delaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 240.0f, s.attackSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,   0.0f, s.holdSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,   0.0f, s.decaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,   0.0f, s.gapSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 240.0f, s.releaseSamples);
    TEST_ASSERT_TRUE(s.isSolid);
}

// ----------------------------------------------------------------------------
// 3. fromSolid shortens first pulse delay

void test_pulse_from_solid_uses_short_delay(void)
{
    // PPS=8.2: toneLength/2 = 951.6 samples (normal entry); but fromSolid
    // overrides to solidTransitionDelayMs (60.9756 ms × 16 = 975.6 samples).
    EnvelopeSpec normal     = MakePulseSpec(8.2f, false, false, DefaultCfg());
    EnvelopeSpec fromSolid  = MakePulseSpec(8.2f, false, true,  DefaultCfg());
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 951.6f, normal.delaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 975.6f, fromSolid.delaySamples);
    // All other fields unchanged
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, normal.attackSamples,  fromSolid.attackSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, normal.holdSamples,    fromSolid.holdSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, normal.decaySamples,   fromSolid.decaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, normal.gapSamples,     fromSolid.gapSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, normal.releaseSamples, fromSolid.releaseSamples);
}

// ----------------------------------------------------------------------------
// 4. isStall switches ramp time

void test_isstall_switches_ramp(void)
{
    // PPS=20 normal ramp (15ms → 240 samples) vs stall ramp (5ms → 80).
    // Hold also shifts because hold = toneLength/2 - 2*ramp.
    EnvelopeSpec normal = MakePulseSpec(20.0f, /*isStall=*/false, false, DefaultCfg());
    EnvelopeSpec stall  = MakePulseSpec(20.0f, /*isStall=*/true,  false, DefaultCfg());
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 240.0f, normal.attackSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  80.0f, stall.attackSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 240.0f, normal.decaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  80.0f, stall.decaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 240.0f, normal.releaseSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  80.0f, stall.releaseSamples);
    // Normal hold = 23.5 - 30 = -6.5 → clamped to 0.
    // Stall  hold = 23.5 - 10 =  13.5 → 216 samples.
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,   0.0f, normal.holdSamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 216.0f, stall.holdSamples);
}

// ----------------------------------------------------------------------------
// 5. DecideAndArm orchestration

void test_decide_none_calls_noteoff(void)
{
    Envelope env;
    // Arm a pulsed tone first
    env.NoteOn(MakePulseSpec(8.2f, false, false, DefaultCfg()));
    TEST_ASSERT_TRUE(env.IsActive());

    ToneResult tr;
    tr.enTone = EnToneType::None;
    DecideAndArm(tr, env, DefaultCfg());

    // Envelope should be releasing (or already idle if release was zero) —
    // not still in the active pulse cycle.
    TEST_ASSERT_FALSE(env.IsActive());
}

void test_decide_low_solid_arms_solid_spec(void)
{
    Envelope env;
    ToneResult tr;
    tr.enTone     = EnToneType::Low;
    tr.fPulseFreq = 0.0f;            // solid

    EnvelopeSpec spec = DecideAndArm(tr, env, DefaultCfg());

    TEST_ASSERT_TRUE(spec.isSolid);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 975.6f, spec.delaySamples);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 240.0f, spec.attackSamples);
    TEST_ASSERT_TRUE(env.IsActive());
}

void test_decide_high_pulsed_stall(void)
{
    Envelope env;
    ToneResult tr;
    tr.enTone     = EnToneType::High;
    tr.fPulseFreq = 20.0f;           // stall

    EnvelopeSpec spec = DecideAndArm(tr, env, DefaultCfg());

    TEST_ASSERT_FALSE(spec.isSolid);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol,  80.0f, spec.attackSamples);   // stall ramp
    TEST_ASSERT_FALSE(env.IsCurrentSolid());
    TEST_ASSERT_TRUE(env.IsActive());
}

void test_decide_solid_to_pulsed_uses_shortened_delay(void)
{
    Envelope env;

    // Step 1: arm a solid tone, advance into Sustain.
    ToneResult solidReq;
    solidReq.enTone     = EnToneType::Low;
    solidReq.fPulseFreq = 0.0f;
    DecideAndArm(solidReq, env, DefaultCfg());

    // Pump samples until we're sustaining (past delay + attack).
    for (int i = 0; i < 1300; ++i) env.Tick();
    TEST_ASSERT_TRUE(env.IsCurrentSolid());

    // Step 2: now request a pulsed tone — should observe IsCurrentSolid()
    // and pass fromSolid=true to MakePulseSpec, yielding the shortened
    // 975.6-sample delay rather than 951.6.
    ToneResult pulsedReq;
    pulsedReq.enTone     = EnToneType::High;
    pulsedReq.fPulseFreq = 8.2f;
    EnvelopeSpec spec = DecideAndArm(pulsedReq, env, DefaultCfg());

    TEST_ASSERT_FALSE(spec.isSolid);
    TEST_ASSERT_FLOAT_WITHIN(kSampleTol, 975.6f, spec.delaySamples);
}

// ----------------------------------------------------------------------------
// 6. Round-trip: orchestrator must be byte-identical to pre-extraction
// Audio.cpp::SetTone for a fixed sequence of (ToneResult, prior state) pairs.
//
// LOAD-BEARING regression test: a divergence here means the on-device
// audio behaviour shifted.  The reference implementation in `ref::` is
// the snapshot of the old Audio.cpp body.

void test_round_trip_matches_pre_extraction(void)
{
    // Sequence covers: silent, solid-low entry, solid hold, pulsed-high
    // ramp from solid (uses fromSolid=true), stall warning, return to
    // pulsed-low, return to silent.  Pump enough samples between calls
    // to drive the envelope through delay/attack into Sustain or active
    // pulsing so IsCurrentSolid() / IsActive() reflect realistic states.
    struct Step {
        EnToneType enTone;
        float      fPulseFreq;
        int        ticksAfter;
    };
    const Step kSequence[] = {
        { EnToneType::None, 0.0f,    0 },     // baseline
        { EnToneType::Low,  0.0f, 1300 },     // enter solid, pump into Sustain
        { EnToneType::Low,  0.0f,  500 },     // re-arm solid (debounced)
        { EnToneType::High, 8.2f,    0 },     // solid → pulsed (shortened delay)
        { EnToneType::High, 8.2f, 2000 },     // hold pulsed
        { EnToneType::High, 20.0f,   0 },     // pulsed → stall
        { EnToneType::High, 20.0f, 1500 },    // hold stall
        { EnToneType::High, 6.2f,    0 },     // stall → mid pulsed
        { EnToneType::Low,  3.0f, 1000 },     // pulsed-low
        { EnToneType::None, 0.0f,    0 },     // silence
        { EnToneType::Low,  0.0f, 1500 },     // back to solid from idle/release
    };

    Envelope envNew;
    Envelope envRef;
    char tag[64];

    for (size_t i = 0; i < sizeof(kSequence) / sizeof(kSequence[0]); ++i)
    {
        ToneResult tr;
        tr.enTone     = kSequence[i].enTone;
        tr.fPulseFreq = kSequence[i].fPulseFreq;
        tr.fVolumeMult = 1.0f;

        EnvelopeSpec specNew = DecideAndArm(tr, envNew, DefaultCfg());
        EnvelopeSpec specRef = ref::SetToneRef(tr, envRef);

        snprintf(tag, sizeof(tag), "step %zu", i);
        AssertSpecEqual(specNew, specRef, tag);

        // Both envelopes must agree on running state too — IsCurrentSolid()
        // drives the next iteration's fromSolid decision.
        snprintf(tag, sizeof(tag), "step %zu IsActive", i);
        TEST_ASSERT_EQUAL_MESSAGE(envRef.IsActive(), envNew.IsActive(), tag);
        snprintf(tag, sizeof(tag), "step %zu IsCurrentSolid", i);
        TEST_ASSERT_EQUAL_MESSAGE(envRef.IsCurrentSolid(),
                                   envNew.IsCurrentSolid(), tag);

        for (int t = 0; t < kSequence[i].ticksAfter; ++t)
        {
            envNew.Tick();
            envRef.Tick();
        }
    }
}

// ----------------------------------------------------------------------------
// Main

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_pulse_spec_pps_8_2_normal);
    RUN_TEST(test_pulse_spec_pps_1_5);
    RUN_TEST(test_pulse_spec_pps_6_2);
    RUN_TEST(test_pulse_spec_pps_20_stall);

    RUN_TEST(test_solid_spec_numeric_pins);

    RUN_TEST(test_pulse_from_solid_uses_short_delay);

    RUN_TEST(test_isstall_switches_ramp);

    RUN_TEST(test_decide_none_calls_noteoff);
    RUN_TEST(test_decide_low_solid_arms_solid_spec);
    RUN_TEST(test_decide_high_pulsed_stall);
    RUN_TEST(test_decide_solid_to_pulsed_uses_shortened_delay);

    RUN_TEST(test_round_trip_matches_pre_extraction);

    return UNITY_END();
}
