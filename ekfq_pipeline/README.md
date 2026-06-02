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
  --host-main ../tools/regression/.pio/build/native/program \
  --log log_007_fixed.csv \
  --config-path onspeed2.cfg \
  --trials 200 --loss-mode cruise-aoa
```

The host-main driver runs trials against the binary that flies, so
any future Python↔C++ divergence in the EKFQ port surfaces as a
tuning regression instead of a silent flight behaviour change.

A 150-row smoke fixture lives at
`tools/regression/fixtures/ekfq_substrate_smoke.{csv,cfg}` (with a
committed golden) and is wired into `tools/regression/run_snapshot.py`
so CI catches drift before a full tuning run picks it up.

### `--n-jobs`: parallel trial execution

Each host-main trial is an independent subprocess, so Optuna can fan
out across cores via `--n-jobs N` (or `--n-jobs -1` for all cores).
The `python` driver shares the GIL and won't benefit; the flag is
host-main-only in practice.

Measured on a 14-core machine, 988k-row log:

| Mode | Wall (10 trials) | 200-trial extrapolation |
|---|---:|---:|
| `--driver python` (serial) | ~9.2 min | ~3.0 hours |
| `--driver host-main` (serial) | ~57 sec | ~19 min |
| `--driver host-main --n-jobs -1` | ~11 sec | ~3.5 min |

Per-trial time rises modestly under parallel load (cache + I/O
contention), but wall time drops by ~5× over serial host-main.

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

## Symbolic source of truth

### What's generated and from where

`onspeed_ekf/symbolic.py` is the source of truth for EKFQ's predict
Jacobian **F** and measurement Jacobian **H**. It builds the symbolic
state-transition and measurement models and derives the Jacobians by
`sympy.diff`. From those expressions it emits two artifacts:

- `onspeed_ekf/_generated_jacobians.py` — NumPy callables `F(x, u, dt)`
  and `H(x, u)`, used by the oracle and parity tests.
- `software/Libraries/onspeed_core/src/ahrs/EKFQ_jacobians.generated.h` —
  C structs and filler functions for the firmware (slotted into the flight
  loop by a later "Move 2" PR, see below).

Regenerate both with:

```bash
cd ekfq_pipeline && uv run --group dev python -m onspeed_ekf.symbolic
```

Both files carry a `DO-NOT-EDIT` banner. CI (`ekfq-jacobian-oracle`)
regenerates them and runs `git diff --exit-code` against the committed
copies, so a hand-edit or a stale checkout fails the build.

### The oracle

F and H are cross-checked three independent ways over randomized in-air
and on-ground states:

1. **SymPy** — the symbolic derivation in `symbolic.py`.
2. **JAX autodiff** — `onspeed_ekf/jax_model.py` differentiates the same
   model through `jax.jacobian`.
3. **Hand-written** — the explicit Jacobian expressions in
   `onspeed_ekf/ekf_quat.py` that the Python twin runs.

`tests/test_jacobian_oracle.py` asserts all three agree. Agreement of
three derivations built from independent machinery is the correctness
gate against hand-derivation algebra errors. `tests/test_cpp_python_parity.py`
adds the C side: it checks the generated NumPy matches SymPy and that
`host_main`'s attitude output matches the Python twin.

### The mean/Jacobian asymmetry

The predict **mean** uses the exact exponential map
`q ← q ⊗ exp(½ ω·dt)`. The predict **Jacobian** F is deliberately the
first-order Jacobian of the *linear* mean `q + q̇·dt`. This is standard
EKF practice: the covariance linearization is first-order, and the exact
map's extra terms are `O((ω·dt)³)`, which the covariance does not track.
The oracle pins this asymmetry — its consistency test asserts
`jacobian(exact) ≈ jacobian(linear)` to ~3e-6 at a 208 Hz step.

### Performance and the generated layer

**Generated = the algebra (what the Jacobian entries *are*). Hand-written
= the schedule (how they're combined and looped).** Performance
optimizations live in the generator's options or in the hand-written
schedule — never as edits to the generated file (the DO-NOT-EDIT banner
plus the CI regenerate-diff gate make hand-edits fail loudly).

On the firmware (C++) — the hot path, 208 Hz on the ESP32-S3 — this split
is load-bearing. The generator owns the per-entry algebra plus CSE,
float-not-double rendering, and integer-power expansion (`x*x`, no `pow`
or `<math.h>`, keeping the header core-pure). The MCU perf — the sparse
`F·P·Fᵀ` that skips identity rows, the scalar-update specializations,
stack-only P — stays hand-written and *consumes* the generated entries;
it survives every regeneration because it is not generated. When you want
a firmware speed or size win, change the generator option or the
hand-written schedule, never the emitted header.

**Emit-size knob (ahead of need).** Fully-unrolled per-entry codegen grows
`.text` as the state count rises (the wall ArduPilot hit with EKF
codegen). The C emitter therefore offers two forms via
`emit_c_header(mode=...)`: named-struct fillers (unrolled, default,
fastest) and array-indexed fillers (rolled, compact `.text`). They compute
identical values; the consumer picks per the size/speed tradeoff. At N=11
unrolled is fine; the knob exists so the choice is a setting, not a
retrofit.

**Move 2 (later PR) flips the firmware to consume the header** and deletes
the hand-written F/H expressions, keeping the sparse multiply. That PR is
gated on a *cost* check — bench the generated path's flop-count, `.text`,
and runtime against the hand-written path — layered on top of this PR's
*correctness* gate. The generated path is proven as-fast and as-small
before it touches the flight loop.

On the Python side, the twin, tuner, and oracle are offline rather than
hot paths, so the generated NumPy is clarity-first (`lambdify` + CSE). The
one perf lever — vectorizing a whole-log pass — is a generator emit
option, reached for only if a tuning sweep is actually too slow.

### Future note

A future v16 Optuna study runs against the exp-map twin. The existing
`Config` defaults were tuned on the linear mean; the difference is
third-order-small, so those defaults remain valid.
