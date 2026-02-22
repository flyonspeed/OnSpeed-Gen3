/**
 * @file test_ekf6_octave_comparison.cpp
 * @brief Comparison tests between C++ EKF6 and Octave reference implementation
 *
 * These tests run the C++ EKF6 with the same inputs as the Octave reference
 * and compare the outputs at key points. This validates that the C++
 * implementation matches the reference algorithm.
 */

#include <unity.h>
#include <EKF6.h>
#include <cmath>

using namespace onspeed;

static constexpr float DT = 1.0f / 208.0f;
static constexpr float DEG2RAD = 3.14159265358979f / 180.0f;
static constexpr float G = 9.80665f;

// Tolerances for comparison with Octave reference
// Allow 0.1 degree difference due to floating point precision
static constexpr float OCTAVE_TOL = 0.1f;

void setUp(void) {}
void tearDown(void) {}

/**
 * Compare C++ EKF6 pitch_rate test against Octave reference
 *
 * Octave reference: ekf6_pitch_rate.csv
 * Input: 5 deg/s pitch rate for 2 seconds, then hold for 3 seconds
 * Expected final theta: 10.0 degrees
 *
 * Key reference points from Octave:
 *   t=0.5s:  theta=2.500 deg
 *   t=1.0s:  theta=5.000 deg
 *   t=2.0s:  theta=10.000 deg
 *   t=5.0s:  theta=10.000 deg
 */
void test_octave_pitch_rate_comparison(void) {
    float pitch_rate = 5.0f * DEG2RAD;
    float pitch_duration = 2.0f;

    EKF6 ekf;
    ekf.init();

    float theta_true = 0.0f;
    int n_samples = static_cast<int>(5.0f / DT);

    // Track values at key time points
    float theta_at_0_5s = 0.0f;
    float theta_at_1_0s = 0.0f;
    float theta_at_2_0s = 0.0f;
    float theta_at_5_0s = 0.0f;

    for (int i = 0; i < n_samples; i++) {
        float t = i * DT;

        float q;
        if (t < pitch_duration) {
            q = pitch_rate;
            theta_true += pitch_rate * DT;
        } else {
            q = 0.0f;
        }

        EKF6::Measurements meas = {
            .ax = G * std::sin(theta_true),
            .ay = 0.0f,
            .az = -G * std::cos(theta_true),
            .p = 0.0f,
            .q = q,
            .r = 0.0f,
            .gamma = 0.0f
        };

        ekf.update(meas, DT);

        EKF6::State state = ekf.getState();

        // Capture values at key times
        if (std::fabs(t - 0.5f) < DT / 2) theta_at_0_5s = state.theta_deg();
        if (std::fabs(t - 1.0f) < DT / 2) theta_at_1_0s = state.theta_deg();
        if (std::fabs(t - 2.0f) < DT / 2) theta_at_2_0s = state.theta_deg();
    }

    theta_at_5_0s = ekf.getState().theta_deg();

    // Compare against Octave reference values
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 2.5f, theta_at_0_5s);
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 5.0f, theta_at_1_0s);
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 10.0f, theta_at_2_0s);
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 10.0f, theta_at_5_0s);
}

/**
 * Compare C++ EKF6 level_flight test against Octave reference
 *
 * Input: Level attitude (ax=0, ay=0, az=-g), no gyro rates
 * Expected: phi=0, theta=0, alpha=0
 */
void test_octave_level_flight_comparison(void) {
    EKF6 ekf;
    ekf.init();

    EKF6::Measurements meas = {
        .ax = 0.0f,
        .ay = 0.0f,
        .az = -G,
        .p = 0.0f,
        .q = 0.0f,
        .r = 0.0f,
        .gamma = 0.0f
    };

    int n_samples = static_cast<int>(5.0f / DT);
    for (int i = 0; i < n_samples; i++) {
        ekf.update(meas, DT);
    }

    EKF6::State state = ekf.getState();

    // Octave reference shows all zeros
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.phi_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.theta_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.alpha_deg());
}

/**
 * Compare C++ EKF6 pitched_10deg test against Octave reference
 *
 * Input: 10 deg pitch attitude
 * Expected: phi=0, theta=10, alpha=10
 */
void test_octave_pitched_10deg_comparison(void) {
    float theta_true = 10.0f * DEG2RAD;

    EKF6 ekf;
    ekf.init();

    EKF6::Measurements meas = {
        .ax = G * std::sin(theta_true),
        .ay = 0.0f,
        .az = -G * std::cos(theta_true),
        .p = 0.0f,
        .q = 0.0f,
        .r = 0.0f,
        .gamma = 0.0f
    };

    int n_samples = static_cast<int>(5.0f / DT);
    for (int i = 0; i < n_samples; i++) {
        ekf.update(meas, DT);
    }

    EKF6::State state = ekf.getState();

    // Octave reference converges to theta=10.0, alpha=10.0
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.phi_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 10.0f, state.theta_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 10.0f, state.alpha_deg());
}

/**
 * Compare C++ EKF6 banked_20deg test against Octave reference
 *
 * Input: 20 deg bank attitude
 * Expected: phi=20, theta=0, alpha=0
 */
void test_octave_banked_20deg_comparison(void) {
    float phi_true = 20.0f * DEG2RAD;

    EKF6 ekf;
    ekf.init();

    EKF6::Measurements meas = {
        .ax = 0.0f,
        .ay = -G * std::sin(phi_true),
        .az = -G * std::cos(phi_true),
        .p = 0.0f,
        .q = 0.0f,
        .r = 0.0f,
        .gamma = 0.0f
    };

    int n_samples = static_cast<int>(5.0f / DT);
    for (int i = 0; i < n_samples; i++) {
        ekf.update(meas, DT);
    }

    EKF6::State state = ekf.getState();

    // Octave reference converges to phi=20.0, theta=0.0
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 20.0f, state.phi_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.theta_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.alpha_deg());
}

/**
 * Compare C++ EKF6 gyro_bias test against Octave reference
 *
 * Input: Level attitude with 2 deg/s pitch gyro bias
 * Expected: theta stays near 0, bq moves toward 2 deg/s (slowly)
 */
void test_octave_gyro_bias_comparison(void) {
    float q_bias = 2.0f * DEG2RAD;

    EKF6 ekf;
    ekf.init();

    EKF6::Measurements meas = {
        .ax = 0.0f,
        .ay = 0.0f,
        .az = -G,
        .p = 0.0f,
        .q = q_bias,
        .r = 0.0f,
        .gamma = 0.0f
    };

    int n_samples = static_cast<int>(5.0f / DT);
    for (int i = 0; i < n_samples; i++) {
        ekf.update(meas, DT);
    }

    EKF6::State state = ekf.getState();

    // Octave reference: theta ~0.014, bq ~0.386 after 5s
    // Theta should stay small (accelerometer correction works)
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, state.theta_deg());

    // Bias should be positive and moving toward true value
    TEST_ASSERT_TRUE(state.bq_dps() > 0.0f);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_octave_level_flight_comparison);
    RUN_TEST(test_octave_pitched_10deg_comparison);
    RUN_TEST(test_octave_banked_20deg_comparison);
    RUN_TEST(test_octave_pitch_rate_comparison);
    RUN_TEST(test_octave_gyro_bias_comparison);

    return UNITY_END();
}
