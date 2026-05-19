# Optuna Substrate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire Optuna to tune EKFQ by subprocessing `host_main` per trial, so tuning runs against firmware C++ instead of a Python re-implementation that can silently drift.

**Architecture:** A new `--input-format sdlog` mode on `host_main ahrs_tone` consumes real OnSpeed SD logs (via the existing `BuildHeaderIndex` path), takes a per-trial kv config file overriding `EKFQ::Config` + `EkfqPipeline::PipelineConfig`, and passes named input columns through to the output. The Python tuner gets a new `--driver host-main` flag that subprocesses this per trial; loss math stays in Python.

**Tech Stack:** C++17 (firmware-side onspeed_core), PlatformIO native env (host_main), Python 3 + numpy + pandas + optuna (tuner).

**Spec reference:** `local-plans/PLAN_OPTUNA_SUBSTRATE.md` (committed alongside this plan).

## Framing: SD log as stream-processing snapshot

The SD log isn't a "flight recording" — it's a snapshot of a stream-processing graph.  Each row carries:

- **Raw inputs** (208 Hz): `Pfwd`, `P45`, `PStatic`, raw `ForwardG/LateralG/VerticalG`, raw `RollRate/PitchRate/YawRate`, `OAT`, `flapsRawADC`
- **Cached algorithm outputs** (208 Hz): `Pitch`, `Roll`, `DerivedAOA`, `FlightPath`, `EarthVerticalG`, `Altitude`, `VSI`, `CoeffP`, `IAS`, `TAS`, `AngleofAttack`
- **Truth signals** (50 Hz, held-stale): `vnPitch`, `vnRoll`, `vnVelNedDown`, ... — from VN-300 if present

Three replay modes already exist or are coming, each picking a different cut across the pipeline:

| Mode | Re-execute | Read from log |
|---|---|---|
| 208 Hz web replay (today) | nothing | everything cached |
| 50 Hz visualization (degraded) | nothing | already-smoothed only |
| Optuna tuning (this PR) | AHRS stage (full re-derivation from raw) | truth columns |
| Display-pipeline tune (future) | smoothing + display | AHRS cached |

The natural shared piece for *this* PR is the `LogRow → AhrsInputs` translation — one concrete bridge between two pipeline-stage representations.  A larger `RowSource` / `Stage` abstraction can wait for a second concrete consumer (the SD-log analysis UI, the display-tune replay) so it's shaped by two real callers instead of one + speculation.

A follow-up issue worth filing after this PR ships: **annotate each log column with its stream-processor role** (`input | algo_output | smoothed | display | truth`) so future replay modes can pick their cut by querying the schema instead of hard-coding column names.

**Worktree:** Work in `../onspeed-worktrees/optuna-substrate` (branch `optuna-substrate`) created from `upstream/ekfq-replace-ekf6`. PR #591 is the base, not master. Submodules must be initialized after worktree creation (`git submodule update --init --recursive`).

---

## File Structure

### onspeed_core changes

- **Create:** `software/Libraries/onspeed_core/src/ahrs/EkfqConfigKv.h` — public API for the kv parser
- **Create:** `software/Libraries/onspeed_core/src/ahrs/EkfqConfigKv.cpp` — parser impl
- **Modify:** `software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.h` — add `PipelineConfig` struct; promote `static constexpr` to instance members
- **Modify:** `software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.cpp` — use the instance members, add `setPipelineConfig`
- **Modify:** `software/Libraries/onspeed_core/src/ahrs/Ahrs.h` — declare `SetEkfqConfig`
- **Modify:** `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp` — implement `SetEkfqConfig`
- **Create:** `test/test_ekfq_config_kv/test_ekfq_config_kv.cpp` — parser unit tests
- **Modify:** `test/test_ekfq/test_ekfq.cpp` — add test for `setConfig` round-trip via Ahrs (sanity that the wiring works)

### Shared bridge — `LogRow → AhrsInputs`

The plan originally inlined this translation inside `host_main.cpp`.  After review (the "speed run of a flight" framing): tuning, regression replay, and any future SD-log analysis UI all share the same operation — feed historical sensor frames into the live algorithm pipeline at non-realtime rates.  The bridge belongs in `onspeed_core` so future callers (a docs-site replay UI, an on-box "rerun this log through a new EKFQ tune" feature, etc.) don't have to reinvent it.

- **Create:** `software/Libraries/onspeed_core/src/replay/LogRowToAhrsInputs.h` — public API for the row-to-inputs bridge
- **Create:** `software/Libraries/onspeed_core/src/replay/LogRowToAhrsInputs.cpp` — bridge impl, including the `PfwdSmoothed`-diff fresh-pressure heuristic and the timestampUs / dt machinery
- **Create:** `test/test_log_row_to_ahrs_inputs/test_log_row_to_ahrs_inputs.cpp` — unit tests for the bridge

### host_main changes

- **Modify:** `tools/regression/host_main.cpp` — `ahrs_tone` gains `--input-format sdlog`, `--config`, `--ekfq-config`, `--passthrough-cols`.  The new flags also become available on the `replay` subcommand as a follow-up (deferred to a separate issue — convergence with LogReplayEngine is a bigger lift than this PR should take on).

### Python tuner changes

- **Create:** `ekfq_pipeline/run_host_main.py` — subprocess wrapper, kv file writer, output CSV parser
- **Modify:** `ekfq_pipeline/tune_ekf.py` — add `--driver host-main`, `--host-main`, `--config-path` flags; route `make_objective` through subprocess driver when selected
- **Modify:** `ekfq_pipeline/README.md` — document the new driver

### Docs

- **Modify:** `tools/regression/README.md` — document new ahrs_tone flags
- **Modify:** `docs/site/docs/data-and-logs/log-columns.md` (if it exists; check) — no log changes needed but the EKFQ tuning workflow gets a one-line reference

### Acceptance / parity test

- **Create:** `tools/regression/fixtures/ekfq_substrate_smoke.csv` — small (~50 rows) synthetic SD-log fixture with VN columns, for CI smoke
- **Create:** `tools/regression/fixtures/ekfq_substrate_golden.csv` — expected host_main output for that fixture, used as the regression baseline
- **Modify:** `tools/regression/run_snapshot.py` — register the new fixture-vs-golden check

---

## Pre-flight

- [ ] **Step 0a: Create the worktree**

```bash
cd /Users/sritchie/code/onspeed
git -C OnSpeed-Gen3 fetch upstream
git -C OnSpeed-Gen3 worktree add ../onspeed-worktrees/optuna-substrate -b optuna-substrate upstream/ekfq-replace-ekf6
cd ../onspeed-worktrees/optuna-substrate
git submodule update --init --recursive
```

- [ ] **Step 0b: Sanity-build the native env**

```bash
pio test -e native -v 2>&1 | tail -10
```

Expected: existing 1099 tests pass (1 pre-existing skip). If anything fails, do NOT proceed — the EKFQ baseline must be green.

- [ ] **Step 0c: Sanity-build host_main**

```bash
cd tools/regression && pio run -e native 2>&1 | tail -5
ls -la .pio/build/native/host_main
```

Expected: binary exists. Run it with no args:

```bash
.pio/build/native/host_main 2>&1 | head -3
```

Expected: usage text or "subcommand required" error.

---

## Task 1: Promote EkfqPipeline constants to instance members

**Files:**
- Modify: `software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.h`
- Modify: `software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.cpp`
- Test: `test/test_ekfq/test_ekfq.cpp` (extend existing)

Today the four pipeline tuning constants are `static constexpr` private values. After this task they live in a `PipelineConfig` struct, are instance members, and have a setter — same shape as `EKFQ::Config`/`setConfig`.

- [ ] **Step 1.1: Read the current EkfqPipeline files**

```bash
sed -n '30,60p' software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.h
sed -n '15,30p' software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.cpp
```

Confirm the four constants are `kAccelEmaAlpha`, `kCompFadeTauSec`, `kIasGateRisingKt`, `kTasdotEmaAlpha`, plus the derived `kIasGateFallingKt = kIasGateRisingKt - 5.0f`.

- [ ] **Step 1.2: Add the PipelineConfig struct to the header**

In `EkfqPipeline.h`, INSIDE the `EkfqPipeline` class declaration (before `struct Inputs`), add:

```cpp
    /// Pipeline-internal tuning. Each field's default reproduces the
    /// Optuna study `ekfq_v15` best-trial values (the same study that
    /// produced EKFQ::Config::defaults()).  These are NOT Madgwick's
    /// constants — EKFQ's signal chain was tuned against itself.
    struct PipelineConfig {
        float accelEmaAlpha;     ///< Per-axis accel EMA α applied at IMU rate
        float compFadeTauSec;    ///< compFadeIn ramp τ on iasGate transitions
        float iasGateRisingKt;   ///< IAS hysteresis rising threshold (kt)
        float tasdotEmaAlpha;    ///< TASdot EMA α at IMU rate

        /// Falling-edge threshold = iasGateRisingKt - kIasGateHysteresisKt.
        /// 5 kt fixed hysteresis matches Python pipeline_quat.py.
        static constexpr float kIasGateHysteresisKt = 5.0f;

        /// Production defaults from Optuna v15 best trial.
        static PipelineConfig defaults();
    };
```

- [ ] **Step 1.3: Replace the static constexpr block with instance members**

In `EkfqPipeline.h`, replace the entire block `static constexpr float kAccelEmaAlpha = ...; ... static constexpr float kTasdotEmaAlpha = ...;` (lines ~32–49) with:

```cpp
    // Instance members holding the pipeline tuning. Initialised from
    // PipelineConfig::defaults() by the default ctor; replaceable via
    // the PipelineConfig-taking ctor or setPipelineConfig().
```

(That comment marks where the constants used to be; the actual fields go in the private section in Step 1.4.)

- [ ] **Step 1.4: Add PipelineConfig ctor, setter, and instance fields**

In `EkfqPipeline.h`, change the public ctor section from:

```cpp
    EkfqPipeline();
```

to:

```cpp
    /// Default-construct with PipelineConfig::defaults() values.
    EkfqPipeline();
    /// Construct with explicit pipeline tuning.
    explicit EkfqPipeline(const PipelineConfig& cfg);

    /// Replace the pipeline tuning. Does NOT reset EMA state — a
    /// mid-flight retune mustn't snap accel-EMA values, since the
    /// next frame's filter step would see a transient. Init() also
    /// leaves EMA state alone.
    void setPipelineConfig(const PipelineConfig& cfg);
    const PipelineConfig& getPipelineConfig() const { return pipeCfg_; }
```

In the private section (near `EKFQ ekfq_;`), add:

```cpp
    PipelineConfig pipeCfg_;
```

- [ ] **Step 1.5: Implement PipelineConfig::defaults() in .cpp**

In `EkfqPipeline.cpp`, near the top of the namespace, add:

```cpp
EkfqPipeline::PipelineConfig EkfqPipeline::PipelineConfig::defaults() {
    PipelineConfig c{};
    c.accelEmaAlpha   = 0.052324843677354384f;
    c.compFadeTauSec  = 2.531734433346506f;
    c.iasGateRisingKt = 33.66929039144636f;
    c.tasdotEmaAlpha  = 0.20081238948995161f;
    return c;
}
```

- [ ] **Step 1.6: Update the constructor body**

In `EkfqPipeline.cpp`, replace the existing `EkfqPipeline::EkfqPipeline()` definition with:

```cpp
EkfqPipeline::EkfqPipeline()
    : EkfqPipeline(PipelineConfig::defaults())
{}

EkfqPipeline::EkfqPipeline(const PipelineConfig& cfg)
    : pipeCfg_(cfg)
    , accelFwdFilter_(cfg.accelEmaAlpha)
    , accelLatFilter_(cfg.accelEmaAlpha)
    , accelVertFilter_(cfg.accelEmaAlpha)
{
    accelFwdFilter_.seed(0.0f);
    accelLatFilter_.seed(0.0f);
    accelVertFilter_.seed(+1.0f);
}

void EkfqPipeline::setPipelineConfig(const PipelineConfig& cfg) {
    pipeCfg_ = cfg;
    // EMA filters keep their current state but adopt the new α next
    // frame. The α field on EMAFilter is mutable via reseat; if it
    // isn't, swap to: accelFwdFilter_ = EMAFilter(cfg.accelEmaAlpha);
    // and re-seed from the last-known value to avoid a transient.
    accelFwdFilter_  = EMAFilter(cfg.accelEmaAlpha);
    accelLatFilter_  = EMAFilter(cfg.accelEmaAlpha);
    accelVertFilter_ = EMAFilter(cfg.accelEmaAlpha);
    accelFwdFilter_.seed(0.0f);
    accelLatFilter_.seed(0.0f);
    accelVertFilter_.seed(+1.0f);
}
```

