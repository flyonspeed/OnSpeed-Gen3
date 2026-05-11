// test_log_replay_task.cpp — unit tests for onspeed::replay::LogReplayTask.
//
// Covers the seam-collapsing single-source pipeline: feed a synthetic
// LogRow into the task, capture the wire bytes, parse them back via
// ParseDisplayFrame, and assert the round-trip matches the input.
//
// What the task is responsible for vs. what the engine is responsible
// for:
//   - Engine: alpha smoothing, accel EMA, GOnsetFilter, AOA polynomial
//             (separately tested in test_aoa_calculator,
//             test_log_replay_engine, etc.).
//   - Task:   iasAlive hysteresis, ReplayStepResult → DisplayBuildInputs
//             field mapping, BuildDisplayFrame call. THIS suite focuses
//             on the task-layer responsibilities.

#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include <config/OnSpeedConfig.h>
#include <proto/DisplaySerial.h>
#include <replay/LogReplayTask.h>
#include <sensors/IasAlive.h>
#include <types/LogRow.h>

using onspeed::LogRow;
using onspeed::config::OnSpeedConfig;
using onspeed::proto::DisplayFrame;
using onspeed::proto::ParseDisplayFrame;
using onspeed::proto::kDisplayFrameSizeBytes;
using onspeed::replay::LogReplayTask;

void setUp(void) {}
void tearDown(void) {}

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

// Build a minimally-calibrated config with one flap entry. iAoaSmoothing
// = 0 (no EMA contamination of the first sample). Identity polynomial
// so aoa = coeffP = p45/pfwd.
static OnSpeedConfig MakeIdentityCfg()
{
    OnSpeedConfig cfg;
    cfg.aFlaps.clear();
    OnSpeedConfig::SuFlaps f;
    f.iDegrees       = 0;
    f.iPotPosition   = 2048;
    f.fLDMAXAOA      = 4.1f;
    f.fONSPEEDFASTAOA = 4.5f;
    f.fONSPEEDSLOWAOA = 5.0f;
    f.fSTALLWARNAOA  = 7.5f;
    f.fAlpha0        = -3.0f;
    f.fAlphaStall    = 10.0f;
    // Identity polynomial: aoa = 1.0 * coeffP + 0.
    // afCoeff layout is [a3, a2, a1, a0].
    f.AoaCurve.iCurveType = 1;
    f.AoaCurve.afCoeff[0] = 0.0f;
    f.AoaCurve.afCoeff[1] = 0.0f;
    f.AoaCurve.afCoeff[2] = 1.0f;
    f.AoaCurve.afCoeff[3] = 0.0f;
    cfg.aFlaps.push_back(f);
    cfg.iAoaSmoothing = 0;        // disable AOA EMA so first sample is clean
    return cfg;
}

// Build a minimal LogRow with sane defaults; caller overrides individual
// fields per-test.
static LogRow MakeRow()
{
    LogRow row;
    row.pfwdSmoothed     = 0.0f;
    row.p45Smoothed      = 0.0f;
    row.pStaticMbar      = 1013.25f;
    row.paltFt           = 0.0f;
    row.iasKt            = 0.0f;
    row.iasValid         = false;     // task ignores this; recomputed via UpdateIasAlive
    row.flapsPos         = 0;
    row.flapsRawAdc      = 2048;
    row.flapsRawAdcPresent = true;
    row.imuVerticalG     = 1.0f;
    row.imuLateralG      = 0.0f;
    row.imuForwardG      = 0.0f;
    row.imuRollRateDps   = 0.0f;
    row.imuPitchRateDps  = 0.0f;
    row.imuYawRateDps    = 0.0f;
    row.pitchDeg         = 0.0f;
    row.rollDeg          = 0.0f;
    row.flightPathDeg    = 0.0f;
    row.vsiFpm           = 0.0f;
    row.dataMark         = 0;
    row.oatCelsius       = 0.0f;
    return row;
}

// ----------------------------------------------------------------------------
// Construction / lifecycle
// ----------------------------------------------------------------------------

void test_construct_and_process_single_row()
{
    LogReplayTask task(MakeIdentityCfg(), 50, /*flapsRawAdcAvailable=*/true);
    LogRow row = MakeRow();
    row.iasKt = 80.0f;
    row.pfwdSmoothed = 1.0f;
    row.p45Smoothed  = 2.0f;          // coeffP = 2.0, identity → aoa = 2.0
    row.pitchDeg = 5.0f;

    const std::vector<uint8_t> bytes = task.processRow(row);
    TEST_ASSERT_EQUAL(kDisplayFrameSizeBytes, bytes.size());
}

