// test_ema_filter.cpp - Unit tests for EMAFilter

#include <unity.h>
#include <EMAFilter.h>
#include <cmath>

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Core Behavior
// ============================================================================

void test_first_update_seeds_value()
{
    EMAFilter f(10);  // alpha = 0.1

    // First update returns input directly (no ramp from zero)
    float result = f.update(100.0f);

    TEST_ASSERT_TRUE(f.isInitialized());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, result);
}

void test_no_smoothing_passthrough()
{
    EMAFilter f(0);  // alpha = 1.0, no smoothing

    f.update(10.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, f.update(5.0f));  // Immediate change
}

void test_exponential_smoothing()
{
    EMAFilter f(2);  // alpha = 0.5

    f.update(10.0f);  // Seed with 10.0

    // smoothed = 0.5 * 5.0 + 0.5 * 10.0 = 7.5
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.5f, f.update(5.0f));
}

void test_convergence()
{
    EMAFilter f(4);  // alpha = 0.25

    f.update(0.0f);  // Seed at 0
    for (int i = 0; i < 50; i++) {
        f.update(10.0f);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, f.get());
}

// ============================================================================
// NaN Safety
// ============================================================================

void test_nan_input_is_ignored()
{
    EMAFilter f(2);

    f.update(10.0f);
    float result = f.update(std::nanf(""));

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, result);  // Unchanged
}

// ============================================================================
// Reset
// ============================================================================

void test_reset()
{
    EMAFilter f(4);

    f.update(100.0f);
    f.reset();

    TEST_ASSERT_FALSE(f.isInitialized());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, f.update(50.0f));  // Re-seeds
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_first_update_seeds_value);
    RUN_TEST(test_no_smoothing_passthrough);
    RUN_TEST(test_exponential_smoothing);
    RUN_TEST(test_convergence);
    RUN_TEST(test_nan_input_is_ignored);
    RUN_TEST(test_reset);

    return UNITY_END();
}
