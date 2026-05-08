# PLAN_DRIFT_PREVENTION.md — Cross-Implementation Spec Strategy

**Date:** 2026-05-08
**Owner:** Sam
**Status:** Proposed — not yet implemented.

## The problem

OnSpeed now has **three implementations** of the same flight-data
algorithms across the project:

| Module | Firmware (C++) | Python (offline) | JavaScript (replay) |
|---|---|---|---|
| Percent-lift | `software/Libraries/onspeed_core/src/aoa/PercentLift.cpp` | `tools/onspeed_py/percent_lift.py` | `tools/web/lib/replay/percentLift.js` |
| Display anchors | `software/Libraries/onspeed_core/src/aoa/DisplayPctAnchors.cpp` | (inlined in `log_replay.py`) | `tools/web/lib/replay/percentLift.js::computeAnchors` |
| Variable-dt EMA | `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp` (kAccSmoothing) | `tools/onspeed_py/log_replay.py` (KACC_TAU_S) | `tools/web/lib/replay/logReplay.js` (KACC_TAU_S) |
| Flap detection | `software/sketch_common/src/sensors/FlapsDetector.cpp` | (synthesized in `log_replay.py`) | `tools/web/lib/replay/logReplay.js::synthLeverSweep` |
| Config XML parse | `software/sketch_common/src/config/Config.cpp` | `tools/onspeed_py/config.py` | `tools/web/lib/replay/config.js` |

Each language has a real reason to exist:

- **Firmware C++** — runs on the ESP32. Real-time constraints, no
  network, no allocations in hot path.
- **Python** — offline analysis, the synth-record orchestrator, future
  log-import tools. Already part of the project, well-tested.
- **JavaScript** — runs in the browser at the replay tool / dev
  server. WASM compilation of the C++ is technically possible but a
  major project (build system, FreeRTOS shim, etc.).

**Rewriting any two of these in a single language is a huge project.**
Drift prevention has to live above the language boundary.

## The proposed solution: JSON fixture-driven cross-implementation tests

Instead of declaring one language the source of truth, declare the
**spec** to be a set of JSON test fixtures stored in `test/spec_fixtures/`.
Each fixture carries inputs and reference outputs:

```json
{
  "name": "percent_lift_basic_clean",
  "description": "Clean detent, alpha_0=-3.72, alpha_stall=10.31, AOA=3.24 (LDmax)",
  "inputs": {
    "aoa_deg": 3.24,
    "flap_cfg": {
      "alpha_0": -3.72,
      "alpha_stall": 10.31,
      "ldmax_aoa": 3.24,
      "onspeed_fast_aoa": 3.98,
      "onspeed_slow_aoa": 5.26,
      "stallwarn_aoa": 8.24
    }
  },
  "expected": {
    "percent_lift": 49.6
  },
  "tolerance": 0.05
}
```

Each language has a small test runner that walks the fixture
directory, calls the real implementation (no shims, no rewrites), and
asserts inputs → outputs. CI runs all three runners on every PR. If
any of them fails, the PR can't merge until either the fixture or the
implementation changes.

## Why this is the right shape

1. **The fixture is the spec.** When we change behavior (e.g. bump
   `KACC_TAU_S`), one PR updates the fixture file AND the
   implementation in lockstep. Future readers see exactly what the
   intended values are.
2. **No language is privileged.** Firmware, Python, JS all read from
   the same JSON. None is "canonical" except where the JSON says so.
3. **Tests double as documentation.** A new contributor reads
   `percent_lift_basic_clean.json` and understands: "for this AOA at
   this flap, percent-lift is 49.6%." More legible than a unit test
   buried in any one language's test harness.
4. **It's shippable in one PR.** Write the runner shells once, port
   the existing tests over, done.
5. **CI-enforced.** Drift can't sneak in via "I forgot to update Python."

## What the fixtures should cover

Priority order:

### Tier 1 — flight-safety algorithms

These directly affect what the pilot sees / hears in the airplane.
Any drift is unacceptable.

- `percent_lift_*.json` — `(aoa, flapCfg) → percent`. Cover: LDmax,
  fast/slow band edges, stallwarn, alpha_0 (negative bodies), alpha_stall
  fallback, NaN AOA, IAS-invalid input.
- `display_anchors_*.json` — `(activeFlap, allFlaps, rawAdc) →
  {tonesOn, fast, slow, stallWarn, pip}`. Cover: clean, mid-detent
  pot, full-flap pot, off-end pot, single-detent edge case.
- `tone_calc_*.json` — `(aoa, thresholds) → {toneType, pulseFreq}`.
  Cover: every band transition, full-flaps mute, stall buzz.

### Tier 2 — replay-equivalence algorithms

These don't run on the airplane in real-time but they govern how
flight data is interpreted offline. Drift here means analysis tools
disagree.

