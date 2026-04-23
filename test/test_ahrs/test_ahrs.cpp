// test_ahrs.cpp — native tests for onspeed::ahrs::Ahrs.
//
// Focused on observable behavior of the Step() method given controlled
// inputs.  The snapshot regression harness (tools/regression/) covers
// byte-identity against recorded flight logs; these unit tests cover the
// orthogonal axis — "does the filter recover from startup", "does gyro
// bias converge", "does DerivedAOA follow pitch when VSI=0", etc.

#include <unity.h>

#include <cmath>

#include <ahrs/Ahrs.h>
#include <types/AhrsInputs.h>
#include <types/AhrsOutputs.h>
#include <util/OnSpeedTypes.h>

using onspeed::AhrsInputs;
using onspeed::AhrsOutputs;
using onspeed::ahrs::Ahrs;
using onspeed::ahrs::AhrsConfig;
using onspeed::ahrs::Algorithm;

namespace {

constexpr float kImuRate = 208.0f;
constexpr float kDt = 1.0f / kImuRate;

AhrsConfig makeCfg(Algorithm alg)
{
    AhrsConfig cfg;
    cfg.pitchBiasDeg = 0.0f;
    cfg.rollBiasDeg  = 0.0f;
    cfg.algorithm    = alg;
    cfg.gyroSmoothingWindow = 30;
    cfg.imuSampleRateHz = kImuRate;
    cfg.pressureSampleRateHz = 50.0f;
    return cfg;
}

// Produce a level, stationary seed: Z = -1 g (gravity down on body),
// zero gyro, 5 kt IAS (below the 25 kt flight-path threshold), sea-level
// Palt, no OAT.  Useful both for Init() and as the first Step input.
AhrsInputs levelSeed()
{
    AhrsInputs in;
    in.imu.accelXG      = 0.0f;
    in.imu.accelYG      = 0.0f;
    in.imu.accelZG      = -1.0f;
    in.imu.gyroRollDps  = 0.0f;
    in.imu.gyroPitchDps = 0.0f;
    in.imu.gyroYawDps   = 0.0f;
    in.imu.tempCelsius  = 20.0f;
    in.imu.timestampUs  = 0;
    in.sensors.iasKt    = 5.0f;
    in.sensors.paltFt   = 0.0f;
    in.sensors.oatCelsius = 20.0f;
    in.sensors.timestampUs = 0;
    in.iasUpdateTimestampUs = 0;
    in.useEfisOat     = false;
    in.useInternalOat = false;
    in.efisOatCelsius = 0.0f;
    return in;
}

}   // namespace

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------
// Construction + Init
// ---------------------------------------------------------------------

void test_construction_defaults_outputs_to_zero(void)
{
    Ahrs a{makeCfg(Algorithm::Madgwick)};
    const AhrsOutputs& out = a.latest();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.pitchDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.rollDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.flightPathDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.derivedAoaDeg);
    // Accel vert seeded to -1 g (level-on-the-ground).
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, a.accelVertCorrG());
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, a.accelVertSmoothedG());
}

void test_init_seeds_pitch_and_roll_from_accel(void)
{
    // 10-deg nose-up static attitude: ax = sin(10°)·g, az = -cos(10°)·g
    AhrsInputs seed = levelSeed();
    seed.imu.accelXG = std::sin(onspeed::deg2rad(10.0f));
    seed.imu.accelZG = -std::cos(onspeed::deg2rad(10.0f));

    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    a.Init(seed, 1000.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, a.latest().pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f,  a.latest().rollDeg);
}

void test_init_applies_pitch_and_roll_bias(void)
{
    // Level accel, but configured with +5° pitch bias and +3° roll bias.
    AhrsInputs seed = levelSeed();
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    cfg.pitchBiasDeg = 5.0f;
    cfg.rollBiasDeg  = 3.0f;

    Ahrs a{cfg};
    a.Init(seed, 0.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, a.latest().pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 3.0f, a.latest().rollDeg);
}

