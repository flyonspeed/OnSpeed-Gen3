# PLAN_FIRMWARE_LOG_REPLAY_PARITY.md — Replay Shows What the M5 Saw

**Date:** 2026-05-08 (v3 — supersedes prior premises; v1 had the wrong filter, v2 added unnecessary log columns).
**Owner:** Sam
**Status:** Proposed.
**Sequenced:** BEFORE `PLAN_WASM_CORE.md` Step 2 (the streaming-pipeline binding).
**Companion to:** `PLAN_WASM_CORE.md`, `PLAN_VIDEO_OVERLAY.md`.

## The thesis

> Replay shows what the M5 saw. The log captures raw IMU at 50 Hz;
> the wire's slip-ball lateral-g comes from a 208 Hz EMA in the AHRS
> engine. Replay can't perfectly reproduce the wire because the high-
> frequency content the firmware filter rejected isn't in the log —
> but it can come as close as the 50 Hz data permits, by applying a
> **rate-adjusted EMA** whose continuous-time τ matches the firmware's.
> No log schema change, no UI presentation hack, no "make the video
> calmer than flight" — just: filter the raw column with the right α
> for 50 Hz so the result matches the M5's display behavior.

## What this is NOT

- **NOT** "make replay videos watchable" through over-smoothing.
  Replay shows what the M5 saw. The M5 IS jittery in flight; the
  replay should be too. The current JS `KACC_TAU_S = 0.50` over-
  smooths to something calmer than the M5's actual output — that's a
  workaround we're closing, not a feature we're keeping.
- **NOT** adding new log columns. The log captures raw `g_pIMU->Ay`;
  that's enough.
- **NOT** re-running AHRS at 50 Hz inside replay. Filtering the raw
  channel directly with rate-adjusted α gives a strictly better
  approximation than running AHRS on under-sampled data.

## What we got wrong, twice

