# OnSpeed offline calibration analysis

A set of Python scripts that re-run the OnSpeed wizard's calibration fit (and several alternatives) against a flight log captured on the SD card, so you can compare fits, diagnose calibration quality, and compute a "what would I load if I'd had more runs and better weighting" recommended config.

**None of this runs on the airplane.** It's pure offline analysis. The firmware is untouched. The goal is to let pilots and developers experiment with calibration strategies on real flight data before committing to any on-device change.

---

## Why this exists

The master calibration wizard (`software/OnSpeed-Gen3-ESP32/Web/javascript_calibration.h`) does a **single run, unweighted OLS** fit each time the pilot hits "Save Calibration". Each save overwrites the previous one. There's no way to know, after a flight, whether the saved calibration was the cleanest run of the day or a slightly noisy one that happened to be the last one you saved.

There are also two unmerged proposals floating around the repo — PR #82 (noise-weighted WLS) and PR #83 (stacked multi-run calibration) — that promise better fits by (a) down-weighting noisy stall-region data, and (b) pooling multiple runs at the same flap setting. Vac's [Stacked Calibration folder](../../docs/../../Stacked%20Calibration) has published benchmark numbers from an 8-run dataset showing small but real improvements.

These tools let us test both proposals on **real flight data** without requiring a firmware rebuild:

1. Ingest an SD-card flight log
2. Re-derive the master wizard's OLS fit as a sanity gate (proves the port matches master)
3. Apply the PR #82/#83 alternatives to the same data
4. Compare six configurations side-by-side: device cfg, OLS single-best, WLS single-best, OLS stacked-all, WLS stacked-all, WLS stacked-KEEP
5. Rank each run by quality so the pilot knows which ones flew well
6. Emit a recommended drop-in cfg patch

The offline pipeline is also **reusable**: the slicer is deterministic, the fits are portable, and the scripts will work against any future OnSpeed log + cfg combo.

---

## What's in this directory

| File | Purpose |
|---|---|
| `analyze.py` | Main analysis pipeline. Ports master's OLS wizard to Python, adds the PR #82 WLS variant and PR #83 stacked variant, slices stall windows out of an SD log, ranks runs by quality, and emits a markdown report + per-flap plots. This is the workhorse. |
| `synthesize_cfg.py` | Compares six calibration-synthesis strategies per flap (cfg, OLS single-best, median-per-run, weighted median, concat stacked, and a "recommended" weighted-median-plus-observed-stall-IAS approach) and prints drop-in XML blocks for the recommended one. |
| `write_patched_cfg.py` | Takes the F recommendation from `synthesize_cfg.py` and produces a complete patched cfg file by regex-replacing the FLAP_POSITION blocks in an existing cfg. Preserves POT_VALUE and all non-flap fields byte-for-byte. Validates the output via re-parse. |
| `plot_best_run.py` | Generates a 4-panel diagnostic plot for the single highest-quality stall run (or the best run at a specific flap setting). Shows IAS/AOA time series, CP→AOA curves, 1/IAS² physics fit, and a numerical summary. |
| `supplement_dynon.py` | Cross-sensor sanity check: compares OnSpeed's pitot IAS against Dynon's `efisIAS` at every detected stall run, and reports Dynon's `efisPercentLift` at the stall-break moment. Useful for validating that the OnSpeed slicer's stall IAS matches what the Dynon also saw. |
| `example_outputs/` | (intentionally empty — each pilot runs against their own data) |

### What the scripts share

All scripts import the `analyze` module, which provides:
- **`parse_config(path)`** — reads an OnSpeed CONFIG2 XML cfg into typed dataclasses
- **`parse_cal_csv(path)`** — reads a wizard-saved calibration CSV (header + body)
- **`detect_stall_windows(log_df, flap_bins)`** — the deterministic slicer
- **`build_runs_from_windows(log_df, windows)`** — Run dataclasses with smoothed arrays
- **`fit_run(run, algo)`** — single-run OLS or WLS fit
- **`fit_stacked(runs, algo)`** — multi-run concat + fit
- **`rank_runs(runs, per_run_ols)`** — quality scoring + KEEP/BORDERLINE/DISCARD verdict
- **`derive_setpoints(K, α₀, α_stall, stall_ias, flap, aircraft, current_weight)`** — port of master's setpoint derivation (NAOA + LDmax/StallWarn/Maneuvering rules)
- **`wls_linear(x, y, w)` / `wls_quadratic(x, y, w)`** — the WLS helpers, also usable as OLS by passing uniform weights

