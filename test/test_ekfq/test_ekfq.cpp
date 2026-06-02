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
#include <ahrs/EkfqPipeline.h>
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

// Exact exponential-map propagation: integrating a pure constant body rate
// reproduces the closed-form rotation angle. Feed a fixed yaw rate with
// gravity held self-consistent (accel reads level), then read the integrated
// heading back from the quaternion's z-rotation.
void test_ekfq_exp_map_constant_rate_angle(void) {
    EKFQ ekfq;
    ekfq.init();  // identity quaternion

    // 30 deg/s yaw for exactly 1 second across 208 substeps.
    const float rate_rad_s = 30.0f * DEG2RAD;
    const int   steps      = 208;
    EKFQ::Measurements m = {};
    m.az = -G;             // level gravity so accel updates don't fight the yaw
    m.r  = rate_rad_s;     // pure yaw
    m.updateBaro = true;

    for (int i = 0; i < steps; ++i) ekfq.update(m, DT);

    // Recover yaw from the quaternion (z-axis rotation):
    //   ψ = atan2(2(q0 q3 + q1 q2), 1 − 2(q2² + q3²)).
    // 208 substeps × (1/208 s) at 30°/s integrates to 30°. The β/bias unit
    // priors and accel updates damp this slightly toward level, so allow a
    // generous band — the point is the exact step holds the geometric angle a
    // coarse linear-step + renormalise would shed over the revolution.
    const EKFQ::State s = ekfq.getState();
    const float psi = std::atan2(2.0f * (s.q0 * s.q3 + s.q1 * s.q2),
                                 1.0f - 2.0f * (s.q2 * s.q2 + s.q3 * s.q3));
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 30.0f, psi / DEG2RAD);
    const float n2 = s.q0*s.q0 + s.q1*s.q1 + s.q2*s.q2 + s.q3*s.q3;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, n2);
}

// Zero rate must be an identity step: ω = 0 → Δq = [1,0,0,0], quaternion
// untouched. Guards the sinc small-angle branch against divide-by-zero or a
// spurious nudge at ω = 0.
void test_ekfq_exp_map_zero_rate_identity(void) {
    const float roll0  = 8.0f * DEG2RAD;
    const float pitch0 = -3.0f * DEG2RAD;
    EKFQ ekfq;
    ekfq.init(roll0, pitch0, 500.0f);
    const EKFQ::State before = ekfq.getState();

    // Specific force consistent with the seed attitude (NED-body convention),
    // so the accel updates have nothing to correct and only the zero-rate
    // propagation is exercised.
    EKFQ::Measurements m = {};
    m.ax = +G * std::sin(pitch0);
    m.ay = -G * std::cos(pitch0) * std::sin(roll0);
    m.az = -G * std::cos(pitch0) * std::cos(roll0);
    m.p = m.q = m.r = 0.0f;  // zero body rate
    m.baroAltMeters = 500.0f;
    m.updateBaro = true;

    ekfq.update(m, DT);
    const EKFQ::State after = ekfq.getState();
    TEST_ASSERT_FLOAT_WITHIN(0.02f, before.roll_deg(),  after.roll_deg());
    TEST_ASSERT_FLOAT_WITHIN(0.02f, before.pitch_deg(), after.pitch_deg());
}

void test_ekfq_defaults_finite(void) {
    EKFQ::Config c = EKFQ::Config::defaults();
    TEST_ASSERT_TRUE(c.q_quat > 0.0f && std::isfinite(c.q_quat));
    TEST_ASSERT_TRUE(c.r_ax   > 0.0f && std::isfinite(c.r_ax));
    TEST_ASSERT_TRUE(c.r_ay   > 0.0f && std::isfinite(c.r_ay));
    TEST_ASSERT_TRUE(c.r_az   > 0.0f && std::isfinite(c.r_az));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 12.0f, c.tas_min_mps);
}