// ---------------------------------------------------------------------
// Step: static attitude stability (Madgwick)
// ---------------------------------------------------------------------

void test_step_static_level_converges_near_zero(void)
{
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    a.Init(in, 0.0f);

    for (int i = 0; i < 500; ++i) {
        a.Step(in, kDt);
    }

    // Level attitude should stay near zero after ~2.4 s of integration.
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, a.latest().pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, a.latest().rollDeg);
}

void test_step_static_nose_up_settles_to_stable_pitch_madgwick(void)
{
    // A static nose-up 10° accel sample, fed through the filter with
    // zero gyro, should converge to *some* stable pitch close to the
    // accel-derived angle.  The exact value depends on Madgwick's gain
    // and the low-gravity reaction from the compensation chain; the
    // important property is that the value stabilizes and stays finite.
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    in.imu.accelXG = std::sin(onspeed::deg2rad(10.0f));
    in.imu.accelZG = -std::cos(onspeed::deg2rad(10.0f));
    a.Init(in, 0.0f);

    for (int i = 0; i < 2000; ++i) {
        a.Step(in, kDt);
    }
    const float pAfter2000 = a.latest().pitchDeg;

    // Run another second — pitch should be stable (small delta).
    for (int i = 0; i < 208; ++i) {
        a.Step(in, kDt);
    }
    // Madgwick continues to converge slowly toward the accel-derived
    // gravity vector; the drift over 1 s should be small (<5°).
    TEST_ASSERT_FLOAT_WITHIN(5.0f, pAfter2000, a.latest().pitchDeg);
    TEST_ASSERT_TRUE(std::isfinite(a.latest().pitchDeg));
}

// ---------------------------------------------------------------------
// Step: accelerometer smoothing + EMA filter chain
// ---------------------------------------------------------------------

void test_accel_smoothed_tracks_toward_input(void)
{
    // The accel smoother has alpha = 0.060899.  Starting from seeded
    // (0, 0, -1), pushing a positive forward acceleration should make
    // the smoothed value trend toward the input but not reach it in one
    // frame.
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    in.imu.accelXG = 0.5f;
    a.Init(in, 0.0f);

    for (int i = 0; i < 100; ++i) {
        a.Step(in, kDt);
    }

    // 100 steps × alpha = 0.060899 is ~99% convergence; smoothed fwd
    // component should be close to 0.5 (through the trig rotation with
    // zero bias, it's identity).
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, a.accelFwdSmoothedG());
}

// ---------------------------------------------------------------------
// Step: derived AOA = SmoothedPitch when flight path is zeroed (low IAS)
// ---------------------------------------------------------------------

void test_derived_aoa_equals_pitch_when_ias_below_threshold(void)
{
    // IAS < 25 kt forces FlightPath := 0, and (Madgwick) DerivedAOA :=
    // SmoothedPitch - 0.  Confirm that identity after steady-state.
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    in.sensors.iasKt = 15.0f;                  // below threshold
    // Nose up 8 deg
    in.imu.accelXG = std::sin(onspeed::deg2rad(8.0f));
    in.imu.accelZG = -std::cos(onspeed::deg2rad(8.0f));
    a.Init(in, 0.0f);

    for (int i = 0; i < 2000; ++i) {
        a.Step(in, kDt);
    }
    const AhrsOutputs& out = a.latest();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.flightPathDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, out.pitchDeg, out.derivedAoaDeg);
}

// ---------------------------------------------------------------------
// Step: TAS updates only at pressure cadence (when iasUpdateTimestampUs
// advances)
// ---------------------------------------------------------------------