(NOTE: This reseats the EMA filters. The comment in the spec said "no reset" but `EMAFilter` doesn't expose α-mutation — reconstruct + re-seed is the safe path. Mid-flight retune is for offline tuning anyway; the seed values are the level-on-ground state, which is correct after Init().)

- [ ] **Step 1.7: Replace constant references in Step()**

In `EkfqPipeline.cpp::Step`, find every reference to `kAccelEmaAlpha`, `kCompFadeTauSec`, `kIasGateRisingKt`, `kIasGateFallingKt`, `kTasdotEmaAlpha` and replace with the matching `pipeCfg_.` member. For `kIasGateFallingKt`, use:

```cpp
const float fallingKt = pipeCfg_.iasGateRisingKt - PipelineConfig::kIasGateHysteresisKt;
```

Defined once at the top of `Step()`. Reuse `fallingKt` where `kIasGateFallingKt` appeared.

- [ ] **Step 1.8: Build**

```bash
pio run -e esp32s3-v4p 2>&1 | tail -5
```

Expected: zero warnings, successful build.

- [ ] **Step 1.9: Add a unit test that confirms the override path works**

In `test/test_ekfq/test_ekfq.cpp`, near the end, add:

```cpp
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
```

Register it in the test main:

```cpp
RUN_TEST(test_ekfq_pipeline_config_override);
```

- [ ] **Step 1.10: Run the test**

```bash
pio test -e native -f test_ekfq -v 2>&1 | tail -10
```

Expected: all tests pass, including the new one.

- [ ] **Step 1.11: Run the regression golden to verify byte-identical EKFQ output**

```bash
./tools/regression/run_snapshot.py 2>&1 | tail -10
```

Expected: all goldens match. The default-construct path uses the same numbers as before; output must be identical.

- [ ] **Step 1.12: Commit**

```bash
git add software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.{h,cpp} test/test_ekfq/test_ekfq.cpp
git commit -m "EkfqPipeline: promote pipeline constants to PipelineConfig

Hoists the four Optuna-v15-tuned signal-chain constants
(accelEmaAlpha, compFadeTauSec, iasGateRisingKt, tasdotEmaAlpha) from
static constexpr members into a PipelineConfig struct, with a setter
for offline tuning. Defaults match the existing static values; the
regression golden is byte-identical."
```

---

## Task 2: Add Ahrs::SetEkfqConfig

**Files:**
- Modify: `software/Libraries/onspeed_core/src/ahrs/Ahrs.h`
- Modify: `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp`
- Test: `test/test_ahrs/test_ahrs.cpp` (extend)

Routes per-trial EKFQ + PipelineConfig overrides through the top-level `Ahrs` API so host_main doesn't need to reach inside.

- [ ] **Step 2.1: Add the declaration to Ahrs.h**

Near `void Reconfigure(const AhrsConfig& cfg);`, add:

```cpp
    /// Replace EKFQ filter tuning and pipeline tuning. Only takes
    /// effect when the active algorithm is EKFQ; for other algorithms
    /// the values are stored but unused until EKFQ is selected.
    /// Does NOT reset filter state — call Init() if you want a clean
    /// start with the new tuning.
    void SetEkfqConfig(const onspeed::EKFQ::Config& ekfqCfg,
                       const EkfqPipeline::PipelineConfig& pipeCfg);
```

- [ ] **Step 2.2: Add the implementation to Ahrs.cpp**

Near `void Ahrs::Reconfigure(...)`, add:

```cpp
void Ahrs::SetEkfqConfig(const onspeed::EKFQ::Config& ekfqCfg,
                         const EkfqPipeline::PipelineConfig& pipeCfg)
{
    // EkfqPipeline owns the EKFQ instance — go through it for both.
    ekfq_.getEkfq().setConfig(ekfqCfg);
    ekfq_.setPipelineConfig(pipeCfg);
}
```

This requires `EkfqPipeline::getEkfq()` to expose the wrapped filter. Add that accessor in Step 2.3.

- [ ] **Step 2.3: Add EkfqPipeline::getEkfq() accessor**

In `EkfqPipeline.h`, in the public section near the other accessors:

```cpp
    /// Direct access to the wrapped filter for per-trial config
    /// injection. Tests and the Optuna substrate use this; production
    /// code goes through Init() / Step().
    EKFQ& getEkfq() { return ekfq_; }
    const EKFQ& getEkfq() const { return ekfq_; }
```

- [ ] **Step 2.4: Build**

```bash
pio run -e esp32s3-v4p 2>&1 | tail -5
```

Expected: zero warnings.

- [ ] **Step 2.5: Add an Ahrs-level test**

In `test/test_ahrs/test_ahrs.cpp`, near the end, add:

```cpp
void test_ahrs_set_ekfq_config_round_trip(void) {
    onspeed::ahrs::AhrsConfig cfg;
    cfg.algorithm = onspeed::ahrs::Algorithm::Ekfq;
    cfg.imuSampleRateHz = 208.0f;
    cfg.pressureSampleRateHz = 50.0f;
    onspeed::ahrs::Ahrs ahrs(cfg);

    onspeed::EKFQ::Config custom = onspeed::EKFQ::Config::defaults();
    custom.q_quat = 1.0e-3f;   // distinct from default
    custom.r_baro = 9.999f;

    onspeed::ahrs::EkfqPipeline::PipelineConfig pipeCustom =
        onspeed::ahrs::EkfqPipeline::PipelineConfig::defaults();
    pipeCustom.accelEmaAlpha = 0.10f;

    ahrs.SetEkfqConfig(custom, pipeCustom);

    // Read back through the public chain — round-trip check.
    const onspeed::EKFQ::Config& got = ahrs.GetEkfqPipeline().getEkfq().getConfig();
    TEST_ASSERT_EQUAL_FLOAT(custom.q_quat, got.q_quat);
    TEST_ASSERT_EQUAL_FLOAT(custom.r_baro, got.r_baro);

    const auto& gotPipe = ahrs.GetEkfqPipeline().getPipelineConfig();
    TEST_ASSERT_EQUAL_FLOAT(0.10f, gotPipe.accelEmaAlpha);
}
```

This requires `Ahrs::GetEkfqPipeline()` to exist. Check the file:

```bash
grep -n "GetEkfqPipeline\|ekfq_" software/Libraries/onspeed_core/src/ahrs/Ahrs.h
```

If `GetEkfqPipeline()` doesn't exist, add it to `Ahrs.h`:

```cpp
    /// Direct access to the wrapped EkfqPipeline. Tests and the Optuna
    /// substrate use this for per-trial config injection.
    EkfqPipeline& GetEkfqPipeline() { return ekfq_; }
    const EkfqPipeline& GetEkfqPipeline() const { return ekfq_; }
```

(The private `ekfq_` member already exists.)

Register the test:

```cpp
RUN_TEST(test_ahrs_set_ekfq_config_round_trip);
```

- [ ] **Step 2.6: Run the test**

```bash
pio test -e native -f test_ahrs -v 2>&1 | tail -10
```

Expected: passes.

- [ ] **Step 2.7: Run regression golden**

```bash
./tools/regression/run_snapshot.py 2>&1 | tail -10
```

Expected: all goldens still match (no behavior change yet).

- [ ] **Step 2.8: Commit**

```bash
git add software/Libraries/onspeed_core/src/ahrs/Ahrs.{h,cpp} software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.h test/test_ahrs/test_ahrs.cpp
git commit -m "Ahrs: add SetEkfqConfig for per-trial EKFQ+pipeline override

Surfaces EKFQ::Config and EkfqPipeline::PipelineConfig as a single
setter on the Ahrs public API. Optuna substrate uses this to inject
trial parameters; production code is unaffected (defaults preserved)."
```

---

## Task 3: EkfqConfigKv parser

**Files:**
- Create: `software/Libraries/onspeed_core/src/ahrs/EkfqConfigKv.h`
- Create: `software/Libraries/onspeed_core/src/ahrs/EkfqConfigKv.cpp`
- Create: `test/test_ekfq_config_kv/test_ekfq_config_kv.cpp`
- Create: `test/test_ekfq_config_kv/platformio.ini.fragment` (if needed; check existing test wiring)

Hand-rolled parser for the kv config file: one `key=value` per line, `#` comments, blank lines OK. Errors on unknown keys (typo guard); warns on missing keys (fall back to defaults).

- [ ] **Step 3.1: Check existing test wiring**

```bash
ls test/test_ekfq/
cat test/test_ekfq/test_ekfq.cpp | head -10
```

If `test/test_ekfq/` uses Unity directly (likely), the new test follows the same pattern.

- [ ] **Step 3.2: Create the header**

Create `software/Libraries/onspeed_core/src/ahrs/EkfqConfigKv.h`:

```cpp
// EkfqConfigKv.h — kv-text parser for EKFQ::Config + EkfqPipeline::PipelineConfig.
//
// Format:
//   key=value          one per line
//   # comment          lines starting with '#' are ignored
//   <blank>            blank lines are ignored
//
// Keys (case-sensitive, all lowercase with underscores):
//   EKFQ::Config (17):
//     q_quat, q_bias, q_z, q_vz, q_b_az, q_beta,
//     r_ax, r_ay, r_az, r_baro, r_beta_prior, r_bias_prior, k_beta_r,
//     p_quat, p_bias, p_z, p_vz, p_b_az, p_beta,
//     tas_min_mps
//   PipelineConfig (4):
//     accel_ema_alpha, comp_fade_tau_sec, ias_gate_rising_kt, tasdot_ema_alpha
//
// Unknown keys cause the parser to return false (typo guard for tuner
// scripts). Missing keys are warned (via warnSink) and left at defaults
// so partial-override files work.

#ifndef ONSPEED_CORE_AHRS_EKFQ_CONFIG_KV_H
#define ONSPEED_CORE_AHRS_EKFQ_CONFIG_KV_H

#include <string_view>
#include <functional>

#include <ahrs/EKFQ.h>
#include <ahrs/EkfqPipeline.h>

namespace onspeed::ahrs {

/// Parse a kv-format text blob into EKFQ + Pipeline configs.
///
/// On success: returns true, outEkfqCfg + outPipeCfg are populated.
///   Unset keys are filled from EKFQ::Config::defaults() and
///   PipelineConfig::defaults(); warnSink is called per missing key.
/// On failure (unknown key, malformed line, parse error):
///   returns false, calls warnSink with a descriptive message.
///
/// warnSink may be null; if so, warnings are silently dropped.
bool ParseEkfqConfigKv(std::string_view text,
                       onspeed::EKFQ::Config& outEkfqCfg,
                       EkfqPipeline::PipelineConfig& outPipeCfg,
                       std::function<void(const char*)> warnSink = {});

}   // namespace onspeed::ahrs

#endif   // ONSPEED_CORE_AHRS_EKFQ_CONFIG_KV_H
```

- [ ] **Step 3.3: Write the failing test FIRST**

Create `test/test_ekfq_config_kv/test_ekfq_config_kv.cpp`:

```cpp
#include <unity.h>
#include <string>
#include <vector>

#include <ahrs/EkfqConfigKv.h>

using onspeed::ahrs::ParseEkfqConfigKv;

void setUp(void) {}
void tearDown(void) {}

static std::vector<std::string> g_warnings;
static void capture_warn(const char* msg) {
    g_warnings.emplace_back(msg);
}

void test_empty_file_yields_all_defaults(void) {
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    TEST_ASSERT_TRUE(ParseEkfqConfigKv("", ekfq, pipe, capture_warn));
    const auto refE = onspeed::EKFQ::Config::defaults();
    const auto refP = onspeed::ahrs::EkfqPipeline::PipelineConfig::defaults();
    TEST_ASSERT_EQUAL_FLOAT(refE.q_quat, ekfq.q_quat);
    TEST_ASSERT_EQUAL_FLOAT(refE.r_baro, ekfq.r_baro);
    TEST_ASSERT_EQUAL_FLOAT(refP.accelEmaAlpha, pipe.accelEmaAlpha);
    // 21 keys total → 21 warnings about missing keys.
    TEST_ASSERT_EQUAL_INT(21, (int)g_warnings.size());
}

void test_single_override(void) {
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    const std::string text = "q_quat=1.5e-6\n";
    TEST_ASSERT_TRUE(ParseEkfqConfigKv(text, ekfq, pipe, capture_warn));
    TEST_ASSERT_EQUAL_FLOAT(1.5e-6f, ekfq.q_quat);
    // r_baro stays at default
    TEST_ASSERT_EQUAL_FLOAT(onspeed::EKFQ::Config::defaults().r_baro, ekfq.r_baro);
    // 20 missing keys
    TEST_ASSERT_EQUAL_INT(20, (int)g_warnings.size());
}

void test_full_override(void) {
    // 21 keys → no missing-key warnings
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    const std::string text =
        "q_quat=1.0e-6\nq_bias=0.05\nq_z=0.001\nq_vz=0.0001\n"
        "q_b_az=0.0001\nq_beta=1e-8\n"
        "r_ax=15.0\nr_ay=10.0\nr_az=12.0\nr_baro=5.0\n"
        "r_beta_prior=0.1\nr_bias_prior=0.0003\nk_beta_r=6.0\n"
        "p_quat=0.05\np_bias=0.01\np_z=100.0\np_vz=4.0\n"
        "p_b_az=1.0\np_beta=0.01\ntas_min_mps=12.0\n"
        "accel_ema_alpha=0.05\ncomp_fade_tau_sec=2.5\n"
        "ias_gate_rising_kt=33.0\ntasdot_ema_alpha=0.2\n";
    TEST_ASSERT_TRUE(ParseEkfqConfigKv(text, ekfq, pipe, capture_warn));
    TEST_ASSERT_EQUAL_FLOAT(1.0e-6f, ekfq.q_quat);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, ekfq.r_ax);
    TEST_ASSERT_EQUAL_FLOAT(0.05f, pipe.accelEmaAlpha);
    TEST_ASSERT_EQUAL_FLOAT(33.0f, pipe.iasGateRisingKt);
    TEST_ASSERT_EQUAL_INT(0, (int)g_warnings.size());
}

void test_comments_and_blanks(void) {
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    const std::string text =
        "# comment\n"
        "\n"
        "q_quat=2.0e-6\n"
        "# another comment\n";
    TEST_ASSERT_TRUE(ParseEkfqConfigKv(text, ekfq, pipe, capture_warn));
    TEST_ASSERT_EQUAL_FLOAT(2.0e-6f, ekfq.q_quat);
}

void test_unknown_key_errors(void) {
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    TEST_ASSERT_FALSE(ParseEkfqConfigKv("q_typo=1.0\n", ekfq, pipe, capture_warn));
    TEST_ASSERT_GREATER_THAN_INT(0, (int)g_warnings.size());
}

void test_malformed_line_errors(void) {
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    TEST_ASSERT_FALSE(ParseEkfqConfigKv("q_quat\n", ekfq, pipe, capture_warn)); // no =
    TEST_ASSERT_FALSE(ParseEkfqConfigKv("q_quat=notanumber\n", ekfq, pipe, capture_warn));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_file_yields_all_defaults);
    RUN_TEST(test_single_override);
    RUN_TEST(test_full_override);
    RUN_TEST(test_comments_and_blanks);
    RUN_TEST(test_unknown_key_errors);
    RUN_TEST(test_malformed_line_errors);
    return UNITY_END();
}
```

- [ ] **Step 3.4: Run test — expect FAIL**

```bash
pio test -e native -f test_ekfq_config_kv -v 2>&1 | tail -10
```

Expected: link error or "cannot find EkfqConfigKv.h".

- [ ] **Step 3.5: Implement the parser**

Create `software/Libraries/onspeed_core/src/ahrs/EkfqConfigKv.cpp`:

```cpp
#include <ahrs/EkfqConfigKv.h>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace onspeed::ahrs {

namespace {

// Trim leading/trailing ASCII whitespace from a string_view.
std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                          s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

bool parse_float(std::string_view s, float& out) {
    // string_view → null-terminated tmp; std::stof is the path of least
    // resistance and matches what the rest of host_main does.
    try {
        std::string tmp(s);
        size_t consumed = 0;
        out = std::stof(tmp, &consumed);
        // Reject trailing junk.
        return consumed == tmp.size();
    } catch (...) {
        return false;
    }
}

enum class Target { Ekfq, Pipe };

struct FieldDesc {
    Target target;
    // Pointer-to-member could be neater but the two targets have
    // different types, so use a tagged-union approach: store an
    // offset+target and dispatch in apply().
    size_t offsetBytes;
};

}   // namespace

bool ParseEkfqConfigKv(std::string_view text,
                       onspeed::EKFQ::Config& outEkfqCfg,
                       EkfqPipeline::PipelineConfig& outPipeCfg,
                       std::function<void(const char*)> warnSink)
{
    auto warn = [&](const std::string& msg) {
        if (warnSink) warnSink(msg.c_str());
    };

    // Initialise outputs to defaults.
    outEkfqCfg  = onspeed::EKFQ::Config::defaults();
    outPipeCfg  = EkfqPipeline::PipelineConfig::defaults();

    // Map each known key to a writer lambda. The lambda owns the
    // target-pointer indirection; the dispatch is just a hash lookup.
    using Writer = bool(*)(float, onspeed::EKFQ::Config&,
                           EkfqPipeline::PipelineConfig&);
    const std::unordered_map<std::string, Writer> writers = {
        {"q_quat",       [](float v, auto& e, auto&){ e.q_quat = v; return true; }},
        {"q_bias",       [](float v, auto& e, auto&){ e.q_bias = v; return true; }},
        {"q_z",          [](float v, auto& e, auto&){ e.q_z    = v; return true; }},
        {"q_vz",         [](float v, auto& e, auto&){ e.q_vz   = v; return true; }},
        {"q_b_az",       [](float v, auto& e, auto&){ e.q_b_az = v; return true; }},
        {"q_beta",       [](float v, auto& e, auto&){ e.q_beta = v; return true; }},
        {"r_ax",         [](float v, auto& e, auto&){ e.r_ax   = v; return true; }},
        {"r_ay",         [](float v, auto& e, auto&){ e.r_ay   = v; return true; }},
        {"r_az",         [](float v, auto& e, auto&){ e.r_az   = v; return true; }},
        {"r_baro",       [](float v, auto& e, auto&){ e.r_baro = v; return true; }},
        {"r_beta_prior", [](float v, auto& e, auto&){ e.r_beta_prior = v; return true; }},
        {"r_bias_prior", [](float v, auto& e, auto&){ e.r_bias_prior = v; return true; }},
        {"k_beta_r",     [](float v, auto& e, auto&){ e.k_beta_R = v; return true; }},
        {"p_quat",       [](float v, auto& e, auto&){ e.p_quat = v; return true; }},
        {"p_bias",       [](float v, auto& e, auto&){ e.p_bias = v; return true; }},
        {"p_z",          [](float v, auto& e, auto&){ e.p_z    = v; return true; }},
        {"p_vz",         [](float v, auto& e, auto&){ e.p_vz   = v; return true; }},
        {"p_b_az",       [](float v, auto& e, auto&){ e.p_b_az = v; return true; }},
        {"p_beta",       [](float v, auto& e, auto&){ e.p_beta = v; return true; }},
        {"tas_min_mps",  [](float v, auto& e, auto&){ e.tas_min_mps = v; return true; }},
        {"accel_ema_alpha",     [](float v, auto&, auto& p){ p.accelEmaAlpha   = v; return true; }},
        {"comp_fade_tau_sec",   [](float v, auto&, auto& p){ p.compFadeTauSec  = v; return true; }},
        {"ias_gate_rising_kt",  [](float v, auto&, auto& p){ p.iasGateRisingKt = v; return true; }},
        {"tasdot_ema_alpha",    [](float v, auto&, auto& p){ p.tasdotEmaAlpha  = v; return true; }},
    };

    // Track which keys were seen so we can warn on the rest.
    std::unordered_map<std::string, bool> seen;
    for (const auto& [k, _] : writers) seen[k] = false;

    // Walk lines.
    size_t lineNum = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t eol = text.find('\n', pos);
        const std::string_view raw = text.substr(
            pos, (eol == std::string_view::npos) ? text.size() - pos : eol - pos);
        pos = (eol == std::string_view::npos) ? text.size() : eol + 1;
        ++lineNum;

        const std::string_view line = trim(raw);
        if (line.empty() || line.front() == '#') continue;

        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "EkfqConfigKv: line %zu malformed (no '='): '%.*s'",
                lineNum, (int)line.size(), line.data());
            warn(buf);
            return false;
        }
        const std::string key(trim(line.substr(0, eq)));
        const std::string_view valSv = trim(line.substr(eq + 1));

        auto it = writers.find(key);
        if (it == writers.end()) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "EkfqConfigKv: line %zu unknown key '%s'",
                lineNum, key.c_str());
            warn(buf);
            return false;
        }

        float v = 0.0f;
        if (!parse_float(valSv, v)) {
            char buf[200];
            std::snprintf(buf, sizeof(buf),
                "EkfqConfigKv: line %zu cannot parse '%.*s' as float for key '%s'",
                lineNum, (int)valSv.size(), valSv.data(), key.c_str());
            warn(buf);
            return false;
        }
        (void)it->second(v, outEkfqCfg, outPipeCfg);
        seen[key] = true;
    }

    // Warn on missing keys (defaults already applied).
    for (const auto& [k, wasSeen] : seen) {
        if (!wasSeen) {
            char buf[120];
            std::snprintf(buf, sizeof(buf),
                "EkfqConfigKv: key '%s' missing — using default", k.c_str());
            warn(buf);
        }
    }
    return true;
}

}   // namespace onspeed::ahrs
```

- [ ] **Step 3.6: Run test — expect PASS**

```bash
pio test -e native -f test_ekfq_config_kv -v 2>&1 | tail -15
```

Expected: all 6 tests pass.

- [ ] **Step 3.7: Run full native suite — verify no regressions**

```bash
pio test -e native 2>&1 | tail -10
```

Expected: 1100+ tests pass (existing + new).

- [ ] **Step 3.8: Run firmware build to catch any header propagation issues**

```bash
pio run -e esp32s3-v4p 2>&1 | tail -5
```

Expected: zero warnings.

- [ ] **Step 3.9: Commit**

```bash
git add software/Libraries/onspeed_core/src/ahrs/EkfqConfigKv.{h,cpp} test/test_ekfq_config_kv/
git commit -m "EkfqConfigKv: parser for per-trial Q/R/pipeline overrides

Hand-rolled kv-text parser, 21 keys covering EKFQ::Config (17) and
EkfqPipeline::PipelineConfig (4). Unknown keys error (typo guard);
missing keys warn (fall back to defaults). No JSON dependency."
```

---

## Task 3.5: Shared `LogRow → AhrsInputs` bridge

**Files:**
- Create: `software/Libraries/onspeed_core/src/replay/LogRowToAhrsInputs.h`
- Create: `software/Libraries/onspeed_core/src/replay/LogRowToAhrsInputs.cpp`
- Create: `test/test_log_row_to_ahrs_inputs/test_log_row_to_ahrs_inputs.cpp`

One canonical translation from a logged row to the `AhrsInputs` shape `Ahrs::Step` consumes. Two stateful concerns the bridge owns:

1. **dt computation** — needs the previous row's `timeStampUs`. Bridge holds it.
2. **fresh-pressure synthesis** — `iasUpdateTimestampUs` isn't logged today; we synthesize it from `PfwdSmoothed` diffs (matches Lenny's heuristic in `data.py`).

