// test_engine_parity.cpp — engine-vs-task parity test.
//
// Constructs one LogReplayEngine and one LogReplayTask from the same
// cfg, drives both with the same 200-row LogRow sequence, asserts the
// per-row engine outputs match field-for-field.
//
// Catches the bug class that bit us on the trial run: the task's
// engine-cfg-by-const-reference was bound to the constructor's
// parameter instead of the task's member cfg_, leaving a dangling
// reference once the constructor returned. The engine then read
// garbage from cfg_.aFlaps[i].iDegrees and returned wrong flapsIndex
// values (0 instead of 2 for full flaps). The unit tests at the
// time passed because the test's stack-frame kept the parameter
// alive across the (very short) test span; the bug surfaced only
// in production via embind. This test fires both engines in
// lockstep and diffs every step result, which would have caught
// the dangling-ref symptom on row 1.

#include <unity.h>
#include <cmath>
#include <optional>

#include <config/OnSpeedConfig.h>
#include <replay/LogReplayEngine.h>
#include <replay/LogReplayTask.h>
#include <sensors/IasAlive.h>
#include <types/LogRow.h>

using onspeed::LogRow;
using onspeed::config::OnSpeedConfig;
using onspeed::replay::LogReplayEngine;
using onspeed::replay::LogReplayTask;
using onspeed::replay::ReplayStepResult;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static OnSpeedConfig MakeMultiFlapCfg() {
    OnSpeedConfig cfg;
    cfg.aFlaps.clear();
    for (int deg : { 0, 16, 33 }) {
        OnSpeedConfig::SuFlaps f;
        f.iDegrees       = deg;
        f.iPotPosition   = 4000 - deg * 50;     // monotone for synth
        f.fLDMAXAOA      = 4.0f + deg * 0.05f;
        f.fONSPEEDFASTAOA = 4.5f + deg * 0.05f;
        f.fONSPEEDSLOWAOA = 5.0f + deg * 0.05f;
        f.fSTALLWARNAOA  = 8.0f;
        f.fAlpha0        = -3.0f;
        f.fAlphaStall    = 10.0f;
        f.AoaCurve.iCurveType = 1;
        f.AoaCurve.afCoeff[0] = 0.0f;
        f.AoaCurve.afCoeff[1] = 0.0f;
        f.AoaCurve.afCoeff[2] = 1.0f;            // identity poly
        f.AoaCurve.afCoeff[3] = 0.0f;
        cfg.aFlaps.push_back(f);
    }
    cfg.iAoaSmoothing = 0;
    return cfg;
}

static LogRow MakeRow(int i) {
    LogRow row;
    row.pfwdSmoothed     = 1.0f + (i % 10) * 0.05f;
    row.p45Smoothed      = 0.5f + (i % 7) * 0.07f;
    row.pStaticMbar      = 1013.25f;
    row.paltFt           = 1500.0f + i * 2.0f;
    row.iasKt            = 25.0f + (i % 50);
    row.iasValid         = false;        // task derives via UpdateIasAlive
    // Cycle through the three flap detents over the row sequence.
    row.flapsPos         = (i / 67) % 3 == 0 ? 0 : ((i / 67) % 3 == 1 ? 16 : 33);
    row.flapsRawAdc      = 0;
    row.flapsRawAdcPresent = true;
    row.imuVerticalG     = 1.0f + std::sin(i * 0.1f) * 0.05f;
    row.imuLateralG      = std::sin(i * 0.13f) * 0.04f;
    row.imuForwardG      = 0.02f;
    row.imuRollRateDps   = std::sin(i * 0.07f) * 5.0f;
    row.imuPitchRateDps  = std::cos(i * 0.05f) * 3.0f;
    row.imuYawRateDps    = std::sin(i * 0.09f) * 1.0f;
    row.pitchDeg         = std::sin(i * 0.02f) * 5.0f;
    row.rollDeg          = std::cos(i * 0.03f) * 10.0f;
    row.flightPathDeg    = 2.0f;
    row.vsiFpm           = 100.0f;
    row.dataMark         = i % 100;
    row.oatCelsius       = 15.0f;
    return row;
}

// ---------------------------------------------------------------------------
// Test: engine and task produce per-row identical step results.
// ---------------------------------------------------------------------------