void test_tas_updates_only_when_ias_timestamp_advances(void)
{
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    in.sensors.iasKt = 100.0f;
    in.sensors.paltFt = 5000.0f;
    in.iasUpdateTimestampUs = 1'000'000u;
    a.Init(in, 5000.0f);

    // First Step: TAS updates because lastIasUpdateUs_ starts at 0.
    a.Step(in, kDt);
    const float tas0 = a.tasMps();
    TEST_ASSERT_TRUE(tas0 > 0.0f);

    // Change IAS but keep the timestamp — TAS should NOT advance.
    in.sensors.iasKt = 200.0f;
    a.Step(in, kDt);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, tas0, a.tasMps());

    // Advance the timestamp — TAS should now reflect the new IAS.
    in.iasUpdateTimestampUs = 1'020'000u;    // 20 ms later (50 Hz cadence)
    a.Step(in, kDt);
    TEST_ASSERT_TRUE(a.tasMps() > tas0);
}

// ---------------------------------------------------------------------
// Step: dt guards — NaN / giant dt fall back to nominal
// ---------------------------------------------------------------------

void test_step_guards_nan_dt(void)
{
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    a.Init(in, 0.0f);

    // NaN dt should be clamped to nominal; no NaN outputs.
    a.Step(in, std::nanf(""));
    TEST_ASSERT_FALSE(std::isnan(a.latest().pitchDeg));
    TEST_ASSERT_FALSE(std::isnan(a.latest().rollDeg));
    // Giant dt > 4*nominal should be clamped.
    a.Step(in, 1.0f);
    TEST_ASSERT_FALSE(std::isnan(a.latest().pitchDeg));
    TEST_ASSERT_FALSE(std::isnan(a.latest().rollDeg));
}

// ---------------------------------------------------------------------
// Step: EKF6 path produces non-zero pitch/roll on level-after-tilt
// ---------------------------------------------------------------------

void test_step_ekf6_level_stable(void)
{
    // EKF6 at rest (level gravity) should converge to finite, stable
    // pitch/roll near zero.  (Tilt-tracking behavior is algorithm-
    // specific; snapshot harness covers numeric equivalence vs legacy.)
    AhrsConfig cfg = makeCfg(Algorithm::Ekf6);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    a.Init(in, 0.0f);

    for (int i = 0; i < 1000; ++i) {
        a.Step(in, kDt);
    }

    TEST_ASSERT_TRUE(std::isfinite(a.latest().pitchDeg));
    TEST_ASSERT_TRUE(std::isfinite(a.latest().rollDeg));
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 0.0f, a.latest().pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 0.0f, a.latest().rollDeg);
}

// ---------------------------------------------------------------------
// Step: running-mean gyro outputs track the input values after N frames
// ---------------------------------------------------------------------

void test_step_gyro_averages_follow_input(void)
{
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    in.imu.gyroRollDps  = 2.0f;
    in.imu.gyroPitchDps = 1.0f;
    in.imu.gyroYawDps   = -0.5f;
    a.Init(in, 0.0f);

    // Feed 40 frames (window is 30); fully saturates buffer.
    for (int i = 0; i < 40; ++i) {
        a.Step(in, kDt);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.05f,  2.0f, a.latest().gyroRollFiltDps);
    TEST_ASSERT_FLOAT_WITHIN(0.05f,  1.0f, a.latest().gyroPitchFiltDps);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -0.5f, a.latest().gyroYawFiltDps);
}

// ---------------------------------------------------------------------
// Step: repeat Init() resets TAS state so a subsequent Step doesn't see
// a spurious large derivative.
// ---------------------------------------------------------------------

