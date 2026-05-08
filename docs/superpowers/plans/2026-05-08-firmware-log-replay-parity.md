# PLAN_FIRMWARE_LOG_REPLAY_PARITY.md — Firmware LogReplay Closes the Gaps

**Date:** 2026-05-08
**Owner:** Sam
**Status:** Proposed.
**Sequenced:** BEFORE `PLAN_WASM_CORE.md` Step 2 (the streaming-pipeline binding).
**Companion to:** `PLAN_WASM_CORE.md`, `PLAN_VIDEO_OVERLAY.md`.

## The thesis

> Firmware LogReplay mode should produce, from an SD log, the same wire
> output the firmware produced live during that flight. Today it
> doesn't — the JS Replay tool papers over two firmware-side gaps. Fix
> them in firmware. The Replay tool then inherits correct behavior for
> free via the WASM build.

## The two gaps

### Gap 1 — IMU EMA at log rate

The firmware reads IMU at **208 Hz** and applies a fixed-α EMA
(α=0.060899). Continuous-time τ ≈ 0.0741 s.

The SD log records **raw, pre-EMA** `Ax/Ay/Az` at the log rate
(50 Hz on default builds, 208 Hz only when the user sets
`iLogRate=208`).

When LogReplay reads back a 50 Hz log and feeds those samples to the
*same* fixed-α EMA, every sample carries 4× more weight per real
second than it did in flight. The effective continuous-time τ shrinks
to ~0.018 s. The replayed wire output looks visibly twitchier than
flight.

This is not an algorithmic bug in the EMA. It's a **rate-coupling
bug** in LogReplay: the filter is being driven at the wrong sample
rate.

### Gap 2 — Missing `flapsRawADC` column in old logs

Logs from before ~PR #221 don't carry the raw flap-pot ADC. The
firmware's `FlapsDetector` snaps to the nearest detent before logging,
so old logs only contain the snapped detent integer. Mid-detent
fractions are lost.

The L/Dmax pip on the M5 indexer is a function of `flapsRawADC` (the
firmware morphs the pip linearly between detents during a flap
movement). Without raw ADC, the pip jumps at every detent transition
on replay.

