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

// ============================================================
// Cholesky-failure counter tests (issue #593 item #1)
// ============================================================

void test_ekfq_counter_starts_at_zero(void) {
    EKFQ ekfq;
    ekfq.init();
    TEST_ASSERT_EQUAL_UINT32(0u, ekfq.getUpdateCallCount());
    TEST_ASSERT_EQUAL_UINT32(0u, ekfq.getFailedUpdateCount());
    TEST_ASSERT_EQUAL_UINT32(0u, ekfq.getLastFailedCallNum());
}

void test_ekfq_counter_unchanged_on_normal_update(void) {
    EKFQ ekfq;
    ekfq.init();
    EKFQ::Measurements meas{};
    meas.ax = 0.0f;
    meas.ay = 0.0f;
    meas.az = -G;
    meas.p  = 0.0f;
    meas.q  = 0.0f;
    meas.r  = 0.0f;
    meas.tasMps       = 0.0f;
    meas.tasDotMps2   = 0.0f;
    meas.baroAltMeters = 0.0f;
    meas.updateBaro    = true;
    for (int i = 0; i < 10; ++i) {
        ekfq.update(meas, DT);
    }
    TEST_ASSERT_EQUAL_UINT32(10u, ekfq.getUpdateCallCount());
    TEST_ASSERT_EQUAL_UINT32(0u,  ekfq.getFailedUpdateCount());
    TEST_ASSERT_EQUAL_UINT32(0u,  ekfq.getLastFailedCallNum());
}

void test_ekfq_counter_bumps_on_degenerate_S(void) {
    // Poison r_baro to drive the Cholesky diagonal negative on the
    // baro row. With a strongly negative R diagonal entry, the
    // corresponding row of S = H·P·H^T + R sums into a negative
    // diagonal entry within the j-loop, triggering sum<=0.0f.
    EKFQ::Config cfg = EKFQ::Config::defaults();
    cfg.r_baro = -1.0e9f;
    EKFQ ekfq(cfg);
    ekfq.init();
    EKFQ::Measurements meas{};
    meas.ax = 0.0f;
    meas.ay = 0.0f;
    meas.az = -G;
    meas.baroAltMeters = 0.0f;
    meas.updateBaro    = true;
    ekfq.update(meas, DT);
    TEST_ASSERT_EQUAL_UINT32(1u, ekfq.getUpdateCallCount());
    TEST_ASSERT_EQUAL_UINT32(1u, ekfq.getFailedUpdateCount());
    TEST_ASSERT_EQUAL_UINT32(1u, ekfq.getLastFailedCallNum());
}

void test_ekfq_counter_persists_across_init(void) {
    // Bump the counter via a degenerate-S update, then reseed via
    // init(). Counters must survive — a failure burst right before a
    // reseed is the kind of pattern we want post-flight reviewers
    // to spot.
    EKFQ::Config cfg = EKFQ::Config::defaults();
    cfg.r_baro = -1.0e9f;
    EKFQ ekfq(cfg);
    ekfq.init();
    EKFQ::Measurements meas{};
    meas.az = -G;
    meas.baroAltMeters = 0.0f;
    meas.updateBaro    = true;
    ekfq.update(meas, DT);
    TEST_ASSERT_EQUAL_UINT32(1u, ekfq.getFailedUpdateCount());
    const uint32_t failBefore = ekfq.getFailedUpdateCount();
    const uint32_t lastBefore = ekfq.getLastFailedCallNum();
    const uint32_t callBefore = ekfq.getUpdateCallCount();
    ekfq.init(0.0f, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT32(failBefore, ekfq.getFailedUpdateCount());
    TEST_ASSERT_EQUAL_UINT32(lastBefore, ekfq.getLastFailedCallNum());
    TEST_ASSERT_EQUAL_UINT32(callBefore, ekfq.getUpdateCallCount());
}

void test_ekfq_counter_increments_by_one_on_batch_failure(void) {
    // Master's EKFQ::correct() is a pure batch update — one Cholesky
    // guard for all 8 measurements. Even with every R_diag entry
    // poisoned, a single failed update() must bump
    // failedUpdateCount_ by exactly 1, not 8. This test documents the
    // batch semantics so a future port back to scalar updates won't
    // silently change counter semantics without also changing this
    // test.
    EKFQ::Config cfg = EKFQ::Config::defaults();
    cfg.r_ax   = -1.0e9f;
    cfg.r_ay   = -1.0e9f;
    cfg.r_az   = -1.0e9f;
    cfg.r_baro = -1.0e9f;
    EKFQ ekfq(cfg);
    ekfq.init();
    EKFQ::Measurements meas{};
    meas.az = -G;
    meas.baroAltMeters = 0.0f;
    meas.updateBaro    = true;
    ekfq.update(meas, DT);
    TEST_ASSERT_EQUAL_UINT32(1u, ekfq.getFailedUpdateCount());
}

void test_ekfq_counter_advances_via_direct_predict_correct(void) {
    // Production path: EkfqPipeline::Step() calls predict() and
    // correct() separately. Confirms the counter increments in that
    // path, not just via the update() convenience wrapper.
    EKFQ ekfq;
    ekfq.init();
    const float ax = 0.0f, ay = 0.0f, az = -G;
    const float p = 0.0f, q = 0.0f, r = 0.0f;
    const float tas = 0.0f, tasDot = 0.0f, baro = 0.0f;
    for (int i = 0; i < 5; ++i) {
        ekfq.predict(p, q, r, ax, ay, az, tas, DT);
        ekfq.correct(ax, ay, az, tas, tasDot, q, r, baro, true);
    }
    TEST_ASSERT_EQUAL_UINT32(5u, ekfq.getUpdateCallCount());
    TEST_ASSERT_EQUAL_UINT32(0u, ekfq.getFailedUpdateCount());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ekfq_init_default);
    RUN_TEST(test_ekfq_init_with_attitude);
    RUN_TEST(test_ekfq_level_flight);
    RUN_TEST(test_ekfq_pitched_static);
    RUN_TEST(test_ekfq_quaternion_stays_unit);
    RUN_TEST(test_ekfq_defaults_finite);
    RUN_TEST(test_ekfq_pipeline_config_override);
    RUN_TEST(test_ekfq_counter_starts_at_zero);
    RUN_TEST(test_ekfq_counter_unchanged_on_normal_update);
    RUN_TEST(test_ekfq_counter_bumps_on_degenerate_S);
    RUN_TEST(test_ekfq_counter_persists_across_init);
    RUN_TEST(test_ekfq_counter_increments_by_one_on_batch_failure);
    RUN_TEST(test_ekfq_counter_advances_via_direct_predict_correct);
    return UNITY_END();
}
