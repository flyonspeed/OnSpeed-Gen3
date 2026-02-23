// test_curve_calc.cpp - Unit tests for CurveCalc
//
// Tests polynomial, logarithmic, and exponential calibration curves.

#include <unity.h>
#include <CurveCalc.h>

using namespace onspeed;

void setUp(void) {
}

void tearDown(void) {
}

// ============================================================================
// Polynomial Curves (iCurveType = 1)
// ============================================================================

void test_polynomial_constant() {
    // y = 5 (constant polynomial: 0*x^3 + 0*x^2 + 0*x + 5)
    SuCalibrationCurve curve = {{0, 0, 0, 5}, 1};

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, CurveCalc(0, curve));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, CurveCalc(1, curve));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, CurveCalc(100, curve));
}

void test_polynomial_linear() {
    // y = 2x + 3 (0*x^3 + 0*x^2 + 2*x + 3)
    SuCalibrationCurve curve = {{0, 0, 2, 3}, 1};

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, CurveCalc(0, curve));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, CurveCalc(1, curve));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.0f, CurveCalc(10, curve));
}

void test_polynomial_quadratic() {
    // y = x^2 + 2 (0*x^3 + 1*x^2 + 0*x + 2)
    SuCalibrationCurve curve = {{0, 1, 0, 2}, 1};

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, CurveCalc(0, curve));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, CurveCalc(1, curve));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0f, CurveCalc(2, curve));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.0f, CurveCalc(3, curve));
}

void test_polynomial_cubic() {
    // y = x^3 + 2*x^2 + 3*x + 4
    SuCalibrationCurve curve = {{1, 2, 3, 4}, 1};

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, CurveCalc(0, curve));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, CurveCalc(1, curve));  // 1+2+3+4
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 26.0f, CurveCalc(2, curve));  // 8+8+6+4
}

void test_polynomial_negative_x() {
    // y = x^2 (0*x^3 + 1*x^2 + 0*x + 0)
    SuCalibrationCurve curve = {{0, 1, 0, 0}, 1};

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, CurveCalc(-2, curve));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, CurveCalc(-1, curve));
}

// ============================================================================
// Logarithmic Curves (iCurveType = 2)
// ============================================================================

void test_logarithmic() {
    // y = 10*ln(x) + 5
    // Uses last two coefficients: afCoeff[2] = 10, afCoeff[3] = 5
    SuCalibrationCurve curve = {{0, 0, 10, 5}, 2};

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, CurveCalc(1, curve));  // ln(1)=0
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.931f, CurveCalc(2, curve)); // 10*0.693+5
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.986f, CurveCalc(3, curve)); // 10*1.099+5
}

void test_logarithmic_e() {
    // y = ln(x) (coefficient = 1, offset = 0)
    SuCalibrationCurve curve = {{0, 0, 1, 0}, 2};

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, CurveCalc(1, curve));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, CurveCalc(2.71828f, curve));  // ln(e) ≈ 1
}

void test_logarithmic_zero_returns_zero() {
    // log(0) = -Inf, which the old code let through.
    // Guard returns 0 for non-positive x.
    SuCalibrationCurve curve = {{0, 0, 10, 5}, 2};

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, CurveCalc(0, curve));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, CurveCalc(-1, curve));
}

// ============================================================================
// Exponential Curves (iCurveType = 3)
// ============================================================================

void test_exponential() {
    // y = 2*e^(0.5*x)
    // Uses last two coefficients: afCoeff[2] = 2, afCoeff[3] = 0.5
    SuCalibrationCurve curve = {{0, 0, 2, 0.5f}, 3};

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, CurveCalc(0, curve));  // 2*e^0 = 2
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.297f, CurveCalc(1, curve)); // 2*e^0.5 ≈ 3.297
}

void test_exponential_decay() {
    // y = 10*e^(-0.5*x) - exponential decay
    SuCalibrationCurve curve = {{0, 0, 10, -0.5f}, 3};

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, CurveCalc(0, curve));  // 10*e^0 = 10
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.065f, CurveCalc(1, curve));  // 10*e^-0.5 ≈ 6.065
}

// ============================================================================
// Edge Cases
// ============================================================================

void test_unknown_curve_type() {
    SuCalibrationCurve curve = {{1, 2, 3, 4}, 99};
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, CurveCalc(5, curve));
}

void test_zero_curve_type() {
    SuCalibrationCurve curve = {{1, 2, 3, 4}, 0};
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, CurveCalc(5, curve));
}

void test_polynomial_zero_input() {
    // y = x^3 + x^2 + x + 1
    SuCalibrationCurve curve = {{1, 1, 1, 1}, 1};
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, CurveCalc(0, curve));  // Just constant term
}

// ============================================================================
// Main
// ============================================================================

int main() {
    UNITY_BEGIN();

    // Polynomial tests
    RUN_TEST(test_polynomial_constant);
    RUN_TEST(test_polynomial_linear);
    RUN_TEST(test_polynomial_quadratic);
    RUN_TEST(test_polynomial_cubic);
    RUN_TEST(test_polynomial_negative_x);

    // Logarithmic tests
    RUN_TEST(test_logarithmic);
    RUN_TEST(test_logarithmic_e);
    RUN_TEST(test_logarithmic_zero_returns_zero);

    // Exponential tests
    RUN_TEST(test_exponential);
    RUN_TEST(test_exponential_decay);

    // Edge cases
    RUN_TEST(test_unknown_curve_type);
    RUN_TEST(test_zero_curve_type);
    RUN_TEST(test_polynomial_zero_input);

    return UNITY_END();
}