// ----------------------------------------------------------------------------
// iasAlive hysteresis: the bug-1 class fix
// ----------------------------------------------------------------------------

void test_ias_alive_below_rising_threshold()
{
    // 18 kt: below 20 kt rising threshold, iasAlive should stay false.
    // Wire encodes iasValid=false → 9999 sentinel for iasKt.
    LogReplayTask task(MakeIdentityCfg(), 50, true);
    LogRow row = MakeRow();
    row.iasKt = 18.0f;
    row.pfwdSmoothed = 1.0f;
    row.p45Smoothed  = 1.0f;

    const std::vector<uint8_t> bytes = task.processRow(row);
    TEST_ASSERT_EQUAL(kDisplayFrameSizeBytes, bytes.size());

    DisplayFrame f;
    auto opt = ParseDisplayFrame(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(opt.has_value());
    f = opt.value();
    TEST_ASSERT_FALSE(f.iasIsValid);
}

void test_ias_alive_crosses_rising_threshold()
{
    LogReplayTask task(MakeIdentityCfg(), 50, true);
    LogRow row = MakeRow();
    row.pfwdSmoothed = 1.0f;
    row.p45Smoothed  = 1.0f;

    // First row: 25 kt → iasAlive flips to true (passed 20 kt rising).
    row.iasKt = 25.0f;
    auto bytes = task.processRow(row);
    auto opt = ParseDisplayFrame(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(opt.has_value());
    TEST_ASSERT_TRUE(opt.value().iasIsValid);

    // Second row: 18 kt → still above 15 kt falling threshold; held.
    row.iasKt = 18.0f;
    bytes = task.processRow(row);
    opt = ParseDisplayFrame(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(opt.has_value());
    TEST_ASSERT_TRUE(opt.value().iasIsValid);

    // Third row: 14 kt → below 15 kt falling threshold; flips to false.
    row.iasKt = 14.0f;
    bytes = task.processRow(row);
    opt = ParseDisplayFrame(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(opt.has_value());
    TEST_ASSERT_FALSE(opt.value().iasIsValid);
}

void test_ias_alive_ignores_input_iasValid()
{
    // The task must IGNORE row.iasValid (parser-set) and re-derive
    // hysteretically. If the input row claimed iasValid=true at 5 kt,
    // the task should still emit iasValid=false because UpdateIasAlive
    // says no.
    LogReplayTask task(MakeIdentityCfg(), 50, true);
    LogRow row = MakeRow();
    row.iasKt    = 5.0f;
    row.iasValid = true;              // adversarial — we ignore this
    row.pfwdSmoothed = 1.0f;
    row.p45Smoothed  = 1.0f;

    auto bytes = task.processRow(row);
    auto opt = ParseDisplayFrame(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(opt.has_value());
    TEST_ASSERT_FALSE(opt.value().iasIsValid);
}

// ----------------------------------------------------------------------------
// Field mapping: each ReplayStepResult field lands in the right wire slot
// ----------------------------------------------------------------------------

void test_field_mapping_attitude_and_paltFt()
{
    LogReplayTask task(MakeIdentityCfg(), 50, true);
    LogRow row = MakeRow();
    row.iasKt = 80.0f;                // > 20 kt → iasAlive=true
    row.pfwdSmoothed = 1.0f;
    row.p45Smoothed  = 1.0f;
    row.pitchDeg     = 7.5f;
    row.rollDeg      = -12.3f;
    row.paltFt       = 5500.0f;
    row.flightPathDeg = 2.1f;

    auto bytes = task.processRow(row);
    auto opt = ParseDisplayFrame(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(opt.has_value());
    DisplayFrame f = opt.value();

    TEST_ASSERT_FLOAT_WITHIN(0.2f, 7.5f, f.pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, -12.3f, f.rollDeg);
    TEST_ASSERT_FLOAT_WITHIN(50.0f, 5500.0f, f.paltFt);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 2.1f, f.flightPathDeg);
}

void test_field_mapping_oat_and_dataMark()
{
    LogReplayTask task(MakeIdentityCfg(), 50, true);
    LogRow row = MakeRow();
    row.iasKt        = 80.0f;
    row.pfwdSmoothed = 1.0f;
    row.p45Smoothed  = 1.0f;
    row.oatCelsius   = 22.4f;         // round to 22
    row.dataMark     = 7;

    auto bytes = task.processRow(row);
    auto opt = ParseDisplayFrame(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(opt.has_value());
    TEST_ASSERT_EQUAL_INT(22, opt.value().oatC);
    TEST_ASSERT_EQUAL_INT(7,  opt.value().dataMark);
}

void test_field_mapping_dataMark_wraps_at_100()
{
    LogReplayTask task(MakeIdentityCfg(), 50, true);
    LogRow row = MakeRow();
    row.iasKt        = 80.0f;
    row.pfwdSmoothed = 1.0f;
    row.p45Smoothed  = 1.0f;
    row.dataMark     = 234;           // mod 100 = 34

    auto bytes = task.processRow(row);
    auto opt = ParseDisplayFrame(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(opt.has_value());
    TEST_ASSERT_EQUAL_INT(34, opt.value().dataMark);
}

// ----------------------------------------------------------------------------
// Reset semantics
// ----------------------------------------------------------------------------

void test_reset_clears_iasAlive_state()
{
    LogReplayTask task(MakeIdentityCfg(), 50, true);
    LogRow row = MakeRow();
    row.pfwdSmoothed = 1.0f;
    row.p45Smoothed  = 1.0f;

    // Bring iasAlive up.
    row.iasKt = 25.0f;
    task.processRow(row);

    task.reset();

    // After reset, iasAlive=false even at 18 kt (which is below the
    // rising threshold but above the falling threshold).
    row.iasKt = 18.0f;
    auto bytes = task.processRow(row);
    auto opt = ParseDisplayFrame(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(opt.has_value());
    TEST_ASSERT_FALSE(opt.value().iasIsValid);
}

// ----------------------------------------------------------------------------
// Empty cfg edge case
// ----------------------------------------------------------------------------

void test_empty_cfg_produces_zero_anchors_no_crash()
{
    OnSpeedConfig cfg;
    cfg.aFlaps.clear();
    LogReplayTask task(cfg, 50, true);
    LogRow row = MakeRow();
    row.iasKt = 80.0f;
    row.pfwdSmoothed = 1.0f;
    row.p45Smoothed  = 1.0f;

    auto bytes = task.processRow(row);
    TEST_ASSERT_EQUAL(kDisplayFrameSizeBytes, bytes.size());

    auto opt = ParseDisplayFrame(bytes.data(), bytes.size());
    TEST_ASSERT_TRUE(opt.has_value());
    DisplayFrame f = opt.value();
    // Anchors are all 0 when cfg is uncalibrated.
    TEST_ASSERT_EQUAL_INT(0, f.tonesOnPctLift);
    TEST_ASSERT_EQUAL_INT(0, f.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL_INT(0, f.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL_INT(0, f.stallWarnPctLift);
    TEST_ASSERT_EQUAL_INT(0, f.pipPctLift);
    TEST_ASSERT_EQUAL_INT(0, f.flapsMinDeg);
    TEST_ASSERT_EQUAL_INT(0, f.flapsMaxDeg);
}

// ----------------------------------------------------------------------------
// Synth-path lag returns empty on early rows
// ----------------------------------------------------------------------------

void test_synth_path_returns_empty_during_lag()
{
    // flapsRawAdcAvailable=false triggers synth-buffer mode. The
    // engine's step() returns nullopt for the first
    // kSynthHalfWindowTicks rows; the task should propagate this as
    // an empty wire-bytes vector.
    LogReplayTask task(MakeIdentityCfg(), 50, /*flapsRawAdcAvailable=*/false);
    LogRow row = MakeRow();
    row.iasKt = 80.0f;
    row.pfwdSmoothed = 1.0f;
    row.p45Smoothed  = 1.0f;
    row.flapsRawAdcPresent = false;

    auto bytes = task.processRow(row);
    TEST_ASSERT_EQUAL(0u, bytes.size());
}

// ----------------------------------------------------------------------------
// Runner
// ----------------------------------------------------------------------------

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_construct_and_process_single_row);
    RUN_TEST(test_ias_alive_below_rising_threshold);
    RUN_TEST(test_ias_alive_crosses_rising_threshold);
    RUN_TEST(test_ias_alive_ignores_input_iasValid);
    RUN_TEST(test_field_mapping_attitude_and_paltFt);
    RUN_TEST(test_field_mapping_oat_and_dataMark);
    RUN_TEST(test_field_mapping_dataMark_wraps_at_100);
    RUN_TEST(test_reset_clears_iasAlive_state);
    RUN_TEST(test_empty_cfg_produces_zero_anchors_no_crash);
    RUN_TEST(test_synth_path_returns_empty_during_lag);
    return UNITY_END();
}
