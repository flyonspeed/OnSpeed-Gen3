# PLAN — Optuna substrate: tune EKFQ against firmware C++

**Date:** 2026-05-19
**Owner:** Sam
**Status:** Spec — ready for implementation plan
**Issue:** [#592](https://github.com/flyonspeed/OnSpeed-Gen3/issues/592)
**Predecessor:** PR #591 (EKFQ replaces EKF6)
**Reference:** Lenny's pipeline at https://github.com/flyonspeed/OnSpeed-Gen3/tree/EKFQ-tuned/ekfq_pipeline

## TL;DR

Optuna currently tunes EKFQ by replaying flights through a Python reimplementation (`PipelineQuat`) of the firmware's `Ahrs::Step` + `EkfqPipeline` path. PR #591 ported that algorithm into C++ verbatim. The risk now is silent Python↔C++ drift: a future bug fix or refactor on either side moves the trained behaviour without anybody noticing until a pilot complains.

This plan wires Optuna to subprocess `host_main ahrs_tone` per trial, replaying through the firmware's actual C++ code with per-trial config injection. Loss math stays in Python (composite_loss + regime masking) — the C++ binary is a pure replay engine.

**Net effect:** any Python↔C++ divergence surfaces as a tuning-loop regression instead of a flight surprise.

**Baseline (Python, full 79-min log, cruise-aoa loss):** 55 sec/trial × 200 trials ≈ 3 hours. C++ replay should be 5–10× faster.

## What's missing today

After PR #591:

| Gap | Where | Blocks |
|---|---|---|
| `EKFQ::Config` (17 params) is compile-time only | `EKFQ.cpp::Config::defaults()` | Per-trial Q/R/p override |
| `EkfqPipeline`'s 4 pre-filter constants are `static constexpr` | `EkfqPipeline.h` | Per-trial signal-chain α/τ override |
| `host_main ahrs_tone` reads a synthetic 9-column CSV only | `host_main.cpp` | Real SD log replay |
| No way to passthrough truth columns to output | `host_main.cpp` | Python-side scoring |
| Loss math reimplements `Ahrs::Step` stage 1 (install bias, gyro bias) | `pipeline_quat.py` | Drift risk |
| No truth fixture in repo | `tools/regression/fixtures/` | CI smoke |

## Architecture

```
Optuna trial (Python tune_ekf.py)
  │
  ├── trial.params → /tmp/trial_NNN.kv
  │
  ▼
host_main ahrs_tone --algorithm ekfq                       (one subprocess per trial)
                    --input-format sdlog
                    --input log_007_fixed.csv              (real 208 Hz SD log)
                    --config onspeed2.cfg                  (per-aircraft install bias)
                    --ekfq-config /tmp/trial_NNN.kv        (Q/R/p + signal-chain α/τ)
                    --passthrough-cols vnPitch,vnRoll,vnVelNedDown,vnDataAge,IAS,TAS,AngleofAttack,Palt
                    --output-format csv
  │
  ▼
stdout: per-row AHRS state + passthrough truth columns (~30 columns)
  │
  ▼
Python composite_loss(history, df, mask_fresh_vn, ...) → scalar
```

One process per trial. Process spawn overhead ~1 ms; negligible against 5–10 sec replay.

## Decisions

### 1. Install bias comes from the per-aircraft .cfg, not trial params

Lenny's `PipelineQuat` reads `pitch_bias_deg`/`roll_bias_deg` from the testbed's `onspeed2.cfg` and pins them. The firmware does the equivalent — `Ahrs::Step` reads `cfg_.pitchBiasDeg`/`cfg_.rollBiasDeg`. The new path follows suit: `host_main --config <path>` loads the same V1/V2 OnSpeedConfig the firmware uses and feeds it through `AhrsConfig`. Optuna only tunes the EKFQ + signal-chain knobs.

**Why not tune install bias too:** Optuna would compensate for a poorly-measured calibration by drifting the bias values, conflating the calibration-wizard's job with the filter-tuner's job. Keep them separate.

Lenny's pipeline also has `gx_bias_dps`/`gy_bias_dps`/`gz_bias_dps` — empirically pinned to 0 (the cfg values are bad). The firmware likewise never subtracts cfg.Gx/Gy/Gz. **No change needed here**; we just don't expose them.

### 2. EKFQ::Config and EkfqPipeline pre-filter constants both become trial params

The 17 EKFQ::Config fields and the 4 EkfqPipeline constants (`kAccelEmaAlpha`, `kCompFadeTauSec`, `kIasGateRisingKt`, `kTasdotEmaAlpha`) are both tuned in Lenny's study v15. Both need a per-trial setter:

- `EKFQ::setConfig(const Config&)` already exists. Wire it through `Ahrs::SetEkfqConfig(...)`.
- `EkfqPipeline`'s constants become a `PipelineConfig` struct member, defaulted to v15 values. Add `EkfqPipeline(const PipelineConfig&)` overload, plus a setter for the per-trial path.

### 3. Loss scoring stays in Python

The composite loss (Huber knees, regime masks, kinematic-α residual, train/val split) is in flux as we explore loss profiles. Porting that to C++ would slow iteration on the part that's still evolving. Each trial's host_main writes AHRS state + passthrough truth columns; Python runs `composite_loss()` unchanged.

### 4. Truth fixture is a user-supplied path

No git-lfs. `tune_ekf.py --log <path>` defaults to a relative path the user is expected to symlink/copy. CI gets a tiny committed synthetic fixture (~few-second deterministic sequence with VN columns) to verify the wiring, not the tuning quality.

### 5. The `--driver` flag is opt-in

The Python tuner gets `--driver {python|host-main}` defaulting to `python` for back-compat. Once parity is verified on a real study, a follow-up PR flips the default to `host-main`. This keeps the substrate PR landable without invalidating any in-flight tuning runs.

### 6. Python `PipelineQuat` and `EKFQ` stay around

They become the parity reference. Two independent implementations of the same algorithm is exactly how you catch a port bug. They're a parity-test target, not a tuning target.

## File-by-file

### onspeed_core changes (~120 LOC)

**`software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.h`** — promote pipeline constants
- New `struct EkfqPipeline::PipelineConfig { float accelEmaAlpha; float compFadeTauSec; float iasGateRisingKt; float tasdotEmaAlpha; static PipelineConfig defaults(); }`
- `static constexpr float kAccelEmaAlpha = ...` → instance member `accelEmaAlpha_` initialised from PipelineConfig
- Same for the other three
- Keep `kIasGateFallingKt` derived from `iasGateRisingKt - 5.0f` (the 5 kt hysteresis matches Python)
- New `EkfqPipeline(const PipelineConfig&)` constructor overload; existing default-ctor calls `PipelineConfig::defaults()`
- New `void setPipelineConfig(const PipelineConfig&)` — does NOT reset EMA state (mid-flight config change can't snap the filter)

**`software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.cpp`** — wire it up. PipelineConfig::defaults() returns the v15 values currently in the header.

**`software/Libraries/onspeed_core/src/ahrs/Ahrs.h` + `.cpp`** — new setter
- `void Ahrs::SetEkfqConfig(const EKFQ::Config& ekfqCfg, const EkfqPipeline::PipelineConfig& pipeCfg)` forwards to `ekfq_.setConfig()` + `ekfq_.setPipelineConfig()`

**`software/Libraries/onspeed_core/src/ahrs/EkfqConfigKv.h` + `.cpp`** (new) — parser
- `bool ParseEkfqConfigKv(std::string_view text, EKFQ::Config& outEkfqCfg, EkfqPipeline::PipelineConfig& outPipeCfg, std::function<void(const char*)> warnSink)`
- Format: `key=value` per line, `#` comments, blank lines OK
- Keys: 17 EKFQ::Config fields + 4 PipelineConfig fields = 21 keys total
- Unknown keys → error (false return, sink writes the bad key)
- Missing keys → warning (sink writes; field stays at default)
- Hand-rolled, no JSON dep. Errors return false.

**`test/test_ekfq_config_kv/`** — unit tests for the parser (round-trip, unknown key, missing key, empty, comments)

### host_main changes (~150 LOC)

**`tools/regression/host_main.cpp`** — extend `ahrs_tone` subcommand

- New flag `--input-format {synthetic|sdlog}` (default: `synthetic`). `sdlog` mode:
  - Reuses `BuildHeaderIndex` from the `replay` subcommand
  - Maps logged columns → `AhrsInputs` via a new `BuildAhrsInputsFromLogRow()` helper. Maps:
    - `ForwardG/LateralG/VerticalG` → `in.imu.accel{X,Y,Z}G`
    - `RollRate/PitchRate/YawRate` → `in.imu.gyro{Roll,Pitch,Yaw}Dps` (with PitchRate sign re-flip — `LogCsv::FormatRow` flips on write)
    - `IAS/Palt/OAT` → `in.sensors.{iasKt,paltFt,oatCelsius}`
    - `TAS` → density-corrected; reused if present, otherwise computed from IAS+Palt+OAT (firmware path)
  - `iasUpdateTimestampUs` synthesized from `PfwdSmoothed` diff (matches Lenny's fresh-pressure heuristic): when `PfwdSmoothed[i] != PfwdSmoothed[i-1]`, bump the timestamp

- New flag `--config PATH`. Reuses `LoadConfig` from the `replay` subcommand (refactor it into a shared helper at the top of the file). Sets `AhrsConfig::pitchBiasDeg` and `AhrsConfig::rollBiasDeg`.

- New flag `--ekfq-config PATH`. Calls `ParseEkfqConfigKv()` on the file contents, calls `ahrs.SetEkfqConfig(...)` before the replay loop starts.

- New flag `--passthrough-cols A,B,C,...`. Splits on `,`. For each name, look up its column index in the `HeaderIndex` (or fail loudly if not present). The output CSV header becomes the existing `kAhrsToneOutputHeader` + `,passthrough_<name>` per requested column. Each row appends the raw input column value, formatted as `%.6g`.

- Update `--input-format synthetic` path to be unchanged byte-for-byte (existing fixture must still match). The synthetic path is the regression baseline; the sdlog path is the tuning baseline.

### Python tuner changes (~80 LOC)

**`ekfq_pipeline/run_host_main.py`** (new) — subprocess wrapper
- `run_host_main(host_main_path, log_path, config_path, ekfq_cfg, pipe_cfg, passthrough_cols) -> pd.DataFrame`
- Writes ekfq_cfg + pipe_cfg to a temp `.kv` file
- Runs subprocess with check_output, parses stdout via `io.StringIO + pd.read_csv`
- Returns a DataFrame with AHRS state + passthrough columns
- Raises `RuntimeError` (caught upstream as `math.inf` loss) on non-zero exit

**`ekfq_pipeline/tune_ekf.py`** — add the host-main driver path
- New `--driver {python|host-main}` flag, default `python`
- New `--host-main` flag pointing at the compiled `host_main` binary (default `tools/regression/host_main`)
- New `--config-path` flag for the per-aircraft .cfg (no default — required when `--driver host-main`)
- New `objective_host_main()` parallel to existing `objective()`:
  1. Build `EKFQConfig` and (new) `PipelineQuatConfig` instances from trial params (existing code does this)
  2. Call `run_host_main(...)` to get the per-row AHRS state DataFrame
  3. Reconstruct `history` dict from the DataFrame (pitch_deg → `pitch_deg` column, etc.)
  4. Call existing `composite_loss(history, df, mask_fresh_vn, ...)`
- `make_objective()` picks `objective_python` or `objective_host_main` based on `args.driver`

### Acceptance harness (~30 LOC)

**`tools/regression/test_optuna_parity.py`** (or extend existing) — parity check
- Build an empty `.kv` file (all defaults)
- Run `host_main ahrs_tone --input-format sdlog --input <tiny synthetic with VN cols> --algorithm ekfq --ekfq-config <empty .kv> --passthrough-cols vnPitch,vnRoll`
- Compare output to a committed `golden_ekfq_sdlog.csv` (small, deterministic, ~150 rows)
- Use `rtol=1e-3, atol=1e-4` per the issue's acceptance criteria

### Docs (~50 LOC)

**`tools/regression/README.md`** — document the new flags

**`ekfq_pipeline/README.md`** — add a "Driver: host-main" section explaining the subprocess path and the `--driver` flag

## Logging-completeness verdict

**The 208 Hz SD log is sufficient for Optuna against EKFQ as-is.** Three things to know:

1. **Fresh-VN masking** uses the `vnDataAge`-reset heuristic Lenny built (no dedicated column needed). The 50 Hz VN-300 sample arrives, `vnDataAge` resets to ~0, then climbs monotonically until the next sample. `diff < 0` → fresh.
2. **Fresh-pressure masking** uses `PfwdSmoothed` diff (also Lenny's heuristic). Adequate because the 50 Hz pressure sensor produces a different smoothed value every fresh sample.
3. **dtSec** is reconstructed from `timeStampUs - prevTimeStampUs`. `timeStampUs` was added to the log in a prior PR (column index 2). At 208 Hz with µs resolution, the dt jitter is ~ns — well below the EKF tolerance.

**One nice-to-have for the future, not for this PR:** emit `iasUpdateTimestampUs` as a log column so we can drop the `PfwdSmoothed` diff heuristic. ~10 LOC, but adds a log-format-version bump and an end-of-VN-data dependency. Defer until we have a concrete tuning regression that traces back to the heuristic.

## Out of scope

- Re-tuning EKFQ for production. This issue is *infrastructure* to enable re-tuning; the next study run is separate (and waits on real flight data).
- Reworking composite_loss shape or loss profiles.
- Multi-fixture support (tune against multiple logs in one run).
- Auto-detecting `iLogRate` from the log header — `--log-rate` stays a required flag, but for tuning it's always 208.
- Web-UI exposure of `EkfqPipeline::PipelineConfig`. Pilots don't tune these; Optuna does.

## Acceptance criteria (from Issue #592)

- [ ] `host_main ahrs_tone --input-format sdlog --input <real.csv> --config <cfg> --ekfq-config <kv> --passthrough-cols <list>` produces a row-aligned output CSV with AHRS state + passthrough columns.
- [ ] Empty `.kv` file (all keys missing, fall back to defaults) reproduces compile-time-default behaviour to within `rtol=1e-3, atol=1e-4`.
- [ ] `tune_ekf.py --driver host-main` runs end-to-end and completes at least one trial against a small committed fixture (CI smoke).
- [ ] **Timing:** measured sec/trial on the full 79-min log via host-main driver, reported in the PR description. Target: at least 3× faster than the Python baseline (55 sec/trial).
- [ ] Short README documents how to launch a new Optuna run via either driver.

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| `BuildAhrsInputsFromLogRow` drifts from the firmware's `Ahrs::Step` sensor-stage interpretation of the same columns | Same regression-snapshot pattern as everything else in onspeed_core: golden CSV gated in CI. |
| Subprocess overhead dominates on short fixtures | Acceptance: measured per-trial cost on the 79-min log goes in the PR description. Short fixtures use the synthetic path. |
| Python and C++ drift detected only by tuning loss going up | Add an explicit parity test: same input + same kv config → both implementations should produce within `rtol=1e-3` per-row attitude. Wired into CI alongside existing goldens. |
| Refactoring `LoadConfig` into a shared helper breaks `replay` | Both subcommands use the same V1/V2 OnSpeedConfig parser today. Test: existing `run_snapshot.py` still passes. |
| kv-parser unknown-key behaviour (error vs warn) creates friction when adding new fields | Error on unknown keys; tuners must rev the kv file when EKFQ gains a field. This is the safer default — silent ignore would mask typos. |
