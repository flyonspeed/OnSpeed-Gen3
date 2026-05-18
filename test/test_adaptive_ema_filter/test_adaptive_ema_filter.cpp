// test_adaptive_ema_filter.cpp - Unit tests for AdaptiveEmaFilter

#include <unity.h>
#include <filters/AdaptiveEmaFilter.h>
#include <cmath>
#include <limits>

using onspeed::AdaptiveEmaFilter;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Core behavior
// ============================================================================

void test_first_update_seeds_value()
{
    AdaptiveEmaFilter f({0.1f, 0.6f, 0.3f});

    float r = f.update(100.0f);

    TEST_ASSERT_TRUE(f.isInitialized());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, r);
}

void test_degenerate_matches_fixed_ema()
{
    // alphaMin == alphaMax → behaves like fixed-alpha EMA, regardless of k.
    AdaptiveEmaFilter f({0.5f, 0.5f, 5.0f});

    f.update(10.0f);   // seeds to 10.0
    float r = f.update(0.0f);

    // 0.5 * 0 + 0.5 * 10 = 5.0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, r);
}

void test_no_smoothing_default_constructor()
{
    // Default-constructed: alphaMin=alphaMax=1.0 → pass-through, mirrors
    // EMAFilter(samples=0).
    AdaptiveEmaFilter f;

    f.update(10.0f);
    float r = f.update(5.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, r);
}

void test_convergence_steady_signal()
{
    AdaptiveEmaFilter f({0.1f, 0.6f, 0.3f});

    f.update(0.0f);
    for (int i = 0; i < 200; i++) {
        f.update(10.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, f.get());
}

// ============================================================================
// Adaptive behavior
// ============================================================================

void test_small_step_uses_alpha_min()
{
    // Small per-frame error → alpha stays at alphaMin.
    AdaptiveEmaFilter f({0.05f, 0.6f, 0.3f});

    f.update(10.0f);            // seed
    f.update(10.1f);            // |err|=0.1, boost = 0.03, α = 0.08, clamped to 0.08 (< αmax)
    float a = f.lastAlpha();
    TEST_ASSERT_TRUE_MESSAGE(a >= 0.05f, "alpha should be at least alphaMin");
    TEST_ASSERT_TRUE_MESSAGE(a < 0.10f, "alpha should be near alphaMin for small steps");
}

void test_large_step_pegs_alpha_max()
{
    // Big per-frame error → alpha pegs to alphaMax.
    AdaptiveEmaFilter f({0.05f, 0.6f, 0.3f});

    f.update(0.0f);             // seed
    f.update(10.0f);            // |err|=10, boost = 3.0, α clamped to 0.6
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, f.lastAlpha());
}

void test_alpha_clamped_to_max_with_huge_k()
{
    // Even with absurd k, alpha must not exceed alphaMax.
    AdaptiveEmaFilter f({0.05f, 0.6f, 1000.0f});

    f.update(0.0f);
    f.update(1.0f);            // |err|=1, boost = 1000, clamped to 0.6
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, f.lastAlpha());
}

void test_pull_up_responds_faster_than_fixed_alpha()
{
    // Step input: 0 → 10 across one sample.
    // Adaptive should converge faster than fixed α=alphaMin.
    AdaptiveEmaFilter adaptive({0.05f, 0.6f, 0.3f});
    AdaptiveEmaFilter fixed   ({0.05f, 0.05f, 0.0f});  // = fixed α=0.05

    adaptive.seed(0.0f);
    fixed.seed(0.0f);

    // Step
    float a1 = adaptive.update(10.0f);
    float f1 = fixed.update(10.0f);

    // Adaptive should be much closer to 10 (α=0.6 → y=6) than fixed (α=0.05 → y=0.5)
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0f, a1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, f1);
}

// ============================================================================
// NaN / sanity
// ============================================================================

void test_nan_input_ignored()
{
    AdaptiveEmaFilter f({0.1f, 0.6f, 0.3f});

    f.update(10.0f);
    float r = f.update(std::nanf(""));

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, r);
}

void test_nan_does_not_initialize()
{
    AdaptiveEmaFilter f({0.1f, 0.6f, 0.3f});

    f.update(std::nanf(""));
    TEST_ASSERT_FALSE(f.isInitialized());
}

// ============================================================================
// Reset / seed
// ============================================================================

void test_reset_clears_state()
{
    AdaptiveEmaFilter f({0.1f, 0.6f, 0.3f});

    f.update(100.0f);
    f.reset();

    TEST_ASSERT_FALSE(f.isInitialized());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, f.update(50.0f));  // re-seeds
}

void test_seed_skips_blend_on_next_update()
{
    AdaptiveEmaFilter f({0.5f, 0.5f, 0.0f});  // fixed α=0.5

    f.seed(-1.0f);
    TEST_ASSERT_TRUE(f.isInitialized());

    // Next update blends from -1.0, not from zero.
    // α=0.5, prev=-1.0, new=1.0 → 0.5*1.0 + 0.5*-1.0 = 0.0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, f.update(1.0f));
}

void test_seed_ignores_nan()
{
    AdaptiveEmaFilter f({0.1f, 0.6f, 0.3f});

    f.seed(std::numeric_limits<float>::quiet_NaN());
    TEST_ASSERT_FALSE(f.isInitialized());

    f.seed(5.0f);
    TEST_ASSERT_TRUE(f.isInitialized());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, f.get());
}

// ============================================================================
// Config setter
// ============================================================================

void test_set_config_keeps_state()
{
    AdaptiveEmaFilter f({0.1f, 0.6f, 0.3f});

    f.update(7.5f);
    TEST_ASSERT_TRUE(f.isInitialized());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.5f, f.get());

    f.setConfig({0.2f, 0.8f, 1.0f});

    TEST_ASSERT_TRUE(f.isInitialized());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.5f, f.get());

    AdaptiveEmaFilter::Config cfg = f.getConfig();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, cfg.alphaMin);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, cfg.alphaMax);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, cfg.kBoost);
}

void test_config_clamps_inverted_bounds()
{
    // If a caller passes alphaMax < alphaMin, the filter should coerce
    // to a valid range rather than producing a bad clamp interval.
    AdaptiveEmaFilter f({0.7f, 0.2f, 0.3f});

    AdaptiveEmaFilter::Config cfg = f.getConfig();
    // alphaMax was lifted to match alphaMin
    TEST_ASSERT_TRUE(cfg.alphaMax >= cfg.alphaMin);
}

void test_config_clamps_negative_k()
{
    AdaptiveEmaFilter f({0.05f, 0.6f, -1.0f});

    AdaptiveEmaFilter::Config cfg = f.getConfig();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, cfg.kBoost);
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_first_update_seeds_value);
    RUN_TEST(test_degenerate_matches_fixed_ema);
    RUN_TEST(test_no_smoothing_default_constructor);
    RUN_TEST(test_convergence_steady_signal);

    RUN_TEST(test_small_step_uses_alpha_min);
    RUN_TEST(test_large_step_pegs_alpha_max);
    RUN_TEST(test_alpha_clamped_to_max_with_huge_k);
    RUN_TEST(test_pull_up_responds_faster_than_fixed_alpha);

    RUN_TEST(test_nan_input_ignored);
    RUN_TEST(test_nan_does_not_initialize);

    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_seed_skips_blend_on_next_update);
    RUN_TEST(test_seed_ignores_nan);

    RUN_TEST(test_set_config_keeps_state);
    RUN_TEST(test_config_clamps_inverted_bounds);
    RUN_TEST(test_config_clamps_negative_k);

    return UNITY_END();
}