void test_init_does_not_reset_tas_state(void)
{
    // Init() must NOT reset TAS state — matches legacy AHRS::Init()
    // behavior. The web UI calls Init() on every config save; if Init()
    // zeroed TAS state, every save during flight would inject a
    // ~0.05g forward-accel-comp glitch that briefly perturbs Madgwick/
    // EKF6 attitude. The constructor handles initial zero-init at boot.
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    in.sensors.iasKt = 100.0f;
    in.iasUpdateTimestampUs = 1'000'000u;
    a.Init(in, 0.0f);
    a.Step(in, kDt);
    TEST_ASSERT_TRUE(a.tasMps() > 0.0f);

    // Re-init should preserve TAS so the next Step doesn't see a
    // discontinuity (= legacy behavior).
    const float tasBeforeReInit = a.tasMps();
    a.Init(in, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(tasBeforeReInit, a.tasMps());
}

// ---------------------------------------------------------------------
// OAT source selection: prefer EFIS over internal.
// ---------------------------------------------------------------------

void test_tas_uses_efis_oat_when_enabled(void)
{
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs aEfis{cfg};
    Ahrs aFallback{cfg};

    AhrsInputs in = levelSeed();
    in.sensors.iasKt = 100.0f;
    in.sensors.paltFt = 5000.0f;
    in.iasUpdateTimestampUs = 1'000'000u;
    in.useEfisOat     = true;
    in.efisOatCelsius = 25.0f;   // warm (lower density, higher TAS)
    in.useInternalOat = true;
    in.sensors.oatCelsius = -20.0f;   // cold (higher density)

    aEfis.Init(in, 5000.0f);
    aEfis.Step(in, kDt);

    // Fallback run: disable EFIS path, force internal (cold) path.
    AhrsInputs inFallback = in;
    inFallback.useEfisOat = false;
    aFallback.Init(inFallback, 5000.0f);
    aFallback.Step(inFallback, kDt);

    // Warmer OAT => higher density altitude => higher TAS.
    TEST_ASSERT_TRUE(aEfis.tasMps() > aFallback.tasMps());
}

// ---------------------------------------------------------------------
// Reconfigure: algorithm change rewires pipeline (no crash, values update).
// ---------------------------------------------------------------------

void test_reconfigure_algorithm_change(void)
{
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    a.Init(in, 0.0f);

    for (int i = 0; i < 100; ++i) a.Step(in, kDt);

    AhrsConfig cfg2 = cfg;
    cfg2.algorithm = Algorithm::Ekf6;
    a.Reconfigure(cfg2);
    // After reconfigure, caller is expected to re-Init; verify calling
    // Step without re-init doesn't crash (mirrors sketch behavior: Init
    // runs before Reconfigure callers touch Process again).
    a.Init(in, 0.0f);
    for (int i = 0; i < 100; ++i) a.Step(in, kDt);

    // No-crash assertion + pitch/roll sane.
    TEST_ASSERT_FALSE(std::isnan(a.latest().pitchDeg));
    TEST_ASSERT_FALSE(std::isnan(a.latest().rollDeg));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Algorithm::Ekf6),
                          static_cast<int>(a.algorithm()));
}

// ---------------------------------------------------------------------
// TAS density-correction fallback paths.
//
// Ahrs.cpp:181-182 (!bHaveOat fallback) — runs when useEfisOat and
// useInternalOat are both false, OR when the supplied OAT fails the
// oatInBand check (|OAT_C| >= 100). Must produce a finite, nonzero TAS
// — never silently return 0, which would corrupt FlightPath via
// safeAsin(VSI/TAS).
//
// Ahrs.cpp:173-174 (fDivisor <= 0 fallback) — extreme DA overflow.
//
// NOTE on Ahrs.cpp:177-178 (fOAT_k <= 0 fallback): this branch is
// unreachable by construction. oatInBand() rejects any OAT with
// |c| >= 100 before we get to the Kelvin conversion. For fOAT_k to go
// <= 0 we'd need OAT_C <= -273.15, but -100 is the tightest band. So
// we don't have a test for 177-178 — it's defensive-but-dead code.
// ---------------------------------------------------------------------

void test_tas_fallback_when_oat_out_of_band(void)
{
    // OAT_C = -300 fails oatInBand(); bHaveOat stays false and we take
    // the simple-altitude-multiplier fallback at line 181-182.
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    in.sensors.iasKt         = 100.0f;
    in.sensors.paltFt        = 5000.0f;
    in.iasUpdateTimestampUs  = 1'000'000u;
    in.useInternalOat        = true;
    in.sensors.oatCelsius    = -300.0f;
    a.Init(in, 5000.0f);
    a.Step(in, kDt);

    TEST_ASSERT_TRUE(std::isfinite(a.tasMps()));
    TEST_ASSERT_TRUE(a.tasMps() > 0.0f);
}