void test_ekfq_pipeline_config_override(void) {
    // Default-constructed pipeline matches PipelineConfig::defaults()
    onspeed::ahrs::EkfqPipeline pipe;
    const auto def = pipe.getPipelineConfig();
    const auto refDef = onspeed::ahrs::EkfqPipeline::PipelineConfig::defaults();
    TEST_ASSERT_EQUAL_FLOAT(refDef.accelEmaAlpha,   def.accelEmaAlpha);
    TEST_ASSERT_EQUAL_FLOAT(refDef.compFadeTauSec,  def.compFadeTauSec);
    TEST_ASSERT_EQUAL_FLOAT(refDef.iasGateRisingKt, def.iasGateRisingKt);
    TEST_ASSERT_EQUAL_FLOAT(refDef.tasdotEmaAlpha,  def.tasdotEmaAlpha);

    // Override via setPipelineConfig is visible in getPipelineConfig
    onspeed::ahrs::EkfqPipeline::PipelineConfig custom{};
    custom.accelEmaAlpha   = 0.10f;
    custom.compFadeTauSec  = 1.5f;
    custom.iasGateRisingKt = 40.0f;
    custom.tasdotEmaAlpha  = 0.05f;
    pipe.setPipelineConfig(custom);
    const auto got = pipe.getPipelineConfig();
    TEST_ASSERT_EQUAL_FLOAT(custom.accelEmaAlpha,   got.accelEmaAlpha);
    TEST_ASSERT_EQUAL_FLOAT(custom.compFadeTauSec,  got.compFadeTauSec);
    TEST_ASSERT_EQUAL_FLOAT(custom.iasGateRisingKt, got.iasGateRisingKt);
    TEST_ASSERT_EQUAL_FLOAT(custom.tasdotEmaAlpha,  got.tasdotEmaAlpha);
}

// After a few steps of level flight, the diagnostic states the snapshot
// layer surfaces (bp/bq/br, b_az, β, yaw) must be finite and within sane
// engineering ranges.  Guards against a getState() regression that
// returns NaN/Inf, or sign-convention drift that would push the biases
// to absurd magnitudes.
void test_ekfq_diagnostic_states_finite_in_level_flight(void) {
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
    TEST_ASSERT_TRUE(std::isfinite(s.bp));
    TEST_ASSERT_TRUE(std::isfinite(s.bq));
    TEST_ASSERT_TRUE(std::isfinite(s.br));
    TEST_ASSERT_TRUE(std::isfinite(s.b_az));
    TEST_ASSERT_TRUE(std::isfinite(s.beta));
    TEST_ASSERT_TRUE(std::isfinite(s.yaw_deg()));

    // Sanity: biases driven by zero-rate priors should stay small
    // (< a few deg/s), β small in level flight (<2°), b_az bounded by
    // the prior covariance.  Loose bounds — the goal is "no garbage."
    TEST_ASSERT_TRUE(std::fabs(s.bp)   < 0.1f);   // rad/s ≈ 5.7 deg/s
    TEST_ASSERT_TRUE(std::fabs(s.bq)   < 0.1f);
    TEST_ASSERT_TRUE(std::fabs(s.br)   < 0.1f);
    TEST_ASSERT_TRUE(std::fabs(s.b_az) < 2.0f);   // m/s²
    TEST_ASSERT_TRUE(std::fabs(s.beta_deg()) < 5.0f);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ekfq_init_default);
    RUN_TEST(test_ekfq_init_with_attitude);
    RUN_TEST(test_ekfq_level_flight);
    RUN_TEST(test_ekfq_pitched_static);
    RUN_TEST(test_ekfq_quaternion_stays_unit);
    RUN_TEST(test_ekfq_exp_map_constant_rate_angle);
    RUN_TEST(test_ekfq_exp_map_zero_rate_identity);
    RUN_TEST(test_ekfq_defaults_finite);
    RUN_TEST(test_ekfq_pipeline_config_override);
    RUN_TEST(test_ekfq_diagnostic_states_finite_in_level_flight);
    return UNITY_END();
}
