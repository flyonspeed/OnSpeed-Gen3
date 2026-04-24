// test_ahrs.cpp — native tests for onspeed::ahrs::Ahrs.
//
// Focused on observable behavior of the Step() method given controlled
// inputs.  The snapshot regression harness (tools/regression/) covers
// byte-identity against recorded flight logs; these unit tests cover the
// orthogonal axis — "does the filter recover from startup", "does gyro
// bias converge", "does DerivedAOA follow pitch when VSI=0", etc.

#include <unity.h>

#include <cmath>
#include <cstdio>

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
// zero gyro, 5 kt IAS below the pitot noise floor (iasAlive=false),
// sea-level Palt, no OAT.  Useful both for Init() and as the first Step
// input.  Tests that need in-flight conditions override iasKt AND
// iasAlive to avoid the compensation gate masking their intent.
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
    in.sensors.iasAlive = false;                     // rest-on-ground
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
    in.sensors.iasKt    = 100.0f;
    in.sensors.iasAlive = true;
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
    in.sensors.iasKt    = 100.0f;
    in.sensors.iasAlive = true;
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
    in.sensors.iasKt    = 100.0f;
    in.sensors.iasAlive = true;
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
    in.sensors.iasAlive      = true;
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
    in.sensors.iasAlive      = true;
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
    in.sensors.iasKt    = 10.0f;
    in.sensors.iasAlive = false;
    for (int i = 0; i < 50; ++i) a.Step(in, kDt);
    TEST_ASSERT_TRUE(std::isfinite(a.latest().pitchDeg));

    // Cross the iasAlive gate — this triggers the alpha reset branch.
    in.sensors.iasKt    = 30.0f;
    in.sensors.iasAlive = true;
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
// IAS-alive gate on accel compensation.
//
// When iasAlive=false, AccelFwdCompFactor/AccelLatCompFactor/
// AccelVertCompFactor must be suppressed.  Phantom IAS (from pitot
// sensor noise at rest) would otherwise propagate through tas_ and
// tasDotSmoothed_ into the comp factors and corrupt the smoothed
// accels fed to Madgwick/EKF6, producing a slow pitch oscillation
// in the hangar.  Pairs with the deadband in SensorIO (the first
// line of defense); this is the second.
// ---------------------------------------------------------------------

void test_ias_alive_false_zeros_comp_factors(void)
{
    // Stationary seed: iasAlive=false, measurable yaw rate and a
    // non-zero TAS (as if the filter had flown earlier and retained
    // state).  With the gate enforced, accelLatCompG should equal the
    // raw installation-corrected accel — no centripetal subtraction.
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs seed = levelSeed();
    seed.sensors.iasKt    = 100.0f;      // prime TAS state
    seed.sensors.iasAlive = true;
    seed.iasUpdateTimestampUs = 1'000'000u;
    a.Init(seed, 0.0f);
    a.Step(seed, kDt);
    TEST_ASSERT_TRUE(a.tasMps() > 0.0f);

    // Now go to rest-on-ground with a yaw rate that WOULD produce
    // centripetal compensation if the gate were not there.
    AhrsInputs rest = levelSeed();       // iasAlive=false
    rest.imu.gyroYawDps = 10.0f;         // 10 deg/s yaw, taxi-turn scale
    rest.iasUpdateTimestampUs = 1'020'000u;

    a.Step(rest, kDt);

    // Lateral comp factor = mps2g(deg2rad(tas_ * yawRate)).  With
    // tas_ ≈ 51 m/s carried over and 10 deg/s, the raw factor would
    // be ~0.9 g — clearly present in accelLatCompG if applied.
    // Gated path: accelLatCompG == accelLatSmoothedG (no subtraction).
    const float latComp     = a.accelLatCompG();
    const float latSmoothed = a.accelLatSmoothedG();
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, latSmoothed, latComp);
}