- `ema_smoothing_*.json` — `(input_series, dt_series, tau) →
  output_series`. Cover: variable-dt at 50 Hz vs 208 Hz, NaN
  passthrough, seed-from-NaN.
- `flap_detection_*.json` — `(rawAdc_series, flapEntries) →
  detected_detent_series`. Cover: midpoint-flip behavior, sweep
  through all detents, hold-at-detent.
- `config_parse_*.json` — `(xml_text) → {flaps, mute_under_ias, ...}`.
  Cover: V1 + V2 formats, missing tags, malformed input.

### Tier 3 — wire-format protocol

These are byte-level contracts where any language disagreement breaks
inter-component communication.

- `display_serial_frame_*.json` — `(record) → wire_bytes` and inverse.
  Cover: every field, IAS-invalid sentinel (9999), AOA-invalid
  sentinel (-100, until issue #455 lands), checksum.
- `live_data_json_*.json` — `(record) → json_string`. Cover: null vs
  number for AOA/IAS/percentLift, the kAoaSentinel transition.

## Where the fixtures live

```
test/spec_fixtures/
  percent_lift/
    basic_clean.json
    alpha_0_negative.json
    alpha_stall_fallback.json
    nan_input.json
    ...
  display_anchors/
    ...
  ema_smoothing/
    ...
```

One directory per algorithm. Each file is one test case.

## Test runners (one per language)

### C++ — Unity / native test

A new test suite under `test/test_spec_fixtures/test_spec_fixtures.cpp`
that:
1. Walks `test/spec_fixtures/` at build time (or via embedded
   constants generated by a Python script).
2. For each fixture in a covered algorithm, calls the production
   function and asserts within tolerance.

### Python — pytest

`tools/onspeed_py/tests/test_spec_fixtures.py` walks the same
directory at runtime, parametrize-style.

### JavaScript — node test runner

`tools/web/test/spec_fixtures.mjs` walks the directory via `fs`,
imports the production module, asserts.

All three CI'd on every PR.

## What's NOT in scope

- **Replacing existing per-language unit tests.** Those stay. The
  spec fixtures are an *additional* layer that catches *cross-language*
  drift; per-language unit tests still catch within-language
  regressions and edge cases the fixtures don't cover.
- **Auto-generating tests from one language.** The fixtures are
  hand-curated. If we ever auto-generate from C++ outputs, we lose
  the spec-as-documentation property.
- **Running the firmware-C++ tests on real hardware.** PlatformIO
  native tests are enough — the algorithms are platform-free.

## How this rolls out

Sequence:

1. **PR 1**: scaffold the fixture directory + the three runners
   with one trivial fixture per algorithm. Verify CI passes in all
   three languages.
2. **PR 2**: port `percent_lift` test cases from the existing C++ +
   Python unit tests into JSON fixtures.
3. **PR 3**: port `display_anchors` cases.
4. **PR 4**: port `ema_smoothing` cases.
5. **PR 5+**: as new algorithms are added, fixtures land alongside
   the implementation in the same PR.

Total work: ~3 days for someone who knows the algorithms.
Maintenance: ~zero — each new test case is one JSON file.

## Why this matters more than it seems

OnSpeed is at a stage where:
- Multiple pilots are flying real OnSpeed code on real airframes.
- The team is shipping multiple PRs per day.
- The replay tool, the WASM simulator, the X-Plane plugin all
  consume the same algorithms.

Without a cross-implementation spec, every PR risks a silent
"this works in firmware but not in the analysis tool" regression.
The fixture approach makes those regressions impossible to merge.

## Dispatch prompt

```
You're implementing PLAN_DRIFT_PREVENTION.md PR 1: scaffold the
cross-implementation fixture system.

WORKTREE: a fresh worktree off origin/master
WHAT TO BUILD:
  1. test/spec_fixtures/percent_lift/basic_clean.json with one
     test case (alpha_0=-3.72, alpha_stall=10.31, aoa=3.24, expected=49.6).
  2. C++ runner: test/test_spec_fixtures/test_spec_fixtures.cpp.
     Walks test/spec_fixtures/ via a build-time script that
     converts JSON to embedded const data. Calls
     ::onspeed::aoa::ComputePercentLift(...) and asserts.
  3. Python runner: tools/onspeed_py/tests/test_spec_fixtures.py.
     Walks the directory at runtime via pytest parametrize.
     Calls onspeed_py.percent_lift.compute_percent_lift(...) and asserts.
  4. JS runner: tools/web/test/spec_fixtures.mjs. Walks the
     directory via fs.readdirSync, imports the production
     module, asserts. Plumb into npm test.

VERIFY: all three runners pass green on the one fixture; modify
the fixture's `expected` to a wrong value, all three should fail.

COMMIT: one focused PR titled "test: cross-language spec
fixtures (scaffold)".
```
