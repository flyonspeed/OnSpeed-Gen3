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

~2 days, in 1-2 small PRs.

- Day 1: Fix 1 (Option A — up-sample at ingest). Bench-test against a
  known SD log + flight reference.
- Day 2: Fix 2 (synthesize ADC when column is missing). Bench-test
  with an old (pre-PR-#221) log to verify pip morphs smoothly.

If bench tests reveal Option A masks something, cut over to Option B
(another 0.5-1 day; structural change to `EMAFilter`).

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

## Dispatch prompt

```
Implement PLAN_FIRMWARE_LOG_REPLAY_PARITY.md: fix firmware LogReplay
so SD-log replay produces wire output indistinguishable from the
flight that wrote the log.

WORKTREE: a fresh worktree off origin/master
PLAN: docs/superpowers/plans/2026-05-08-firmware-log-replay-parity.md
COMPANION READING:
  - software/OnSpeed-Gen3-ESP32/LogReplay.cpp/h (the mode itself)
  - software/Libraries/onspeed_core/src/filters/EMAFilter.h
  - software/OnSpeed-Gen3-ESP32/IMU330.cpp (how EMA is applied today)
  - tools/web/lib/replay/logReplay.js (the JS patches we're closing)

WHAT TO BUILD:
  Fix 1 (rate-correct EMA at ingest):
    - Read header sample rate from the log.
    - If logHz < 208, up-sample to 208 Hz before feeding the EMA
      (linear interp or hold-last between rows). Filter sees 208 Hz.
    - If logHz == 208, no upsampling needed.
    - Downstream wire output stays paced at its existing 20 Hz tick.
  Fix 2 (synth ADC when column missing):
    - Detect missing flapsRawADC column in log header.
    - Two-pass synth (mirror tools/web/lib/replay/logReplay.js::synthLeverSweep):
      1. Fill every row with current detent pot.
      2. Paint smoothstep window around each detent transition.
    - When the column IS present, ingest verbatim.

VERIFY:
  - tools/regression/run_snapshot.py with a NEW 50-Hz fixture log and
    a NEW 208-Hz fixture log. Both should produce wire output matching
    a committed golden.
  - Bench: LogReplay an SD log on real hardware, compare M5 readout
    against an independent reference for that flight. No visible twitchiness
    in slip ball; pip morphs smoothly through detent transitions.

COMMIT: "firmware: rate-correct LogReplay EMA + synth ADC sweep".
```

## Coordination

- Lands BEFORE `PLAN_WASM_CORE.md` Step 2. Step 2's "delete the JS
  variable-dt EMA + synth sweep" instruction depends on the firmware
  doing it correctly first.
- Independent of `PLAN_PYTHON_CONSOLIDATION.md` Step 0 (host_main CLI).
  Either order works; both are foundation work.
- Does not touch the Replay tool. The cleanup of JS patches is in
  WASM Step 2.