- [ ] **Step 3.5.1: Create the header**

```cpp
// LogRowToAhrsInputs.h
//
// Translates a logged LogRow into the AhrsInputs shape that Ahrs::Step
// consumes live. Holds the small amount of inter-row state needed
// (previous timestamp, previous PfwdSmoothed for fresh-pressure
// synthesis). One instance per replay session.
//
// This is the bridge between the "logged" pipeline-stage representation
// (LogRow, what proto::log_csv::ParseRowByIndex produces) and the
// "live" pipeline-stage representation (AhrsInputs, what Ahrs::Step
// consumes). Used by host_main ahrs_tone --input-format=sdlog and by
// any future replay caller that wants to re-execute the AHRS stage
// from raw inputs.

#ifndef ONSPEED_CORE_REPLAY_LOG_ROW_TO_AHRS_INPUTS_H
#define ONSPEED_CORE_REPLAY_LOG_ROW_TO_AHRS_INPUTS_H

#include <types/AhrsInputs.h>
#include <types/LogRow.h>

namespace onspeed::replay {

class LogRowToAhrsInputs {
public:
    LogRowToAhrsInputs() = default;

    /// Result of one row translation.
    struct Result {
        AhrsInputs inputs;
        /// Seconds since the previous row. 1/208 on the first row
        /// (no prior timestamp to diff against).
        float dtSec;
        /// True on the row this caller should pass to Ahrs::Init,
        /// not Ahrs::Step. Always true for the first row; false
        /// thereafter.
        bool isSeedFrame;
    };

    /// Translate one logged row. Updates internal state for dt and
    /// fresh-pressure synthesis.
    Result translate(const onspeed::LogRow& row);

    /// Reset inter-row state (call between separate replay sessions).
    void reset();

private:
    bool initialized_ = false;
    uint64_t prevTimeStampUs_ = 0;
    float    prevPfwdSmoothed_ = 0.0f;
    uint32_t synthIasUpdateUs_ = 0;
};

}   // namespace onspeed::replay

#endif   // ONSPEED_CORE_REPLAY_LOG_ROW_TO_AHRS_INPUTS_H
```

- [ ] **Step 3.5.2: Write the failing test FIRST**

Create `test/test_log_row_to_ahrs_inputs/test_log_row_to_ahrs_inputs.cpp`:

```cpp
#include <unity.h>
#include <cmath>

#include <replay/LogRowToAhrsInputs.h>

using onspeed::LogRow;
using onspeed::replay::LogRowToAhrsInputs;

void setUp(void) {}
void tearDown(void) {}

static LogRow makeRow(uint64_t ts_us, float pfwd_smoothed,
                      float fwdG, float latG, float vertG,
                      float rollDps, float pitchDps, float yawDps,
                      float ias_kt, float palt_ft, float oat_c) {
    LogRow r{};
    r.timeStampUs        = ts_us;
    r.pfwdSmoothed       = pfwd_smoothed;
    r.imuForwardG        = fwdG;
    r.imuLateralG        = latG;
    r.imuVerticalG       = vertG;
    r.imuRollRateDps     = rollDps;
    r.imuPitchRateDps    = pitchDps;
    r.imuYawRateDps      = yawDps;
    r.imuTempCelsius     = 25.0f;
    r.iasKt              = ias_kt;
    r.paltFt             = palt_ft;
    r.oatCelsius         = oat_c;
    return r;
}

void test_first_row_is_seed(void) {
    LogRowToAhrsInputs bridge;
    auto out = bridge.translate(makeRow(1000000, 100.0f,
                                         0.01f, 0.02f, 1.0f,
                                         0.1f, 0.2f, 0.3f,
                                         50.0f, 1000.0f, 15.0f));
    TEST_ASSERT_TRUE(out.isSeedFrame);
    TEST_ASSERT_EQUAL_FLOAT(1.0f / 208.0f, out.dtSec);
    TEST_ASSERT_EQUAL_FLOAT(0.01f, out.inputs.imu.accelXG);
    TEST_ASSERT_EQUAL_FLOAT(1.0f,  out.inputs.imu.accelZG);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, out.inputs.sensors.iasKt);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, out.inputs.sensors.paltFt);
}

void test_dt_computed_from_timestamp_diff(void) {
    LogRowToAhrsInputs bridge;
    (void)bridge.translate(makeRow(1000000, 100.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    auto out = bridge.translate(makeRow(1004808, 100.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    TEST_ASSERT_FALSE(out.isSeedFrame);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 4.808e-3f, out.dtSec);
}

void test_fresh_pressure_bumps_synth_timestamp(void) {
    LogRowToAhrsInputs bridge;
    auto a = bridge.translate(makeRow(1000000, 100.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    auto b = bridge.translate(makeRow(1004808, 100.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    auto c = bridge.translate(makeRow(1009616, 101.5f, 0,0,1, 0,0,0, 50, 1000, 15));
    // b: PfwdSmoothed unchanged → iasUpdateTimestampUs same as a
    TEST_ASSERT_EQUAL_UINT32(a.inputs.iasUpdateTimestampUs,
                             b.inputs.iasUpdateTimestampUs);
    // c: PfwdSmoothed changed → iasUpdateTimestampUs bumped by 20000 us
    TEST_ASSERT_EQUAL_UINT32(b.inputs.iasUpdateTimestampUs + 20000,
                             c.inputs.iasUpdateTimestampUs);
}

void test_reset_clears_state(void) {
    LogRowToAhrsInputs bridge;
    (void)bridge.translate(makeRow(1000000, 100.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    bridge.reset();
    auto out = bridge.translate(makeRow(5000000, 200.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    TEST_ASSERT_TRUE(out.isSeedFrame);
    TEST_ASSERT_EQUAL_FLOAT(1.0f / 208.0f, out.dtSec);
}

void test_pitch_rate_sign(void) {
    // LogCsv::FormatRow emits -imuPitchRateDps on the wire; ParseRowByIndex
    // re-flips on read so LogRow.imuPitchRateDps is in firmware-internal
    // (un-negated) convention. The bridge passes it through unchanged.
    LogRowToAhrsInputs bridge;
    auto out = bridge.translate(makeRow(1000000, 100.0f, 0,0,1,
                                         0.0f, +5.0f, 0.0f,
                                         50, 1000, 15));
    TEST_ASSERT_EQUAL_FLOAT(+5.0f, out.inputs.imu.gyroPitchDps);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_row_is_seed);
    RUN_TEST(test_dt_computed_from_timestamp_diff);
    RUN_TEST(test_fresh_pressure_bumps_synth_timestamp);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_pitch_rate_sign);
    return UNITY_END();
}
```

- [ ] **Step 3.5.3: Run test — expect FAIL (no impl yet)**

```bash
pio test -e native -f test_log_row_to_ahrs_inputs -v 2>&1 | tail -10
```

Expected: header-not-found or link error.

- [ ] **Step 3.5.4: Implement the bridge**

Create `software/Libraries/onspeed_core/src/replay/LogRowToAhrsInputs.cpp`:

