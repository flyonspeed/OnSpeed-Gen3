// test_savgol_derivative.cpp - Unit tests for SavGolDerivative

#include <unity.h>
#include <SavGolDerivative.h>
#include <cmath>

using onspeed::SavGolDerivative;

void setUp(void) {}
void tearDown(void) {}

void test_returns_zero_until_buffer_filled()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 5);  // Window 5: need 5 samples to fill

    // First 5 samples fill the buffer, return 0
    for (int i = 1; i <= 5; i++) {
        input = (double)i * 10.0;
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, filter.Compute());
    }

    // 6th sample should produce a non-zero result
    input = 60.0;
    float result = filter.Compute();
    TEST_ASSERT_TRUE(result != 0.0f);
}

void test_window15_needs_15_samples_to_fill()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 15);

    // First 15 samples fill the buffer, return 0
    for (int i = 1; i <= 15; i++) {
        input = (double)i * 10.0;
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, filter.Compute());
    }

    // 16th sample should produce output
    input = 160.0;
    float result = filter.Compute();
    TEST_ASSERT_TRUE(result != 0.0f);
}// ============================================================================

void test_constant_input_zero_derivative()
{
    double input = 50.0;
    SavGolDerivative filter(&input, 5);

    // Fill buffer with constant value
    for (int i = 0; i < 10; i++) {
        filter.Compute();
    }

    // Derivative of constant should be ~0
    float result = filter.Compute();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, result);
}

void test_linear_ramp_exact_derivative()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 5);

    // Feed linear ramp: 0, 10, 20, 30, 40, 50...
    float lastDerivative = 0.0f;

    for (int i = 0; i < 20; i++) {
        input = (double)i * 10.0;
        lastDerivative = filter.Compute();
    }

    // For linear ramp with slope 10, derivative should be exactly +10
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, lastDerivative);
}

void test_positive_derivative_for_increasing_input()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 7);

    // Feed increasing values
    for (int i = 0; i < 15; i++) {
        input = (double)(i * i);
        filter.Compute();
    }

    input = 225.0;  // Continue increasing
    float result = filter.Compute();

    // Correct S-G returns positive for increasing values
    TEST_ASSERT_TRUE(result > 0.0f);
}

void test_negative_derivative_for_decreasing_input()
{
    double input = 100.0;
    SavGolDerivative filter(&input, 7);

    // Feed decreasing values
    for (int i = 0; i < 15; i++) {
        input = 100.0 - (double)(i * 5);
        filter.Compute();
    }

    input = 20.0;  // Continue decreasing
    float result = filter.Compute();

    // Correct S-G returns negative for decreasing values
    TEST_ASSERT_TRUE(result < 0.0f);
}

void test_default_window_on_invalid_size()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 6);  // Invalid (not odd), should default to 15

    // Should behave like window 15 - needs 15 samples to fill, 16th produces output
    for (int i = 1; i <= 15; i++) {
        input = (double)i * 10.0;
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, filter.Compute());
    }

    input = 160.0;
    float result = filter.Compute();
    TEST_ASSERT_TRUE(result != 0.0f);
}

void test_various_window_sizes_exact_derivative()
{
    // Verify each valid window size returns exact derivative for linear input
    int windowSizes[] = {5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25};

    for (int ws : windowSizes) {
        double input = 0.0;
        SavGolDerivative filter(&input, ws);

        // Feed enough samples to fill any window
        for (int i = 0; i < 30; i++) {
            input = (double)i * 5.0;
            filter.Compute();
        }

        // Should produce exactly +5 derivative for slope 5 linear input
        input = 150.0;
        float result = filter.Compute();
        TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, result);
    }
}

// ============================================================================
// Reset
// ============================================================================

void test_reset_clears_state()
{
    double input = 100.0;
    SavGolDerivative filter(&input, 5);

    // Fill and compute
    for (int i = 0; i < 10; i++) {
        filter.Compute();
    }

    filter.reset();

    // After reset, should return 0 until buffer refills
    input = 50.0;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, filter.Compute());
}

void test_smooths_noisy_signal()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 15);  // Larger window = more smoothing

    // Feed noisy linear ramp: slope 10 with noise ±5
    // Need more samples since window 15 requires 15 samples to fill
    float derivatives[30];
    for (int i = 0; i < 30; i++) {
        double noise = (i % 2 == 0) ? 5.0 : -5.0;
        input = (double)i * 10.0 + noise;
        derivatives[i] = filter.Compute();
    }

    // After buffer fills (at sample 15), derivatives should be reasonably stable
    // despite input noise
    float sum = 0.0f;
    int count = 0;
    for (int i = 18; i < 30; i++) {  // Start after buffer is filled
        sum += derivatives[i];
        count++;
    }
    float avgDerivative = sum / count;

    // Should be close to +10 (the underlying slope) despite noise
    TEST_ASSERT_FLOAT_WITHIN(3.0f, 10.0f, avgDerivative);
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    UNITY_BEGIN();

    // Initialization
    RUN_TEST(test_returns_zero_until_buffer_filled);
    RUN_TEST(test_window15_needs_15_samples_to_fill);

    // Derivative correctness
    RUN_TEST(test_constant_input_zero_derivative);
    RUN_TEST(test_linear_ramp_exact_derivative);
    RUN_TEST(test_positive_derivative_for_increasing_input);
    RUN_TEST(test_negative_derivative_for_decreasing_input);

    // Window sizes
    RUN_TEST(test_default_window_on_invalid_size);
    RUN_TEST(test_various_window_sizes_exact_derivative);

    // Reset
    RUN_TEST(test_reset_clears_state);

    // Smoothing
    RUN_TEST(test_smooths_noisy_signal);

    return UNITY_END();
}
