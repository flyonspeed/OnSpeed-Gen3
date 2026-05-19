---
name: optuna-tuning
description: Use when the user asks to tune EKFQ parameters via Optuna, retune the AHRS filter, run a tuning study against a flight log, kick off a new tuning study, reproduce the v15 study, or diagnose why a previous tuning run misbehaved. Covers both the legacy in-process Python driver and the host-main subprocess driver added in #595.
---

# Running an Optuna tuning study for EKFQ

## What this is

`ekfq_pipeline/` holds the Optuna tuner that produces the 17 `EKFQ::Config` + 4 `EkfqPipeline::PipelineConfig` values baked into the firmware via `EKFQ::Config::defaults()` and `PipelineConfig::defaults()`. The tuner replays a real flight log through one of two drivers per trial, scores the output against VN-300 truth via `composite_loss`, and uses Optuna's TPESampler to converge on a best-trial parameter set.

Two drivers exist:

- **`python`** (default, legacy): in-process `PipelineQuat.run()` — a Python reimplementation of the firmware's AHRS stage. Used for the original v15 study.
- **`host-main`** (added in #595): subprocesses the firmware's compiled `host_main` binary per trial via `--input-format=sdlog`. The binary that flies *is* the binary being tuned, closing the Python↔C++ drift loop. Several × faster (see "Driver tradeoffs" below).

The next production retune should use `host-main` unless there's a specific reason not to. The Python driver is preserved as a parity reference.

## When to use this skill

- "Retune EKFQ against the new Vac flight log"
- "Run an Optuna study on log_NNN.csv"
- "Reproduce the v15 study to verify nothing broke"
- "How long will a 500-trial run take?"
- "Why is my tuning study not converging?"
- "I want to try a new loss profile — how do I add one?"

## When NOT to use this skill

- User wants to change the EKFQ algorithm itself (architecture, state vector, measurement equations) — that lives in `software/Libraries/onspeed_core/src/ahrs/EKFQ.{h,cpp}` and is a firmware change, not a tuning study
- User wants to clean / preprocess a raw flight log — that's `ekfq_pipeline/clean_and_interpolate.py`, a one-shot script, not Optuna
- User wants to update the production defaults baked into `EKFQ::Config::defaults()` — that's a manual transcription step *after* a tuning study converges; the study itself doesn't write the firmware

## Iron rules

1. **Always use `--driver host-main` for production retunes unless you have a reason not to.** It runs against the firmware binary; the Python driver runs against a Python sibling that drifts.
2. **Always pass `--config-path` with the per-aircraft `.cfg` file.** The cfg's `<BIAS><PITCH>` and `<BIAS><ROLL>` provide the install bias the EKF needs. Wrong cfg → wrong attitude reference → garbage tuning. The cfg lives next to the flight log on the OnSpeed shared drive.
3. **Always pass `--n-jobs -1`** unless you're debugging. Parallel host-main trials are ~7× faster than serial on a 14-core Mac and the search quality is unchanged (Optuna's TPESampler is parallel-safe).
4. **Don't tune install bias.** `pitch_bias_deg` / `roll_bias_deg` come from the calibration wizard, not the EKF tuner. Lenny's `PipelineQuatConfig` has them as fields but they're pinned at cfg values for production runs. Don't add them to the search space.
5. **Don't retune for a single flight log without validation data.** The TPE sampler will overfit to whatever maneuvers happen to be in the log. Use the train/val split (already wired by `build_train_val_split`) and compare val loss across runs.
6. **Don't ship retuned defaults to firmware without re-verifying calibration.** The per-flap AOA curves were fit against EKFQ's old `DerivedAOA` output. Different `Config` → different `DerivedAOA` → curves need refitting. This is a separate process; document it.

## Quick reference: launch a tuning run