void test_ias_alive_true_applies_centripetal(void)
{
    // Mirror: iasAlive=true with a yaw rate should move accelLatCompG
    // away from the smoothed accel by the centripetal amount.  Runs long
    // enough (1.5 s, ~3τ) for the issue-#114 fade-in to saturate near 1
    // so the expected centripetal magnitude is observable.
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    in.sensors.iasKt    = 100.0f;
    in.sensors.iasAlive = true;
    uint32_t iasTs = 1'000'000u;
    in.iasUpdateTimestampUs = iasTs;
    a.Init(in, 0.0f);
    a.Step(in, kDt);

    in.imu.gyroYawDps = 10.0f;
    for (int i = 0; i < 500; i++) {    // ~2.4 s so compFadeIn saturates
        if (i % 4 == 0) { iasTs += 20'000u; in.iasUpdateTimestampUs = iasTs; }
        a.Step(in, kDt);
    }
    TEST_ASSERT_TRUE(a.compFadeIn() > 0.95f);

    // Comp should differ from smoothed by a measurable centripetal amount.
    const float delta = std::fabs(a.accelLatCompG() - a.accelLatSmoothedG());
    TEST_ASSERT_TRUE_MESSAGE(delta > 0.05f,
        "iasAlive=true with 10 deg/s yaw at 100 kt must produce non-trivial lateral comp");
}

void test_rising_edge_transient_bounded_on_takeoff(void)
{
    // Regression guard for the takeoff-through-20-kt transient.  The
    // issue-#114 fade-in reduces the AccelFwdComp spike by ~100× on the
    // first frame (see test_comp_fade_in_suppresses_rising_edge_accel_spike
    // for the load-bearing assertion).  But in THIS sterile unit-test
    // scenario, Madgwick's integrated pitch excursion over ~1 s is nearly
    // the same pre- and post-fade (~1.3°).  Two reasons it isn't reduced
    // proportionally here:
    //   (a) Madgwick's `UpdateIMU` normalizes the accel vector to unit
    //       length before the gradient step, so the Kalman-like "pull
    //       strength" is governed by `beta` (≈0.012), not by accel
    //       magnitude.
    //   (b) `tasDotSmoothed_` EMA-decays with τ ≈ 265 ms, so by ~1 s the
    //       pre- and post-fade comp factors have converged.
    // The fade's in-flight value is primarily on the accel signal itself
    // (what logs, displays, and especially EKF6 — which uses measurement
    // magnitudes — consume).  This test stays as a loose 3° regression
    // guard that would catch a change that dramatically worsens behaviour
    // (e.g. removing the gate, shortening IAS smoothing, or breaking the
    // fade so comp is applied at full strength on frame 0).
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    a.Init(in, 0.0f);

    // Phase 1: ~2.4 s of rest with iasAlive=false, IAS=0 (deadbanded).
    uint32_t iasTs = 0;
    for (int i = 0; i < 500; i++) {
        if (i % 4 == 0) { iasTs += 20'000u; in.iasUpdateTimestampUs = iasTs; }
        a.Step(in, kDt);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, a.latest().pitchDeg);

    // Phase 2: rising-edge step — gate flips, IAS = 20.3 kt.
    in.sensors.iasAlive = true;
    in.sensors.iasKt    = 20.3f;

    float maxPitchDeviation = 0.0f;
    for (int i = 0; i < 210; i++) {  // ~1 second at 208 Hz
        if (i % 4 == 0) { iasTs += 20'000u; in.iasUpdateTimestampUs = iasTs; }
        a.Step(in, kDt);
        const float absPitch = std::fabs(a.latest().pitchDeg);
        if (absPitch > maxPitchDeviation) maxPitchDeviation = absPitch;
    }

    TEST_ASSERT_TRUE_MESSAGE(maxPitchDeviation < 3.0f,
        "Rising-edge pitch transient exceeded 3.0° — did a change worsen #114?");
}

// ---------------------------------------------------------------------
// Issue #114: fade-in of compensation factors at iasAlive rising edge.
//
// The fade-in coefficient EMA-ramps from 0 to 1 with τ ≈ 0.5 s after
// iasAlive goes true, scaling the forward/lateral/vertical accel
// compensation factors so the one-frame TAS step at takeoff doesn't
// dump ~3.75 g of spurious forward comp into the smoothed accel on the
// first post-gate frame.
// ---------------------------------------------------------------------

void test_comp_fade_in_starts_at_zero_and_ramps_to_one(void)
{
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    a.Init(in, 0.0f);

    // At construction + after a few rest frames, fade is zero.
    a.Step(in, kDt);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, a.compFadeIn());

    // Rising edge.
    in.sensors.iasAlive = true;
    in.sensors.iasKt    = 25.0f;
    uint32_t iasTs = 1'000'000u;
    in.iasUpdateTimestampUs = iasTs;

    // First frame after rising edge: fade is a single EMA step from 0,
    // so ≈ dt / (τ + dt) ≈ 0.0096 at 208 Hz.  Assert it's << 0.05.
    a.Step(in, kDt);
    TEST_ASSERT_TRUE(a.compFadeIn() > 0.0f);
    TEST_ASSERT_TRUE(a.compFadeIn() < 0.05f);

    // After ~1.5 s (three time constants), fade is > 0.95.
    for (int i = 0; i < 313; i++) {
        if (i % 4 == 0) { iasTs += 20'000u; in.iasUpdateTimestampUs = iasTs; }
        a.Step(in, kDt);
    }
    TEST_ASSERT_TRUE_MESSAGE(a.compFadeIn() > 0.95f,
        "Fade-in should reach > 0.95 after ~1.5 s (3τ)");
    TEST_ASSERT_TRUE(a.compFadeIn() < 1.0f + 1e-6f);
}

void test_comp_fade_in_survives_init(void)
{
    // Init() is called from the web UI on every config save.  The fade
    // state must persist across Init() — otherwise a mid-flight config
    // save would instantly drop compFadeIn_ to 0 and re-disable the
    // AHRS compensation for ~0.5 s, injecting exactly the kind of
    // transient issue #114 is designed to eliminate.
    //
    // (Same policy as tas_/prevTas_/tasDotSmoothed_; see the comment
    // in Ahrs::Init.)
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    a.Init(in, 0.0f);

    // Ramp fade to ~1 with iasAlive=true.
    in.sensors.iasAlive = true;
    in.sensors.iasKt    = 25.0f;
    uint32_t iasTs = 1'000'000u;
    in.iasUpdateTimestampUs = iasTs;
    for (int i = 0; i < 500; i++) {
        if (i % 4 == 0) { iasTs += 20'000u; in.iasUpdateTimestampUs = iasTs; }
        a.Step(in, kDt);
    }
    const float fadeBefore = a.compFadeIn();
    TEST_ASSERT_TRUE(fadeBefore > 0.95f);

    // Re-Init (simulating a mid-flight web UI config save).
    a.Init(in, 0.0f);

    // Fade must be preserved.  Using a tight bound because Init() should
    // be a pure no-op on this state.
    TEST_ASSERT_EQUAL_FLOAT(fadeBefore, a.compFadeIn());
}

void test_comp_fade_in_resets_when_ias_alive_drops(void)
{
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    a.Init(in, 0.0f);

    // Ramp fade toward 1 with iasAlive=true.
    in.sensors.iasAlive = true;
    in.sensors.iasKt    = 25.0f;
    uint32_t iasTs = 1'000'000u;
    in.iasUpdateTimestampUs = iasTs;
    for (int i = 0; i < 500; i++) {    // ~2.4 s so compFadeIn saturates
        if (i % 4 == 0) { iasTs += 20'000u; in.iasUpdateTimestampUs = iasTs; }
        a.Step(in, kDt);
    }
    TEST_ASSERT_TRUE(a.compFadeIn() > 0.95f);

    // Drop iasAlive.  One step later, fade must be exactly zero.
    in.sensors.iasAlive = false;
    a.Step(in, kDt);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.compFadeIn());
}

void test_comp_fade_in_suppresses_rising_edge_accel_spike(void)
{
    // Mirror of test_rising_edge_transient_bounded_on_takeoff's scenario,
    // asserting directly on the AccelFwdComp spike that the fade is
    // designed to suppress.  Before issue #114, |accelFwdCompG| spiked
    // to several g on the first frame after iasAlive flipped: tas_
    // steps from its pre-gate value to kts2mps(~20 kt) in one pressure
    // frame, the IAS-smoothing EMA lifts tasDotSmoothed_ to tens of
    // m/s², and the resulting AccelFwdCompFactor lands on the smoothed
    // accel in one frame.  With the fade multiplier EMA-ramping from 0
    // with τ ≈ 0.5 s, the applied comp on frame 0 is cut by ~100×
    // (fadeAlpha ≈ 0.0095) and climbs smoothly from there.
    //
    // Why assert on AccelFwdComp rather than on pitch: the accel spike
    // is what feeds every downstream consumer — logs, external displays,
    // and especially EKF6 (which uses measurement magnitudes directly).
    // Madgwick's UpdateIMU normalizes accel to unit length before its
    // gradient step, so in a sterile zero-gyro-noise unit test the
    // integrated pitch over ~1 s is dominated by beta, not by the spike
    // magnitude — making pitch a poor ruler for this fade.  The accel
    // spike is the quantity the fade actually controls.
    AhrsConfig cfg = makeCfg(Algorithm::Madgwick);
    Ahrs a{cfg};
    AhrsInputs in = levelSeed();
    a.Init(in, 0.0f);

    uint32_t iasTs = 0;
    for (int i = 0; i < 500; i++) {
        if (i % 4 == 0) { iasTs += 20'000u; in.iasUpdateTimestampUs = iasTs; }
        a.Step(in, kDt);
    }

    in.sensors.iasAlive = true;
    in.sensors.iasKt    = 20.3f;

    float maxFwdCompSpike = 0.0f;
    for (int i = 0; i < 10; i++) {  // first ~50 ms
        if (i % 4 == 0) { iasTs += 20'000u; in.iasUpdateTimestampUs = iasTs; }
        a.Step(in, kDt);
        const float absComp = std::fabs(a.accelFwdCompG());
        if (absComp > maxFwdCompSpike) maxFwdCompSpike = absComp;
    }

    // Bound is 0.5 g, comfortably above the post-fade peak (< 0.25 g
    // for the first 10 frames with these starting conditions) and ~10×
    // below the pre-fade peak (~2.8 g with levelSeed's 5 kt rest IAS;
    // ~3.75 g with rest IAS = 0).  A regression that removed the fade
    // multiplier or the iasAlive gate would fail this immediately.
    char msg[160];
    std::snprintf(msg, sizeof(msg),
        "AccelFwdComp spike in first 10 frames: %.4f g (bound 0.5 g)",
        maxFwdCompSpike);
    TEST_ASSERT_TRUE_MESSAGE(maxFwdCompSpike < 0.5f, msg);
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

    RUN_TEST(test_ias_alive_false_zeros_comp_factors);
    RUN_TEST(test_ias_alive_true_applies_centripetal);
    RUN_TEST(test_rising_edge_transient_bounded_on_takeoff);

    RUN_TEST(test_comp_fade_in_starts_at_zero_and_ramps_to_one);
    RUN_TEST(test_comp_fade_in_survives_init);
    RUN_TEST(test_comp_fade_in_resets_when_ias_alive_drops);
    RUN_TEST(test_comp_fade_in_suppresses_rising_edge_accel_spike);

    return UNITY_END();
}