**v1 (PR #475, closed):** claimed the firmware ran an EMA at 208 Hz
and LogReplay drove that same filter at the wrong rate. Bulldog
caught it: the AOA EMA runs natively at 50 Hz inside `SensorIO::Read`
(`HardwareMap.h:257`, `kPressureSampleRateHz=50`); the 208 Hz / α=0.060899
filter is the AHRS accel EMA which lives inside the engine
(`onspeed_core/ahrs/Ahrs.cpp:317`), not in the LogReplay code path.
Upsampling at ingest was solving a non-problem on the wrong filter.
PR closed.

**v2 (briefly):** proposed adding new "smoothed" columns to the SD
log. Unnecessary — the existing raw column plus the right filter at
the right rate produces faithful output. Avoids touching the schema
(every offline tool that reads OnSpeed logs gets to keep its parser
unchanged).

This is v3. We finally read the actual code paths and the real
flight data, and the math agrees with the architecture.

## How the data actually flows

```
Live flight:
  g_pIMU->Ay (raw, 208 Hz inside g_pIMU updates)
       │
       │ Ahrs::Step() at IMU rate (208 Hz)
       ▼
  core_.accelLatFilter_.update(accelLatCorr_)         # α=0.060899
       │
       │ via PublishCoreState_, AccelLatFilter.seed(...)
       ▼
  g_AHRS.AccelLatFilter.get()  ──►  inputs.lateralG  ──►  WIRE  ──►  M5 slip ball

Separately, at 50 Hz log tick:
  g_pIMU->Ay  ──►  log column LateralG    (RAW — same source value, but at the
                                            log tick instead of the IMU tick)
```

Two different timelines, two different views. The wire timeline runs
at 208 Hz with a low-pass filter. The log timeline runs at 50 Hz
with no filter. The log captures one in every ~four IMU samples,
unfiltered; the wire emits a smoothed running estimate over all
four.

## What "matches the M5" can mean given 50 Hz data

The firmware's filter is:
```
y[k+1] = (1-α) · y[k] + α · x[k+1]   at 208 Hz, α = 0.060899
```
Continuous-time τ:
```
τ ≈ -(1/Hz) / ln(1-α) ≈ 1 / (α·Hz) ≈ 0.079 s
```

To match that τ at 50 Hz, with input dt=20ms:
```
α' = 1 - exp(-dt / τ) = 1 - exp(-(1/50) / 0.079) ≈ 0.224
```

Applied to the same underlying signal, sampled at 50 Hz instead of
208 Hz, the rate-adjusted EMA produces an output that — for
band-limited signals (energy below 25 Hz Nyquist) — converges to
the same value as the firmware's filter on the same physical signal.
For signals with energy between 25 Hz and 67 Hz (the IMU's analog LPF
bandwidth), the 50 Hz log has aliased content; rate-adjusted filtering
can't recover that. **The bound on accuracy is determined by the
flight's signal content, not the filter math.** A 208 Hz reference
flight log (Sub-task 1's flight-truth test) tells us how big that
bound actually is on real flights.

A first-pass quantitative check on Sam's existing 50 Hz log shows
the math at least produces the expected shape. On a 200-second
high-activity segment:

| filter | effective τ | output std | sample-to-sample jitter |
|---|---|---|---|
| raw | — | 0.128 g | 0.200 g |
| firmware α=0.060899 at 50 Hz (naive, wrong rate) | 0.328 s | 0.025 g | 0.008 g |
| rate-adjusted α=0.224 at 50 Hz | 0.079 s | 0.043 g | 0.031 g |
| JS over-smooth τ=0.50 (today's workaround) | 0.500 s | 0.022 g | 0.005 g |

Naive-firmware-α at log rate gives ~4× the smoothing the firmware
actually applied (because at 50 Hz that α has 4× the time-constant).
JS τ=0.50 is 6× the firmware's τ. **Rate-adjusted is the only one
that matches the M5's actual display behavior.** Naive and JS are
both over-smoothed by accident or for video aesthetics; both go
away.

## Sub-task 1 — Rate-adjusted accel EMA primitive

What ships:

1. **`software/Libraries/onspeed_core/src/filters/RateAdjustedAccelEma.h`**
   (header-only is fine if the math fits — class with `update()`,
   `get()`, `reset()`, constructed with `(inputHz, targetTauSec)`).
   Computes `α = 1 - exp(-(1/inputHz) / targetTauSec)` at construct
   time. No platform deps. `check_core_purity.sh` enforces.

2. **A canonical constant for the firmware's accel-EMA target τ**
   (≈0.079 s — derived from α=0.060899 at 208 Hz). Live in a header
   alongside the firmware constant, so any change to the firmware
   filter automatically updates the rate-adjusted target.

3. **Synthetic-signal tests** in `test/test_rate_adjusted_accel_ema/`:
   - **Step input**: both filters (firmware-form at 208 Hz on the
     original signal, rate-adjusted at 50 Hz on the decimated signal)
     must converge to the same final value. Time-to-90% within
     bounded tolerance.
   - **Ramp**: same final value, bounded steady-state error.
   - **Sine sweep at 1/5/10/20 Hz** (well below 25 Hz Nyquist):
     output amplitudes must match within bounded tolerance.
   - **Sine at 30 Hz** (above 50 Hz Nyquist, near 67 Hz IMU LPF
     bandwidth): looser bound; documents the aliasing-induced gap.
     Test asserts the gap is bounded but doesn't demand parity.

4. **A flight-truth test** scaffolded but **awaiting reference data**
   (see Issue [TBD] — Sam recording a 208 Hz reference log). When
   the log lands, the test:
   - Reads the 208 Hz raw `Ay` column.
   - Runs firmware-form filter (α=0.060899, dt=1/208) on it. This is
     the "what the M5 actually saw" reference.
   - Decimates to 50 Hz (every 4th sample), runs rate-adjusted filter
     (α=0.224, dt=1/50) on the decimation.
   - Sample-aligns and computes RMS divergence over the entire
     flight.
   - Asserts RMS divergence ≤ pinned tolerance (number TBD; ship the
     measured number with the PR description).

   Until the reference log exists, the synthetic tests are the gate.

5. **Optional firmware-side adoption**: the firmware's existing
   `AccelLatFilter` (`AHRS.cpp:124`, currently a vestigial
   `EMAFilter` mirror seeded from the engine output) can be replaced
   with `RateAdjustedAccelEma(208, 0.079)`. Algebraically identical
   for that input rate, but unifies the implementation — one filter
   class, two construct-time configurations. Ship as a follow-on if
   the diff is mechanical; defer if it surfaces hidden coupling.

Effort: ~1 day (synthetic tests + class). Flight-truth test scaffold
adds another ~half-day on top, deferred to whenever the reference
log exists.

## Sub-task 2 — `LogReplayEngine` uses the filter

What ships:

1. `LogReplayEngine` constructs three `RateAdjustedAccelEma`
   instances (lateral, vertical, forward) at log rate. Per row,
   feeds raw `imuLateralG` / `imuVerticalG` / `imuForwardG` through
   them.
2. `ReplayStepResult` exposes the smoothed values. `step()` no longer
   passes raw IMU through to the wire-shaped output.
3. Update existing characterization tests
   (`test_log_replay_engine.cpp`):
   - Pin numeric values for the smoothed channels using the
     rate-adjusted output.
   - Add at least one assertion against a real flight-log row using
     the actual filter — pin the expected smoothed value to ≥4
     decimals.
4. JS Replay tool's `KACC_TAU_S = 0.50` and the variable-dt EMA loop
   in `tools/web/lib/replay/logReplay.js` get **deleted** in this
   sub-task or its immediate WASM follow-on. After WASM Step 2
   binds the engine, JS calls into the same C++ that firmware-side
   replay does. No JS over-smooth.
5. `tools/regression/host_main.cpp::CmdReplay` — output schema
   reflects the smoothed channels. Regenerate the snapshot golden
   (`tools/regression/fixtures/replay_engine_golden.csv`).

Effort: ~half-day.

## Sub-task 3 — Synth `flapsRawADC` for old logs

Unchanged from prior versions. Logs from before ~PR #221 lack a raw
flap-pot ADC column; without it, the L/Dmax pip jumps at every
detent transition.

What ships:

- `LogReplayEngine` detects a missing `flapsRawADC` column.
- Synthesizes a smoothstep sweep across detent transitions
  (mirrors firmware's flip-at-midpoint physics; the JS
  `synthLeverSweep` is the math reference).
- When the column IS present, ingests it verbatim.

Effort: ~half-day.

## Total effort

~2 days, in 3 small PRs. Independent of each other except that
Sub-task 2 depends on Sub-task 1's filter primitive existing.

## What this DOES NOT change

- Live flight behavior. The firmware's actual flight-time filter
  (`core_.accelLatFilter_` at IMU rate) is unchanged. Optional
  Sub-task 1 follow-on unifies the class but doesn't change behavior.
- The SD log's columns. Schema is preserved; old offline tools keep
  reading old logs as before.
- The wire format. The M5 sees the same bytes it always did.
- The Replay tool's UI, sync, marks, clip builder.

## Cascading benefit

After Sub-tasks 1 + 2:
- `tools/web/lib/replay/logReplay.js::KACC_TAU_S` and variable-dt
  EMA — deleted.
- `tools/web/lib/replay/logReplay.js::synthLeverSweep` — moved into
  the engine (Sub-task 3) or deleted if Sub-task 3 lands first.

After WASM Step 2 binds the engine, JS Replay calls the same C++
that the firmware-side replay path will use. The "compile, don't
port" architecture finally extends to the replay path.

## What we learned (bake into AGENT_CONTEXT)

Three principles surfaced by this rewrite arc:

1. **Trace the data path through actual firmware code before
   specifying a fix.** PR #475 implemented exactly what its plan
   said and was wrong because the plan had the wrong premise. Future
   plans involving cross-module data flow must include "trace the
   data path through the actual firmware code" as a verification
   step.

2. **Don't add log columns to fix a math problem.** Schema changes
   are expensive (every offline tool that reads OnSpeed logs has to
   update). When the same fix can be done in math (rate-adjusted
   filter), prefer math.

3. **Don't introduce presentation-only filtering disguised as
   correctness.** The JS `KACC_TAU_S = 0.50` was tuned for "ball
   looks calmer than the M5 displayed" — that was an aesthetic
   choice in correctness clothing. Replay shows what the M5 saw.
   Period. If a future feature wants different filtering for some
   workflow, that's a separate visible toggle, not a hidden default.

## Open work tracked as GitHub issues

- **Issue: Record a 208 Hz reference flight log for filter validation.**
  Sam set `iLogRate=208` in config and flies a representative pattern
  (taxi → takeoff → climb → maneuvering turns → cruise → descent →
  landing). The log file becomes a permanent test fixture for
  Sub-task 1's flight-truth test. Until this exists, Sub-task 1
  ships with synthetic-signal validation only.

## Dispatch prompts

### Sub-task 1 prompt — Rate-adjusted accel EMA

```
Implement Sub-task 1 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md (v3):
build the rate-adjusted accel EMA primitive in onspeed_core, with
synthetic-signal tests. Flight-truth test scaffolded but skipped
until a 208 Hz reference log lands.

WORKTREE: a fresh worktree off origin/master
PLAN: docs/superpowers/plans/2026-05-08-firmware-log-replay-parity.md
PRECEDENT: software/Libraries/onspeed_core/src/filters/EMAFilter.h
           (the existing fixed-α implementation; this is similar but
           computes α from rate + tau)

WHAT TO BUILD:
  1. New software/Libraries/onspeed_core/src/filters/RateAdjustedAccelEma.h.
     Constructor: (inputHz, targetTauSec). Method: update(value),
     get(), reset(). Computes alpha = 1 - exp(-(1/inputHz) /
     targetTauSec). No platform deps. Header-only.
  2. A header constant for the firmware accel filter's target tau:
     onspeed::filters::kAccelEmaTauSec ≈ 0.079f, derived from the
     existing α=0.060899 at 208 Hz. Live alongside the firmware
     constant so they can't drift.
  3. test/test_rate_adjusted_accel_ema/test_rate_adjusted_accel_ema.cpp:
     - Step input: assert convergence to final value, time-to-90%
       within tolerance.
     - Ramp: assert steady-state error.
     - Sine at 1/5/10/20 Hz: amplitude match within tolerance.
     - Sine at 30 Hz: looser bound documenting aliasing gap.
  4. test_flight_truth.cpp scaffold with the comparison logic but
     a TEST_IGNORE() until the 208 Hz reference log fixture exists
     (track via GitHub issue).

VERIFY:
  - pio test -e native green, including the new test suite
  - check_core_purity.sh green
  - pio run -e esp32s3-v4p green (no firmware regression)

COMMIT: "filters: rate-adjusted accel EMA + synthetic-signal tests".
```

### Sub-task 2 prompt — LogReplayEngine integration

```
Implement Sub-task 2 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md (v3):
LogReplayEngine uses RateAdjustedAccelEma to filter raw IMU columns
into smoothed wire-shaped values.

WORKTREE: a fresh worktree off origin/master with Sub-task 1 merged
PLAN: docs/superpowers/plans/2026-05-08-firmware-log-replay-parity.md

WHAT TO BUILD:
  1. LogReplayEngine constructor instantiates three
     RateAdjustedAccelEma instances at the log rate.
  2. step() runs raw imuLateralG / imuVerticalG / imuForwardG
     through them; emits smoothed values in ReplayStepResult.
  3. Existing characterization tests (test_log_replay_engine.cpp):
     update pinned numeric assertions to reflect the smoothed
     output. Pin a representative real flight-log row (from
     ~/Downloads/sam_onspeed_aoa_4_11_2026.csv if available locally,
     or a smaller fixture) to ≥4 decimal places.
  4. tools/regression/host_main.cpp::CmdReplay output schema
     updated to expose smoothed channels. Regenerate
     tools/regression/fixtures/replay_engine_golden.csv.
  5. tools/web/lib/replay/logReplay.js: delete KACC_TAU_S, the
     variable-dt EMA loop, and any related smoothing wiring. The
     replay UI now displays whatever ReplayStepResult emits, no JS-
     side filtering. (If WASM Step 2 hasn't shipped yet, the JS code
     reads its values from a bridging path; document the temporary
     state and remove the workaround as soon as WASM is in place.)

VERIFY:
  - pio test -e native green
  - run_snapshot.py green (with regenerated replay_engine golden,
    AHRS regression untouched)
  - check_core_purity.sh green
  - manual: load a flight log in /replay, eyeball that the slip
    ball moves at flight-realistic rate (jittery during turns,
    stable during cruise)

COMMIT: "replay: engine uses rate-adjusted accel EMA on raw IMU".
```

### Sub-task 3 prompt — Synth flapsRawADC for old logs

```
(Unchanged from prior versions of the plan — same dispatch prompt
as in v2.)

Implement Sub-task 3 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md (v3):
synthesize flapsRawADC sweep when missing.

WORKTREE: fresh worktree off origin/master with Sub-tasks 1+2 merged
PLAN: docs/superpowers/plans/2026-05-08-firmware-log-replay-parity.md
COMPANION: tools/web/lib/replay/logReplay.js::synthLeverSweep
           (the JS version we're porting).

WHAT TO BUILD:
  - LogReplayEngine constructor takes flapsRawAdcAvailable (already
    in place from PR #470).
  - When false, synthesize the ADC sweep across detent transitions:
    1. First pass: fill every row with current detent's pot value.
    2. Second pass: paint smoothstep window centered on transitions.
  - When true, ingest verbatim.
  - FlapsDetector inside engine consumes synth ADC same as real ADC.

VERIFY:
  - run_snapshot.py with a pre-PR-#221 fixture log: pip morphs
    smoothly across detent transitions.
  - Bench: SD log replay of an old log shows pip morphing.

COMMIT: "replay: synth flap-pot ADC when log lacks the column".
```

## Coordination

- Closes the JS Replay tool's `KACC_TAU_S = 0.50` hand-tune by
  superseding it (Sub-task 2).
- Lands BEFORE `PLAN_WASM_CORE.md` Step 2 (the streaming-pipeline
  binding). After Step 2, the JS UI calls into the same engine the
  firmware uses for replay; no separate JS smoothing exists at all.
- Does not touch the Replay tool UI, sync, marks, clip builder.
- Does not change wire format, live flight behavior, or M5 firmware.

## Reference: prior PR closures

- **PR #475** ("rate-correct IMU EMA at log ingest") — closed without
  merge. The v1 plan's premise was wrong about which filter ran at
  which rate. See the PR's closure comment.
- **No PR for v2 (log-schema-add)** — the v2 plan was drafted and
  reviewed but not implemented before v3 superseded it.