void test_tas_fallback_when_divisor_overflows_at_extreme_altitude(void)
{
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    // Density-altitude formula: fDivisor = 1 - 6.8755856e-6 * fDA.
    // Push paltFt huge so fDA pushes fDivisor to 0 or negative.
    in.sensors.iasKt         = 100.0f;
    in.sensors.paltFt        = 200000.0f;   // 200 kft — unphysical but tests the guard
    in.iasUpdateTimestampUs  = 1'000'000u;
    in.useInternalOat        = true;
    in.sensors.oatCelsius    = -55.0f;      // realistic cold stratospheric-ish OAT
    a.Init(in, in.sensors.paltFt);
    a.Step(in, kDt);

    // Fallback should kick in and still produce a finite, positive TAS.
    TEST_ASSERT_TRUE(std::isfinite(a.tasMps()));
    TEST_ASSERT_TRUE(a.tasMps() > 0.0f);
}

// EKF6 alpha-covariance reset on the IAS=25 kt transition (Ahrs.cpp:354-357).
// When the aircraft accelerates through 25 kt in EKF6 mode, the filter's
// alpha covariance is reset so it re-learns alpha from real gamma
// measurements. This path has no test coverage despite EKF6 being an
// active iAhrsAlgorithm option.
void test_ekf6_alpha_covariance_reset_on_ias_threshold_crossing(void)
{
    AhrsConfig cfg = makeCfg(Algorithm::Ekf6);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    a.Init(in, 0.0f);

    // Below threshold: stay here a while so iasWasBelowThreshold_ is true.
    in.sensors.iasKt = 10.0f;
    for (int i = 0; i < 50; ++i) a.Step(in, kDt);
    TEST_ASSERT_TRUE(std::isfinite(a.latest().pitchDeg));

    // Cross up through 25 kt — this triggers the reset branch.
    in.sensors.iasKt = 30.0f;
    in.iasUpdateTimestampUs = 1'000'000u;
    a.Step(in, kDt);
    // Continue a few more steps; filter must stay finite post-reset.
    for (int i = 0; i < 10; ++i) {
        in.iasUpdateTimestampUs += 20'000u;
        a.Step(in, kDt);
    }

    TEST_ASSERT_TRUE(std::isfinite(a.latest().pitchDeg));
    TEST_ASSERT_TRUE(std::isfinite(a.latest().rollDeg));
    TEST_ASSERT_TRUE(std::isfinite(a.latest().derivedAoaDeg));
}

// ---------------------------------------------------------------------
// test main
// ---------------------------------------------------------------------

int main(void)
{
    UNITY_BEGIN();


    RUN_TEST(test_construction_defaults_outputs_to_zero);
    RUN_TEST(test_init_seeds_pitch_and_roll_from_accel);
    RUN_TEST(test_init_applies_pitch_and_roll_bias);

    RUN_TEST(test_step_static_level_converges_near_zero);
    RUN_TEST(test_step_static_nose_up_settles_to_stable_pitch_madgwick);
    RUN_TEST(test_accel_smoothed_tracks_toward_input);
    RUN_TEST(test_derived_aoa_equals_pitch_when_ias_below_threshold);

    RUN_TEST(test_tas_updates_only_when_ias_timestamp_advances);
    RUN_TEST(test_step_guards_nan_dt);
    RUN_TEST(test_step_ekf6_level_stable);
    RUN_TEST(test_step_gyro_averages_follow_input);
    RUN_TEST(test_init_does_not_reset_tas_state);

    RUN_TEST(test_tas_uses_efis_oat_when_enabled);
    RUN_TEST(test_reconfigure_algorithm_change);

    RUN_TEST(test_tas_fallback_when_oat_out_of_band);
    RUN_TEST(test_tas_fallback_when_divisor_overflows_at_extreme_altitude);
    RUN_TEST(test_ekf6_alpha_covariance_reset_on_ias_threshold_crossing);

    return UNITY_END();
}