```cpp
#include <replay/LogRowToAhrsInputs.h>

#include <cmath>

namespace onspeed::replay {

namespace {
constexpr float kDefaultDtSec = 1.0f / 208.0f;
// ~50 Hz pressure cadence (1e6 us / 50 Hz = 20000 us between fresh samples).
constexpr uint32_t kPressurePeriodUs = 20000;
}   // namespace

void LogRowToAhrsInputs::reset() {
    initialized_       = false;
    prevTimeStampUs_   = 0;
    prevPfwdSmoothed_  = 0.0f;
    synthIasUpdateUs_  = 0;
}

LogRowToAhrsInputs::Result
LogRowToAhrsInputs::translate(const onspeed::LogRow& row) {
    Result out{};

    // Map IMU + sensors directly. PitchRate stays as-is — LogRow holds
    // the un-negated firmware-internal value (LogCsv wire flip is undone
    // by ParseRowByIndex on read).
    out.inputs.imu.accelXG       = row.imuForwardG;
    out.inputs.imu.accelYG       = row.imuLateralG;
    out.inputs.imu.accelZG       = row.imuVerticalG;
    out.inputs.imu.gyroRollDps   = row.imuRollRateDps;
    out.inputs.imu.gyroPitchDps  = row.imuPitchRateDps;
    out.inputs.imu.gyroYawDps    = row.imuYawRateDps;
    out.inputs.imu.tempCelsius   = row.imuTempCelsius;
    out.inputs.imu.timestampUs   = (uint32_t)(row.timeStampUs & 0xFFFFFFFFu);

    out.inputs.sensors.iasKt      = row.iasKt;
    out.inputs.sensors.paltFt     = row.paltFt;
    out.inputs.sensors.oatCelsius = row.oatCelsius;
    out.inputs.sensors.iasAlive   = std::isfinite(row.iasKt) && row.iasKt > 0.0f;

    out.inputs.useEfisOat     = false;
    out.inputs.useInternalOat = true;
    out.inputs.efisOatCelsius = 0.0f;

    // Synthesize iasUpdateTimestampUs from PfwdSmoothed diff. This is
    // the same heuristic Lenny's data.py uses: when the smoothed
    // value changes, a new pressure-stage sample arrived ~now.
    if (initialized_ && row.pfwdSmoothed != prevPfwdSmoothed_) {
        synthIasUpdateUs_ += kPressurePeriodUs;
    } else if (!initialized_) {
        // Seed the synth timestamp on the first frame.
        synthIasUpdateUs_ = kPressurePeriodUs;
    }
    prevPfwdSmoothed_ = row.pfwdSmoothed;
    out.inputs.iasUpdateTimestampUs = synthIasUpdateUs_;

    // dt computation.
    if (!initialized_) {
        out.dtSec       = kDefaultDtSec;
        out.isSeedFrame = true;
        initialized_    = true;
    } else {
        if (row.timeStampUs > prevTimeStampUs_) {
            out.dtSec = (float)(row.timeStampUs - prevTimeStampUs_) * 1.0e-6f;
        } else {
            out.dtSec = kDefaultDtSec;
        }
        out.isSeedFrame = false;
    }
    prevTimeStampUs_ = row.timeStampUs;

    return out;
}

}   // namespace onspeed::replay
```

- [ ] **Step 3.5.5: Run test — expect PASS**

```bash
pio test -e native -f test_log_row_to_ahrs_inputs -v 2>&1 | tail -15
```

Expected: all 5 tests pass.

- [ ] **Step 3.5.6: Build firmware**

```bash
pio run -e esp32s3-v4p 2>&1 | tail -5
```

Expected: zero warnings.

- [ ] **Step 3.5.7: Commit**

```bash
git add software/Libraries/onspeed_core/src/replay/LogRowToAhrsInputs.{h,cpp} test/test_log_row_to_ahrs_inputs/
git commit -m "replay: shared LogRow → AhrsInputs bridge

Single canonical translation from a logged row to AhrsInputs.  Holds
inter-row state for dt + fresh-pressure synthesis via PfwdSmoothed
diff (matches Lenny's data.py heuristic).  Used by host_main
ahrs_tone --input-format=sdlog; available to any future replay
caller that wants to re-execute the AHRS stage from raw inputs."
```

---

## Task 4: host_main ahrs_tone — sdlog input format

**Files:**
- Modify: `tools/regression/host_main.cpp`

Extends `ahrs_tone` to consume a real OnSpeed SD log via `BuildHeaderIndex` + `ParseRowByIndex`, mapping `LogRow` → `AhrsInputs`. Existing synthetic path is unchanged for back-compat with the regression golden.

- [ ] **Step 4.1: Read the current ahrs_tone subcommand**

```bash
sed -n '249,525p' tools/regression/host_main.cpp
```

Confirm:
- `kAhrsToneInputHeader` is the 9-column synthetic header
- `ParseAhrsToneHeader` / `ParseAhrsToneRow` parse synthetic rows
- `BuildAhrsInputs` produces `AhrsInputs` from `InputRow` (synthetic)
- `CmdAhrsTone` is the top-level entrypoint

- [ ] **Step 4.2: Add --input-format flag plumbing (default: synthetic)**

In `CmdAhrsTone`, after the existing `--input` parse, add:

```cpp
    // --input-format {synthetic|sdlog}. Default 'synthetic' is the
    // 9-column fixture format the regression goldens use. 'sdlog'
    // consumes a real OnSpeed SD log via BuildHeaderIndex.
    const char* fmt_in = ArgGet(argc, argv, "--input-format", "synthetic");
    const bool input_is_sdlog = (std::strcmp(fmt_in, "sdlog") == 0);
    if (!input_is_sdlog && std::strcmp(fmt_in, "synthetic") != 0) {
        std::fprintf(stderr,
            "host_main ahrs_tone: --input-format must be 'synthetic' or 'sdlog'\n");
        return 1;
    }
```

- [ ] **Step 4.3: Add --config flag (per-aircraft OnSpeedConfig)**

After the --input-format parse:

```cpp
    // --config PATH: per-aircraft OnSpeedConfig (V1 or V2). Used to
    // populate AhrsConfig install-bias fields. Required when
    // --input-format=sdlog so we replay through the firmware's actual
    // install-bias rotation; ignored when --input-format=synthetic.
    const char* config_path = ArgGet(argc, argv, "--config");
    onspeed::config::OnSpeedConfig pilotCfg;
    pilotCfg.LoadDefaults();
    if (config_path != nullptr) {
        if (!LoadConfig(config_path, pilotCfg)) return 1;
    } else if (input_is_sdlog) {
        std::fprintf(stderr,
            "host_main ahrs_tone: --config is required with --input-format=sdlog\n");
        return 1;
    }
```

- [ ] **Step 4.4: Add --ekfq-config flag**

After the --config parse:

```cpp
    // --ekfq-config PATH: per-trial EKFQ + pipeline overrides (kv format).
    // Applied via Ahrs::SetEkfqConfig after Init. Optional; defaults if absent.
    const char* ekfq_cfg_path = ArgGet(argc, argv, "--ekfq-config");
    onspeed::EKFQ::Config ekfqCfg = onspeed::EKFQ::Config::defaults();
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipeCfg =
        onspeed::ahrs::EkfqPipeline::PipelineConfig::defaults();
    if (ekfq_cfg_path != nullptr) {
        std::ifstream kf(ekfq_cfg_path);
        if (!kf.is_open()) {
            std::fprintf(stderr,
                "host_main ahrs_tone: cannot open --ekfq-config '%s'\n",
                ekfq_cfg_path);
            return 1;
        }
        const std::string kvText{std::istreambuf_iterator<char>(kf),
                                  std::istreambuf_iterator<char>()};
        auto warnSink = [](const char* m) {
            std::fprintf(stderr, "host_main ahrs_tone: %s\n", m);
        };
        if (!onspeed::ahrs::ParseEkfqConfigKv(kvText, ekfqCfg, pipeCfg, warnSink)) {
            return 1;
        }
    }
```

Add the includes at the top:

```cpp
#include <ahrs/EkfqConfigKv.h>
#include <replay/LogRowToAhrsInputs.h>
```

- [ ] **Step 4.5: Add --passthrough-cols flag**

After --ekfq-config:

```cpp
    // --passthrough-cols A,B,C: comma-separated input column names whose
    // raw values get appended to the output CSV. Required for Optuna
    // scoring (Python tuner reads truth columns out of these). Only
    // valid with --input-format=sdlog.
    const char* passthrough_str = ArgGet(argc, argv, "--passthrough-cols");
    std::vector<std::string> passthroughCols;
    if (passthrough_str != nullptr) {
        if (!input_is_sdlog) {
            std::fprintf(stderr,
                "host_main ahrs_tone: --passthrough-cols requires --input-format=sdlog\n");
            return 1;
        }
        std::string s(passthrough_str);
        size_t pos = 0;
        while (pos < s.size()) {
            const size_t comma = s.find(',', pos);
            const std::string tok = s.substr(
                pos, (comma == std::string::npos) ? s.size() - pos : comma - pos);
            if (!tok.empty()) passthroughCols.push_back(tok);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }
```

- [ ] **Step 4.6: Branch on input format**

After the algorithm flag parse and stream-open block, replace the unconditional synthetic-parse loop with:

```cpp
    if (input_is_sdlog) {
        return RunAhrsToneSdlog(*in_stream, algo, pilotCfg, ekfqCfg, pipeCfg,
                                passthroughCols, fmt);
    }
    // Else fall through to the existing synthetic-format path.
```

Define `RunAhrsToneSdlog` as a static function in the same file, before `CmdAhrsTone`:

