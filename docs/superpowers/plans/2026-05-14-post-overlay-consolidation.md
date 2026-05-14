# Post-Overlay Architecture Consolidation

**Date:** 2026-05-14
**Owner:** Sam
**Status:** Plan — ready to execute
**Predecessor:** Project C (video overlay replay tool) — shipped, PR #504 closed
**Bulldog audit:** Captured in conversation 2026-05-14; recommendations consolidated here

## The story we want to be able to tell

> `onspeed_core` (C++ pure-algorithm library) is the heart of the system. Every consumer that needs an OnSpeed algorithm (AOA calc, tone decision, Kalman VSI, EKF6, Madgwick fusion, percent-lift, wire encode/decode) reaches that algorithm through one canonical path:
>
> - **Firmware** (ESP32-S3): compiles `onspeed_core` natively
> - **M5-Display firmware** (and huVVer-AVI): compiles `onspeed_core`; decodes-and-renders only
> - **Replay tool** (docs site): WASM-compiled `onspeed_core` via `LogReplayTask` + M5 firmware compiled to WASM
> - **X-Plane plugin**: native `onspeed_core` (where it needs OnSpeed algorithms; uses X-Plane's own AOA dataref + plugin-local thresholds intentionally)
> - **`host_main`**: native CLI gateway around `onspeed_core` for Python + CI
> - **Python tooling**: shells out to `host_main`
>
> No hand-ports. No duplicate implementations. If you change a calculation in `onspeed_core`, every consumer picks it up automatically.

## Current state (post-bulldog audit)

**Flight-safety hot path is clean.** Firmware ↔ M5 ↔ replay all funnel through `onspeed_core`. Audited consumers:

| Consumer | Verdict | Evidence |
|---|---|---|
| Firmware (ESP32) | ✅ canonical | Includes `ahrs/Ahrs.h`, `aoa/PercentLift.h`, `audio/ToneCalc.h`, `proto/DisplaySerial.h` |
| M5-Display firmware | ✅ canonical | `lib_deps = onspeed_core`; zero AHRS/Kalman/AOACalc references in M5 source |
| huVVer-AVI display | ✅ canonical | Same M5 codebase, different PlatformIO env |
| Replay tool (docs site) | ✅ canonical | `buildWireFrames.js` is a thin wrapper around `LogReplayTask` (WASM); `m5sim.js` loads M5 firmware WASM |
| `packages/ui-core` | ✅ canonical | Layout/format/geometry only; zero algorithm code |
| X-Plane plugin | ✅ canonical | (intentionally uses X-Plane datarefs + plugin-local thresholds) |
| `host_main` | ✅ canonical | 1484 lines, all subcommands shell into `onspeed_core::*` |
| `tools/m5-replay/` | ⚠️ partial | Native `parse_frame.cpp` links onspeed_core (good); Python encoder side is hand-rolled |
| Python tooling | ⚠️ partial | Most files shell to `host_main`; two notable hand-ports remain |
| Documentation site | ✅ canonical | Percent-lift formula in `how-aoa-works.md` matches `ComputePercentLift` byte-for-byte |
| Live indexer (`tools/web/lib/pages/IndexerPage.js`) | ⚠️ partial | Uses canonical ui-core renderers via `wsRecordToState` adapter, but Mode 3 recomputes a JS EMA the M5 panel already publishes |

**The duplications that remain are tools-tree only and cannot corrupt flight behavior.** None of them are in the firmware build path.

## What's being killed

Five named hand-ports / drift risks, ranked by blast radius.

### Track 1 — `tools/onspeed_py/frame.py::Frame.to_bytes()` (high priority)

**The bug-shaped duplication.** `frame.py` re-implements the 77-byte v4.23 wire encoder line-for-line against `onspeed_core/proto/DisplaySerial.cpp::BuildDisplayFrame`. The payload printf format (24 fields) is hand-maintained alongside the C++ version.

**Risk**: next wire bump (v4.24, etc.) will silently fork the two encoders. The fork is only detected when `tools/m5-replay/test_replay.py` hits a CRC mismatch downstream.

**Fix**: shellout to `host_main build_frame --record <json>` (the subcommand already exists at `host_main.cpp:977`). Python `Frame` dataclass stays as the input shape; only `to_bytes()` changes from local format-string to subprocess.

**Verification**: existing `tools/m5-replay/test_replay.py` already provides Python encoder → C++ decoder round-trip; expand to also assert byte-identity against `host_main build_frame` output over a randomized record fixture (~100 random JSONs).

**Estimated effort**: 1 PR, half-day.

### Track 2 — `tools/regression/vsi_bench/run_bench.py` (high priority, time-sensitive)

**The just-written hand-port.** Created today as a Python re-implementation of `MadgwickFusion::UpdateIMU` + `KalmanFilter::Update` + the AHRS compensation pipeline, line-for-line. Used to prototype the Madgwick centripetal-gate fix. Header comment is candid: `The Python math mirrors onspeed_core/ahrs/MadgwickFusion.cpp, KalmanFilter.cpp, and Ahrs.cpp line-for-line.`

`host_main ahrs_replay` shipped alongside it (also uncommitted, same `feature/bundler-esbuild` branch) as the C++ replacement. Same `--gate-disabled` / `--gate-hi` / `--gate-lo` flags.

**Risk**: if `BETA = 0.011617` or any of the comp-factor smoothing alphas are tuned in `MadgwickFusion.cpp` / `Ahrs.cpp`, the bench silently uses stale values and produces a misleading bench output.

**Fix while it's fresh**: gut the math from `run_bench.py`, keep only the CSV-loading, windowing, truth-VSI computation, and plotting layers. Source the simulated trace from `host_main ahrs_replay --input <csv>` JSONL output.

**Estimated effort**: 1 PR, half-day. Co-land with Madgwick-gate PR.

### Track 3 — Native ↔ WASM byte-equivalence test (high priority)

**The drift-prevention test that should have been there from day one.** `LogReplayTask` is the canonical replay engine, compiled to both native (for firmware-test parity) and WASM (for the replay tool). Today only the C++ side has cross-implementation tests (`test_log_replay_task_engine_parity`).

**Risk**: Emscripten quietly changes float behavior or bindings.cpp introduces a subtle calling-convention mismatch. Caught only when a user notices wrong values on the replay page (the worst possible discovery channel).

**Fix**: pick a 1000-row slice of the engine-parity fixture. For each row, run both `host_main replay` (native onspeed_core) and the WASM `LogReplayTask.processRow` (via the existing Node smoke-test harness). Assert byte-identical wire frames.

**Companion**: a WASM `compute_percent_lift` ↔ `host_main percent_lift` parity check on the same fixture rows. The current `tools/web/test/wasm-smoke.mjs` only pins WASM output against hand-typed numbers; it should pin against same-input C++.

**Estimated effort**: 1 PR, full day (test infra + fixture wiring).

### Track 4 — `tools/web/lib/scenarios.js` percent-lift hand-roll (low priority, defensive)

**The PR #33 bug, waiting to be reintroduced.** Dev-server mock data only:

```js
const RV10_FULL_FLAPS = {
  alpha0: -3.2,
  alphaStall: 16.5,
  aoaToPct: (aoaDeg) => (aoaDeg - this.alpha0) / (this.alphaStall - this.alpha0) * 100,
  // ...
};
```

This is byte-for-byte the formula PR #33 (Feb 2026) killed in production code. Today it lives in dev-mock land. The risk is a future contributor copy-pasting it into a real page.

**Fix**: replace with `import { compute_percent_lift } from '<onspeed_core_wasm>'` once that import is ergonomic. Until then, add a one-line comment pointing at `ComputePercentLift` as the canonical implementation, plus a `eslint-disable` flag tag so the file shows up in any future audit grep.

**Estimated effort**: depends on WASM ergonomics. Add the comment now (5 minutes); land the replacement when a future PR makes the WASM import natural.

### Track 5 — `tools/web/lib/core/ema.js` decel-smoothing mirror (low priority)

**The "we got away with it" mirror.** Live `/indexer` Mode 3 ingests `r.decelRate` (raw 20 Hz wire field) and runs its own EMA in JS at `alpha = 0.04` to match the M5 firmware's `SerialRead.cpp:344` decel-smoothing.

**Why it exists**: the live page doesn't run a WASM M5 sim (the replay tool does, the live indexer doesn't). The hand-roll is the cost of not loading the M5 firmware on the live page.

