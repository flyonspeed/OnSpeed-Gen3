// test_aoa_calculator.cpp - Unit tests for CalcAOA and AOACalculator

#include <unity.h>
#include <AOACalculator.h>
#include <cmath>

void setUp(void) {}
void tearDown(void) {}

// Helper: Linear curve where AOA = coeffP * scale + offset
static SuCalibrationCurve makeLinearCurve(float scale, float offset)
{
    return {{0.0f, 0.0f, scale, offset}, 1};
}

// ============================================================================
// CalcAOA
// ============================================================================

void test_CalcAOA_basic()
{
    // AOA = 10 * coeffP + 5
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 5.0f);

    // Pfwd=100, P45=50 => coeffP=0.5 => AOA=10
    AOAResult result = CalcAOA(100.0f, 50.0f, curve);

    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, result.coeffP);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, result.aoa);
}

void test_CalcAOA_zero_pfwd_invalid()
{
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 5.0f);

    AOAResult result = CalcAOA(0.0f, 50.0f, curve);

    TEST_ASSERT_FALSE(result.valid);
}

void test_CalcAOA_negative_p45_valid()
{
    // Negative P45 happens in some flight conditions
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 5.0f);

    AOAResult result = CalcAOA(100.0f, -50.0f, curve);

    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.5f, result.coeffP);
}

void test_CalcAOA_not_clamped()
{
    // Raw output is NOT clamped - clamping is caller's job
    SuCalibrationCurve curve = makeLinearCurve(100.0f, 0.0f);

    AOAResult result = CalcAOA(100.0f, 100.0f, curve);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, result.aoa);  // > AOA_MAX_VALUE
}

// ============================================================================
// AOACalculator (stateful, with smoothing)
// ============================================================================

void test_AOACalculator_no_smoothing()
{
    AOACalculator calc(0);  // No smoothing
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 5.0f);

    // Pfwd=100, P45=50 => coeffP=0.5 => AOA=10
    AOACalculatorResult r1 = calc.calculate(100.0f, 50.0f, curve);
    TEST_ASSERT_TRUE(r1.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, r1.aoa);

    // Immediate change with no smoothing
    AOACalculatorResult r2 = calc.calculate(100.0f, 0.0f, curve);  // coeffP=0 => AOA=5
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, r2.aoa);
}

void test_AOACalculator_with_smoothing()
{
    AOACalculator calc(2);  // alpha = 0.5
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 0.0f);

    // First value seeds: AOA = 10
    calc.calculate(100.0f, 100.0f, curve);

    // Second value: raw AOA = 0, smoothed = 0.5*0 + 0.5*10 = 5
    AOACalculatorResult r = calc.calculate(100.0f, 0.0f, curve);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, r.aoa);
}

void test_AOACalculator_clamps_output()
{
    AOACalculator calc(0);
    SuCalibrationCurve curve = makeLinearCurve(100.0f, 0.0f);

    // Raw AOA would be 100, but should be clamped
    AOACalculatorResult r = calc.calculate(100.0f, 100.0f, curve);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, AOA_MAX_VALUE, r.aoa);
}

void test_AOACalculator_reset()
{
    AOACalculator calc(10);  // Heavy smoothing
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 0.0f);

    // Build up state
    for (int i = 0; i < 20; i++) {
        calc.calculate(100.0f, 100.0f, curve);  // AOA = 10
    }

    calc.reset();

    // After reset, next value should seed fresh
    AOACalculatorResult r = calc.calculate(100.0f, 0.0f, curve);  // AOA = 0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.aoa);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    // CalcAOA
    RUN_TEST(test_CalcAOA_basic);
    RUN_TEST(test_CalcAOA_zero_pfwd_invalid);
    RUN_TEST(test_CalcAOA_negative_p45_valid);
    RUN_TEST(test_CalcAOA_not_clamped);

    // AOACalculator
    RUN_TEST(test_AOACalculator_no_smoothing);
    RUN_TEST(test_AOACalculator_with_smoothing);
    RUN_TEST(test_AOACalculator_clamps_output);
    RUN_TEST(test_AOACalculator_reset);

    return UNITY_END();
}