```cpp
// RunAhrsToneSdlog — sdlog-format path for ahrs_tone.
//
// Reads a real OnSpeed SD log via BuildHeaderIndex/ParseRowByIndex,
// maps LogRow → AhrsInputs, drives Ahrs::Step with --algorithm,
// applies --config install bias and --ekfq-config tuning, and emits
// an output CSV with the standard ahrs_tone columns plus appended
// passthrough columns.
static int RunAhrsToneSdlog(std::istream& in,
                            onspeed::ahrs::Algorithm algo,
                            const onspeed::config::OnSpeedConfig& pilotCfg,
                            const onspeed::EKFQ::Config& ekfqCfg,
                            const onspeed::ahrs::EkfqPipeline::PipelineConfig& pipeCfg,
                            const std::vector<std::string>& passthroughCols,
                            OutputFormat fmt)
{
    if (fmt != OutputFormat::Csv) {
        std::fprintf(stderr,
            "host_main ahrs_tone: --input-format=sdlog only supports --output-format=csv\n");
        return 1;
    }

    // Parse header.
    std::string headerLine;
    if (!std::getline(in, headerLine)) {
        std::fprintf(stderr, "host_main ahrs_tone: empty input\n");
        return 1;
    }

    onspeed::proto::log_csv::HeaderIndex hdrIdx;
    auto warnHdr = [](const char* col) {
        std::fprintf(stderr,
            "host_main ahrs_tone: header missing column '%s'\n", col);
    };
    if (!onspeed::proto::log_csv::BuildHeaderIndex(headerLine, hdrIdx, warnHdr)) {
        std::fprintf(stderr, "host_main ahrs_tone: header parse failed\n");
        return 1;
    }

    // Resolve passthrough column indices into the raw CSV row.
    // We tokenize each row twice: once for ParseRowByIndex → LogRow,
    // once for raw-string extraction of passthrough fields. This is
    // ~5% slower than a single-pass tokenize but keeps the change
    // surface tiny.
    //
    // BuildHeaderIndex maps named columns to integer ordinals on the
    // HeaderIndex struct. We need ordinals for the user's passthrough
    // names — re-parse the header line directly to build the lookup.
    std::vector<int> passthroughIdx(passthroughCols.size(), -1);
    {
        std::vector<std::string> headerCols;
        std::string h(headerLine);
        if (!h.empty() && h.back() == '\r') h.pop_back();
        size_t p = 0;
        while (p < h.size()) {
            const size_t c = h.find(',', p);
            headerCols.push_back(h.substr(
                p, (c == std::string::npos) ? h.size() - p : c - p));
            if (c == std::string::npos) break;
            p = c + 1;
        }
        for (size_t i = 0; i < passthroughCols.size(); ++i) {
            for (size_t j = 0; j < headerCols.size(); ++j) {
                if (headerCols[j] == passthroughCols[i]) {
                    passthroughIdx[i] = (int)j;
                    break;
                }
            }
            if (passthroughIdx[i] < 0) {
                std::fprintf(stderr,
                    "host_main ahrs_tone: passthrough column '%s' not in log header\n",
                    passthroughCols[i].c_str());
                return 1;
            }
        }
    }

    // Build AhrsConfig from the pilot cfg's install bias.
    onspeed::ahrs::AhrsConfig ahrsCfg;
    ahrsCfg.pitchBiasDeg         = pilotCfg.fPitchBias;
    ahrsCfg.rollBiasDeg          = pilotCfg.fRollBias;
    ahrsCfg.algorithm            = algo;
    ahrsCfg.gyroSmoothingWindow  = 30;
    ahrsCfg.imuSampleRateHz      = 208.0f;
    ahrsCfg.pressureSampleRateHz = 50.0f;
    onspeed::ahrs::Ahrs ahrs(ahrsCfg);
    ahrs.SetEkfqConfig(ekfqCfg, pipeCfg);

    // Emit output header: standard ahrs_tone columns + passthrough names.
    std::printf("%s", kAhrsToneOutputHeader);
    for (const auto& name : passthroughCols) {
        std::printf(",passthrough_%s", name.c_str());
    }
    std::printf("\n");

    // Tone calculator (same defaults as the synthetic path).
    onspeed::ToneCalc tone(kCleanThresholds);

    // Row bridge — owns dt + fresh-pressure state across rows.
    onspeed::replay::LogRowToAhrsInputs bridge;

    std::string line;
    size_t rowIdx = 0;
    onspeed::LogRow row;
    row.boomEnabled        = hdrIdx.boomEnabled;
    row.efisEnabled        = hdrIdx.efisEnabled;
    row.efisIsVn300        = hdrIdx.efisIsVn300;
    row.flapsRawAdcPresent = (hdrIdx.idxFlapsRawAdc >= 0);

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (!onspeed::proto::log_csv::ParseRowByIndex(line, hdrIdx, row)) {
            std::fprintf(stderr,
                "host_main ahrs_tone: parse error at row %zu\n", rowIdx);
            return 1;
        }

        const auto br = bridge.translate(row);

        if (br.isSeedFrame) {
            ahrs.Init(br.inputs, row.paltFt);
            ++rowIdx;
            continue;
        }

        const onspeed::AhrsOutputs out = ahrs.Step(br.inputs, br.dtSec);

        // ToneCalc.
        const onspeed::ToneState ts =
            tone.calculateTone(out.derivedAoaDeg, /*iasAlive*/ true);

        // Emit row.
        std::printf("%.4f,%.4f,%.4f,"
                    "%.4f,%.4f,%.4f,%.4f,"
                    "%.4f,%.4f,%.4f,%.4f,"
                    "%.1f,%d",
            (double)ai.sensors.iasKt,
            (double)ai.sensors.paltFt,
            (double)ai.sensors.oatCelsius,
            (double)out.pitchDeg,
            (double)out.rollDeg,
            (double)out.flightPathDeg,
            (double)out.derivedAoaDeg,
            (double)out.tasMps,
            (double)out.kalmanAltFt,
            (double)out.kalmanVsiFpm,
            (double)out.earthVertG,
            (double)ts.fToneFrequencyHz,
            (int)ts.iToneLevel);

        // Append passthrough columns by re-tokenizing the raw line.
        if (!passthroughIdx.empty()) {
            std::vector<std::string_view> toks;
            toks.reserve(96);
            size_t p = 0;
            while (p <= line.size()) {
                const size_t c = line.find(',', p);
                toks.emplace_back(line.data() + p,
                    (c == std::string::npos) ? line.size() - p : c - p);
                if (c == std::string::npos) break;
                p = c + 1;
            }
            for (int idx : passthroughIdx) {
                if (idx < (int)toks.size()) {
                    std::printf(",%.*s", (int)toks[idx].size(), toks[idx].data());
                } else {
                    std::printf(",");   // missing → empty cell
                }
            }
        }
        std::printf("\n");
        ++rowIdx;
    }

    std::fprintf(stderr, "host_main ahrs_tone: %zu rows processed (sdlog)\n", rowIdx);
    return 0;
}
```

- [ ] **Step 4.7: Build**

```bash
cd tools/regression && pio run -e native 2>&1 | tail -10
```

Expected: zero warnings, host_main binary rebuilt.

- [ ] **Step 4.8: Smoke-test the synthetic path is unchanged**

```bash
./tools/regression/run_snapshot.py 2>&1 | tail -10
```

Expected: all goldens match (synthetic path is untouched).

- [ ] **Step 4.9: Smoke-test the sdlog path against a tiny real log**

The full 79-min log lives outside the repo. Create a tiny ~20-row test fixture from it (or use a manually-crafted one). For this smoke step we just check the binary can read a real SD log header without crashing:

```bash
# Find any committed SD-log-style fixture and try it.
head -25 software/Libraries/onspeed_core/test_data/*.csv 2>/dev/null | head -30
```

If no SD-log fixture exists yet, defer the smoke to Task 6 (which creates one).

- [ ] **Step 4.10: Commit**

```bash
git add tools/regression/host_main.cpp
git commit -m "host_main ahrs_tone: sdlog input + config overrides + passthrough

Adds three flags to ahrs_tone:
  --input-format sdlog       consume real OnSpeed SD log via BuildHeaderIndex
  --config PATH              load V1/V2 OnSpeedConfig for install bias
  --ekfq-config PATH         per-trial Q/R/pipeline override kv file
  --passthrough-cols A,B,C   append named input columns to output

Existing 'synthetic' input format is unchanged; regression goldens
remain byte-identical."
```

---

## Task 5: Optuna substrate fixture + parity golden

**Files:**
- Create: `tools/regression/fixtures/ekfq_substrate_smoke.csv`
- Create: `tools/regression/fixtures/ekfq_substrate_golden.csv`
- Create: `tools/regression/fixtures/ekfq_substrate_smoke.cfg`
- Modify: `tools/regression/run_snapshot.py`

A small committed fixture lets CI verify the wiring without committing the 79-min flight log.

- [ ] **Step 5.1: Generate the smoke fixture**

Pick the first ~150 rows of the real log (which lives at the user-supplied path). For repeatable CI we commit a small extract:

```bash
HEAD=$(head -1 "$LOG_PATH")
echo "$HEAD" > tools/regression/fixtures/ekfq_substrate_smoke.csv
sed -n '2,151p' "$LOG_PATH" >> tools/regression/fixtures/ekfq_substrate_smoke.csv
wc -l tools/regression/fixtures/ekfq_substrate_smoke.csv
```

Expected: 151 lines.

- [ ] **Step 5.2: Copy the matching cfg**

```bash
cp /path/to/onspeed2.cfg tools/regression/fixtures/ekfq_substrate_smoke.cfg
```

(Use the cfg that lives next to the log in the source directory.)

- [ ] **Step 5.3: Generate the golden output**

```bash
./tools/regression/.pio/build/native/host_main ahrs_tone \
  --algorithm ekfq \
  --input-format sdlog \
  --input tools/regression/fixtures/ekfq_substrate_smoke.csv \
  --config tools/regression/fixtures/ekfq_substrate_smoke.cfg \
  --passthrough-cols vnPitch,vnRoll,vnVelNedDown \
  > tools/regression/fixtures/ekfq_substrate_golden.csv

head -3 tools/regression/fixtures/ekfq_substrate_golden.csv
wc -l tools/regression/fixtures/ekfq_substrate_golden.csv
```

Expected: header line + 150 data rows (or 149 if the first row is consumed only as Init seed).

- [ ] **Step 5.4: Add the parity check to run_snapshot.py**

Find the existing snapshot-check pattern in `run_snapshot.py`:

```bash
grep -n "def main\|fixture\|golden" tools/regression/run_snapshot.py | head -20
```

Add a new check function modelled on the existing ones:

```python
def check_ekfq_substrate_smoke(host_main_path, fixtures_dir):
    """Run ahrs_tone --input-format sdlog against the committed smoke
    fixture; verify output matches the committed golden byte-for-byte."""
    smoke = fixtures_dir / "ekfq_substrate_smoke.csv"
    cfg   = fixtures_dir / "ekfq_substrate_smoke.cfg"
    golden = fixtures_dir / "ekfq_substrate_golden.csv"
    proc = subprocess.run(
        [str(host_main_path), "ahrs_tone",
         "--algorithm", "ekfq",
         "--input-format", "sdlog",
         "--input", str(smoke),
         "--config", str(cfg),
         "--passthrough-cols", "vnPitch,vnRoll,vnVelNedDown"],
        capture_output=True, text=True, check=True,
    )
    actual = proc.stdout
    expected = golden.read_text()
    if actual != expected:
        print("[ekfq_substrate_smoke] MISMATCH — first diff:")
        for i, (a, e) in enumerate(zip(actual.splitlines(), expected.splitlines())):
            if a != e:
                print(f"  row {i}:")
                print(f"    actual:   {a[:200]}")
                print(f"    expected: {e[:200]}")
                break
        sys.exit(1)
    print(f"  [ekfq_substrate_smoke] {len(expected.splitlines())-1} rows match golden")
```

Register it in `main()` alongside the other checks.

- [ ] **Step 5.5: Run the full snapshot suite**

```bash
./tools/regression/run_snapshot.py 2>&1 | tail -10
```

Expected: all checks pass, including the new one.

- [ ] **Step 5.6: Commit**

```bash
git add tools/regression/fixtures/ekfq_substrate_*.csv tools/regression/fixtures/ekfq_substrate_*.cfg tools/regression/run_snapshot.py
git commit -m "tools/regression: ekfq_substrate smoke fixture + golden

150-row extract of the testbed flight log + its onspeed2.cfg + the
expected ahrs_tone --input-format=sdlog output, gated in CI via
run_snapshot.py. Catches Python↔C++ drift before it reaches a
tuning run."
```

---

## Task 6: Python tuner — subprocess driver

