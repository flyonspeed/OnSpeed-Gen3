// test_envelope.cpp — Tests for the DAHDR envelope generator.
//
// Verifies:
//   - Idle output is zero
//   - Pulsed (non-solid) note: Idle → Delay → Attack → Hold → Decay → Idle
//   - Solid note: Idle → Delay → Attack → Hold → Sustain (forever)
//   - NoteOff during Sustain triggers Release ramp from 1.0 → 0
//   - NoteOn while in-flight queues; current envelope releases first
//   - Output continuity: no per-sample step exceeds the ramp slope
//   - Zero-length phases skip cleanly

#include <unity.h>

#include <audio/Envelope.h>

#include <cmath>
#include <cstddef>
#include <vector>

using onspeed::audio::Envelope;
using onspeed::audio::EnvelopeSpec;
using onspeed::audio::EnvPhase;

void setUp(void) {}
void tearDown(void) {}

// ----------------------------------------------------------------------------
// Helpers

namespace {

EnvelopeSpec MakePulse(float delay = 100.0f, float attack = 80.0f,
                       float hold = 200.0f, float decay = 80.0f,
                       float release = 80.0f)
{
    EnvelopeSpec s;
    s.delaySamples   = delay;
    s.attackSamples  = attack;
    s.holdSamples    = hold;
    s.decaySamples   = decay;
    s.releaseSamples = release;
    s.isSolid        = false;
    return s;
}

EnvelopeSpec MakeSolid(float delay = 0.0f, float attack = 240.0f,
                       float release = 240.0f)
{
    EnvelopeSpec s;
    s.delaySamples   = delay;
    s.attackSamples  = attack;
    s.holdSamples    = 0.0f;
    s.decaySamples   = 0.0f;
    s.releaseSamples = release;
    s.isSolid        = true;
    return s;
}

// Run N samples and return the trace.
std::vector<float> Run(Envelope& env, std::size_t n)
{
    std::vector<float> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        out.push_back(env.Tick());
    return out;
}

}  // namespace

// ----------------------------------------------------------------------------
// Idle behaviour

void test_idle_outputs_zero(void)
{
    Envelope env;
    auto trace = Run(env, 16);
    for (float v : trace)
        TEST_ASSERT_EQUAL_FLOAT(0.0f, v);
    TEST_ASSERT_TRUE(env.IsIdle());
}

void test_noteoff_when_idle_is_noop(void)
{
    Envelope env;
    env.NoteOff();
    TEST_ASSERT_TRUE(env.IsIdle());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, env.Tick());
}

// ----------------------------------------------------------------------------
// Pulsed (non-solid) lifecycle: Delay → Attack → Hold → Decay → Idle

void test_pulse_delay_phase_silent(void)
{
    Envelope env;
    env.NoteOn(MakePulse(50.0f, 10.0f, 10.0f, 10.0f));
    // First 50 samples should be Delay (silent)
    for (int i = 0; i < 50; ++i)
    {
        float v = env.Tick();
        TEST_ASSERT_EQUAL_FLOAT(0.0f, v);
    }
    // Next sample begins Attack (non-zero immediately at sample 1 of attack)
    float v = env.Tick();
    TEST_ASSERT_TRUE(v > 0.0f);
}

void test_pulse_attack_ramp_linear(void)
{
    Envelope env;
    env.NoteOn(MakePulse(0.0f, 10.0f, 10.0f, 10.0f));  // no delay
    // Attack from 0 → 1 over 10 samples, slope = 0.1/sample
    float v1 = env.Tick();
    float v2 = env.Tick();
    float v3 = env.Tick();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, v1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, v2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.3f, v3);
    // After 10 attack samples we should be at 1.0
    for (int i = 0; i < 7; ++i) env.Tick();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, env.Tick());
}

void test_pulse_hold_at_unity(void)
{
    Envelope env;
    env.NoteOn(MakePulse(0.0f, 10.0f, 50.0f, 10.0f));
    // Skip attack (10 samples)
    for (int i = 0; i < 10; ++i) env.Tick();
    // Hold at 1.0 for 50 samples
    for (int i = 0; i < 50; ++i)
    {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, env.Tick());
    }
}