**Risk**: if `decelSmoothingAlpha` is retuned in `SerialRead.cpp`, the live indexer's Mode 3 silently drifts from the M5 panel display. The wire frame already carries the M5's own `displayDecelRate` snapshot (500 ms), so the live page could just read that and skip its own EMA.

**Fix options**:
- (a) Read `displayDecelRate` from the wire frame directly; drop the JS EMA.
- (b) Export the alpha constant from the M5 WASM build and import it (keeps current code shape; just removes hardcoded constant).
- (c) Live with documented drift; tighten the comment.

Decision deferred to a future "live-indexer parity with M5 panel" pass.

**Estimated effort**: (a) is the cleanest — half-day. (b) and (c) are workarounds.

## What's deliberately NOT being killed

- **Two bundlers** (Python firmware-target + Node esbuild replay-target). Acceptable. Firmware PROGMEM is small + simple; the Python+regex bundler is fine. The replay bundle (mediabunny, WebCodecs, large surface) genuinely needed a real bundler. Moving firmware to esbuild would be consistency-for-consistency's-sake and doesn't kill any duplication.
- **Calibration math (`CalWizardPage.js::analyzeDecel`)**. The IAS→AOA hyperbolic fit producing `alpha_0`, `alphaStall`, `kFit`, and the 6 setpoints exists only in JS. **Not a duplicate** (no C++ peer), but it's the only flight-critical numerical pipeline that lives in exactly one language. **Will be forced into `onspeed_core` when the roadmap's "upload SD log, recompute calibration offline" workflow lands**, via `aoa/DecelFit.{h,cpp}`. Deferred until that work begins.
- **Firmware-only tests** (Audio.cpp, LogSensor.cpp, SD writer integration). Inherently platform-bound. `ToneCalc` itself is heavily unit-tested; the SD writer is firmware-integration-tested. Status quo OK.

