// test_panning.cpp — Unit tests for Apply3DPan().
//
// Covers:
//   - Centered (lateralG = 0) settles at 1.0/1.0
//   - Sign convention (positive lateralG → right channel louder)
//   - Hard pan saturates at 0.0/1.0 after the dead-zone curve clips
//   - Mid-pan preserves L/R ratio after normalization
//   - Smoothing IIR converges asymptotically to the steady-state curve
//   - Smoothing factor 1.0 is one-tick instant response
//   - Output gains are always in [0, 1]
//   - State persists correctly across calls

#include <unity.h>
#include <audio/Panning.h>

#include <cmath>

using onspeed::audio::Apply3DPan;
using onspeed::audio::PanConfig;
using onspeed::audio::PanResult;
using onspeed::audio::PanState;

void setUp(void) {}
void tearDown(void) {}

namespace {

// Run Apply3DPan repeatedly with constant lateralG until channelGain
// converges (or steps run out).  Returns the final result.
PanResult RunToSteadyState(float lateralG, PanConfig cfg, PanState& state, int maxSteps = 500)
{
    PanResult last{};
    float prev = state.channelGain - 1.0f;   // force first iter
    int steps = 0;
    while (steps < maxSteps && std::fabs(state.channelGain - prev) > 1e-6f)
    {
        prev = state.channelGain;
        last = Apply3DPan(lateralG, state, cfg);
        ++steps;
    }
    return last;
}

}  // namespace

// ---------------------------------------------------------------------------
// Centered: lateralG == 0 stays at 1.0 / 1.0.
// ---------------------------------------------------------------------------

void test_centered_zero_lateral_yields_unity_gains(void)
{
    PanState s;
    PanConfig cfg;
    PanResult r = Apply3DPan(0.0f, s, cfg);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, r.leftGain);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, r.rightGain);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.channelGain);
}

// ---------------------------------------------------------------------------
// Convergence: lateralG = 0 keeps state at 0 even with many ticks.
// ---------------------------------------------------------------------------

void test_centered_remains_centered_under_repeated_calls(void)
{
    PanState s;
    PanConfig cfg;
    for (int i = 0; i < 100; ++i) Apply3DPan(0.0f, s, cfg);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.channelGain);
}

// ---------------------------------------------------------------------------
// Sign convention: positive lateralG steers to the right channel.
// (Matches the inclinometer convention used in Housekeeping.cpp.)
// ---------------------------------------------------------------------------

void test_positive_lateral_routes_to_right_channel(void)
{
    PanState s;
    PanConfig cfg;
    cfg.smoothingFactor = 1.0f;   // one-tick response
    PanResult r = Apply3DPan(0.05f, s, cfg);
    TEST_ASSERT_TRUE(r.rightGain > r.leftGain);
}

void test_negative_lateral_routes_to_left_channel(void)
{
    PanState s;
    PanConfig cfg;
    cfg.smoothingFactor = 1.0f;
    PanResult r = Apply3DPan(-0.05f, s, cfg);
    TEST_ASSERT_TRUE(r.leftGain > r.rightGain);
}

// ---------------------------------------------------------------------------
// Hard pan saturates: lateralG inside the active zone (|x| <= ~0.1)
// pushes channelGain to ±1, producing raw (0.0, 2.0) → normalized
// (0.0, 1.0).  The curve f(x) = -92.822 x² + 20.025 x peaks at
// x ≈ 0.1079 with f(x) ≈ 1.08 (clamped to 1.0).  Use 0.08 G — the
// "one ball width" tuning point — which already saturates the curve.
// ---------------------------------------------------------------------------

void test_hard_right_pan_saturates_at_zero_one(void)
{
    PanState s;
    PanConfig cfg;
    PanResult r = RunToSteadyState(0.08f, cfg, s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, r.leftGain);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, r.rightGain);
}

void test_hard_left_pan_saturates_at_one_zero(void)
{
    PanState s;
    PanConfig cfg;
    PanResult r = RunToSteadyState(-0.08f, cfg, s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, r.leftGain);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, r.rightGain);
}

// ---------------------------------------------------------------------------
// Curve is a quadratic that goes negative beyond |x| ≈ 0.216.  The
// negative-curve regime is clamped to 0, which means very large
// lateralG decays the panning back to centered.  This is the behavior
// the inline Housekeeping math had before extraction; we pin it here
// so any future curve change is a deliberate decision.
// ---------------------------------------------------------------------------

void test_extreme_lateral_clamps_curve_to_zero(void)
{
    PanState s;
    PanConfig cfg;
    cfg.smoothingFactor = 1.0f;
    // |x| = 0.5: curve = -92.822*0.25 + 20.025*0.5 ≈ -13.2 → clamps to 0.
    // channelGain stays 0 → centered output.
    PanResult r = Apply3DPan(0.5f, s, cfg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, r.leftGain);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, r.rightGain);
}

// ---------------------------------------------------------------------------
// Output gains are always in [0, 1] — never exceeds 1.0 (the bug PR #159
// patched at the output stage; we now guarantee it at the source).
// ---------------------------------------------------------------------------

void test_output_gains_never_exceed_unity(void)
{
    PanConfig cfg;
    cfg.smoothingFactor = 1.0f;
    // Sweep across the full lateralG range, including extreme inputs.
    for (int i = -200; i <= 200; ++i)
    {
        const float g = i * 0.01f;
        PanState fresh;   // independent state per sample so we test the curve, not smoothing
        PanResult r = Apply3DPan(g, fresh, cfg);
        TEST_ASSERT_TRUE(r.leftGain  >= 0.0f);
        TEST_ASSERT_TRUE(r.leftGain  <= 1.0f + 1e-6f);
        TEST_ASSERT_TRUE(r.rightGain >= 0.0f);
        TEST_ASSERT_TRUE(r.rightGain <= 1.0f + 1e-6f);
    }
}