void test_pulse_decay_ramp_linear(void)
{
    Envelope env;
    env.NoteOn(MakePulse(0.0f, 10.0f, 0.0f, 10.0f, 10.0f));
    // Skip attack (10 samples), no hold, enter decay
    for (int i = 0; i < 10; ++i) env.Tick();
    float v1 = env.Tick();
    float v2 = env.Tick();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9f, v1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, v2);
    // After full decay we hit 0 — but pulsed envelopes auto-loop, so
    // the next phase begins (Delay or Attack).  Verify level reaches 0
    // at the end of decay without asserting Idle.
    for (int i = 0; i < 8; ++i) env.Tick();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, env.Level());
    TEST_ASSERT_FALSE(env.IsIdle());   // looped, not idle
}

void test_pulse_auto_loops(void)
{
    // A pulsed envelope (isSolid=false) should re-trigger automatically
    // after Decay, producing continuous pulses indefinitely.  Mirrors
    // Gen2's IntervalTimer-driven `noteOn()` per pulse period.
    Envelope env;
    EnvelopeSpec s = MakePulse(20.0f, 10.0f, 20.0f, 10.0f, 10.0f);
    env.NoteOn(s);
    // Run for ~3 full cycles (60 samples each) — should never go Idle.
    int peaksSeen = 0;
    bool wasHigh = false;
    for (int i = 0; i < 200; ++i)
    {
        float v = env.Tick();
        if (v > 0.99f && !wasHigh) { peaksSeen++; wasHigh = true; }
        if (v < 0.01f) wasHigh = false;
    }
    TEST_ASSERT_TRUE_MESSAGE(peaksSeen >= 2, "Pulsed envelope did not loop");
    TEST_ASSERT_FALSE(env.IsIdle());
}

void test_pulse_noteoff_terminates_loop(void)
{
    // NoteOff during a pulsed loop should release immediately and stay
    // idle (no auto-retrigger after release).
    Envelope env;
    env.NoteOn(MakePulse(20.0f, 10.0f, 20.0f, 10.0f, 10.0f));
    for (int i = 0; i < 100; ++i) env.Tick();
    env.NoteOff();
    // Drain the release
    for (int i = 0; i < 50; ++i)
    {
        env.Tick();
        if (env.IsIdle()) break;
    }
    TEST_ASSERT_TRUE(env.IsIdle());
    // Should stay idle on subsequent ticks (no auto-loop after release)
    for (int i = 0; i < 100; ++i)
    {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, env.Tick());
    }
    TEST_ASSERT_TRUE(env.IsIdle());
}

// ----------------------------------------------------------------------------
// Solid lifecycle: Delay → Attack → Hold → Sustain (forever) → NoteOff → Release

void test_solid_holds_at_unity_indefinitely(void)
{
    Envelope env;
    env.NoteOn(MakeSolid(0.0f, 10.0f, 10.0f));
    for (int i = 0; i < 10; ++i) env.Tick();
    // Now in Sustain — hold at 1.0 forever
    for (int i = 0; i < 1000; ++i)
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, env.Tick());
    TEST_ASSERT_EQUAL(EnvPhase::Sustain, env.Phase());
}

void test_solid_noteoff_releases_to_zero(void)
{
    Envelope env;
    env.NoteOn(MakeSolid(0.0f, 10.0f, 10.0f));
    for (int i = 0; i < 10; ++i) env.Tick();
    TEST_ASSERT_EQUAL(EnvPhase::Sustain, env.Phase());
    env.NoteOff();
    // Release ramps from 1.0 to 0 over 10 samples
    float v1 = env.Tick();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9f, v1);
    for (int i = 0; i < 9; ++i) env.Tick();
    TEST_ASSERT_TRUE(env.IsIdle());
}

// ----------------------------------------------------------------------------
// Re-trigger / queueing semantics

void test_noteon_while_releasing_replaces_spec(void)
{
    Envelope env;
    env.NoteOn(MakeSolid(0.0f, 10.0f, 10.0f));
    for (int i = 0; i < 10; ++i) env.Tick();
    env.NoteOff();
    // We're in Release — call NoteOn with a fresh spec.
    // Per spec: NoteOn while in Release arms immediately, taking over from
    // the current decaying level.
    env.Tick();   // one release sample → level 0.9
    env.NoteOn(MakeSolid(0.0f, 20.0f, 10.0f));
    TEST_ASSERT_EQUAL(EnvPhase::Attack, env.Phase());
    TEST_ASSERT_FALSE(env.HasPending());
}