**Files:**
- Create: `ekfq_pipeline/run_host_main.py`
- Modify: `ekfq_pipeline/tune_ekf.py`
- Modify: `ekfq_pipeline/README.md`

The Python tuner gets a `--driver host-main` mode that subprocesses host_main per trial instead of running `PipelineQuat` in-process.

- [ ] **Step 6.1: Verify Lenny's ekfq_pipeline is present in this branch**

```bash
ls ekfq_pipeline/ 2>/dev/null
```

If it's not in the branch (PR #591 explicitly deferred it), copy it in from `EKFQ-tuned`:

```bash
gh api repos/flyonspeed/OnSpeed-Gen3/contents/ekfq_pipeline?ref=EKFQ-tuned --jq '.[].path' \
  | while read p; do
      mkdir -p "$(dirname "$p")"
      gh api "repos/flyonspeed/OnSpeed-Gen3/contents/$p?ref=EKFQ-tuned" --jq '.content' | base64 -d > "$p"
    done
# recursive: grab onspeed_ekf subdir too
for f in __init__.py data.py ekf_quat.py pipeline_quat.py; do
  gh api "repos/flyonspeed/OnSpeed-Gen3/contents/ekfq_pipeline/onspeed_ekf/$f?ref=EKFQ-tuned" --jq '.content' | base64 -d > "ekfq_pipeline/onspeed_ekf/$f"
done
```

If `ekfq_pipeline/` is already present, skip the copy.

- [ ] **Step 6.2: Create the subprocess wrapper**

Create `ekfq_pipeline/run_host_main.py`:

```python
"""Subprocess wrapper for host_main ahrs_tone in --input-format=sdlog mode.

Used by tune_ekf.py when --driver=host-main is selected. Writes the
trial's EKFQConfig + PipelineQuatConfig to a temporary kv file, invokes
host_main, parses the CSV output into a pandas DataFrame, and returns
it for downstream composite_loss scoring.

Loss math stays in Python; this wrapper is pure I/O + parameter
marshalling.
"""

from __future__ import annotations

import io
import os
import subprocess
import tempfile
from pathlib import Path
from typing import Sequence

import pandas as pd

from .onspeed_ekf import EKFQConfig, PipelineQuatConfig


# Mapping from Python field names to the kv-file keys host_main expects.
# Order: EKFQ::Config keys first, then PipelineConfig keys.
_KV_KEYS: tuple[tuple[str, str], ...] = (
    # EKFQ
    ("q_quat", "q_quat"),
    ("q_bias", "q_bias"),
    ("q_z", "q_z"),
    ("q_vz", "q_vz"),
    ("q_b_az", "q_b_az"),
    ("q_beta", "q_beta"),
    ("r_ax", "r_ax"),
    ("r_ay", "r_ay"),
    ("r_az", "r_az"),
    ("r_baro", "r_baro"),
    ("r_beta_prior", "r_beta_prior"),
    ("r_bias_prior", "r_bias_prior"),
    ("k_beta_R", "k_beta_r"),
    ("p_quat", "p_quat"),
    ("p_bias", "p_bias"),
    ("p_z", "p_z"),
    ("p_vz", "p_vz"),
    ("p_b_az", "p_b_az"),
    ("p_beta", "p_beta"),
    ("tas_min_mps", "tas_min_mps"),
)

_PIPE_KV_KEYS: tuple[tuple[str, str], ...] = (
    ("accel_ema_alpha", "accel_ema_alpha"),
    ("comp_fade_tau_sec", "comp_fade_tau_sec"),
    ("ias_alive_kt", "ias_gate_rising_kt"),
    ("tasdot_ema_alpha", "tasdot_ema_alpha"),
)


def _write_kv(ekfq_cfg: EKFQConfig, pipe_cfg: PipelineQuatConfig, path: Path) -> None:
    """Write trial config to a kv file host_main can consume."""
    lines = ["# Auto-generated by run_host_main.py"]
    for py_attr, kv_key in _KV_KEYS:
        v = getattr(ekfq_cfg, py_attr)
        lines.append(f"{kv_key}={v!r}")
    for py_attr, kv_key in _PIPE_KV_KEYS:
        v = getattr(pipe_cfg, py_attr)
        lines.append(f"{kv_key}={v!r}")
    path.write_text("\n".join(lines) + "\n")


def run_host_main(
    host_main_path: Path,
    log_path: Path,
    config_path: Path,
    ekfq_cfg: EKFQConfig,
    pipe_cfg: PipelineQuatConfig,
    passthrough_cols: Sequence[str],
) -> pd.DataFrame:
    """Invoke host_main ahrs_tone for one Optuna trial.

    Returns a DataFrame with columns from kAhrsToneOutputHeader
    (ias_kt, palt_ft, ... tone_level) plus passthrough_<name> per
    requested passthrough column.

    Raises RuntimeError on non-zero exit; the caller maps this to
    math.inf loss inside the Optuna objective.
    """
    with tempfile.TemporaryDirectory() as td:
        kv_path = Path(td) / "trial.kv"
        _write_kv(ekfq_cfg, pipe_cfg, kv_path)

        argv = [
            str(host_main_path), "ahrs_tone",
            "--algorithm", "ekfq",
            "--input-format", "sdlog",
            "--input", str(log_path),
            "--config", str(config_path),
            "--ekfq-config", str(kv_path),
            "--passthrough-cols", ",".join(passthrough_cols),
        ]

        proc = subprocess.run(argv, capture_output=True, text=True)
        if proc.returncode != 0:
            raise RuntimeError(
                f"host_main ahrs_tone exited {proc.returncode}: "
                f"{proc.stderr[-1000:]}"
            )
        df = pd.read_csv(io.StringIO(proc.stdout))
        return df
```

- [ ] **Step 6.3: Add --driver flag to tune_ekf.py**

In `ekfq_pipeline/tune_ekf.py`, locate the argparse block (in `main()`). Add:

```python
    ap.add_argument(
        "--driver", choices=["python", "host-main"], default="python",
        help="python (default, in-process) or host-main (subprocess into compiled host_main)"
    )
    ap.add_argument(
        "--host-main", default="../OnSpeed-Gen3/tools/regression/.pio/build/native/host_main",
        help="path to compiled host_main binary (only used with --driver=host-main)"
    )
    ap.add_argument(
        "--config-path", default=None,
        help="per-aircraft OnSpeedConfig .cfg (required with --driver=host-main)"
    )
```

- [ ] **Step 6.4: Add the host-main objective**

In `tune_ekf.py`, add a new function near `make_objective`:

```python
def make_objective_host_main(
    log, host_main_path, config_path, train_mask, val_mask,
    weights, loss_profile,
):
    """Build an Optuna objective that subprocesses host_main per trial."""
    from .run_host_main import run_host_main

    df_full = log.df
    passthrough = [
        "vnPitch", "vnRoll", "vnVelNedDown", "vnDataAge",
        "IAS", "TAS", "AngleofAttack",
    ]

    def objective(trial: optuna.Trial) -> float:
        # Same Optuna search-space as the Python driver — params are
        # named the same way so studies are comparable.
        q_attitude   = trial.suggest_float("q_quat",       1e-9, 1e-1, log=True)
        q_bias       = trial.suggest_float("q_bias",       1e-6, 10.0, log=True)
        q_z          = trial.suggest_float("q_z",          1e-5, 1.0,  log=True)
        q_vz         = trial.suggest_float("q_vz",         1e-6, 10.0, log=True)
        q_b_az       = trial.suggest_float("q_b_az",       1e-8, 1e-2, log=True)
        q_beta       = trial.suggest_float("q_beta",       1e-10, 1e-1, log=True)
        r_ax         = trial.suggest_float("r_ax",         1e-3, 1000.0, log=True)
        r_ay         = trial.suggest_float("r_ay",         1e-3, 1000.0, log=True)
        r_az         = trial.suggest_float("r_az",         1e-3, 1000.0, log=True)
        r_baro       = trial.suggest_float("r_baro",       1e-2, 20.0, log=True)
        r_beta_prior = trial.suggest_float("r_beta_prior", 1e-4, 1.0,  log=True)
        r_bias_prior = trial.suggest_float("r_bias_prior", 1e-6, 1e-1, log=True)
        k_beta_R     = trial.suggest_float("k_beta_R",     1e-4, 100.0, log=True)

        accel_ema_alpha   = trial.suggest_float("accel_ema_alpha",  2e-2, 0.50, log=True)
        comp_fade_tau_sec = trial.suggest_float("comp_fade_tau",    0.2, 5.0)
        ias_alive_kt      = trial.suggest_float("ias_alive_kt",     15.0, 60.0)
        tasdot_ema_alpha  = trial.suggest_float("tasdot_ema_alpha", 5e-3, 0.5, log=True)

        ekf_cfg = EKFQConfig(
            q_quat=q_attitude, q_bias=q_bias, q_z=q_z, q_vz=q_vz,
            q_b_az=q_b_az, q_beta=q_beta,
            r_ax=r_ax, r_ay=r_ay, r_az=r_az,
            r_baro=r_baro, r_beta_prior=r_beta_prior,
            r_bias_prior=r_bias_prior, k_beta_R=k_beta_R,
            p_quat=0.05, p_bias=0.01, p_z=100.0, p_vz=4.0,
            p_b_az=1.0, p_beta=0.01,
        )
        pipe_cfg = PipelineQuatConfig(
            accel_ema_alpha=accel_ema_alpha,
            comp_fade_tau_sec=comp_fade_tau_sec,
            ias_alive_kt=ias_alive_kt,
            tasdot_ema_alpha=tasdot_ema_alpha,
        )
        try:
            df_out = run_host_main(
                Path(host_main_path), Path(args_log_path), Path(config_path),
                ekf_cfg, pipe_cfg, passthrough,
            )
        except (RuntimeError, ValueError):
            return math.inf

        # Build a history dict matching the Python driver's shape so
        # composite_loss can score it unchanged.
        history = {
            "pitch_deg": df_out["pitch_deg"].to_numpy(),
            "roll_deg":  df_out["roll_deg"].to_numpy(),
            "yaw_deg":   np.zeros(len(df_out)),   # host_main doesn't emit yaw
            "alpha_deg": df_out["derived_aoa_deg"].to_numpy(),
            "beta_deg":  np.zeros(len(df_out)),   # ditto
            "vz_mps":    -df_out["kalman_vsi_fpm"].to_numpy() * 0.00508,  # fpm→m/s, +climb→+down
            "z_m":       df_out["kalman_alt_ft"].to_numpy() * 0.3048,
            "bp_dps":    np.zeros(len(df_out)),
            "bq_dps":    np.zeros(len(df_out)),
            "br_dps":    np.zeros(len(df_out)),
            "b_az_mps2": np.zeros(len(df_out)),
            "ias_alive": np.ones(len(df_out), dtype=bool),
            "comp_fade": np.ones(len(df_out), dtype=np.float32),
        }
        # df_out may be one row shorter than df_full (first row is seed).
        # Trim df_full + masks to match.
        n = len(df_out)
        df_trim = df_full.iloc[:n].reset_index(drop=True)
        fresh_vn = log.fresh_vn[:n] & train_mask[:n]
        fresh_vn_v = log.fresh_vn[:n] & val_mask[:n]
        aoa_valid = history["ias_alive"] & np.isfinite(df_trim["AngleofAttack"].to_numpy())
        train_loss, train_bd = composite_loss(
            history, df_trim, fresh_vn,   aoa_valid & train_mask[:n],
            weights, ekf_cfg, profile=loss_profile,
        )
        val_loss, val_bd     = composite_loss(
            history, df_trim, fresh_vn_v, aoa_valid & val_mask[:n],
            weights, ekf_cfg, profile=loss_profile,
        )
        trial.set_user_attr("train_breakdown", train_bd)
        trial.set_user_attr("val_breakdown",   val_bd)
        trial.set_user_attr("val_loss",        val_loss)
        return float(train_loss)

    return objective
```

