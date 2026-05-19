# Snapshot Regression Harness

Catches behavior regressions in `onspeed_core` that unit tests miss. Used as
a gate during the core extraction refactor — platform code belongs in the
sketch, pure logic in `onspeed_core/`.

## What it does

Builds `host_main.cpp` against the current `onspeed_core` (via PlatformIO
`[env:native]`) and runs a series of golden checks. All must pass; none
short-circuits the others.

### 1. `ahrs_tone` — AHRS + ToneCalc regression (bedrock)

- Feeds `fixtures/short_replay.csv` (simplified sensor CSV:
  `ias_kt,palt_ft,oat_c,ax,ay,az,gx,gy,gz`) to `host_main ahrs_tone`.
- Diffs the output against `fixtures/golden.csv`.
- Exercises the Madgwick AHRS, Kalman altitude/VSI filter, and ToneCalc
  pipeline end-to-end.
- Designated "the bedrock of behavior regression" in
  `PLAN_PYTHON_CONSOLIDATION.md` (line 416-418). Post-`PLAN_WASM_CORE.md`,
  the same harness runs against the WASM build for parity.

### 2. `replay_engine` — LogReplayEngine regression

- Feeds `fixtures/replay_engine_input.csv` (real SD log format:
  `timeStamp,Pfwd,...,DerivedAOA,CoeffP`) to `host_main replay`.
- Diffs the output against `fixtures/replay_engine_golden.csv`.
- Exercises `LogReplayEngine`: AOA smoothing, flap index lookup,
  pressure-coefficient computation, and all pass-through log fields.
- The primary gate for PRs 2/3 of `PLAN_FIRMWARE_LOG_REPLAY_PARITY.md`.

### 3. `ekfq_substrate_smoke` — Optuna substrate parity

- Feeds `fixtures/ekfq_substrate_smoke.csv` (150-row extract of the
  testbed flight log, real SD log format) + `fixtures/ekfq_substrate_smoke.cfg`
  to `host_main ahrs_tone --algorithm ekfq --input-format sdlog`.
- Diffs against `fixtures/ekfq_substrate_golden.csv`.
- Exercises the same code path `ekfq_pipeline/tune_ekf.py --driver host-main`
  drives per Optuna trial. Catches Python↔C++ drift in the EKFQ port
  before it surfaces in a tuning run.

## `ahrs_tone` input formats

`ahrs_tone` accepts two CSV layouts via `--input-format`:

- `simple` (default): the 9-column synthetic fixture format used by
  `short_replay.csv` — `ias_kt,palt_ft,oat_c,ax,ay,az,gx,gy,gz`.
- `sdlog`: real OnSpeed SD logs (same format as `replay_engine_input.csv`).
  Used by the Optuna substrate (`ekfq_pipeline/`).

### sdlog input format

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
  install bias (`pitchBiasDeg`, `rollBiasDeg`). Required with
  `--input-format=sdlog`.
- `--ekfq-config PATH`: per-trial Q/R/pipeline override kv file. See
  `software/Libraries/onspeed_core/src/ahrs/EkfqConfigKv.h` for the
  format. Optional; defaults if absent.
- `--passthrough-cols A,B,C`: comma-separated input column names. The
  raw values are appended to each output row as `passthrough_<name>`
  columns. Used so a Python scorer can read truth columns aligned to
  the per-row AHRS outputs.

## Tolerance model

Uses `math.isclose(a, b, rel_tol=rtol, abs_tol=atol)` — a match if EITHER the
relative or absolute difference is within tolerance.

Default tolerances:

- `atol = 1e-4` — matches the `%.4f` CSV wire precision. Two values that
  round to the same 4-decimal string stay within atol.
- `rtol = 1e-3` — absorbs cross-platform FP nondeterminism (macOS libc++ vs
  Linux libstdc++ produce up to ~3e-4 relative drift on Madgwick/Kalman
  intermediate values when the harness feeds non-physical input data; see
  issue #204). Real math regressions show up at much larger magnitudes.

An absolute-only tolerance breaks at both extremes: too loose on small
values, too tight on large ones. `math.isclose` correctly handles both.

## Usage

```bash
# Run both regression checks
./run_snapshot.py

# Regenerate both goldens after an intentional behavior change
./run_snapshot.py --update-golden

# Override tolerances (useful for debugging a near-miss diff)
./run_snapshot.py --rtol 1e-4 --atol 1e-3

# Skip rebuild — use existing binary
./run_snapshot.py --no-build

# Manual: run ahrs_tone subcommand directly
./.pio/build/native/program ahrs_tone \
    --input fixtures/short_replay.csv \
    --output-format csv | head -5

# Manual: run replay subcommand with 208 Hz log rate
./.pio/build/native/program replay \
    --input fixtures/replay_engine_input.csv \
    --log-rate 208 --output-format csv | head -3
```

## When to run

- Before flashing a core-extraction PR to the bench.
- In CI on every PR that touches `software/Libraries/onspeed_core/` or the
  sketch-side consumers of core modules.

## How the pipeline grows

Each PR that moves a new module into `onspeed_core` or changes LogReplayEngine
behavior extends `host_main.cpp` and regenerates the affected golden(s) as
part of its commit history.

- `ahrs_tone` goldens (`golden.csv`, `golden_ekfq.csv`): update when
  AHRS, Kalman, or ToneCalc math changes.
- `replay_engine` golden (`replay_engine_golden.csv`): update when
  LogReplayEngine, FlapsDetector, or AOA smoothing changes.
- `ekfq_substrate` golden (`ekfq_substrate_golden.csv`): update when
  the EKFQ + EkfqPipeline + sdlog input adapter behaviour changes.