void test_noteon_during_attack_queues_for_pulse_boundary(void)
{
    // Gen3 behaviour: NoteOn during a pulse (Attack/Hold/Decay) queues
    // the new spec so the current pulse finishes naturally.  The Decay
    // phase picks up the pending spec at the pulse boundary.
    Envelope env;
    env.NoteOn(MakePulse(0.0f, 100.0f, 100.0f, 100.0f, 100.0f));
    for (int i = 0; i < 10; ++i) env.Tick();
    TEST_ASSERT_EQUAL(EnvPhase::Attack, env.Phase());
    // Trigger a fresh note while attack is in progress.  Current pulse
    // should continue — phase stays Attack, spec is queued.
    env.NoteOn(MakePulse(0.0f, 50.0f, 50.0f, 50.0f, 50.0f));
    TEST_ASSERT_EQUAL(EnvPhase::Attack, env.Phase());
    TEST_ASSERT_TRUE(env.HasPending());
    // Pump until the current pulse finishes.  At the end of Decay, the
    // pending spec fires and we transition to the new pulse's Attack.
    for (int i = 0; i < 500; ++i)
    {
        env.Tick();
        if (!env.HasPending()) break;
    }
    TEST_ASSERT_FALSE(env.HasPending());
    // After pending fires, we should be in Attack with the new spec armed.
    // (Delay is zero in the MakePulse default for this test.)
    TEST_ASSERT_TRUE(env.Phase() == EnvPhase::Attack ||
                     env.Phase() == EnvPhase::Delay);
}

void test_noteon_during_pulse_latest_pending_wins(void)
{
    // Rapid re-triggering during a pulse: only the most-recent pending
    // spec survives (replacement, not chaining).  Simulates the 208 Hz
    // UpdateTones() rate chattering the stall threshold.
    Envelope env;
    env.NoteOn(MakePulse(0.0f, 100.0f, 100.0f, 100.0f, 100.0f));
    for (int i = 0; i < 10; ++i) env.Tick();
    env.NoteOn(MakePulse(0.0f, 50.0f, 50.0f, 50.0f, 50.0f));
    env.NoteOn(MakePulse(0.0f, 25.0f, 25.0f, 25.0f, 25.0f));
    env.NoteOn(MakePulse(0.0f, 200.0f, 200.0f, 200.0f, 200.0f));
    TEST_ASSERT_EQUAL(EnvPhase::Attack, env.Phase());
    TEST_ASSERT_TRUE(env.HasPending());
    // Pump until pending fires
    for (int i = 0; i < 1000; ++i)
    {
        env.Tick();
        if (!env.HasPending()) break;
    }
    // The active spec should now be the last-queued one (attack=200).
    // Verify by measuring attack ramp slope (should be 1/200 = 0.005/sample).
    float prev = env.Level();
    for (int i = 0; i < 50; ++i) env.Tick();
    float delta = env.Level() - prev;
    // 50 samples at 0.005/sample ≈ 0.25 level rise (bounded by remaining
    // attack progress — we don't know exact start offset, so just check
    // it's small relative to the 25-sample-attack spec).
    TEST_ASSERT_TRUE(delta < 0.30f);  // not the 25-sample-attack rate
}

void test_noteon_during_sustain_releases(void)
{
    // Solid tone must release to transition — Sustain doesn't auto-exit.
    // The release tail hides behind the new spec's silent Delay.
    Envelope env;
    env.NoteOn(MakeSolid(0.0f, 10.0f, 10.0f));
    for (int i = 0; i < 10; ++i) env.Tick();
    TEST_ASSERT_EQUAL(EnvPhase::Sustain, env.Phase());
    // NoteOn another solid: should release first, then fire pending
    env.NoteOn(MakeSolid(0.0f, 5.0f, 5.0f));
    TEST_ASSERT_EQUAL(EnvPhase::Release, env.Phase());
    TEST_ASSERT_TRUE(env.HasPending());
    // Pump until queued NoteOn fires
    for (int i = 0; i < 100; ++i)
    {
        env.Tick();
        if (env.Phase() == EnvPhase::Attack || env.Phase() == EnvPhase::Sustain)
            break;
    }
    TEST_ASSERT_FALSE(env.HasPending());
}

// ----------------------------------------------------------------------------
// Output continuity

