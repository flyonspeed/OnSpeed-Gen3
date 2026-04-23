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
// IsActive — used by sketch debounce logic to avoid leaving the envelope
// stuck Idle when SetTone(same shape) is called after a release.

void test_isactive_idle_returns_false(void)
{
    Envelope env;
    TEST_ASSERT_FALSE(env.IsActive());
}

void test_isactive_during_pulse_returns_true(void)
{
    Envelope env;
    env.NoteOn(MakePulse(20.0f, 10.0f, 50.0f, 10.0f));
    // Tick into delay
    env.Tick();
    TEST_ASSERT_TRUE(env.IsActive());
    // Tick well into hold
    for (int i = 0; i < 50; ++i) env.Tick();
    TEST_ASSERT_TRUE(env.IsActive());
}

void test_isactive_during_sustain_returns_true(void)
{
    Envelope env;
    env.NoteOn(MakeSolid(0.0f, 5.0f, 5.0f));
    for (int i = 0; i < 10; ++i) env.Tick();
    TEST_ASSERT_EQUAL(EnvPhase::Sustain, env.Phase());
    TEST_ASSERT_TRUE(env.IsActive());
}

void test_isactive_during_release_returns_false(void)
{
    // Release is "winding down, not actively playing" — sketch debounce
    // must NOT skip a fresh NoteOn during release, otherwise the envelope
    // would finish releasing and stay Idle indefinitely.
    Envelope env;
    env.NoteOn(MakeSolid(0.0f, 5.0f, 100.0f));
    for (int i = 0; i < 10; ++i) env.Tick();
    env.NoteOff();
    TEST_ASSERT_EQUAL(EnvPhase::Release, env.Phase());
    TEST_ASSERT_FALSE(env.IsActive());
}

void test_release_then_same_noteon_re_arms(void)
{
    // Reproduces a real failure mode: SetTone(None) was called
    // (NoteOff), then SetTone(same shape) before Release completed.
    // The sketch debounce uses IsActive() exactly to avoid skipping
    // this NoteOn — because if it were skipped, the envelope would
    // finish releasing and produce silence forever.
    Envelope env;
    EnvelopeSpec spec = MakePulse(10.0f, 10.0f, 30.0f, 10.0f, 50.0f);
    env.NoteOn(spec);
    for (int i = 0; i < 30; ++i) env.Tick();
    env.NoteOff();
    TEST_ASSERT_FALSE(env.IsActive());

    // Sketch detects "not active" → does NOT debounce → calls NoteOn
    env.NoteOn(spec);
    TEST_ASSERT_TRUE(env.IsActive());
    // Pump and verify we cycle a pulse
    bool sawHigh = false;
    for (int i = 0; i < 200; ++i)
    {
        if (env.Tick() > 0.99f) { sawHigh = true; break; }
    }
    TEST_ASSERT_TRUE(sawHigh);
}

// ----------------------------------------------------------------------------
// Internal debounce — NoteOn with same spec while active is a no-op.
// This is the critical property that lets the sketch call SetTone() at
// 208 Hz from UpdateTones() without disturbing the running pulse cycle.

void test_noteon_with_identical_spec_is_noop(void)
{
    Envelope env;
    EnvelopeSpec spec = MakePulse(50.0f, 10.0f, 50.0f, 10.0f, 10.0f);
    env.NoteOn(spec);
    // Tick into Hold
    for (int i = 0; i < 65; ++i) env.Tick();
    TEST_ASSERT_EQUAL(EnvPhase::Hold, env.Phase());

    // Re-trigger with the identical spec — should be a no-op.
    env.NoteOn(spec);
    TEST_ASSERT_EQUAL(EnvPhase::Hold, env.Phase());
    TEST_ASSERT_FALSE(env.HasPending());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, env.Level());
}

void test_noteon_with_floating_point_noise_is_noop(void)
{
    // Real PPS-driven specs go through SAMPLE_RATE / (pps * 2.0) which
    // produces float-noise differences across calls.  Debounce should
    // tolerate sub-sample differences.
    Envelope env;
    EnvelopeSpec a = MakePulse(50.0f, 10.0f, 50.0f, 10.0f, 10.0f);
    env.NoteOn(a);
    for (int i = 0; i < 30; ++i) env.Tick();

    EnvelopeSpec b = a;
    b.delaySamples   += 0.3f;   // sub-sample noise
    b.attackSamples  -= 0.4f;
    b.holdSamples    += 0.2f;
    env.NoteOn(b);
    TEST_ASSERT_FALSE(env.HasPending());
}

void test_noteon_with_meaningful_change_queues(void)
{
    Envelope env;
    EnvelopeSpec a = MakePulse(50.0f, 10.0f, 50.0f, 10.0f, 10.0f);
    env.NoteOn(a);
    for (int i = 0; i < 30; ++i) env.Tick();

    EnvelopeSpec c = a;
    c.holdSamples += 100.0f;   // > 1 sample — meaningful
    env.NoteOn(c);
    TEST_ASSERT_TRUE(env.HasPending());
}

void test_noteon_with_isolid_change_queues(void)
{
    // Solid vs pulsed flag is structural — never debounce across modes.
    Envelope env;
    env.NoteOn(MakePulse(0.0f, 5.0f, 50.0f, 5.0f, 5.0f));
    for (int i = 0; i < 30; ++i) env.Tick();
    EnvelopeSpec solid = MakePulse(0.0f, 5.0f, 50.0f, 5.0f, 5.0f);
    solid.isSolid = true;
    env.NoteOn(solid);
    TEST_ASSERT_TRUE(env.HasPending());
}