---

## Scientific scope

### Baseline: master's OLS wizard

Master (commit `1d5f7a2` as of this PR) uses the `regression.js` library to do an **unweighted OLS** polynomial fit on each single calibration run:

1. **CP→AOA**: quadratic OLS on `(smoothedCP, DerivedAOA)` — this is the polynomial the firmware evaluates at 50 Hz in flight to convert pressure readings to AOA.
2. **Physics fit**: linear OLS on `(1/IAS², DerivedAOA)` — this yields `K` (slope) and `α₀` (intercept). `α_stall` is computed as `K/Vs² + α₀` where `Vs` is the smoothed IAS at the peak-CP index.
3. **Setpoints**: derived from K/α₀/α_stall via NAOA multipliers (1.35×Vs for OS Fast, 1.25×Vs for OS Slow, Vs+5 kt for Stall Warn, Vs·√G-limit for Maneuvering).

`analyze.py` ports this to Python exactly. The OLS port correctness gate at startup reproduces master's output from each saved cal CSV to within 1% on K, 0.05° on α₀/α_stall, 0.005 on R². Byte-perfect to master's `regression.polynomial(order: 2)` on tested data.

### PR #82 (WLS) and PR #83 (stacked) — applied alongside OLS

These PRs are **unmerged** and exist as design references. Their on-branch code has rebase bugs that would be caught before merge; we ported the *intended algorithm*, not the buggy on-branch code.

