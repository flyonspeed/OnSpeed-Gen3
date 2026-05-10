---
date: 2026-05-09
owner: Sam
status: post-PR-512-trial-run retro and architectural correction
related:
  - 2026-05-09-replay-m5-wasm.md
  - 2026-05-08-replay-INDEX.md
---

# Replay tool retro: bugs we missed, and the architecture that would have caught them

This doc is written to a future agent (or future Sam) picking up the
replay-tool work after a context reset. It's a brutally honest account
of the bugs that survived the foundation PRs (#507, #511, #512) and
made it to first user trial, AND a redirected architecture for fixing
them at the root.

Read this BEFORE writing any new code on the replay tool.

## What worked

- **PR #507 (M5 firmware → WASM)**: solid. Compiles the firmware
  unchanged, exposes state-var accessors, virtual `millis()` for
  time control. Bit-identical to the on-hardware code path.

- **PR #511's `tone_calc` binding**: small, well-scoped, drift-impossible.

- **PR #511's `LogReplayEngine` field-completeness audit**: identified
  fields like `gOnsetRate`, `turnRateDps`, `oatC` that were defaulted
  to zero. Caught real gaps.

## What didn't work — bugs that shipped to trial

### Bug 1: `iasValid` was a JS hand-port, broken silently

**Symptom:** IAS dashes never appear during low-IAS taxi. Every flight
phase shows iasValid=true even at 0 kt.

**Root cause:** `tools/web/lib/pages/ReplayPage.js::rowObjAt(log, i)`
computed `iasValid = iasKt > 0`. The firmware uses a hysteretic state
machine (rising 20 kt, falling 15 kt) in
`onspeed_core/src/sensors/IasAlive.h::UpdateIasAlive`. We typed the
wrong rule into JS and never tested it.

**How testing missed it:** the wire-completeness golden test used a
synthetic 6-row fixture where `iasValid` was provided directly (as
`true`/`false` per row). It never exercised the rule that DERIVES
iasValid from raw IAS. The pipeline test never saw the JS port.

### Bug 2: AOA polynomial coefficients dropped on cfg round-trip

**Symptom:** Engine returns `aoaDeg ≈ 0` regardless of pressure inputs.
PercentLift stuck at the value of `(0 - alpha0) / (alphaStall - alpha0)`
— which for full-flaps was 44%, masquerading as a real reading.

**Root cause:** `software/Libraries/onspeed_core/wasm/bindings.cpp`
had:
```cpp
// kFit and AoaCurve default to zero/identity from SuFlaps ctor.
```
The `parse_config` export side emitted thresholds (`alpha0`,
`alphaStall`, etc.) but NOT the AOA polynomial. The cfg-import side
(`parseConfigVal`, used by `LogReplayEngine`'s constructor) restored
the same thresholds but left the polynomial as a zero default. So
the engine's `CurveCalc(coeffP, AoaCurve)` returned the constant
term (0) for every input.

**How testing missed it:** the wire-completeness golden test used a
hand-built cfg object (in `wireBridgeForTest.js`) that bypassed
`parseConfigXml` entirely. The cfg parser unit test verified that
parsing produced the right field values but never drove the engine
with a parsed cfg. The replay tool DID go through the broken path,
which is why real-flight data showed flat AOA.

### Bug 3: Catch-up scramble on first sync / scrub

**Symptom:** When user marks the video anchor, screen burst-flashes
through ~30 seconds of stale state before settling. Same on every
forward scrub > 5 s.

**Root cause:** The per-frame effect in `ReplayPage.js` ticked
virtual time forward in 50 ms steps. On first sync, last=0, target
=videoT*1000 (1.5M ms typical). The effect injected ~600 wire frames
per video frame and called `setM5State` with the resulting state,
which mostly reflected pre-takeoff log rows.

**How testing missed it:** no test runs the React effect. Render-smoke
tests render mode components against synthetic state objects. The
"videoT → effect → setM5State" path is the canonical glue and has
no automated coverage.

### Bug 4: gOnset / Slip jitter, real or fake?

**Symptom:** gOnset bar jumps wildly per video frame even at cruise.

**Root cause:** UNKNOWN. Two hypotheses:
- (a) Real: the 50 Hz SD log aliases IMU vibration into the smoothed
  signal; the firmware running on 208 Hz IMU silently filtered it
  out. Our replay is faithful but ugly.
- (b) Fake: there's a bug in the gOnset filter wiring or the state
  read.

**How we'd tell:** record a 208 Hz reference log on a bench (issue
#485), replay it, see if gOnset is smooth. If yes, (a) is the
culprit. If no, dig.

**How testing missed it:** no flight-data fixture exists. All
fixtures are synthetic.

### Bug 5: Mode click during pause doesn't refresh visible mode

**Symptom:** Pause video, click "Indexer" — mode buttons highlight
correctly but SVG keeps showing previous mode.

**Root cause:** Mode change calls `sim.setMode()` synchronously, which
updates the firmware's `displayType` global. But the React
`m5State` was not refreshed — only the per-frame effect refreshes
it, and it only fires on videoT change. Paused video → no refresh
→ stale state drives SVG.

**Fix:** mode-id effect now calls `setM5State(sim.read())` directly
after `setMode`. Trivial bug, ugly UX.

### Bug 6: Files reset on page reload

**Symptom:** User refreshes browser, must re-pick all 3 files.

**Root cause:** Replay tool stores no file-handle persistence. Sync
state is keyed by file content hash, but you have to re-load the
file to get back to it.

**Fix:** `FileSystemFileHandle` API stored in IndexedDB across
sessions. Documented in PR #461's plan as a Layer-1 task; not yet
shipped.

### Bug 7: M5-accurate toggle off then on → blank

**Symptom:** Toggle M5-accurate off, then on. Mode renders blank.

**Root cause:** TBD. Likely the sim load effect bails out (sim
already exists, returns early), but `m5State` has been set to null
by the off-toggle, and no per-frame effect fires before user
interacts again to refresh it.

**Fix:** TBD. Forces an immediate `setM5State(sim.read())` after
toggle-on.

### Bug 8: Export plays at realtime + WebM is broken

**Symptom:** Export-WebM button records in real time and produces a
"black shadow" video.

**Root cause:** Phase 4.5 of the original PLAN_VIDEO_OVERLAY was
deferred (mp4-muxer for faster-than-realtime). Current implementation
uses MediaRecorder which only records as fast as playback. The black
output sounds like the canvas-capture is grabbing the wrong layer
(maybe page background instead of overlay-frame).

**Fix:** Phase 4.5 work; not blocking trial run.

## The architectural diagnosis

The plan's "drift impossible by construction" claim relied on three
preconditions. Foundation PRs nailed only one of them.

### Precondition 1: Same code runs on hardware and in replay

✅ Achieved. M5 firmware compiles to WASM via PR #507. The wire-
decode + state-machine path is bit-identical.

### Precondition 2: Same data shape flows through both

❌ Broken in multiple places:

- **`rowObjAt(log, i)`** is a JS hand-derivation of every engine
  input field. Six fields are mapped, two are computed (iasValid,
  iasKt validation). Each computation is a potential drift seam.
  Bug 1 lived here.

- **`bindings.cpp::parse_config`** drops the AOA polynomial and the
  KFit constant on export, and `parseConfigVal` restores them as
  zero defaults on import. Bug 2 lived here.

- **`wireBridge.js::buildDisplayInputs`** is a JS hand-derivation
  of every wire-encoder input field. Currently 23 fields, each a
  potential drift seam. The presentation smoother lives here too.
  Future bugs will live here.

### Precondition 3: A known-good reference proves correctness

❌ Missing entirely.

All test fixtures are synthetic. The wire-completeness golden was
generated FROM the code, so it locks in whatever the code produced
on day 1 — including bugs. There's no flight-data fixture and no
firmware-emitted-bytes reference.

### Why so many seams?

The plan modeled the replay tool as "JS UI + WASM artifacts." That
framing implies the JS layer is small glue. **It isn't.** It's
where the data shape gets translated between three different
representations (log row, engine input, wire input). Every
translation is a potential bug location.

## The corrected architecture

The "no JS hand-ports" goal should extend to data shape, not just
algorithms. The JS layer should be glue ONLY: file pickers, time
control, accessor reads, SVG render. Every shape transformation
should live in C++ where the firmware's transformations already
live.

### Target

```
[JS]  CSV file
       ↓ ArrayBuffer
[WASM] LogCsv::ParseRow → LogRow                     (already exists)
       ↓
[WASM] LogReplayTask::process(logRow, cfg) → wire bytes
       ↓ Uint8Array
[JS]  m5sim.injectBytes(bytes)
       ↓
[WASM] M5 firmware: SerialRead → SerialProcess → main loop()
       ↓
[JS]  m5sim.read() → state object
       ↓
[JS]  SVG component
```

**Two C++-side artifacts:** the new `LogReplayTask` (wraps engine +
wire encode) and the existing M5 firmware. **JS is purely glue.**

### What `LogReplayTask` does

Wraps `LogReplayEngine` + the wire-encode call. Already exists in
`sketch_common/src/tasks/LogReplay.cpp` for the firmware's built-in
log-replay mode. Needs to be:
1. Lifted into `onspeed_core` (it's currently in sketch_common,
   which the WASM build doesn't compile).
2. Exposed via embind as `process_row(logRow, cfg) → frameBytes`.
3. Stateful (carries iasAlive state, EMA filter state, gHistory,
   etc.).
4. Resettable (caller calls `task.reset()` on backward scrub).

After this lands:
- `rowObjAt` deletes (LogRow is constructed in C++).
- `wireBridge.js::buildDisplayInputs` deletes (wire inputs derived
  in C++).
- `iasValid` hand-port deletes (firmware computes it).
- AOA polynomial bug becomes impossible (engine uses real cfg, not
  parsed-and-dropped JS cfg).

### What stays in JS

- File pickers, video element, UI controls.
- Sync (videoT ↔ logTimestamp anchor mapping).
- Per-frame driver: read CSV bytes, call `task.process_row()` for
  the right row, inject bytes into m5sim, read state, render SVG.
- SVG mode components (rendering pixels from state vars).
- Export pipeline.

## What "correct by construction" requires

Three test types we don't have today:

### Test type 1: End-to-end fixture replay

Take a 30-row real-flight log snippet, the cfg used to record it, and
the known-good wire-byte sequence the firmware emitted at log time.

```
[fixture] real_flight_30s.csv + matching cfg
   ↓
[pipeline] entire JS → WASM → M5 sim → state path
   ↓
[assert] state at row N matches reference state at row N
```

Generated ONCE from a known-good firmware build with a recording
mode. Frozen as bytes. Captures EVERY seam. Today we don't have a
recording mode — we'd need to add one (or use an existing log +
manually-validated reference).

### Test type 2: Cfg round-trip

```
parse_config(real_xml) → JS object → parseConfigVal → OnSpeedConfig
                         ↓ assertion
                     LogReplayEngine(parsedCfg).step(known_inputs)
                         ↓ assert AOA = expected
```

Round-trips the cfg through both bindings and verifies engine
output. Bug 2 would have failed this immediately.

### Test type 3: Hysteretic state-machine fixtures

For every stateful firmware function (iasAlive, GOnsetFilter,
SavGolDerivative, etc.), have a synthetic input ramp that exercises
state transitions. Assert state-output matches what the firmware
function produces against the same input.

```
[fixture] IAS ramp 0 → 25 → 10 → 25 (kt) over 50 rows
[apply]   pass through engine
[assert]  iasValid follows firmware hysteresis exactly
```

## How to use this doc

If you're picking up the replay tool:

1. **Don't add features yet.** First understand whether
   `LogReplayTask` lift-into-onspeed_core has happened. If yes, the
   JS-side hand-ports above should already be deleted. If no, that
   refactor is the highest-priority next PR.

2. **For every bug Sam reports**, ask: which seam did it live in?
   If it's a hand-derived shape transformation, the right fix is
   probably to delete the hand-port, not patch it.

3. **Before merging anything**, ask: is there a fixture-driven test
   that would have caught this if we'd had it on day 1? Not
   necessarily that you write the test — at least know whether the
   test gap is intentional.

4. **Real-flight fixture is gold.** When Sam records a 208 Hz
   bench-flight log (issue #485), USE it. That's the closest thing
   to ground truth we have.

## Open issues

- #485: 208 Hz bench-flight reference log
- #492: firmware re-opens log on rate change
- #508: gOnsetRate (closed by #511)
- #509: docs-site tone-sim hand-port → onspeed_core ToneCalc
- #510: M5 replay WASM SDL2 dead-code tradeoff
- (NEW) Lift `LogReplayTask` into `onspeed_core`, eliminate
  rowObjAt + buildDisplayInputs. **Highest priority post-trial.**
- (NEW) Cfg round-trip test
- (NEW) iasValid hysteresis fixture test
- (NEW) End-to-end real-flight fixture (depends on #485)
- (NEW) WebM export black-shadow bug (Phase 4.5)
- (NEW) M5-accurate toggle-off-then-on stays blank
- (NEW) FileSystemFileHandle persistence (Layer-1)

## A note on context

This work was done across one extremely long session. Context was
near full when the most important architectural realizations
landed. **The lessons here matter more than any specific fix.** If
you find yourself patching symptoms in JS-side hand-ported logic,
stop and lift to C++ instead.
