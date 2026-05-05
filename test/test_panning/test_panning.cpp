// test_panning.cpp — Unit tests for Apply3DPan().
//
// Covers:
//   - Centered (lateralG = 0) settles at 1.0/1.0
//   - Sign convention (positive lateralG → right channel louder)
//   - Hard pan saturates at 0.0/1.0 once |lateralG| reaches 0.125
//   - Extreme lateralG (spin / snap-roll regime) holds full pan
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
// Hard pan saturates: at |lateralG| ≥ 0.125 the curve `min(1, 8·|x|)`
// reaches 1.0, so channelGain converges to ±1 and the raw (left, right)
// of (0.0, 2.0) normalizes to (0.0, 1.0).
// ---------------------------------------------------------------------------

void test_hard_right_pan_saturates_at_zero_one(void)
{
    PanState s;
    PanConfig cfg;
    PanResult r = RunToSteadyState(0.125f, cfg, s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, r.leftGain);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, r.rightGain);
}

void test_hard_left_pan_saturates_at_one_zero(void)
{
    PanState s;
    PanConfig cfg;
    PanResult r = RunToSteadyState(-0.125f, cfg, s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, r.leftGain);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, r.rightGain);
}

// ---------------------------------------------------------------------------
// Spin / snap-roll regime: sustained lateral G in the 0.25 – 1.0 range
// must render as full pan, not centered.  Pins the fix for #371.
// ---------------------------------------------------------------------------

void test_extreme_lateral_saturates_curve_at_one(void)
{
    PanConfig cfg;
    cfg.smoothingFactor = 1.0f;   // one-tick steady state

    // Right side: 0.30 G — typical RV-class developed-spin lateral.
    {
        PanState s;
        PanResult r = Apply3DPan(0.30f, s, cfg);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, r.leftGain);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, r.rightGain);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, s.channelGain);
    }
    // Right side: 0.50 G — heavy / longer-fuselage spin upper end.
    {
        PanState s;
        PanResult r = Apply3DPan(0.50f, s, cfg);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, r.leftGain);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, r.rightGain);
    }
    // Left side: -1.0 G — snap-roll peak.
    {
        PanState s;
        PanResult r = Apply3DPan(-1.0f, s, cfg);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, r.leftGain);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, r.rightGain);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, s.channelGain);
    }
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
    // The curve `min(1, 8·|x|)` hits 0.5 at x = 0.0625 in the rising
    // (pre-saturation) region.
    const float x = 0.0625f;

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

    // 0.125 G is the saturation point of `min(1, 8·|x|)`.
    Apply3DPan(0.125f, s, cfg);
    // After one tick: channelGain = 0.1 * 1.0 + 0.9 * 0 = 0.1
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.1f, s.channelGain);

    // After many ticks it should converge near 1.0.
    for (int i = 0; i < 100; ++i) Apply3DPan(0.125f, s, cfg);
    TEST_ASSERT_TRUE(s.channelGain > 0.99f);
    TEST_ASSERT_TRUE(s.channelGain <= 1.0f + 1e-6f);
}

void test_smoothing_factor_one_is_instant(void)
{
    PanState s;
    PanConfig cfg;
    cfg.smoothingFactor = 1.0f;

    // Saturating lateralG → curve = 1.0 → instant channelGain = 1.0.
    Apply3DPan(0.125f, s, cfg);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, s.channelGain);
}

// ---------------------------------------------------------------------------
// Small lateral G: one tick at default smoothing barely budges the state.
// (No true dead-zone; the curve `min(1, 8·|x|)` has slope 8 near zero,
// so f(0.005) = 0.04 and one tick at α=0.1 leaves channelGain ≈ 0.004.)
// ---------------------------------------------------------------------------

void test_small_lateral_barely_budges_state(void)
{
    PanState s;
    PanConfig cfg;   // default smoothing 0.1
    Apply3DPan(0.005f, s, cfg);
    // channelGain after one tick ≈ 0.1 * 0.04 = 0.004 — still very near zero.
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
        // 0.125 G saturates the curve to 1.0; channelGain climbs
        // monotonically toward 1 under the IIR.
        Apply3DPan(0.125f, s, cfg);
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
    RUN_TEST(test_extreme_lateral_saturates_curve_at_one);
    RUN_TEST(test_output_gains_never_exceed_unity);
    RUN_TEST(test_mid_pan_preserves_lr_ratio);
    RUN_TEST(test_smoothing_factor_controls_convergence_rate);
    RUN_TEST(test_smoothing_factor_one_is_instant);
    RUN_TEST(test_small_lateral_barely_budges_state);
    RUN_TEST(test_state_persists_across_calls);

    return UNITY_END();
}