void test_chatter_at_threshold_does_not_silence(void)
{
    // Reproduces the user's reported failure mode: at 208 Hz the sketch
    // alternates between two PPS values across the stall threshold every
    // few ms.  With internal debounce + queueing, the envelope must
    // continue producing audible pulses (level > some threshold for some
    // contiguous samples) — NOT churn into perpetual Release/Attack.
    Envelope env;
    EnvelopeSpec slow = MakePulse(20.0f, 10.0f, 50.0f, 10.0f, 10.0f);   // 100-sample period
    EnvelopeSpec fast = MakePulse(5.0f, 5.0f, 15.0f, 5.0f, 5.0f);       // 35-sample period
    env.NoteOn(slow);

    // Simulate ~2000 samples of envelope ticks with sketch-side chatter
    // every 8 samples (~ stall threshold ping-pong on a fast loop).
    int peakSamples = 0;
    bool wasHigh = false;
    int peaksSeen = 0;
    for (int i = 0; i < 4000; ++i)
    {
        if ((i % 8) == 0)
            env.NoteOn((i / 8) & 1 ? fast : slow);
        float v = env.Tick();
        if (v > 0.95f)
        {
            peakSamples++;
            if (!wasHigh) { peaksSeen++; wasHigh = true; }
        }
        else if (v < 0.05f)
        {
            wasHigh = false;
        }
    }
    // Without queueing+debounce we'd see almost no peak samples (envelope
    // would always be ramping).  With it, we should see plenty of pulses.
    TEST_ASSERT_TRUE_MESSAGE(peakSamples > 100,
        "Envelope failed to produce sustained pulses under chatter");
    TEST_ASSERT_TRUE_MESSAGE(peaksSeen >= 5,
        "Envelope failed to produce distinct pulses under chatter");
}

// ----------------------------------------------------------------------------
// IsCurrentSolid — used by sketch to detect solid→pulsed transitions
// (Gen2's 61 ms silent-delay trick).

void test_iscurrentsolid_idle_false(void)
{
    Envelope env;
    TEST_ASSERT_FALSE(env.IsCurrentSolid());
}

void test_iscurrentsolid_pulsed_false(void)
{
    Envelope env;
    env.NoteOn(MakePulse(0.0f, 5.0f, 30.0f, 5.0f, 5.0f));
    for (int i = 0; i < 10; ++i) env.Tick();
    TEST_ASSERT_FALSE(env.IsCurrentSolid());
}

void test_iscurrentsolid_solid_true(void)
{
    Envelope env;
    env.NoteOn(MakeSolid(0.0f, 5.0f, 5.0f));
    for (int i = 0; i < 10; ++i) env.Tick();
    TEST_ASSERT_EQUAL(EnvPhase::Sustain, env.Phase());
    TEST_ASSERT_TRUE(env.IsCurrentSolid());
}

void test_iscurrentsolid_releasing_false(void)
{
    // Once we've started releasing, we're no longer "currently solid"
    // even though the spec was solid — IsActive() is false.
    Envelope env;
    env.NoteOn(MakeSolid(0.0f, 5.0f, 100.0f));
    for (int i = 0; i < 10; ++i) env.Tick();
    env.NoteOff();
    TEST_ASSERT_EQUAL(EnvPhase::Release, env.Phase());
    TEST_ASSERT_FALSE(env.IsCurrentSolid());
}

// ----------------------------------------------------------------------------
// NoteOff clears any pending re-trigger — explicit stop wins.

void test_noteoff_clears_pending(void)
{
    Envelope env;
    env.NoteOn(MakePulse(20.0f, 10.0f, 50.0f, 10.0f, 10.0f));
    for (int i = 0; i < 30; ++i) env.Tick();
    // Queue a new spec
    env.NoteOn(MakePulse(5.0f, 5.0f, 5.0f, 5.0f, 5.0f));
    TEST_ASSERT_TRUE(env.HasPending());
    // Stop request — should win over the queued spec
    env.NoteOff();
    TEST_ASSERT_FALSE(env.HasPending());
    // Drain the release
    for (int i = 0; i < 100; ++i)
    {
        env.Tick();
        if (env.IsIdle()) break;
    }
    TEST_ASSERT_TRUE(env.IsIdle());
    // Stay idle — the previously-queued spec should NOT have fired
    for (int i = 0; i < 50; ++i)
        TEST_ASSERT_EQUAL_FLOAT(0.0f, env.Tick());
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

    RUN_TEST(test_isactive_idle_returns_false);
    RUN_TEST(test_isactive_during_pulse_returns_true);
    RUN_TEST(test_isactive_during_sustain_returns_true);
    RUN_TEST(test_isactive_during_release_returns_false);
    RUN_TEST(test_release_then_same_noteon_re_arms);

    RUN_TEST(test_noteon_with_identical_spec_is_noop);
    RUN_TEST(test_noteon_with_floating_point_noise_is_noop);
    RUN_TEST(test_noteon_with_meaningful_change_queues);
    RUN_TEST(test_noteon_with_isolid_change_queues);
    RUN_TEST(test_chatter_at_threshold_does_not_silence);

    RUN_TEST(test_iscurrentsolid_idle_false);
    RUN_TEST(test_iscurrentsolid_pulsed_false);
    RUN_TEST(test_iscurrentsolid_solid_true);
    RUN_TEST(test_iscurrentsolid_releasing_false);

    RUN_TEST(test_noteoff_clears_pending);

    RUN_TEST(test_reset_clears_state);
    return UNITY_END();
}
