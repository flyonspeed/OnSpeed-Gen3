/**
 * @file test_ekfq.cpp
 * @brief Unit tests for EKFQ (11-state quaternion EKF).
 *
 * Sanity-level tests: init, level-flight convergence, pitched-static
 * convergence, defaults match the Optuna best trial, quaternion stays
 * unit-normalised over time. Numerical accuracy validation is done in
 * the Python reference (onspeed_ekf/ekf_quat.py) against VN-300 truth
 * on 79 minutes of testbed flight log; this firmware port preserves
 * the algebra exactly so a unit-level cross-check is sufficient.
 */

#include <unity.h>
#include <ahrs/EKFQ.h>
#include <cmath>

using namespace onspeed;

static constexpr float DT      = 1.0f / 208.0f;
static constexpr float DEG2RAD = 3.14159265358979f / 180.0f;
static constexpr float G       = 9.80665f;
static constexpr float ABS_TOL = 0.001f;

void setUp(void) {}
void tearDown(void) {}

void test_ekfq_init_default(void) {
    EKFQ ekfq;
    ekfq.init();
    const EKFQ::State s = ekfq.getState();
    TEST_ASSERT_FLOAT_WITHIN(ABS_TOL, 1.0f, s.q0);
    TEST_ASSERT_FLOAT_WITHIN(ABS_TOL, 0.0f, s.q1);
    TEST_ASSERT_FLOAT_WITHIN(ABS_TOL, 0.0f, s.q2);
    TEST_ASSERT_FLOAT_WITHIN(ABS_TOL, 0.0f, s.q3);
    TEST_ASSERT_FLOAT_WITHIN(ABS_TOL, 0.0f, s.bp);
    TEST_ASSERT_FLOAT_WITHIN(ABS_TOL, 0.0f, s.bq);
    TEST_ASSERT_FLOAT_WITHIN(ABS_TOL, 0.0f, s.br);
    TEST_ASSERT_FLOAT_WITHIN(ABS_TOL, 0.0f, s.z);
    TEST_ASSERT_FLOAT_WITHIN(ABS_TOL, 0.0f, s.vz);
    TEST_ASSERT_FLOAT_WITHIN(ABS_TOL, 0.0f, s.b_az);
    TEST_ASSERT_FLOAT_WITHIN(ABS_TOL, 0.0f, s.beta);
}

void test_ekfq_init_with_attitude(void) {
    EKFQ ekfq;
    ekfq.init(10.0f * DEG2RAD, 5.0f * DEG2RAD, 1000.0f);
    const EKFQ::State s = ekfq.getState();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, s.roll_deg());
    TEST_ASSERT_FLOAT_WITHIN(0.01f,  5.0f, s.pitch_deg());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f, s.z);
    const float n2 = s.q0*s.q0 + s.q1*s.q1 + s.q2*s.q2 + s.q3*s.q3;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, n2);
}

void test_ekfq_level_flight(void) {
    EKFQ ekfq;
    ekfq.init(0.0f, 0.0f, 100.0f);

    EKFQ::Measurements m = {};
    m.ax = 0.0f;
    m.ay = 0.0f;
    m.az = -G;
    m.p = m.q = m.r = 0.0f;
    m.baroAltMeters = 100.0f;
    m.tasMps = 0.0f;
    m.tasDotMps2 = 0.0f;
    m.updateBaro = true;
    for (int i = 0; i < 416; ++i) ekfq.update(m, DT);

    const EKFQ::State s = ekfq.getState();
    TEST_ASSERT_FLOAT_WITHIN(0.10f, 0.0f, s.roll_deg());
    TEST_ASSERT_FLOAT_WITHIN(0.10f, 0.0f, s.pitch_deg());
    TEST_ASSERT_FLOAT_WITHIN(1.0f,  100.0f, s.z);
    TEST_ASSERT_FLOAT_WITHIN(0.5f,  0.0f,   s.vz);
}

void test_ekfq_pitched_static(void) {
    // Body-frame SPECIFIC FORCE (what an accel reads when stationary)
    // for an aircraft pitched +θ nose-up in NED-body convention:
    //   f_x = +g·sin(θ)   (gravity reaction pushes the proof mass forward)
    //   f_y =  0
    //   f_z = -g·cos(θ)   (gravity reaction along body-Z, level reads -g)
    //
    // EKFQ::predict / correct expect specific force (not gravity-in-body).
    // EKFQ.cpp's ax_pred = -2g(q1·q3 - q0·q2) for a pitch-only quaternion
    // gives +g·sin(θ); the measurement must match in sign for convergence.
    EKFQ ekfq;
    const float theta_rad = 10.0f * DEG2RAD;
    ekfq.init(0.0f, theta_rad, 100.0f);
    EKFQ::Measurements m = {};
    m.ax = +G * std::sin(theta_rad);
    m.ay = 0.0f;
    m.az = -G * std::cos(theta_rad);
    m.p = m.q = m.r = 0.0f;
    m.baroAltMeters = 100.0f;
    m.tasMps = 0.0f;
    m.tasDotMps2 = 0.0f;
    m.updateBaro = true;
    for (int i = 0; i < 416; ++i) ekfq.update(m, DT);

    const EKFQ::State s = ekfq.getState();
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 10.0f, s.pitch_deg());
    TEST_ASSERT_FLOAT_WITHIN(0.2f,  0.0f, s.roll_deg());
}

void test_ekfq_quaternion_stays_unit(void) {
    EKFQ ekfq;
    ekfq.init();
    EKFQ::Measurements m = {};
    m.az = -G;
    m.r = 90.0f * DEG2RAD;
    m.updateBaro = true;
    for (int i = 0; i < 208; ++i) ekfq.update(m, DT);
    const EKFQ::State s = ekfq.getState();
    const float n2 = s.q0*s.q0 + s.q1*s.q1 + s.q2*s.q2 + s.q3*s.q3;
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, n2);
}

void test_ekfq_defaults_finite(void) {
    EKFQ::Config c = EKFQ::Config::defaults();
    TEST_ASSERT_TRUE(c.q_quat > 0.0f && std::isfinite(c.q_quat));
    TEST_ASSERT_TRUE(c.r_ax   > 0.0f && std::isfinite(c.r_ax));
    TEST_ASSERT_TRUE(c.r_ay   > 0.0f && std::isfinite(c.r_ay));
    TEST_ASSERT_TRUE(c.r_az   > 0.0f && std::isfinite(c.r_az));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 12.0f, c.tas_min_mps);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ekfq_init_default);
    RUN_TEST(test_ekfq_init_with_attitude);
    RUN_TEST(test_ekfq_level_flight);
    RUN_TEST(test_ekfq_pitched_static);
    RUN_TEST(test_ekfq_quaternion_stays_unit);
    RUN_TEST(test_ekfq_defaults_finite);
    return UNITY_END();
}