// ---------------------------------------------------------------------------
// Mid-pan ratio preservation: at channelGain = +0.5 (one-tick after
// lateralG produces curve = 0.5), the raw (left, right) is (0.5, 1.5);
// after max-gain normalization the ratio is preserved at 1:3.
// ---------------------------------------------------------------------------

void test_mid_pan_preserves_lr_ratio(void)
{
    // We want channelGain to be +0.5 in one tick, so use smoothing=1.0
    // and find a lateralG that yields curve = 0.5.  The curve is
    // -92.822 x^2 + 20.025 x; solving for f(x)=0.5 in the rising
    // region: x ≈ 0.0271 (closed form via quadratic formula).
    const float a = -92.822f, b = 20.025f, c = -0.5f;
    const float x = (-b + std::sqrt(b * b - 4 * a * c)) / (2 * a);

    PanState s;
    PanConfig cfg;
    cfg.smoothingFactor = 1.0f;
    PanResult r = Apply3DPan(x, s, cfg);

    // channelGain ≈ 0.5 → raw left = |−1+0.5| = 0.5, raw right = |1+0.5| = 1.5.
    // After normalization: left = 0.5/1.5 = 1/3, right = 1.0.  Ratio L/R = 1/3.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, r.rightGain);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f / 3.0f, r.leftGain);
    // Verify ratio explicitly (this is the bug we're guarding against —
    // PR #159's output clamp would compress 0.5/1.5 to 0.5/1.0 = 0.5,
    // not 1/3).
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f / 3.0f, r.leftGain / r.rightGain);
}

// ---------------------------------------------------------------------------
// Smoothing IIR converges asymptotically — single tick is fractional.
// ---------------------------------------------------------------------------

void test_smoothing_factor_controls_convergence_rate(void)
{
    PanState s;
    PanConfig cfg;
    cfg.smoothingFactor = 0.1f;   // production default

    // 0.08 G saturates the active-zone curve to 1.0 (it peaks at ~0.108).
    Apply3DPan(0.08f, s, cfg);
    // After one tick: channelGain = 0.1 * 1.0 + 0.9 * 0 = 0.1
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.1f, s.channelGain);

    // After many ticks it should converge near 1.0.
    for (int i = 0; i < 100; ++i) Apply3DPan(0.08f, s, cfg);
    TEST_ASSERT_TRUE(s.channelGain > 0.99f);
    TEST_ASSERT_TRUE(s.channelGain <= 1.0f + 1e-6f);
}

void test_smoothing_factor_one_is_instant(void)
{
    PanState s;
    PanConfig cfg;
    cfg.smoothingFactor = 1.0f;

    // Saturating lateralG → curve = 1.0 → instant channelGain = 1.0.
    Apply3DPan(0.08f, s, cfg);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, s.channelGain);
}

// ---------------------------------------------------------------------------
// Dead-zone: very small |lateralG| produces near-zero channelGain.
// (The curve has f(0) = 0, f(0.005) ≈ 0.098, f(0.01) ≈ 0.19 — with
// smoothing=0.1 a single tick at 0.005 lateral barely budges the state.)
// ---------------------------------------------------------------------------

void test_small_lateral_in_deadzone(void)
{
    PanState s;
    PanConfig cfg;   // default smoothing 0.1
    Apply3DPan(0.005f, s, cfg);
    // channelGain after one tick ≈ 0.1 * 0.098 ≈ 0.01 — still very near zero.
    TEST_ASSERT_TRUE(std::fabs(s.channelGain) < 0.02f);
}

// ---------------------------------------------------------------------------
// State persists across calls; same state instance + same input yields
// monotonic convergence (no resets).
// ---------------------------------------------------------------------------

void test_state_persists_across_calls(void)
{
    PanState s;
    PanConfig cfg;
    cfg.smoothingFactor = 0.5f;

    float prev = -1.0f;
    for (int i = 0; i < 10; ++i)
    {
        // 0.08 G keeps us inside the active curve zone and avoids the
        // negative-curve-clamps-to-0 regime beyond ~0.216 G.
        Apply3DPan(0.08f, s, cfg);
        TEST_ASSERT_TRUE(s.channelGain >= prev);
        prev = s.channelGain;
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int /*argc*/, char** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_centered_zero_lateral_yields_unity_gains);
    RUN_TEST(test_centered_remains_centered_under_repeated_calls);
    RUN_TEST(test_positive_lateral_routes_to_right_channel);
    RUN_TEST(test_negative_lateral_routes_to_left_channel);
    RUN_TEST(test_hard_right_pan_saturates_at_zero_one);
    RUN_TEST(test_hard_left_pan_saturates_at_one_zero);
    RUN_TEST(test_extreme_lateral_clamps_curve_to_zero);
    RUN_TEST(test_output_gains_never_exceed_unity);
    RUN_TEST(test_mid_pan_preserves_lr_ratio);
    RUN_TEST(test_smoothing_factor_controls_convergence_rate);
    RUN_TEST(test_smoothing_factor_one_is_instant);
    RUN_TEST(test_small_lateral_in_deadzone);
    RUN_TEST(test_state_persists_across_calls);

    return UNITY_END();
}
