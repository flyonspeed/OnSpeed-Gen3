# PLAN — EKF Tuning: SD Log Input Fidelity

**Date:** 2026-05-14 (rewritten 2026-05-18, closed 2026-05-19)
**Owner:** Sam
**Status:** **CLOSED — superseded by the AHRS pipeline refactor (PR #590), EKF6→EKFQ replacement (PR #591), and Optuna substrate (PR #595).** The replay path the plan was trying to enable now exists end-to-end on master.

## Closeout

This plan documented a six-blocker audit (G1–G6) of the SD log against byte-faithful offline EKF replay. By 2026-05-19, master had absorbed every working concern the plan raised — by a different route than the plan proposed:

- **G1 (per-row dt)** — closed by PR #551 (commit `fbeb9888`, 2026-05-15) as a `timeStampUs` uint64 column adjacent to `timeStamp`. Better than the plan's uint16-delta proposal: no `micros()` rollover at flight timescales.

- **G2 (`iasUpdateTimestampUs` reconstruction)** — solved without adding a column. The `LogRowToAhrsInputs` bridge (`software/Libraries/onspeed_core/src/replay/LogRowToAhrsInputs.cpp`, shipped in PR #590) synthesizes `iasUpdateTimestampUs` from `PfwdSmoothed` deltas: whenever the smoothed pitot value changes between rows, bump the synthetic timestamp by one pressure period (20 ms). Same heuristic as Lenny's `data.py`. PR #595's Optuna substrate validated this empirically — the C++ subprocess driver finds the same best trial within ~3% loss as the byte-faithful Python pipeline, at 51× wall-clock speed. The synthesis is not byte-faithful (real flight has jitter on IAS-update arrivals; synthesis bumps in clean 20 ms increments) but is good enough that tuning loops converge to the same parameters.

- **G3 (`compFadeIn_` snapshot for warm-start replay)** — moot under the new architecture. The pipeline refactor (PR #590) moved `compFadeIn_` and `iasGate_` from `Ahrs`'s shared state into the algorithm-stage wrappers (`onspeed::ahrs::Madgwick`, `onspeed::ahrs::EkfqPipeline`). Replay starts from a cold pipeline and warms up over ~3·τ (compFadeTauSec ≈ 2.5 s for EKFQ → ~7.5 s ignore-from-start). For tuning campaigns this is a non-issue: skip the first ~10 s of every replay or seed `compFadeIn_=1.0` if the gate was open at row 0. The cold-start-vs-warm-start ambiguity the original audit flagged is real but is the analyst's problem, not the firmware's.

- **G4 (cfg snapshot embedded in log)** — dropped. The replay tool (`tools/web/lib/pages/ReplayPage.js:703`) and Optuna substrate (`ekfq_pipeline/run_host_main.py`) both require a user-supplied `.cfg`. Cfg-at-time-of-flight comes from the analyst's filesystem / Dropbox / git history.

- **G5 (Q/R/P0 in cfg)** — explicitly out of scope when the plan was written. Still out of scope: PR #595's Optuna substrate tunes Q/R/P0 directly via the `--ekfq-config` flag without touching cfg or firmware, which is the right factoring (tune offline, ship the winning values via a separate cfg-plumbing PR if/when one set of values becomes canonical).

- **G6 (`madgwickGate` columns)** — dropped. The underlying centripetal-gate firmware on `feature/bundler-esbuild` was abandoned when that branch got repurposed for esbuild. The pipeline refactor's IAS gate (which is what shipped) is *not* the same as the centripetal gate that motivated G6.

## What replaced this plan

Three PRs landed in close succession 2026-05-15 through 2026-05-19 and together delivered the replay-and-recomputation capability this plan was trying to scope:

| PR | Title | What it gave us |
|---|---|---|
| #551 | feat(logging): add timeStampUs column | G1 — byte-faithful per-row dt |
| #590 | AHRS prep refactor: four-stage pipeline + iIasDisplayThresholdKt + raw-log + EKF6 regression golden | `LogRowToAhrsInputs` bridge (G2 synthesis), stage-isolated pipeline (G3 moot), raw IAS/AOA always logged, EKF6 regression golden in snapshot harness |
| #591 | AHRS: replace EKF6 with EKFQ (11-state quaternion EKF) | EKFQ as the production EKF; pipeline-owned `compFadeIn_` / `iasGate_` / `tasdotSmoothed_` |
| #595 | EKFQ: Optuna substrate — tune C++ directly via host_main | End-to-end tuning loop: SD log → `host_main ahrs_tone --input-format=sdlog --algorithm ekfq --ekfq-config <trial>` → Optuna TPE search. **51× faster than the Python baseline; same best trial within 3%.** |

The tuning loop the plan envisioned in section 1 of the original audit ("flight log → re-run AHRS with varied Q/R/P0 → minimize residual against VN-300 truth") is now `optuna study + run_host_main.py` and runs in ~3.5 minutes for a 200-trial study on a full 79-min log.

## What is NOT done (and is fine)

- **Byte-faithful `iasUpdateTimestampUs` logging.** The synthesis works; an explicit column would tighten the parity-floor by a small margin but isn't required for tuning to converge. If we ever see Python↔C++ drift in a way the synthesis is masking, revisit.
- **`compFadeIn_` per-row column.** Replay starts cold and warms up. Same logic: if a use case appears where the 10 s warm-up is intolerable, log the column then.
- **Cfg snapshot copied to `<log_NNN>.cfg`.** The analyst pulls cfg from Dropbox / git. The "Vac saves cfg mid-flight via the AP and silently overwrites" failure mode is one we accept rather than guard against in firmware.

## Reference: original audit

The 2026-05-14 audit text is preserved in this file's git history. It correctly identified the gaps; the resolution shape just turned out to be different from what the plan proposed (architecture refactor + replay-side synthesis instead of more log columns). Worth reading if you find yourself making the same audit again — the recipe for "what does a faithful offline EKF replay need from the log?" is the same recipe.

Pointers into the shipped code:

- `software/Libraries/onspeed_core/src/types/LogRow.h:32-45` — both timestamps
- `software/Libraries/onspeed_core/src/replay/LogRowToAhrsInputs.{h,cpp}` — the bridge that closes G2 by synthesis
- `software/Libraries/onspeed_core/src/ahrs/EkfqPipeline.{h,cpp}` — pipeline-owned `compFadeIn_` / `iasGate_` / `tasdotSmoothed_`
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp::updateTas_` — variable-rate TAS EMA (still uses `iasUpdateTimestampUs`; the bridge feeds it the synth value)
- `tools/regression/host_main.cpp` — `ahrs_tone --input-format=sdlog --algorithm ekfq`
- `ekfq_pipeline/run_host_main.py` — Optuna subprocess driver
- `.claude/skills/optuna-tuning/SKILL.md` — end-to-end recipe

## What this means for the PR

This plan is the last live item from PR #549 (post-overlay plan set). With it closed, the PR holds two remaining plans — consolidation and replay-sidecar-persistence — neither of which is time-sensitive. PR #549 itself stays open as a living doc until those two close out or are abandoned.