void test_no_step_exceeds_ramp_slope(void)
{
    // Verify per-sample delta is bounded by max(1/attack, 1/decay, 1/release)
    Envelope env;
    EnvelopeSpec spec = MakePulse(50.0f, 80.0f, 100.0f, 80.0f);
    env.NoteOn(spec);
    auto trace = Run(env, 400);

    const float maxStep = 1.0f / 80.0f + 1e-6f;  // attack/decay slope
    float prev = 0.0f;
    for (float v : trace)
    {
        float delta = std::fabs(v - prev);
        TEST_ASSERT_TRUE_MESSAGE(delta <= maxStep,
            "Per-sample envelope step exceeded ramp slope");
        prev = v;
    }
}

void test_release_from_partial_attack(void)
{
    // NoteOff during attack should release from current level (not 1.0)
    Envelope env;
    env.NoteOn(MakeSolid(0.0f, 100.0f, 100.0f));
    for (int i = 0; i < 30; ++i) env.Tick();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.30f, env.Level());
    env.NoteOff();
    TEST_ASSERT_EQUAL(EnvPhase::Release, env.Phase());
    // Release ramps from 0.30 to 0 at slope 1/100 per sample → 30 samples
    int n = 0;
    while (!env.IsIdle() && n < 1000)
    {
        env.Tick();
        n++;
    }
    TEST_ASSERT_INT_WITHIN(2, 30, n);
}

// ----------------------------------------------------------------------------
// Zero-length phase handling

void test_zero_attack_jumps_to_unity(void)
{
    Envelope env;
    EnvelopeSpec s = MakePulse(0.0f, 0.0f, 10.0f, 10.0f);
    env.NoteOn(s);
    float v = env.Tick();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, v);
    TEST_ASSERT_EQUAL(EnvPhase::Hold, env.Phase());
}

void test_zero_hold_skips_to_decay(void)
{
    Envelope env;
    EnvelopeSpec s = MakePulse(0.0f, 10.0f, 0.0f, 10.0f);
    env.NoteOn(s);
    for (int i = 0; i < 10; ++i) env.Tick();
    // Should now be in Decay (hold was skipped)
    TEST_ASSERT_EQUAL(EnvPhase::Decay, env.Phase());
}

void test_zero_release_drops_immediately(void)
{
    Envelope env;
    EnvelopeSpec s = MakeSolid(0.0f, 10.0f, 0.0f);
    env.NoteOn(s);
    for (int i = 0; i < 10; ++i) env.Tick();
    TEST_ASSERT_EQUAL(EnvPhase::Sustain, env.Phase());
    env.NoteOff();
    env.Tick();
    TEST_ASSERT_TRUE(env.IsIdle());
}

// ----------------------------------------------------------------------------
// Reset

void test_reset_clears_state(void)
{
    Envelope env;
    env.NoteOn(MakeSolid(0.0f, 10.0f, 10.0f));
    for (int i = 0; i < 5; ++i) env.Tick();
    TEST_ASSERT_FALSE(env.IsIdle());
    env.Reset();
    TEST_ASSERT_TRUE(env.IsIdle());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, env.Level());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, env.Tick());
}

// ----------------------------------------------------------------------------
// Main

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_idle_outputs_zero);
    RUN_TEST(test_noteoff_when_idle_is_noop);

    RUN_TEST(test_pulse_delay_phase_silent);
    RUN_TEST(test_pulse_attack_ramp_linear);
    RUN_TEST(test_pulse_hold_at_unity);
    RUN_TEST(test_pulse_decay_ramp_linear);
    RUN_TEST(test_pulse_auto_loops);
    RUN_TEST(test_pulse_noteoff_terminates_loop);

    RUN_TEST(test_solid_holds_at_unity_indefinitely);
    RUN_TEST(test_solid_noteoff_releases_to_zero);

    RUN_TEST(test_noteon_while_releasing_replaces_spec);
    RUN_TEST(test_noteon_during_attack_queues_for_pulse_boundary);
    RUN_TEST(test_noteon_during_pulse_latest_pending_wins);
    RUN_TEST(test_noteon_during_sustain_releases);

    RUN_TEST(test_no_step_exceeds_ramp_slope);
    RUN_TEST(test_release_from_partial_attack);

    RUN_TEST(test_zero_attack_jumps_to_unity);
    RUN_TEST(test_zero_hold_skips_to_decay);
    RUN_TEST(test_zero_release_drops_immediately);

    RUN_TEST(test_reset_clears_state);
    return UNITY_END();
}