## What's left genuinely open from the broader project

- **#521 — SD log captures wire-quantized lateral G**. Firmware feature work, not consolidation. Replay-side workaround (route via M5 WASM) is in place; doesn't address offline consumers who don't want to spin up the WASM sim.
- **Madgwick centripetal-gate firmware fix**. Uncommitted on `feature/bundler-esbuild`. The C++ harness proved gate reduces pitch bias (1.3-3.5° across three turns) but VSI oscillation is dominated by comp-factor residual, not pitch leak. Gate alone is not the full fix. **Deferred** — comp-factor work needed before this ships.
- **Worker-thread the encode/composite loop**, **frame-step keyboard scrub**, **multi-GoPro chapter ingest**, **multi-anchor sync UI**, **clip timeline nudge UX**. All deferred Project C extensions, none blocked.

## Execution sequence

Independent PRs, no inter-dependencies. Order is "blast-radius descending."

| # | Title | Track | Effort | Branches off |
|---|---|---|---|---|
| 1 | Kill `frame.py::to_bytes()` hand-roll | T1 | 0.5 day | master |
| 2 | Native ↔ WASM byte-equivalence test | T3 | 1 day | master |
| 3 | Replace `vsi_bench/run_bench.py` math with `host_main ahrs_replay` | T2 | 0.5 day | (depends on Madgwick gate landing, but the *cleanup* is independent) |
| 4 | scenarios.js + ema.js cleanup pass | T4+T5 | 0.5 day | master |

## Tech-debt riders (bundled into the tracks above)

A second audit (2026-05-14) found 9 small debt items that live in the same files the consolidation tracks touch. Folding them in costs ~80 net lines per PR and zero algorithm changes — comment hygiene, dead-helper deletion, safety-net upgrades, and one diagnostic gate.

### Riders on Track 1 PR (`frame.py` shellout)

- **T1-A** — Delete `test_frame_roundtrip.py::test_offsets_round_trip` (lines 47-100). Re-implements the firmware's offset/scale table in Python; redundant once byte-identity-vs-host_main lands as part of Track 1.
- **T1-B** — Delete `_clamp_int` / `_clamp_uint` in `frame.py:38-59`. Zero callsites after `to_bytes()` becomes a shellout.

