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
// C++ matches Octave to ~5e-6 degrees; use 0.001 deg for margin
static constexpr float OCTAVE_TOL = 0.001f;

void setUp(void) {}
void tearDown(void) {}

/**
 * Compare C++ EKF6 pitch_rate test against Octave reference
 *
 * Octave reference: ekf6_pitch_rate.csv
 * Input: 5 deg/s pitch rate for 2 seconds, then hold for 3 seconds
 *
 * Key reference points from Octave filter output (not ground truth):
 *   t=0.5s:  theta=2.524038 deg  (filter lags slightly behind 2.5)
 *   t=1.0s:  theta=5.024038 deg  (accumulated filter lag)
 *   t=2.0s:  theta=10.000 deg    (converged after rate stops)
 *   t=5.0s:  theta=10.000 deg    (settled)
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

    // Compare against Octave filter values (not ground truth)
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 2.524038f, theta_at_0_5s);
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 5.024038f, theta_at_1_0s);
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 10.0f, theta_at_2_0s);
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 10.0f, theta_at_5_0s);
}

/**
 * Compare C++ EKF6 level_flight test against Octave reference
 *
 * Input: Level attitude (ax=0, ay=0, az=-g), no gyro rates
 * Octave filter output: phi=0, theta=0, alpha=0
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

    // Octave filter output: exact zeros
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.phi_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.theta_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.alpha_deg());
}

/**
 * Compare C++ EKF6 pitched_10deg test against Octave reference
 *
 * Input: 10 deg pitch attitude
 * Octave filter output: phi=0, theta=10.0001, alpha=10.000155
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

    // Octave filter output (not ground truth — filter converges very close)
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.phi_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 10.0001f, state.theta_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 10.000155f, state.alpha_deg());
}

/**
 * Compare C++ EKF6 banked_20deg test against Octave reference
 *
 * Input: 20 deg bank attitude
 * Octave filter output: phi=20.000236, theta=0, alpha=0
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

    // Octave filter output
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 20.000236f, state.phi_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.theta_deg());
    TEST_ASSERT_FLOAT_WITHIN(OCTAVE_TOL, 0.0f, state.alpha_deg());
}

/**
 * Compare C++ EKF6 gyro_bias test against Octave reference
 *
 * Input: Level attitude with 2 deg/s pitch gyro bias
 * Octave filter output: theta≈0.014230, bq>0 (slowly converging)
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

    // Octave filter output: theta≈0.014230 deg after 5s
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, state.theta_deg());

    // Bias should be positive and moving toward true value
    TEST_ASSERT_TRUE(state.bq_dps() > 0.0f);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_octave_level_flight_comparison);
    RUN_TEST(test_octave_pitched_10deg_comparison);
    RUN_TEST(test_octave_banked_20deg_comparison);
    RUN_TEST(test_octave_pitch_rate_comparison);
    RUN_TEST(test_octave_gyro_bias_comparison);

    return UNITY_END();
}