```bash
cd ekfq_pipeline

# Make sure host_main is built (native env, not ESP32):
cd ../tools/regression && pio run -e native && cd -

# Production retune — host-main driver, all cores, full log, cruise-aoa loss:
uv run --with optuna --with pandas --with numpy python3 tune_ekf.py \
  --driver host-main \
  --host-main ../tools/regression/.pio/build/native/program \
  --log /path/to/log_NNN_fixed.csv \
  --config-path /path/to/onspeed2.cfg \
  --trials 200 \
  --n-jobs -1 \
  --study-name ekfq_v16 \
  --loss-mode cruise-aoa
```

On a 14-core M-series Mac with a 988k-row (79-min) log, 200 trials complete in ~3.5 minutes.

Resume an interrupted study by re-running with the same `--study-name` — Optuna stores to SQLite at `studies/<name>.db` and `load_if_exists=True`.

## Driver tradeoffs

| Mode | Wall (10 trials, 988k-row log) | 200-trial extrapolation | Use when |
|---|---:|---:|---|
| `--driver python` | ~9.2 min | ~3.0 hours | Reproducing v15 study, parity reference, you don't have host_main built |
| `--driver host-main` (serial) | ~57 sec | ~19 min | You don't have many cores, or you're debugging a single trial |
| `--driver host-main --n-jobs -1` | ~11 sec | **~3.5 min** | **Default for any production retune** |

The host-main driver requires the compiled `host_main` binary at `tools/regression/.pio/build/native/program`. Build it once per branch with `cd tools/regression && pio run -e native`.

## Inputs you need before starting

1. **A cleaned flight log CSV** at 208 Hz, with VN-300 truth columns (`vnPitch`, `vnRoll`, `vnVelNedDown`, `vnDataAge`). Raw logs from the SD card need `ekfq_pipeline/clean_and_interpolate.py` first to interpolate dropped samples and regenerate fractional-ms timestamps. The cleaned testbed log Vac flew on 2026-05-11 is at:
   `~/Library/CloudStorage/GoogleDrive-sritchie09@gmail.com/.shortcut-targets-by-id/.../Flight Test Data/RV-4 Data/2026-05-11/11 May 26 Cockpit/log_007_fixed.csv`
2. **The per-aircraft `.cfg`** that flew with the log (sits next to the log in the same Drive folder; for the testbed: `onspeed2.cfg`). The cfg's install-bias values get fed to `host_main` via `--config`.
3. **A built `host_main`** (for `--driver host-main`). Build target: `tools/regression/.pio/build/native/program`. Build command: `cd tools/regression && pio run -e native`.

## Loss profiles

`tune_ekf.py --loss-mode {default|cruise-pitch|cruise-aoa}`:

- **`default`**: 1:1:1 value weighting, 1.5 rate weights, 0.1 aerobatic regime weight. Decent everywhere but trades cruise accuracy for aerobatic accuracy. Useful for surveying regimes; not the production loss.
- **`cruise-pitch`**: Pitch-only focus. Pitch Huber knee 0.5°, pitch weight 4×. Useful when only pitch matters (rare).
- **`cruise-aoa`**: **Production loss.** Equal 4× weight on pitch and vz value residuals plus an explicit kinematic-α residual that penalises the compounded error directly. Aerobatic samples ignored; super-cruise sub-regime (|θ|≤10°, |φ|≤15°) gets a 2× boost. Use this for production retunes.

## After a study finishes

The CLI prints the best trial's parameters. To bake them into the firmware:

1. Read the printed `params:` block.
2. Update `software/Libraries/onspeed_core/src/ahrs/EKFQ.cpp::Config::defaults()` with the 17 EKFQ field values.
3. Update `software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.cpp::PipelineConfig::defaults()` with the 4 pipeline values.
4. Rebuild: `pio run -e esp32s3-v4p` should succeed with zero warnings.
5. Run the regression goldens — they will INTENTIONALLY mismatch because the EKFQ output trajectory changes. Regenerate them: `./tools/regression/run_snapshot.py --update-golden` (or generate by hand per the harness's convention).
6. Bump the study name in the commit message so future readers can find the SQLite study DB.
7. Per-flap AOA curves will need refitting — this is a separate calibration-wizard activity, not part of the tuning study itself. Flag this in the commit message.

## Debugging a tuning study

**"All trials return inf loss"** — almost always a path problem. Check that `--log` and `--config-path` resolve, that the cfg is the right one (matches the aircraft that flew the log), and that the log has VN-300 columns (`grep -c vnPitch <log.csv>` should be > 0).

**"Trials complete but losses are all in the 100s"** — bad install bias is the most common cause. Confirm the cfg's `<BIAS><PITCH>` and `<BIAS><ROLL>` aren't 0. Run a single trial with the default seed params and read the breakdown — if `pitch_rms` is over 10° you're not converging on real flight; you're tracking the EKF's drift.

**"Tuning crashes on row 0"** — known issue (see #596). Python's `pipeline_quat.py` initializes `prev_tas_mps=0`, creating a phantom 5 m/s² forward acceleration on the first frame. C++ avoids this by discarding the seed row. If you're stuck on the Python driver, fix line 124 of `pipeline_quat.py`:
```python
prev_tas_mps = float(tas_kt[0]) * _KT_TO_MPS  # was 0.0
```

**"Trials disagree between drivers on the same params"** — partly real (Python row-0 bug, see #596), partly cold-start transients. For aggregate study results both drivers find the same best trial on the full log within ~3% loss; on short slices they diverge more. Don't compare drivers on short slices.

**"How do I check if the host_main subprocess is actually running EKFQ and not Madgwick?"** — invoke it directly with a small input:
```bash
./tools/regression/.pio/build/native/program ahrs_tone \
  --algorithm ekfq \
  --input-format sdlog \
  --input tools/regression/fixtures/ekfq_substrate_smoke.csv \
  --config tools/regression/fixtures/ekfq_substrate_smoke.cfg \
  --passthrough-cols vnPitch \
  | head -5
```
Output should have `pitch_deg, roll_deg, ...` columns. Compare against `tools/regression/fixtures/ekfq_substrate_golden.csv` — they should byte-match (CI gates this).

## Adding a new loss profile

`tune_ekf.py::_LOSS_PROFILES` is the dictionary of profiles. Add a new entry with the same keys as `default` (Huber knees, channel weights, regime weights, alpha-min-tas). Re-run with `--loss-mode <new>`.

If the new profile needs a fundamentally different residual (e.g., scoring against `boomAlpha` instead of `vnPitch`), that's a `composite_loss()` change, not a profile change. Be aware: changing `composite_loss()` invalidates all prior studies that used it for `train_breakdown`/`val_breakdown` comparison.

## Acceptance criteria for a "good" tuning run

Mirror what v15 achieved on the testbed log (RV-4, 2026-05-11):

| channel | RMS | mean bias |
|---|---|---|
| pitch | ≤2° | within 1° of zero |
| roll | ≤4° | within 2° of zero |
| vz | ≤1 m/s | within 0.5 m/s of zero |
| kinematic-α | ≤2° | within 1° of zero |

These are on the val split, with the `cruise-aoa` loss profile, over the full 79-min log. If you can't beat these on a new log/cfg combination, something is wrong with the inputs, not the tuner.

## Related skills, issues, and references

- Spec: `local-plans/PLAN_OPTUNA_SUBSTRATE.md`
- Implementation plan: `docs/superpowers/plans/2026-05-19-optuna-substrate.md`
- PR #595 introduces the host-main substrate
- Issue #596 tracks the Python row-0 tasdot bug + IAS-gate-edge timing parity drift
- Issue #592 is the original substrate issue (closed by #595)
- The original v15 study lives at the `EKFQ-tuned` branch's `ekfq_pipeline/studies/ekfq_v15.db`
- After a successful retune, see also: `docs-update` skill (calibration / DerivedAOA docs may need refresh) and `release-notes` skill (note the retune in the release)