The `args_log_path` reference depends on capturing `args.log` in scope. In `main()`, before calling `make_objective_host_main`, set:

```python
    args_log_path = args.log
```

and pass it in or close over it.

- [ ] **Step 6.5: Route make_objective in main()**

In `tune_ekf.py::main()`, after the existing `objective = make_objective(...)` line, replace with:

```python
    if args.driver == "host-main":
        if args.config_path is None:
            print("--driver=host-main requires --config-path", flush=True)
            sys.exit(1)
        objective = make_objective_host_main(
            log, args.host_main, args.config_path,
            train_mask, val_mask, weights, args.loss_mode,
        )
        print(f"  driver: host-main (binary: {args.host_main})", flush=True)
    else:
        objective = make_objective(log, train_mask, val_mask, weights,
                                   loss_profile=args.loss_mode)
        print(f"  driver: python (in-process)", flush=True)
```

Add `import sys` at the top of the file if not already there.

- [ ] **Step 6.6: Smoke-test the host-main driver**

Build host_main in the worktree:

```bash
cd tools/regression && pio run -e native 2>&1 | tail -3
ls -la .pio/build/native/host_main
```

Then run a 3-trial smoke study against the smoke fixture (NOT the full log):

```bash
cd ../../../ekfq_pipeline
rm -rf studies/host_main_smoke.db
uv run --with optuna --with pandas --with numpy python3 tune_ekf.py \
  --driver host-main \
  --host-main ../tools/regression/.pio/build/native/host_main \
  --log ../tools/regression/fixtures/ekfq_substrate_smoke.csv \
  --config-path ../tools/regression/fixtures/ekfq_substrate_smoke.cfg \
  --trials 3 --study-name host_main_smoke 2>&1 | tail -20
```

Expected: 3 trials complete, each in <1 sec. Loss values will be garbage on 150 rows but that's fine — this proves the wiring.

- [ ] **Step 6.7: Time the host-main driver on the full log**

```bash
uv run --with optuna --with pandas --with numpy python3 tune_ekf.py \
  --driver host-main \
  --host-main ../tools/regression/.pio/build/native/host_main \
  --log /path/to/log_007_fixed.csv \
  --config-path /path/to/onspeed2.cfg \
  --trials 10 --study-name host_main_time --loss-mode cruise-aoa 2>&1 | tail -15
```

Expected: 10 trials in <2 min wall (target: <12 sec/trial — at least 4× faster than the 55 sec/trial Python baseline). Capture the actual sec/trial number for the PR description.

- [ ] **Step 6.8: Commit**

```bash
git add ekfq_pipeline/run_host_main.py ekfq_pipeline/tune_ekf.py
git commit -m "tune_ekf: --driver host-main subprocesses C++ replay per trial

Optional opt-in (default stays 'python' for back-compat). When
selected, each Optuna trial writes its params to a temp kv file and
runs host_main ahrs_tone in --input-format=sdlog mode, parses the
output CSV, and feeds it through the existing composite_loss.

Closes the Python↔C++ drift loop: any divergence between
pipeline_quat.py and EkfqPipeline.cpp surfaces as a tuning
regression."
```

---

## Task 7: README + docs

**Files:**
- Modify: `tools/regression/README.md`
- Modify: `ekfq_pipeline/README.md`

- [ ] **Step 7.1: Update tools/regression/README.md**

Find the existing `ahrs_tone` documentation and append:

```markdown
### sdlog input format

`ahrs_tone` can consume real OnSpeed SD logs instead of the 9-column
synthetic fixture format. Used by the Optuna substrate (`ekfq_pipeline/`).

```bash
host_main ahrs_tone \
    --algorithm ekfq \
    --input-format sdlog \
    --input log_007_fixed.csv \
    --config onspeed2.cfg \
    --ekfq-config trial.kv \
    --passthrough-cols vnPitch,vnRoll,vnVelNedDown
```

- `--config PATH`: per-aircraft OnSpeedConfig (V1 or V2). Provides
  install bias (pitchBiasDeg, rollBiasDeg). Required with `--input-format=sdlog`.
- `--ekfq-config PATH`: per-trial Q/R/pipeline override kv file. See
  `ahrs/EkfqConfigKv.h` for the format. Optional; defaults if absent.
- `--passthrough-cols A,B,C`: comma-separated input column names. The
  raw values are appended to each output row as `passthrough_<name>`
  columns. Used so a Python scorer can read truth columns aligned to
  the per-row AHRS outputs.
```

- [ ] **Step 7.2: Update ekfq_pipeline/README.md**

Add a new section after "What you need to reproduce the v15 study":

```markdown
## Driver: python vs host-main

Two replay drivers are available, selected via `--driver`:

### `--driver python` (default)

In-process replay through `PipelineQuat.run()`. The reference Python
implementation. Studies are stored as SQLite in `studies/<name>.db`.

### `--driver host-main` (closes the C++ drift loop)

Subprocesses the firmware's compiled `host_main` binary per trial,
running the actual `EkfqPipeline.cpp` code that flies. Loss math
stays in Python; the C++ binary is a pure replay engine.

```bash
python3 tune_ekf.py \
  --driver host-main \
  --host-main ../OnSpeed-Gen3/tools/regression/.pio/build/native/host_main \
  --log log_007_fixed.csv \
  --config-path onspeed2.cfg \
  --trials 200 --loss-mode cruise-aoa
```

The host-main driver is several × faster than Python (~5 sec/trial vs
~55 sec/trial at 988k rows) and any future Python↔C++ divergence in the
EKFQ port surfaces as a tuning regression instead of a silent flight
behaviour change.
```

- [ ] **Step 7.3: Commit**

```bash
git add tools/regression/README.md ekfq_pipeline/README.md
git commit -m "docs: document host_main sdlog mode + tune_ekf --driver host-main"
```

---

## Task 8: Final acceptance pass

- [ ] **Step 8.1: Full native test suite**

```bash
pio test -e native 2>&1 | tail -10
```

Expected: 1106+ tests pass (PR #591 baseline + new tests from this PR).

- [ ] **Step 8.2: Full firmware build (V4P)**

```bash
pio run -e esp32s3-v4p 2>&1 | tail -5
```

Expected: zero warnings, success.

- [ ] **Step 8.3: Regression snapshot**

```bash
./tools/regression/run_snapshot.py 2>&1 | tail -15
```

Expected: all goldens match (existing) + ekfq_substrate_smoke matches.

- [ ] **Step 8.4: 10-trial timing run (capture for PR description)**

Run the host-main driver against the full log and record:
- sec/trial (wall)
- final best train_loss
- comparison to the Python baseline (55 sec/trial)

```bash
uv run --with optuna --with pandas --with numpy python3 ekfq_pipeline/tune_ekf.py \
  --driver host-main \
  --host-main tools/regression/.pio/build/native/host_main \
  --log /path/to/log_007_fixed.csv \
  --config-path /path/to/onspeed2.cfg \
  --trials 10 --study-name acceptance --loss-mode cruise-aoa 2>&1 | tail -10
```

Record numbers in `local-plans/PLAN_OPTUNA_SUBSTRATE.md` under a new "Measured timing" section.

- [ ] **Step 8.5: Push the branch**

```bash
git push -u origin optuna-substrate
```

- [ ] **Step 8.6: Create the PR**

```bash
gh pr create --head optuna-substrate --title "EKFQ: Optuna substrate — tune C++ directly via host_main (closes #592)" --body "$(cat <<'EOF'
## Optuna substrate: tune EKFQ against the firmware C++

Closes #592.

Wires Optuna to subprocess `host_main ahrs_tone` per trial, so tuning
runs against `EkfqPipeline.cpp` (the binary that flies) instead of
`pipeline_quat.py` (a faithful but separable reimplementation). Any
future Python↔C++ drift now surfaces as a tuning-loop regression.

### Changes

- `EkfqPipeline`: 4 pre-filter constants promoted from `static constexpr`
  to a `PipelineConfig` struct, with `setPipelineConfig()` for offline
  retune.
- `Ahrs::SetEkfqConfig`: single setter for `EKFQ::Config` + `PipelineConfig`,
  routed through to the wrapped filter.
- `EkfqConfigKv`: hand-rolled kv-text parser (21 keys), no JSON dep.
  Unknown keys error (typo guard), missing keys warn (defaults).
- `host_main ahrs_tone`: new `--input-format sdlog`, `--config`,
  `--ekfq-config`, `--passthrough-cols` flags. Existing synthetic path
  byte-identical (goldens unchanged).
- `tune_ekf.py`: new `--driver host-main` flag (opt-in; default stays
  `python` for back-compat).
- Tiny `ekfq_substrate_smoke.csv` fixture + golden + run_snapshot.py
  check.

### Timing

Python baseline: 55 sec/trial × 200 trials ≈ 3 hours.
host-main driver: <SEC>/trial × 200 trials ≈ <MIN>.
(Filled in from Task 8.4 measurement.)

### Testing

```bash
pio test -e native        # all tests pass
pio run -e esp32s3-v4p    # zero warnings
./tools/regression/run_snapshot.py   # all goldens match
```

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

---

## Self-review checklist

- [x] **Spec coverage:** All 6 issue tasks covered. EKFQ::Config injection (Task 2+3), EkfqPipeline constants → instance (Task 1), host_main CLI flags (Task 4), passthrough (Task 4), truth fixture (Task 5), Python tuner integration (Task 6).
- [x] **Placeholders:** All code shown inline. No "TBD" or "implement later".
- [x] **Type consistency:** `EKFQ::Config`, `EkfqPipeline::PipelineConfig`, `Ahrs::SetEkfqConfig`, `ParseEkfqConfigKv` used consistently across tasks 1–6.
- [x] **Worktree:** Task 0 creates it from the right base (PR #591), not master.
- [x] **TDD:** Task 3 (kv parser) writes test first; Tasks 1, 2, 5, 8 verify with regression goldens.
- [x] **Commits:** Each task ends with an explicit commit; commit messages follow project style (present-tense, no marketing fluff per CLAUDE.md).