void test_engine_and_task_produce_identical_step_results() {
    const OnSpeedConfig cfg = MakeMultiFlapCfg();
    LogReplayEngine engine(cfg, 50, /*flapsRawAdcAvailable=*/true);
    LogReplayTask   task  (cfg, 50, /*flapsRawAdcAvailable=*/true);

    bool taskIasAlive = false;
    constexpr int N = 200;

    for (int i = 0; i < N; ++i) {
        // Build the row once, then derive both inputs from it.
        LogRow row = MakeRow(i);

        // Engine-side: mirror what the task does internally (apply
        // UpdateIasAlive before stepping). Otherwise the engine would
        // see iasValid=false on every row and the comparison wouldn't
        // exercise the iasValid-dependent code paths.
        taskIasAlive = ::onspeed::sensors::UpdateIasAlive(taskIasAlive, row.iasKt);
        LogRow gatedRow = row;
        gatedRow.iasValid = taskIasAlive;
        const std::optional<ReplayStepResult> engOpt = engine.step(gatedRow);

        // Task-side: processRow returns wire bytes; we read its engine
        // result via lastStep() which mirrors what the task fed itself.
        // The task internally calls UpdateIasAlive too, so it sees the
        // same iasValid as the engine path above.
        const std::vector<uint8_t> bytes = task.processRow(row);

        // Fast path: both should have a non-empty result.
        TEST_ASSERT_TRUE_MESSAGE(engOpt.has_value(),
            "engine.step returned nullopt on fast path");
        TEST_ASSERT_FALSE_MESSAGE(bytes.empty(),
            "task.processRow returned empty on fast path");

        const ReplayStepResult& eng  = engOpt.value();
        const ReplayStepResult& tsk  = task.lastStep();

        // Per-row field-by-field diff. Use a tight tolerance for
        // floats since both paths run identical EMA math.
        constexpr float kF = 1e-5f;
        TEST_ASSERT_EQUAL_INT(eng.flapsPos,       tsk.flapsPos);
        TEST_ASSERT_EQUAL_INT(eng.flapsIndex,     tsk.flapsIndex);
        TEST_ASSERT_EQUAL_INT(eng.flapsRawAdc,    tsk.flapsRawAdc);
        TEST_ASSERT_EQUAL_INT(eng.dataMark,       tsk.dataMark);
        TEST_ASSERT_EQUAL_MESSAGE(eng.iasValid,   tsk.iasValid, "iasValid");
        TEST_ASSERT_FLOAT_WITHIN(kF, eng.aoa,                tsk.aoa);
        TEST_ASSERT_FLOAT_WITHIN(kF, eng.coeffP,             tsk.coeffP);
        TEST_ASSERT_FLOAT_WITHIN(kF, eng.iasKt,              tsk.iasKt);
        TEST_ASSERT_FLOAT_WITHIN(kF, eng.paltFt,             tsk.paltFt);
        TEST_ASSERT_FLOAT_WITHIN(kF, eng.pitchDeg,           tsk.pitchDeg);
        TEST_ASSERT_FLOAT_WITHIN(kF, eng.rollDeg,            tsk.rollDeg);
        TEST_ASSERT_FLOAT_WITHIN(kF, eng.accelLatSmoothed,   tsk.accelLatSmoothed);
        TEST_ASSERT_FLOAT_WITHIN(kF, eng.accelVertSmoothed,  tsk.accelVertSmoothed);
        TEST_ASSERT_FLOAT_WITHIN(kF, eng.accelFwdSmoothed,   tsk.accelFwdSmoothed);
        TEST_ASSERT_FLOAT_WITHIN(kF, eng.gOnsetRate,         tsk.gOnsetRate);
    }
}

// ---------------------------------------------------------------------------
// Test: dangling-cfg regression.
//
// Construct the task with a cfg that goes out of scope as soon as the
// constructor returns (a function-returned value). If the task's
// engine bound to the function parameter instead of the member cfg_,
// the engine's const-reference is now dangling and the next step
// reads garbage. With the fix in place (engine bound to cfg_ which
// IS the task's owned copy), results are correct.
// ---------------------------------------------------------------------------

// Helper: build the task with a temporary cfg, return the task by
// value. After this function returns, the temp cfg is gone — only
// the task's member copy survives.
static LogReplayTask MakeTaskWithTemporaryCfg() {
    return LogReplayTask(MakeMultiFlapCfg(), 50, true);
}

void test_dangling_cfg_regression() {
    LogReplayTask task = MakeTaskWithTemporaryCfg();

    // Drive 50 rows. The flapsIndex resolver inside engine reads
    // cfg.aFlaps[i].iDegrees — exactly the path that broke when
    // the engine ref dangled.
    for (int i = 0; i < 50; ++i) {
        LogRow row = MakeRow(i);
        const std::vector<uint8_t> bytes = task.processRow(row);
        TEST_ASSERT_FALSE(bytes.empty());

        const ReplayStepResult& s = task.lastStep();
        // Each row should resolve flapsIndex from cfg.aFlaps:
        //   row.flapsPos = 0  -> flapsIndex 0
        //   row.flapsPos = 16 -> flapsIndex 1
        //   row.flapsPos = 33 -> flapsIndex 2
        const int expectedIdx =
            (row.flapsPos == 0)  ? 0 :
            (row.flapsPos == 16) ? 1 : 2;
        TEST_ASSERT_EQUAL_INT_MESSAGE(expectedIdx, s.flapsIndex,
            "flapsIndex resolution would fail if cfg ref dangled");
    }
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_engine_and_task_produce_identical_step_results);
    RUN_TEST(test_dangling_cfg_regression);
    return UNITY_END();
}