The JS Replay tool today synthesizes a fake ADC sweep across detent
transitions (smoothstep windows centered on the snap tick — mirrors
the firmware's flip-at-midpoint physics). This works but it's a
patch outside the spec.

## Where the fixes live — engine extraction

`software/sketch_common/src/tasks/LogReplay.cpp` today is a FreeRTOS
task that owns SD-card reads, button mocks, LED blink, and the
per-row processing loop all in one file. It uses `Arduino.h`,
`FreeRTOS.h`, and `millis()` — not platform-free. WASM Step 2 from
`PLAN_WASM_CORE.md` cannot bind it as-is.

**Precedent: `software/sketch_common/src/tasks/AHRS.cpp`.** That task
is a thin wrapper around `software/Libraries/onspeed_core/src/ahrs/Ahrs.{h,cpp}`.
The task owns FreeRTOS scheduling and the `g_pIMU` snapshot; the engine
in `onspeed_core/ahrs/` owns Madgwick / EKF6 math. This is the model.

**LogReplay needs the same split.** Both Fix 1 and Fix 2 (and the
existing per-row work) belong in a new
`software/Libraries/onspeed_core/src/replay/LogReplayEngine.{h,cpp}`.
The engine surface is roughly:

```cpp
namespace onspeed::replay {

struct LogRow {            // raw values from one CSV row
  float ax, ay, az, gx, gy, gz;
  float ias, palt, oat;
  int   flapsRawAdc;       // -1 if column missing
  int   flapDetentDeg;     // snapped detent from log
  uint32_t timestampMs;
  // ... etc.
};

struct WireRecord {        // what the M5 receives
  // ... existing display-frame fields ...
};

class LogReplayEngine {
public:
  LogReplayEngine(const ConfigStruct& cfg, int logSampleRateHz,
                  bool flapsRawAdcAvailable);
  WireRecord step(const LogRow& row);  // pure function over engine state
  // No millis(), no SD-card, no FreeRTOS.
};

}  // namespace onspeed::replay
```

`software/sketch_common/src/tasks/LogReplay.cpp` retains its
FreeRTOS task scaffolding (queue draining, SD-card reads, button
mocks, LED blink, USB-CDC writes) but delegates the per-row
`(raw_row → wire_record)` work to `LogReplayEngine::step()`.

`host_main replay` (PLAN_PYTHON_CONSOLIDATION Step 0) constructs a
`LogReplayEngine` directly, drives it from a CSV reader, and writes
JSONL to stdout — no FreeRTOS wrapper.

The WASM build (PLAN_WASM_CORE Step 2) binds `LogReplayEngine`
directly via embind. Same engine, three callers.

`scripts/check_core_purity.sh` enforces that
`onspeed_core/replay/LogReplayEngine` has no `Arduino.h`,
`FreeRTOS.h`, or `millis()` references.

## What firmware LogReplay should do

### Fix 1 — Rate-correct EMA at ingest

Two viable options. **Option A is preferred** (simpler, one code change).

**Option A: Up-sample at ingest.** LogReplay reads each row at the
log's actual rate, then up-samples to 208 Hz before feeding the EMA
(linear interp or hold-last). The EMA sees what the firmware always
sees. No filter changes.

```cpp
// pseudocode in LogReplay.cpp
const int targetHz = 208;
const int logHz = log.headerSampleRate;  // 50 or 208
const int upsampleRatio = targetHz / logHz;  // 4 or 1

for (each row in log) {
  for (int k = 0; k < upsampleRatio; k++) {
    interpolate(row, nextRow, k / upsampleRatio) -> sample;
    g_pIMU->ApplyEma(sample);          // same fixed-α EMA
    g_pIMU->Smoothed -> downstream;    // M5 wire, etc.
  }
}
```

Side effect: the ingested wire stream is at 208 Hz internally. The
downstream wire output to M5 is paced at its own 20 Hz, so the
upsampling is invisible to consumers — only the EMA sees it.

**Option B: Sample-rate-aware EMA.** Add a public
`onspeed_core/filters/EMAFilter.h` constructor that takes an
expected sample period. Adjust α at construct time so the
continuous-time τ stays ~0.0741 s regardless of input rate. Then
LogReplay swaps the filter for the right one based on log rate.

```cpp
// Continuous-time form: y[k+1] = y[k] + (1 - exp(-dt/tau)) * (x[k+1] - y[k])
EMAFilter accelEma(/*tau_s=*/0.0741f, /*dt_s=*/1.0f / logHz);
```

Option B is more honest (the filter genuinely is rate-aware). It also
changes a struct used in flight-time code paths. **Option A is
cheaper to land safely** and produces identical output to flight at
208 Hz log rate; defer Option B unless real flight bench-replay shows
Option A masks a defect.

### Fix 2 — Synthesize ADC sweep when missing

LogReplay knows the log version (header). When `flapsRawADC` column is
absent, it synthesizes the same smoothstep sweep that lives in JS
today, but in C++ in `LogReplay.cpp`.

The math is already a one-page port (the JS version is `synthLeverSweep`
in `tools/web/lib/replay/logReplay.js`):

1. **First pass**: fill every row's synth ADC with the *current*
   detent's pot value. Rows in long mid-flight stretches don't
   accidentally retain the initial value.
2. **Second pass**: paint a smoothstep window centered on each detent
   transition tick.

`FlapsDetector` then runs against the synth ADC and produces correct
fractional positions. The L/Dmax pip morphs smoothly. Nothing
downstream knows the data was synthesized.

When `flapsRawADC` IS present (newer logs), LogReplay ingests it
verbatim. No synthesis.

## Acceptance — bench replay

The firmware-side fix is correct when:

1. Take an SD log from a recent flight (Sam's `4_11_26` log fits).
2. Replay through LogReplay on the bench.
3. Capture the M5 wire output.
4. Compare against the wire output the firmware produced live during
   that same flight (need either a contemporaneous wire dump, or
   reconstruct expected wire output from the live-flight EFIS feed
   the SD log captured).

Slip ball, AOA bar, L/Dmax pip should all match flight to within a
small float tolerance — no visible twitchiness, no detent-tick jumps.

The native unit-test surface is a regression of `host_main replay`
output against committed goldens (existing `tools/regression/run_snapshot.py`).
A 50 Hz golden + a 208 Hz golden, both run through LogReplay, both
producing wire output that matches the existing live-mode reference.

## Cascading benefit — Replay tool deletes its patches

After this lands, post-WASM `PLAN_WASM_CORE.md` Step 2 can delete:

- `tools/web/lib/replay/logReplay.js::KACC_TAU_S` and the variable-dt
  EMA loop (~30 LOC).
- `tools/web/lib/replay/logReplay.js::synthLeverSweep` (~50 LOC).

The WASM-bound LogReplay does both correctly. The Replay tool just
calls into it. Drift impossible by construction — the Replay tool and
firmware run literally the same code on the same SD log.

## What this does NOT change

- Firmware behavior in flight. LogReplay is a separate mode (the
  firmware enters it via console command or boot flag). Live flight
  ingest paths are untouched.
- The 208 Hz IMU EMA itself. Its α stays at 0.060899. We're fixing the
  rate of input it sees in LogReplay mode, not the filter.
- The Replay tool's UI, sync, marks, clip builder. Those stay JS.

## Effort estimate

~3-4 days, in 2-3 small PRs.

- Day 1-2: **Engine extraction.** Move the per-row pipeline from
  `sketch_common/src/tasks/LogReplay.cpp` into
  `onspeed_core/src/replay/LogReplayEngine.{h,cpp}`. No behavior
  change. Existing firmware LogReplay still produces the same
  (twitchy, jumpy) output as before — but now via an engine call.
  Bench-test: SD log replay matches its prior behavior bit-for-bit.
  This is the riskiest PR; gets its own commit so it's reviewable.
- Day 3: Fix 1 (Option A — up-sample at ingest, inside the engine).
  Bench-test against a known SD log + flight reference.
- Day 4: Fix 2 (synthesize ADC when column is missing, inside the
  engine). Bench-test with an old (pre-PR-#221) log to verify pip
  morphs smoothly.

If bench tests reveal Option A masks something, cut over to Option B
(another 0.5-1 day; structural change to `EMAFilter`).

**Independence**: Fix 1 and Fix 2 can ship as separate PRs after the
engine extraction lands. Engine extraction is the load-bearing PR;
Fix 1 and Fix 2 are mechanical edits inside it.

## Why this is the right move

1. **Closes the last "Replay tool reimplements firmware" leak.** The
   percent-lift, anchor, tone-decision, AHRS algorithms already live
   in onspeed_core. The replay-mode rate adapter and ADC-synth gap are
   the last firmware-shaped patches living in JS. They belong in
   firmware.
2. **Makes LogReplay genuinely useful.** "Plug in an SD card, see what
   the M5 saw in flight" is the feature; today it's an approximation.
3. **Eliminates one branch of post-WASM JS** — no special replay code
   path. The WASM build is the firmware build.
4. **Future SD-log ingest paths inherit the fix.** Any new tool that
   loads SD logs (analysis, calibration replay, etc.) gets correct
   behavior automatically.

## Dispatch prompts

### PR 1 — Engine extraction (no behavior change)

```
Extract the LogReplay per-row pipeline into onspeed_core. Behavior
must not change in this PR.

WORKTREE: a fresh worktree off origin/master
PLAN: docs/superpowers/plans/2026-05-08-firmware-log-replay-parity.md
PRECEDENT: software/sketch_common/src/tasks/AHRS.cpp + software/
           Libraries/onspeed_core/src/ahrs/Ahrs.{h,cpp}. AHRS is the
           model: a thin task that delegates per-tick math to an
           onspeed_core engine.
COMPANION READING:
  - software/sketch_common/src/tasks/LogReplay.cpp/h (the task today)
  - software/Libraries/onspeed_core/src/ahrs/Ahrs.{h,cpp} (precedent)
  - software/sketch_common/src/tasks/AHRS.cpp (precedent task wrapper)
  - tools/web/lib/replay/logReplay.js (current JS patches, for context)

WHAT TO BUILD:
  1. New software/Libraries/onspeed_core/src/replay/LogReplayEngine.{h,cpp}.
     - Pure C++. No Arduino.h, no FreeRTOS.h, no millis(), no SD-card I/O.
     - Constructor: takes ConfigStruct, log sample-rate hint,
       flapsRawAdc-available bool.
     - Method: WireRecord step(const LogRow& row).
     - Holds whatever per-replay state is currently in LogReplay.cpp's
       file-scope globals — EMA state, detent-tracker state, etc. —
       as members.
  2. Move the per-row processing code FROM sketch_common/src/tasks/
     LogReplay.cpp INTO LogReplayEngine. Behavior preserved verbatim
     (no rate fix yet, no ADC synth yet).
  3. sketch_common/src/tasks/LogReplay.cpp retains its FreeRTOS task
     scaffolding but constructs a LogReplayEngine and calls step()
     per row. Test-pot mode + button mocks + LED blink stay where
     they are.
  4. tools/regression/host_main.cpp (or a new subcommand if
     PLAN_PYTHON_CONSOLIDATION Step 0 has landed) gets a `replay`
     subcommand that constructs a LogReplayEngine, reads CSV from
     stdin or --input, writes JSONL to stdout. Used by future
     regression fixtures.
  5. Snapshot regression: extend tools/regression/run_snapshot.py
     with a fixture covering LogReplay output. Output must bit-match
     a committed golden generated from current behavior. (We're
     freezing today's behavior so Fix 1/Fix 2 PRs can show diffs.)

VERIFY:
  - pio test -e native (existing tests still pass).
  - ./scripts/check_core_purity.sh (LogReplayEngine has no platform
    deps).
  - pio run -e esp32s3-v4p (firmware still builds).
  - Bench: SD log replay on real hardware produces same output as
    before this PR. Slip ball is still twitchy; pip still jumps.
    That's intentional — the bugs are preserved here, fixed in
    PR 2/PR 3.

COMMIT: "onspeed_core: extract LogReplayEngine (behavior unchanged)".
```

### PR 2 — Fix 1: rate-correct EMA at ingest

```
With LogReplayEngine extracted (PR 1 merged), fix the IMU EMA
rate-coupling bug.

WORKTREE: a fresh worktree off origin/master with PR 1 merged
PLAN: docs/superpowers/plans/2026-05-08-firmware-log-replay-parity.md

WHAT TO BUILD:
  - Inside LogReplayEngine::step(), if logSampleRateHz < 208,
    upsample to 208 Hz before feeding the IMU EMA. Linear interp
    between current row and previous row's IMU values, or hold-last;
    pick whichever is simpler given the existing engine state shape.
  - At logSampleRateHz == 208, no upsampling needed.
  - Wire output frequency to downstream stays unchanged — the engine
    only emits one WireRecord per input row; upsampling is internal
    to the EMA loop.

VERIFY:
  - tools/regression/run_snapshot.py with a 50 Hz fixture log:
    output now differs from PR 1's golden in exactly the IMU-derived
    fields (slip ball, pitch/roll). Regenerate the golden, commit it.
    Run again, green.
  - Bench: SD log replay shows slip ball matching flight smoothness.
    (Compare against a flight where Sam has independent reference.)

COMMIT: "replay: rate-correct LogReplay EMA at log ingest (Fix 1)".
```

### PR 3 — Fix 2: synth ADC when column missing

```
With LogReplayEngine + Fix 1 merged, address the missing-flapsRawADC
case for old logs.

WORKTREE: a fresh worktree off origin/master with PRs 1+2 merged
PLAN: docs/superpowers/plans/2026-05-08-firmware-log-replay-parity.md
COMPANION READING:
  - tools/web/lib/replay/logReplay.js::synthLeverSweep
    (the JS implementation we're porting)

WHAT TO BUILD:
  - In LogReplayEngine constructor, accept a flapsRawAdcAvailable
    bool (already added in PR 1).
  - When false, synthesize the ADC sweep across detent transitions:
    1. First pass: fill every row with current detent's pot value.
    2. Second pass: paint smoothstep window centered on each
       transition tick (mirrors firmware's flip-at-midpoint physics).
  - When true, ingest the column verbatim.
  - FlapsDetector inside the engine consumes the synth ADC the
    same as it consumes real ADC — no other code path knows.

VERIFY:
  - tools/regression/run_snapshot.py with a pre-PR-#221 fixture log
    (no flapsRawADC column): L/Dmax pip output morphs smoothly.
    Regenerate golden, commit it.
  - Bench: SD log replay of an old log shows pip morphing across
    detent transitions instead of jumping.

COMMIT: "replay: synth flap-pot ADC when log lacks the column (Fix 2)".
```

## Coordination

- Lands BEFORE `PLAN_WASM_CORE.md` Step 2. Step 2's "delete the JS
  variable-dt EMA + synth sweep" instruction depends on the firmware
  doing it correctly first.
- Independent of `PLAN_PYTHON_CONSOLIDATION.md` Step 0 (host_main CLI).
  Either order works; both are foundation work.
- Does not touch the Replay tool. The cleanup of JS patches is in
  WASM Step 2.
