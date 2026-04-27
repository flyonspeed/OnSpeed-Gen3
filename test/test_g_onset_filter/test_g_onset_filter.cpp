// test_g_onset_filter.cpp - Unit tests for GOnsetFilter.

#include <unity.h>
#include <filters/GOnsetFilter.h>
#include <cmath>
#include <limits>

using onspeed::GOnsetFilter;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// First-sample seeding
// ============================================================================

void test_first_sample_returns_zero()
{
    GOnsetFilter f(0.25f);

    // First valid sample seeds prev — no derivative possible yet.
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, f.Update(1.0f, 0.05f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, f.Get());
}

// ============================================================================
// Step input — verify low-pass behavior
// ============================================================================

void test_step_input_rises_and_settles()
{
    // Wire-rate cadence: dt = 50 ms (20 Hz).
    // tau = 250 ms => alpha = 0.05 / (0.25 + 0.05) = 1/6 ≈ 0.1667.
    GOnsetFilter f(0.25f);
    const float dt = 0.05f;

    // Seed at 1.0 g (level flight).
    f.Update(1.0f, dt);

    // Single 0.5 g step at the next sample: raw rate = 0.5 / 0.05 = 10 g/s.
    // Filtered output on this first non-seed sample = alpha * 10 + (1-alpha) * 0
    //                                                = 1.6667 g/s.
    float out = f.Update(1.5f, dt);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.6667f, out);

    // Hold at 1.5 g: subsequent raw rates are 0, so the filtered output
    // decays geometrically toward zero with ratio (1 - alpha) = 5/6.
    for (int i = 0; i < 60; ++i) {           // 60 samples * 50 ms = 3 s ≈ 12 tau
        out = f.Update(1.5f, dt);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, out);
}

// ============================================================================
// Smooth ramp — sustained derivative
// ============================================================================

void test_sustained_ramp_settles_to_slope()
{
    // 1.0 g -> 2.0 g over 2 seconds at 20 Hz => 0.5 g/s slope.
    GOnsetFilter f(0.25f);
    const float dt = 0.05f;

    float vG = 1.0f;
    f.Update(vG, dt);                        // seed

    // Step the input at a constant 0.5 g/s rate. After several time constants
    // (tau = 0.25 s, so 5 tau = 1.25 s = 25 samples) the filter should
    // converge to within ~1% of the true slope.
    for (int i = 0; i < 40; ++i) {
        vG += 0.5f * dt;                     // +0.025 g per sample
        f.Update(vG, dt);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, f.Get());
}

// ============================================================================
// Zero input — output stays at zero
// ============================================================================

void test_constant_input_stays_zero()
{
    GOnsetFilter f(0.25f);
    const float dt = 0.05f;

    for (int i = 0; i < 100; ++i) {
        float out = f.Update(1.0f, dt);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, out);
    }
}

// ============================================================================
// NaN / Inf input — output bounded, no propagation
// ============================================================================

void test_nan_input_does_not_propagate()
{
    GOnsetFilter f(0.25f);
    const float dt = 0.05f;

    // Build up a non-zero state with a real ramp.
    f.Update(1.0f, dt);
    f.Update(1.05f, dt);
    const float settled = f.Get();
    TEST_ASSERT_TRUE(settled != 0.0f);
    TEST_ASSERT_FALSE(std::isnan(settled));

    // NaN input is ignored: state and output unchanged.
    float out = f.Update(std::numeric_limits<float>::quiet_NaN(), dt);
    TEST_ASSERT_FALSE(std::isnan(out));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, settled, out);
}

void test_inf_input_does_not_propagate()
{
    GOnsetFilter f(0.25f);
    const float dt = 0.05f;

    f.Update(1.0f, dt);

    float out = f.Update(std::numeric_limits<float>::infinity(), dt);
    TEST_ASSERT_FALSE(std::isinf(out));
    TEST_ASSERT_FALSE(std::isnan(out));
}

void test_nonpositive_dt_does_not_advance_state()
{
    GOnsetFilter f(0.25f);

    f.Update(1.0f, 0.05f);                   // seed

    // dt = 0 and dt < 0 are both rejected; the prior smoothed output is held.
    const float prior = f.Get();
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, prior, f.Update(2.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, prior, f.Update(2.0f, -0.01f));
}

// ============================================================================
// Reset — no derivative spike from the pre-reset sample
// ============================================================================

void test_reset_clears_state_and_avoids_spike()
{
    GOnsetFilter f(0.25f);
    const float dt = 0.05f;

    // Drive to a known non-zero state.
    f.Update(1.0f, dt);
    for (int i = 0; i < 10; ++i) {
        f.Update(1.0f + 0.05f * (i + 1), dt);
    }
    TEST_ASSERT_TRUE(f.Get() != 0.0f);

    f.Reset();
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, f.Get());

    // First post-reset sample re-seeds — no derivative spike, even if the
    // input jumps from the pre-reset value to a wildly different one.
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, f.Update(5.0f, dt));
}

// ============================================================================
// Sign convention — positive d/dt produces positive output
// ============================================================================

void test_increasing_input_produces_positive_output()
{
    GOnsetFilter f(0.25f);
    const float dt = 0.05f;

    f.Update(1.0f, dt);
    f.Update(1.1f, dt);

    TEST_ASSERT_TRUE(f.Get() > 0.0f);
}

void test_decreasing_input_produces_negative_output()
{
    GOnsetFilter f(0.25f);
    const float dt = 0.05f;

    f.Update(1.0f, dt);
    f.Update(0.9f, dt);

    TEST_ASSERT_TRUE(f.Get() < 0.0f);
}

// ============================================================================
// Saturation sanity — the M5 tape saturates at 2 g/s. A 0.5 g/s pull-up
// should produce a clearly visible (but well below saturation) value, and
// a sustained 1.5 g/s should approach but not exceed the wire ±9.99 cap.
// ============================================================================

void test_visual_gain_envelope()
{
    const float dt = 0.05f;

    // Mild pull-up: 0.5 g/s sustained.
    GOnsetFilter mild(0.25f);
    float vG = 1.0f;
    mild.Update(vG, dt);
    for (int i = 0; i < 60; ++i) {
        vG += 0.5f * dt;
        mild.Update(vG, dt);
    }
    // Settles near 0.5 g/s — the M5 will draw ~30 px of the 120-px tape.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, mild.Get());

    // Aggressive pull-up: 1.5 g/s sustained — near visual saturation.
    GOnsetFilter aggressive(0.25f);
    vG = 1.0f;
    aggressive.Update(vG, dt);
    for (int i = 0; i < 60; ++i) {
        vG += 1.5f * dt;
        aggressive.Update(vG, dt);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.5f, aggressive.Get());
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_first_sample_returns_zero);
    RUN_TEST(test_step_input_rises_and_settles);
    RUN_TEST(test_sustained_ramp_settles_to_slope);
    RUN_TEST(test_constant_input_stays_zero);
    RUN_TEST(test_nan_input_does_not_propagate);
    RUN_TEST(test_inf_input_does_not_propagate);
    RUN_TEST(test_nonpositive_dt_does_not_advance_state);
    RUN_TEST(test_reset_clears_state_and_avoids_spike);
    RUN_TEST(test_increasing_input_produces_positive_output);
    RUN_TEST(test_decreasing_input_produces_negative_output);
    RUN_TEST(test_visual_gain_envelope);

    return UNITY_END();
}