### Riders on Track 3 PR (vsi_bench gut + replace)

- **T3-A** — Fix silent `catch (...) {}` on `--gate-hi` / `--gate-lo` in `host_main.cpp:1212-1213`. Malformed float silently falls back to default and mislabels output. 5 minutes.
- **T3-B** — Lift hardcoded `log_007.csv` path out of `run_bench.py:36-40` + `analyze_cpp.py:26-31`. Accept `--log` arg or read from a shared module.

### Riders on Track 4 PR (JS source-tree sweep)

- **T4-A** — Strip "The Replay tool used to live here..." comment in `tools/web/lib/entry.js:21`. CLAUDE.md banned phrasing.
- **T4-B** — Defensive `r.spinRecoveryCue ?? 0` in `packages/ui-core/adapters/wsRecordToState.js:113-116`. The wire format already carries the field; the WebSocket JSON producer just hasn't surfaced it. Cost zero if absent, correct the moment the producer ships it.
- **T4-C** — Strip "the legacy WebM path used to..." comment in `docs/site/docs/data-and-logs/replay/lib/pages/ReplayPage.js:1725`. Same banned-phrasing rule.
- **T4-D** — Gate the always-on `console.log('LogReplayTask cfg.aFlaps[*].iDegrees ...')` in `docs/site/docs/data-and-logs/replay/lib/replay/buildWireFrames.js:91-93` behind `?debug=1` (matching the pattern in `fileHandles.js:375-376`). Leftover A/B-trial diagnostic.

### Items NOT bundled (own follow-up if at all)

- **S2-A** — `tools/onspeed_py/log_replay.py:286-302` `fake_lever_sweep` no-op param + `flap_overrides` `NotImplementedError`. Dead-parameter shape that can come out once external-caller audit confirms zero use. 30 min, but needs the external check, so independent PR.
- **S2-B** — `host_main` emits bare `nan` instead of `null` (Issue #499 referenced twice in `log_replay.py:53-56,150-152`). Three lines of Python die when host_main's JSON emitters use `std::isfinite`. C++ side, ~1 hour. Worth its own PR for the surgical scope.

### Out of scope (noted, not bundled)

- `tools/web/lib/ws/wsClient.js:125,136` — `console.log` for socket errors should be `console.warn`. Unrelated to consolidation; cheap if anyone touches the file.
- `software/Libraries/onspeed_core/` — **clean**. No TODO/FIXME/HACK markers, no abandoned helpers, no duplicate `clamp` implementations. Heart of the system is in good shape.
- `packages/ui-core/components/svg/index.js` — 641 lines, all exports consumed. Not orphaned.

Tracks 1-2 can land as a stack. Track 3 follows the Madgwick PR. Track 4 is a single sweep through `tools/web/`.

## Acceptance — "consolidation complete"

We declare the post-overlay consolidation done when:

- [ ] Track 1: `frame.py::to_bytes()` is a `host_main build_frame` shellout; test_replay.py also asserts byte-identity with C++ encoder
- [ ] Track 2: native ↔ WASM byte-equivalence test runs in CI and gates merges
- [ ] Track 3: `vsi_bench/run_bench.py` consumes `host_main ahrs_replay` output, contains zero hand-port math
- [ ] Track 4: scenarios.js carries the WASM percent-lift import OR a comment + grep-friendly tag; ema.js cleanup decided one way or the other
- [ ] **Verification grep**: in the firmware + replay + M5 build paths, the only files that compute flight values are under `software/Libraries/onspeed_core/`. Any other file that computes a flight value is either documentation, mock data, or a test fixture (and is tagged as such)

## Cross-references

- Bulldog audit: conversation 2026-05-14 (this branch's session log)
- PR #504 close-out comment: covers what shipped from Project C
- Issue #523: closed 2026-05-14 (shared ui-core / adapter pattern)
- Issue #525: closed 2026-05-12 (replay bundle)
- Issue #521: open (firmware feature work; un-quantized lateral G in SD log)
- `software/Libraries/onspeed_core/src/proto/DisplaySerial.cpp` — canonical wire encoder
- `tools/regression/host_main.cpp` — CLI gateway; `CmdAhrsReplay` at line 1195, `CmdBuildFrame` at line 977
