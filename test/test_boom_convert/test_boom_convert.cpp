// test_boom_convert.cpp - Unit tests for boom probe polynomial conversions
//
// Tests the polynomial calibration curves that convert raw ADC counts
// from a boom probe into physical units (degrees, millibars).

#include <unity.h>
#include <cmath>
#include <sensors/BoomConvert.h>

using namespace onspeed;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// BoomStaticConvert — linear: 0.00012207 * (counts - 1638) * 1000 millibars
// ============================================================================

void test_static_at_zero_offset() {
    // At counts = 1638, output should be 0 millibars
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, BoomStaticConvert(1638));
}

void test_static_above_zero() {
    // At counts = 2638, offset = 1000, output = 0.00012207 * 1000 * 1000 = 122.07 mbar
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 122.07f, BoomStaticConvert(2638));
}

void test_static_below_zero() {
    // At counts = 638, offset = -1000, output = 0.00012207 * -1000 * 1000 = -122.07 mbar
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -122.07f, BoomStaticConvert(638));
}

// ============================================================================
// BoomDynamicConvert — linear: (0.01525902 * (counts - 1638)) - 100 millibars
// ============================================================================

void test_dynamic_at_zero_offset() {
    // At counts = 1638, output = -100 millibars
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -100.0f, BoomDynamicConvert(1638));
}

void test_dynamic_at_high_counts() {
    // At counts = 8192, offset = 6554, output = 0.01525902 * 6554 - 100 = 0.0276...
    float expected = 0.01525902f * (8192 - 1638) - 100.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected, BoomDynamicConvert(8192));
}

// ============================================================================
// BoomAlphaConvert — 4th-order polynomial (degrees)
// ============================================================================

void test_alpha_at_zero() {
    // At counts = 0, all polynomial terms are 0 except the constant
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 310.21f, BoomAlphaConvert(0));
}

void test_alpha_monotonic_midrange() {
    // The polynomial should produce varying results across the ADC range
    float a1 = BoomAlphaConvert(5000);
    float a2 = BoomAlphaConvert(10000);
    // Just verify they're different finite values
    TEST_ASSERT_TRUE(std::isfinite(a1));
    TEST_ASSERT_TRUE(std::isfinite(a2));
    TEST_ASSERT_TRUE(a1 != a2);
}

void test_alpha_matches_original_formula() {
    // Verify against the original formula: 7.0918e-13*x^4 - 1.1698e-8*x^3 + 7.0109e-5*x^2 - 0.21624*x + 310.21
    int x = 8192;
    float f = (float)x;
    float expected = 7.0918e-13f*f*f*f*f - 1.1698e-8f*f*f*f + 7.0109e-5f*f*f - 0.21624f*f + 310.21f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expected, BoomAlphaConvert(x));
}

// ============================================================================
// BoomBetaConvert — 4th-order polynomial (degrees)
// ============================================================================

void test_beta_at_zero() {
    // At counts = 0, all polynomial terms are 0 except the constant
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -72.505f, BoomBetaConvert(0));
}

void test_beta_matches_original_formula() {
    int x = 8192;
    float f = (float)x;
    float expected = 2.0096e-13f*f*f*f*f - 3.7124e-9f*f*f*f + 2.5497e-5f*f*f - 3.7141e-2f*f - 72.505f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expected, BoomBetaConvert(x));
}

// ============================================================================
// Main
// ============================================================================

int main() {
    UNITY_BEGIN();

    // Static pressure
    RUN_TEST(test_static_at_zero_offset);
    RUN_TEST(test_static_above_zero);
    RUN_TEST(test_static_below_zero);

    // Dynamic pressure
    RUN_TEST(test_dynamic_at_zero_offset);
    RUN_TEST(test_dynamic_at_high_counts);

    // Alpha angle
    RUN_TEST(test_alpha_at_zero);
    RUN_TEST(test_alpha_monotonic_midrange);
    RUN_TEST(test_alpha_matches_original_formula);

    // Beta angle
    RUN_TEST(test_beta_at_zero);
    RUN_TEST(test_beta_matches_original_formula);

    return UNITY_END();
}