- **WLS** uses inverse-variance weights from a 29-sample centered rolling stddev of `DerivedAOA` (sample variance — `ddof=1` — matching Vac's Excel `STDEV.S`). The 29-sample window is tuned for 50 Hz data, which matches what SD logs provide natively.
- **Stacked** concatenates the raw (IAS, smoothed CP, DerivedAOA) arrays from multiple runs at the same flap setting, then fits once over the union. Stall IAS in `α_stall = K/Vs² + α₀` is the mean across the stacked runs.

### The 2×2 ablation matrix

For every flap, `analyze.py` computes **six configurations**:

| Config | Weighting | Pooling | Notes |
|---|---|---|---|
| device cfg | OLS (master) | single run | what was actually saved |
| OLS single-best | OLS | single run (best R²) | reproduces master on best run |
| WLS single-best | WLS | single run (same run) | isolates weighting effect |
| OLS stacked-all | OLS | all runs concat | isolates pooling effect |
| WLS stacked-all | WLS | all runs concat | combined PR #82 + #83 |
| WLS stacked-KEEP | WLS | KEEP-verdict only | best case with quality filter |

This lets you read the weighting effect (row comparison), the pooling effect (column comparison), and the combined effect (diagonal), plus see whether quality-filtering adds further value.

### Quality ranking

Each run scored 0–1 on five components, weighted:

- IAS-AOA R² × 0.25
- CP-AOA R² × 0.25
- Decel-rate stability × 0.20 (target 1.0 kt/s ± 0.6 per the docs — though empirically slower is fine)
- Window length × 0.15 (target ≥ 300 points = 6 s at 50 Hz)
- K-outlier penalty × 0.15 (penalized if run's K is >15% from the flap-median)

Verdicts: **KEEP** ≥ 0.75, **BORDERLINE** 0.50–0.75, **DISCARD** < 0.50. DISCARD runs are excluded from aggregation upstream in `synthesize_cfg.py`.

### Recommended synthesis (`synthesize_cfg.py`'s F option)

The "highest R²" single run is a fragile estimator — one lucky run dominates. The F option instead:

1. Takes the **R²-weighted median** of K and α₀ across all KEEP runs at each flap. Linear-fit coefficients are decoupled enough that per-run median is statistically robust.
2. Computes `α_stall` from the **median observed stall IAS** across runs (both pitots agreed on these in the validation flight — see `supplement_dynon.py`), not from any single run's extrapolation.
3. Fits the CP polynomial **once on the concatenated KEEP-run data** with OLS. Quadratic coefficients are coupled, so a median-across-runs on (a2, a1, a0) gives a polynomial that doesn't pass through any run's data cleanly — concat-and-fit is the right move.

---

## How to run

All scripts take `--log` and `--cfg` arguments — no hard-coded paths. The typical workflow is:

### 1. Full analysis with report and plots

```bash
uv run --with numpy --with scipy --with pandas --with matplotlib python3 \
    analyze.py \
    --log     ~/Downloads/log_117.csv \
    --cfg     ~/Downloads/sam_onspeed.cfg \
    --cal-dir ~/Downloads/cal_files \
    --out-dir /tmp/cal_analysis
```

Outputs:
- `REPORT.md` with the OLS port gate, slicer detection report, run quality ranking, 2×2 ablation tables per flap, setpoint IAS tables, gap analysis, and a proposed cfg patch block per flap
- `figs/flap{NN}_fits.png` — per-flap (1/IAS², DerivedAOA) scatter with all six fits overlaid
- `figs/flap{NN}_cp_curve.png` — per-flap (CP, AOA) scatter with all six curve fits
- `tables/*.csv` — per-run fits, quality rankings, setpoint IAS tables as CSVs for further analysis

The `--cal-dir` is optional — if present, the script runs the OLS port correctness gate against each saved cal CSV to verify the Python port matches master's fit. If omitted, the gate is skipped with a warning.

### 2. Synthesis comparison + drop-in XML blocks

```bash
uv run --with numpy --with scipy --with pandas --with matplotlib python3 \
    synthesize_cfg.py \
    --log    ~/Downloads/log_117.csv \
    --cfg    ~/Downloads/sam_onspeed.cfg \
    --weight 2190
```

Prints the six-strategy comparison per flap, setpoint IAS table, inter-flap gap analysis, and recommended `<FLAP_POSITION>` XML blocks to stdout.

### 3. Generate a complete patched cfg file

```bash
uv run --with numpy --with scipy --with pandas --with matplotlib python3 \
    write_patched_cfg.py \
    --log    ~/Downloads/log_117.csv \
    --cfg    ~/Downloads/sam_onspeed.cfg \
    --weight 2190 \
    --out    /tmp/sam_onspeed_recommended.cfg
```

Produces a drop-in replacement cfg file with the F recommendation applied. All non-FLAP_POSITION fields are preserved byte-for-byte. POT_VALUE is preserved per flap from the original. The output is re-parsed after writing as a sanity check.

### 4. Single-run diagnostic plot

```bash
uv run --with numpy --with scipy --with pandas --with matplotlib python3 \
    plot_best_run.py \
    --log  ~/Downloads/log_117.csv \
    --cfg  ~/Downloads/sam_onspeed.cfg \
    --out  /tmp/figs \
    --flap 33
```

Picks the highest-quality run (optionally at a specific flap) and plots a 4-panel figure: IAS time series with DerivedAOA overlay, CP→AOA scatter with all three polynomials, 1/IAS² physics fit with IAS overlay, and a numerical summary panel.

### 5. Dynon cross-check

```bash
uv run --with numpy --with scipy --with pandas --with matplotlib python3 \
    supplement_dynon.py \
    --log ~/Downloads/log_117.csv \
    --cfg ~/Downloads/sam_onspeed.cfg
```

Prints a per-run table comparing OnSpeed IAS vs Dynon `efisIAS` at every stall point, plus Dynon's PercentLift reading at the stall break.

---

## Limitations and known gaps

- **Not yet tested against logs from aircraft other than N720AK.** The slicer was tuned on RV-10 data. It may need threshold adjustments for aircraft with different stall characteristics (e.g. `MAX_RPM_THROTTLED = 2800` is RV-10-specific; a Lycoming IO-540 with coarser prop pitch might windmill slower, while a constant-speed 4-cyl could spin faster).
- **Slicer heuristics are rough.** MAP-drop → dive peak → monotonic decel → stall-break-at-peak-CP works well for deliberate calibration runs but will miss unusual stall entries (e.g. accelerated stalls, power-on stalls, approach-to-landing stalls that don't start from cruise).
- **No unit tests yet.** The OLS port correctness gate serves as a regression test against master's output, but there's no synthetic data suite.
- **WLS on low-rate data is in a degraded regime.** The 29-sample window is tuned for 50 Hz. If someone saves wizard CSVs from master (which is still rate-limited to 10 Hz over WebSocket — see PR #1 of the stacked trilogy), applying WLS to those CSVs runs outside the designed regime. The SD-log pipeline is at 50 Hz so this only affects the optional CSV sanity check.
- **The recommended synthesis hasn't been flight-tested.** I have a validated drop-in cfg patch for one specific flight on N720AK, but the improvement hasn't been flown yet.
- **α_stall sensitivity in flap 33°.** Even when K and α₀ are stable across runs (< 5% K spread, < 0.4° α₀ spread), per-run α_stall varies by 2–3°. This is a real data characteristic, not a bug — the stall-break point is naturally noisy. The F recommendation handles it by tying α_stall to the median observed stall IAS rather than averaging per-run α_stall values.
- **Flap-0 clean-stall speed calibration is unverified against published RV-10 data.** My current extrapolation gives 69 kt at gross, higher than commonly-quoted values (58–62 kt), but the published numbers lack verified test-weight attribution. See `REPORT.md` methodology section for details.

---

## Design docs and references

- **`docs/SETPOINT_TUNING_UI.md`** — explains the physics (NAOA, K, α₀, α_stall) and setpoint derivation rules
- **`docs/SETPOINT_IMPROVEMENT_PLAN.md`** — alpha_0 / alpha_stall analysis that informed the physics fit
- **`Stacked Calibration/`** — Vac's original 8-run Excel prototype, WLS VBA macros, and design docs (CLAUDE.md, STACKED_CALIBRATION_ANALYSIS.md, MIGRATION_PLAN.md)
- **`software/OnSpeed-Gen3-ESP32/Web/javascript_calibration.h`** (master) — the OLS wizard being ported
- **`software/OnSpeed-Gen3-ESP32/Web/javascript_calibration.h` @ rev 74c8f1b** — the WLS + stacked spec (not merged, has rebase bugs)
- **`software/OnSpeed-Gen3-ESP32/Web/javascript_calibration.h` @ rev c39f23f** — the WLS-only spec (PR #82 before PR #83 was added)

---

## Open questions for reviewers

1. **Should this land as-is or should the F synthesis be productized in the on-device wizard?** Currently it's a post-flight offline tool. Making it on-device would require firmware changes to (a) keep multiple runs in memory, (b) compute the weighted median across them, (c) persist stall IAS across runs. All doable but non-trivial.
2. **Is the 2×2 ablation the right framing?** Another natural framing would be a 3-way comparison (master OLS baseline, PR #82 WLS alone, PR #83 stacking alone) plus a combined case. The 2×2 framing lets you read off both single-variable effects.
3. **Should WLS use sample variance or population variance?** Vac's Excel uses `STDEV.S` (sample, `ddof=1`), matching his published benchmark numbers. The 74c8f1b JS port uses population (`sum/cnt`, `ddof=0`). I ported sample to match Vac's intent; the 74c8f1b choice would be caught on rebase.
4. **Is `compute_recommendation`'s "weighted median K + α₀, observed-Vs α_stall" synthesis the right recommendation?** It's more robust than highest-R² single-best and more physically grounded than concat-stacked (which produces implausibly-low α_stall values for flap 0° on some datasets). But it's ad-hoc — I haven't found a published precedent.
5. **Should there be a minimum run count before the F recommendation is trusted?** Currently it falls back to BORDERLINE runs if fewer than 2 KEEP runs exist. For flap settings with only 1 or 2 runs, the recommendation is little better than a single-best pick.
6. **The 16°→33° transition gap issue surfaced during analysis.** None of the alternative fits close the 6 kt dead zone — it's a setpoint-design issue (LDmax pinned to Vfe for flap>0, 1.35/1.25 multipliers producing a gap by construction). Should the calibration tool flag this, or is it a separate wizard-UX conversation?

---

## Glossary

| Term | Meaning |
|---|---|
| `K` | Lift sensitivity coefficient (deg·kt²). Slope of `DerivedAOA vs 1/IAS²`. |
| `α₀` | Zero-lift fuselage AOA (deg). Intercept of the physics fit. Typically negative. |
| `α_stall` | Fuselage AOA at stall (deg). Computed as `K/Vs² + α₀`. |
| `Vs` | Stall IAS at current weight. `√(K / (α_stall - α₀))`. |
| NAOA | Normalized AOA, `(AOA - α₀) / (α_stall - α₀)`. Dimensionless 0..1. |
| `CP` | Pressure coefficient, `P45Smoothed / PfwdSmoothed` from the firmware. |
| OLS | Ordinary Least Squares — unweighted polynomial fit. Master's current method. |
| WLS | Weighted Least Squares — inverse-variance-weighted fit from PR #82. |
| Stacked | Multi-run concat per flap from PR #83. |
| KEEP/BORDERLINE/DISCARD | Quality ranking verdicts from `rank_runs()`. |
