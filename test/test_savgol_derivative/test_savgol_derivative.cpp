// test_savgol_derivative.cpp - Unit tests for SavGolDerivative

#include <unity.h>
#include <SavGolDerivative.h>
#include <cmath>

using onspeed::SavGolDerivative;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Initialization / Buffer Fill
// ============================================================================

void test_returns_zero_until_buffer_filled()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 5);  // Window 5: halfWindow = (5+1)/2 = 3

    // First 3 samples fill the buffer, return 0
    input = 10.0;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, filter.Compute());

    input = 20.0;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, filter.Compute());

    input = 30.0;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, filter.Compute());

    // 4th sample should produce a non-zero result
    input = 40.0;
    float result = filter.Compute();
    TEST_ASSERT_TRUE(result != 0.0f);
}

void test_window15_needs_8_samples_to_fill()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 15);  // halfWindow = (15+1)/2 = 8

    // First 8 samples fill the buffer, return 0
    for (int i = 0; i < 8; i++) {
        input = (double)(i + 1) * 10.0;
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, filter.Compute());
    }

    // 9th sample should produce output
    input = 90.0;
    float result = filter.Compute();
    TEST_ASSERT_TRUE(result != 0.0f);
}

// ============================================================================
// Derivative Correctness
// ============================================================================

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

void test_linear_ramp_constant_derivative()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 5);

    // Feed linear ramp: 0, 10, 20, 30, 40, 50...
    // After buffer fills, derivative should stabilize to +slope = +10
    // (positive because we use mathematically correct future-past convention)
    float lastDerivative = 0.0f;

    for (int i = 0; i < 20; i++) {
        input = (double)i * 10.0;
        lastDerivative = filter.Compute();
    }

    // For linear ramp with slope 10, derivative should be +10
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 10.0f, lastDerivative);
}

void test_positive_derivative_for_increasing_input()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 7);

    // Feed increasing values
    for (int i = 0; i < 15; i++) {
        input = (double)(i * i);  // Quadratic increase
        filter.Compute();
    }

    input = 225.0;  // Continue increasing
    float result = filter.Compute();

    // Filter returns positive for increasing values (mathematically correct)
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

    // Filter returns negative for decreasing values (mathematically correct)
    TEST_ASSERT_TRUE(result < 0.0f);
}

// ============================================================================
// Window Size Selection
// ============================================================================

void test_default_window_on_invalid_size()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 6);  // Invalid (not odd), should default to 15

    // Should behave like window 15 - needs 8 samples to fill, 9th produces output
    for (int i = 0; i < 8; i++) {
        input = (double)(i + 1) * 10.0;
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, filter.Compute());
    }

    input = 90.0;
    float result = filter.Compute();
    TEST_ASSERT_TRUE(result != 0.0f);  // 9th sample produces output
}

void test_various_window_sizes()
{
    // Just verify each valid window size can be constructed and produces output
    int windowSizes[] = {5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25};

    for (int ws : windowSizes) {
        double input = 0.0;
        SavGolDerivative filter(&input, ws);

        // Feed enough samples to fill any window
        for (int i = 0; i < 30; i++) {
            input = (double)i * 5.0;
            filter.Compute();
        }

        // Should produce positive derivative for increasing linear input
        // (mathematically correct: future - past)
        input = 150.0;
        float result = filter.Compute();
        TEST_ASSERT_TRUE(result > 0.0f);
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

// ============================================================================
// Smoothing Property
// ============================================================================

void test_smooths_noisy_signal()
{
    double input = 0.0;
    SavGolDerivative filter(&input, 15);  // Larger window = more smoothing

    // Feed noisy linear ramp: slope 10 with noise ±5
    float derivatives[20];
    for (int i = 0; i < 20; i++) {
        double noise = (i % 2 == 0) ? 5.0 : -5.0;
        input = (double)i * 10.0 + noise;
        derivatives[i] = filter.Compute();
    }

    // After buffer fills, derivatives should be reasonably stable
    // despite input noise
    float sum = 0.0f;
    int count = 0;
    for (int i = 12; i < 20; i++) {  // Start after buffer is filled
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

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    // Initialization
    RUN_TEST(test_returns_zero_until_buffer_filled);
    RUN_TEST(test_window15_needs_8_samples_to_fill);

    // Derivative correctness
    RUN_TEST(test_constant_input_zero_derivative);
    RUN_TEST(test_linear_ramp_constant_derivative);
    RUN_TEST(test_positive_derivative_for_increasing_input);
    RUN_TEST(test_negative_derivative_for_decreasing_input);

    // Window sizes
    RUN_TEST(test_default_window_on_invalid_size);
    RUN_TEST(test_various_window_sizes);

    // Reset
    RUN_TEST(test_reset_clears_state);

    // Smoothing
    RUN_TEST(test_smooths_noisy_signal);

    return UNITY_END();
}
