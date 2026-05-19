# EKFQ tuning pipeline

Python reference implementation of the 11-state quaternion EKF (EKFQ)
that the firmware in
`software/Libraries/onspeed_core/src/ahrs/EKFQ.{h,cpp}` consumes, plus
the Optuna-based tuning driver that produced its `Config::defaults()`
parameters.

The firmware port preserves this filter's math byte-for-byte, so any
retune done here drops into a new `<EKFQ>` config-file section without
algorithm changes.

## Layout

```
ekfq_pipeline/
├── onspeed_ekf/
│   ├── ekf_quat.py            ← EKFQ algorithm (canonical reference)
│   ├── pipeline_quat.py       ← signal chain (install bias, EMA,
│   │                            comp-fade, IAS-alive gating, replay loop)
│   └── data.py                ← log CSV loader + VN-300 freshness mask
├── tune_ekf.py                ← Optuna driver
├── analysis/
│   ├── run_best.py            ← replay best-trial params, emit CSVs
│   └── out/
│       └── ekfq_v15.zip       ← v15 best-trial output (attitude, vz,
│                                kinematic-α, β, biases, residuals;
│                                aligned at 208 Hz)
├── studies/
│   └── ekfq_v15.db            ← Optuna study (200 trials, cruise-AOA loss)
└── clean_and_interpolate.py   ← timestamp regeneration for raw logs
```

## What you need to reproduce the v15 study

1. **Python deps** (any recent version of each):
   `numpy pandas optuna`. The driver also benefits from `tqdm` for
   the progress bar but doesn't require it.

2. **Cleaned flight log** at `log_007_fixed.csv` in the pipeline root.
   The raw 208 Hz testbed log is ~350 MB so it's NOT shipped in this
   repo. Source it from the OnSpeed shared drive, or feed any
   ground-truthed flight log with the same column set through
   `clean_and_interpolate.py` first (it backfills the 1 ms timestamps
   to fractional milliseconds, interpolates missing samples, and
   guards against the giant-timestamp corruption that prevents a clean
   208 Hz reconstruction).

3. **Run the tuner:**
   ```bash
   python3 tune_ekf.py \
     --study-name ekfq_v15 \
     --trials 200 \
     --loss-mode cruise-aoa
   ```
   Roughly 3 hours of wall-clock on a modern laptop. Studies are stored
   as SQLite in `studies/<study-name>.db` and are resumable — re-running
   the command keeps the existing trials and continues.

4. **Inspect the best params + write analysis CSVs:**
   ```bash
   python3 analysis/run_best.py --study-name ekfq_v15
   ```
   Outputs go to `analysis/out/<study-name>/{attitude,vertical,alpha,beta,biases,residuals}.csv`.

## Loss profiles

- `default` — 1:1:1 value weighting, 1.5 rate weights, 0.1 aerobatic
  regime weight. Decent everywhere; trades cruise accuracy for
  aerobatic accuracy.
- `cruise-pitch` — Pitch-only focus. Pitch Huber knee 0.5°, weight 4×;
  aerobatic regime ignored.
- `cruise-aoa` — **Production loss.** Equal 4× weight on pitch and vz
  value residuals plus an explicit kinematic-α residual that catches
  pitch/vz trade-offs. Aerobatic samples ignored; super-cruise sub-regime
  (|θ|≤10°, |φ|≤15°) gets a 2× boost so the heart of the AOA-calibration
  envelope dominates the loss.

## v15 results

The `cruise-aoa` study converged at (full-log VN-300 cruise RMS):

| channel | RMS | mean bias |
|---|---|---|
| pitch | 1.58° | −0.24° |
| roll | 3.02° | +0.61° |
| vz | 0.61 m/s | +0.10 m/s |
| kinematic-α | 1.76° | −0.15° |

Best-trial parameters are the values baked into
`EKFQ::Config::defaults()` and `OnSpeedConfig::LoadDefaults()` in the
firmware tree.

## Sign and unit conventions

EKFQ inputs are standard NED-body aerospace:

- accels in m/s², level flight gives `az ≈ −g`
- gyros in rad/s, `+p` = right-wing-down, `+q` = nose-up, `+r` = right-turn
- baro altitude in m, +up
- TAS in m/s

The firmware's IMU pipeline emits a different "+1g level" reaction-force
convention; `pipeline_quat.py` does the sign flip on the firmware-frame
inputs to match the standard convention EKFQ expects.

## Truth alignment

VN-300 truth is logged at ~50 Hz alongside the 208 Hz IMU. The pipeline
masks loss to VN-300 fresh samples only (via the `vnDataAge` reset
detector) so we never score the EKF against held-stale truth values.
