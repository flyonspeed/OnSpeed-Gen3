---
date: 2026-05-09
owner: Sam
status: PR 1 + 1.5 merged; PR 2 (#512) UNMERGED — superseded by A/B-toggle approach in retro
supersedes_in_replay: PLAN_VIDEO_OVERLAY Layer 0+ rendering path
superseded_by_for_pr2: 2026-05-09-replay-retro.md (PR 2's UI-integration approach was redirected after trial-run findings)
relates_to:
  - 2026-05-08-replay-INDEX.md
  - 2026-05-09-replay-retro.md
  - 2026-05-09-replay-continue-prompt.md
---

# Project B2 — M5 firmware compiled to WASM (state-machine layer)

> **🛑 PR 2 onward is REDIRECTED. Read `2026-05-09-replay-retro.md` first.**
> The post-trial-run retro identified that PR 2's "wire JS UI through M5
> sim" approach left intact several JS-side data-shape hand-derivations
> (`rowObjAt`, `buildDisplayInputs`, hand-coded iasValid rule) that are
> drift seams. Real bugs shipped through them. The corrected approach is
> in the retro: lift `LogReplayTask` into `onspeed_core` so JS becomes
> pure glue. PR 1 and PR 1.5 (below) are still correct and merged.

This plan is **Project B2** in the foundation set (see
`2026-05-08-replay-INDEX.md` for sequencing). It compiles the
**state-machine layer** (M5-Display firmware source) to WASM. B1
(`onspeed_core` algorithms) already shipped (PR #496); B2 adds the
state machine over the wire on top of B1.

After B2 lands, five consumers all share **one** "show the M5
display" pipeline:

1. **M5 hardware** (Basic / Core2) — native build, real wire bytes.
2. **huVVer-AVI** — native build, same source as M5 hardware.
3. **Replay tool** — WASM build, synthesized wire bytes from log replay.
4. **X-Plane plugin** — direct C++ link, X-Plane datarefs piped through wire encode.
5. **`tools/m5-replay`** — native build, Python harness drives via USB-TTL.

**Carved out:** the live `/indexer` page. It works fine over
WebSocket JSON today. Migrating it to consume the M5 firmware WASM
via WebSocket bytes is conceptually correct but **not part of this
plan**. If/when we revisit, fresh plan.

## The two invariants of "show what the pilot saw"

"What the pilot saw" depends on **two** things being correct, not
one. We initially conflated them; the plan was a level too thin.

**Invariant 1 — Rendering owned by the M5 firmware.** The 20 Hz
graphics tick, the 2 Hz text snapshot, the SavGol-on-IAS decel
computation, the slip-ball math, mode dispatch, gHistory ring
buffer, IasIsValid edge handling — all of this lives in the M5
firmware. Replay must run that exact source code, not a JS hand-port
of it. **PR 1 closes this invariant** by compiling M5 firmware to
WASM and exposing state-var accessors.

**Invariant 2 — Wire bytes feeding the M5 are complete.** The M5
firmware is a function from "wire frames" to "display state." If
the wire frames it receives are missing fields (e.g., `gOnsetRate =
0`, anchors all zero, `turnRateDps = 0`), the resulting display
state is wrong even though the firmware itself is doing exactly
what it does on real hardware. **PR 1.5 closes this invariant** via
a `LogReplayEngine` field-completeness audit + golden-fixture test.

Both must hold. PR 1 alone is not enough; the wire bytes the M5 sim
parses must populate every meaningful `DisplayBuildInputs` field.
We discovered this gap mid-flight when `gOnsetRate` (issue #508),
`turnRateDps`, `oatC` came up as "stuck at zero" in replay despite
the firmware being correct. Several of these were filed
incrementally; PR 1.5 audits the whole struct in one pass with a
golden-fixture test that prevents recurrence.

## The story in one paragraph

The replay tool's overlay must paint **what the pilot saw on the M5
screen during that flight**, frame-for-frame. Today the replay tool
hand-rolls a "build a 22-field record from log row, render SVG
directly" path that bypasses the M5's wire-rate downsampling, wire
quantization, 2 Hz text snapshot, and locally-computed decel rate.
Result: ball jitter, altitude flicker, text updating too fast, decel
gauge stuck at zero. The fix is the architectural close-out we've been
pointing at: **compile the M5-Display firmware itself to WASM and run
it as the replay tool's state engine** (PR 1), AND **complete the
`LogReplayEngine`'s coverage of every wire field** so the M5 sim
receives accurate input (PR 1.5). JS feeds wire frames in at
20 Hz; the M5 firmware code (literally the same source that flashes to
the panel) decides what numbers to display, when to snapshot, what
the ball position is. JS reads M5 state vars and renders SVG from
them. Drift is impossible because the M5 firmware *is* the replay
engine — but ONLY if the wire bytes feeding it are complete.

## What's already built (this is mostly assembly, not invention)

1. **`software/OnSpeed-M5-Display/sim/build_wasm.sh --target live`**
   compiles the full M5 firmware (`main.cpp` + `SerialRead.cpp` +
   `GaugeWidgets` + M5Unified + M5GFX SDL backend + `onspeed_core`) to
   a single Emscripten module. Already exports `_inject_serial_byte`.
2. **`software/Libraries/onspeed_core/src/proto/DisplaySerial.cpp`**
   `BuildDisplayFrame()` produces the exact 76-byte v4.23 wire bytes
   from a `DisplayBuildInputs` struct. Already used by host_main and
   firmware. Already in our existing WASM bindings (`bindings.cpp`).
3. **`software/Libraries/onspeed_core/src/replay/LogReplayEngine`**
   processes log rows into `ReplayStepResult`. Already smooths accel.
   Already in WASM. PR #487/#490/#491 landed it.
4. **`tools/web/lib/replay/logReplay.js`** wraps the WASM
   `LogReplayEngine` for JS callers. Already used by `ReplayPage.js`.

The only piece missing is **the M5 sim WASM consumed from JS without
its SDL2 canvas, with state-var accessors exposed.** Everything else is
plumbing.

## Why this is the right architectural move

The repo's INDEX and AGENT-CONTEXT have been pointing at this for
weeks (search them for "M5 simulator" and "drift impossible by
construction"). The unified architecture diagram in
`2026-05-08-replay-INDEX.md` already shows "M5 simulator (full-featured
WASM, browser)" as one of the consumers of `onspeed_core`. The replay
tool's box is "Preact UI + WASM-bound algorithms". This plan **collapses
those two boxes into one**: the replay tool *is* the M5 simulator,
overlaid on cockpit video. After this lands, the replay tool no longer
contains any rendering math — it contains a video player, a sync
control, and a thin SVG presentation of M5 state vars.

Concretely it removes from the replay tool:
- `slipFromLateralG` and the SlipBall component math (replaced by reading `Slip` from M5).
- `reassemble.js` lag-pipeline gymnastics (we still need it to drive WASM, but the *display* doesn't see the raw results).
- The 22-field `rec` object in `ReplayPage.js::buildRecord` (replaced by a thin reader of M5 globals).
- The "convert m/s to fpm" lines, the percent-lift math, the AOA-to-fraction conversions — all gone, the M5 already does them.
- The `gOnsetRate: 0, // not available in log replay` placeholder — the M5 reads it from the wire, where the engine puts it.

It also fixes by side-effect, with no replay-side code:
- 2 Hz text update cadence (M5 already does this).
- Wire quantization for altitude / G / VSI / percent-lift / lateral G (M5 sees wire-quantized values).
- Decel rate (M5 computes its own from wire IAS via its SavGol filter).
- AOA bar position (M5 already maps PercentLift → bar geometry correctly).
- Spin recovery cue, edge tape, all five display modes — all already in M5 firmware.

## Architecture

```
┌──────────────────────┐
│  Log file (CSV)      │
│  + cfg + video MP4   │
└────────┬─────────────┘
         │
         │ user picks files
         ▼
┌──────────────────────┐
│  parseLog.js         │
│  parseConfigXml.js   │      already exists
│  videoElement        │
└────────┬─────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────┐
│  LogReplayEngine (WASM, onspeed_core/replay)           │ already exists
│  step(LogRow) → ReplayStepResult                       │
│  Computes smoothed accel, flapsRawAdc synth, AOA, etc. │
└────────┬───────────────────────────────────────────────┘
         │                                ▼ ReplayStepResult
         │                  ┌────────────────────────────────────────┐
         │                  │  BuildDisplayFrame (WASM, proto/)      │ already exists
         │                  │  ReplayStepResult → 76-byte wire frame │
         │                  └────────┬───────────────────────────────┘
         ▼                           │
┌──────────────────────┐             │ wire bytes @ 20 Hz
│  videoT (player)     │             │
└────────┬─────────────┘             ▼
         │              ┌─────────────────────────────────────────┐
         │              │  M5 firmware (WASM, sim/build_wasm.sh   │ NEW: -DREPLAY_TARGET
         │              │       --target replay)                  │
         │              │  inject_serial_byte() in,               │
         │              │  state-var accessors out:               │
         │              │  displayIAS, displayPalt, Slip,         │
         │              │  PercentLift, gOnsetRate,               │
         │              │  SmoothedDecelRate, displayType, etc.   │
         │              └────────┬────────────────────────────────┘
         │                       │ accessor reads at video frame rate
         ▼                       ▼
┌────────────────────────────────────────────────────────┐
│  Replay UI (Preact)                                    │
│  - synces video to log via anchor-based mapping        │
│  - drives M5 sim's virtual clock from videoT           │
│  - reads M5 state, renders existing SVG components     │
└────────────────────────────────────────────────────────┘
```

The dotted line is "shared by all consumers": same wire bytes the real
M5 receives, same M5 firmware, same display logic. The replay tool's
SVG is just a presentation of M5 state.

## Three load-bearing technical questions

### 1. How do we drive M5 time from video time?

The M5 firmware uses `millis()` heavily — every cadence (graphics
50 ms, numbers 500 ms, flash 250 ms, frame-dt for SavGol) reads from
it. In the existing WASM build, `millis()` forwards to M5GFX SDL's
wall clock. **For replay we need a virtual `millis()` that JS
controls.**

Approach: `sim/ArduinoShim.h` adds a `#ifdef REPLAY_TARGET` branch
that uses an `extern uint64_t g_replayMillis` instead of
`lgfx::millis()`. JS sets `g_replayMillis` before each `tick()` call.
SDL is **excluded entirely** from the replay build (no `-sUSE_SDL=2`,
no `M5GFX_BACK_COLOR`). M5GFX doesn't need a panel for replay because
we don't render through it — we read state vars and render SVG.

(Or: we still link M5GFX but use its software canvas without a
display backend. M5GFX has a `Panel_Null` style mode used in some
test contexts — needs verification. If not, we add one. Either way,
no SDL.)

### 2. Does M5GFX need to be linked at all?

The M5 firmware does call into M5GFX for `gdraw.fillSprite`,
`gdraw.drawString`, etc. — those are fine to keep, they paint into a
sprite that we never blit. They're cheap. Or we can stub them all
behind `#ifdef REPLAY_TARGET` no-ops. The state-var update logic
(snapshots, `Slip` computation, `PercentLift`, `gOnsetRate`,
`SmoothedDecelRate`) is **separate** from the drawing calls.

Simpler choice: **stub the rendering**. A `RenderShim.h` that
`#ifdef REPLAY_TARGET` replaces `M5Canvas gdraw(...)` with a
zero-cost stand-in that no-ops every draw call. M5Unified and M5GFX
go away entirely. Build is small (~tens of KB of WASM, not megabytes).
Net work: identify every `gdraw.*` call and route it through the shim.
There are ~hundred call sites; either we do it once or we accept
linking M5GFX.

**Recommendation: stub it.** Cleaner WASM bundle, faster startup,
simpler dependency tree. ~200 LOC of stub macros.

### 3. How do JS callers read M5 state?

Two patterns work. Pick one.

**Option A: explicit accessor exports.**

```cpp
extern "C" EMSCRIPTEN_KEEPALIVE float replay_get_displayIAS()    { return displayIAS; }
extern "C" EMSCRIPTEN_KEEPALIVE float replay_get_displayPalt()   { return displayPalt; }
extern "C" EMSCRIPTEN_KEEPALIVE int   replay_get_Slip()          { return Slip; }
extern "C" EMSCRIPTEN_KEEPALIVE float replay_get_PercentLift()   { return PercentLift; }
// ... ~25 of these
```

JS calls each one per frame. Pros: simple, type-safe per field, easy
to grep. Cons: ~25 WASM↔JS calls per frame (cheap individually but
adds up; 25 × 60 fps = 1500 calls/sec, fine).

**Option B: a single "snapshot to JSON" export that returns a
struct.**

```cpp
extern "C" EMSCRIPTEN_KEEPALIVE void replay_snapshot(uint8_t* outBuf, size_t outLen);
```

That writes a packed binary struct or JSON into a JS-allocated
buffer. JS unpacks. Pros: single WASM call per frame. Cons: extra
struct definition, ABI churn whenever fields change.

**Recommendation: Option A.** WASM↔JS call overhead is ~100 ns each;
25 calls is 2.5 μs per frame, lost in the noise. The maintenance
benefit (each field is greppable, type-safe, and individually
testable) outweighs the bundling argument.

## PR sequence

This is one feature, three reviewable PRs. Each PR is independently
shippable to master without breaking anything (the replay tool today
keeps working until the last PR cuts the swap).

### PR 1 — `feat(m5): add replay WASM build target` (~1 day)

Build the WASM artifact and prove it works in isolation. No JS UI
integration yet — that's PR 2.

- `software/OnSpeed-M5-Display/sim/build_wasm.sh` adds `--target replay` (alongside existing docs/live targets).
- `sim/ArduinoShim.h` gets `#ifdef REPLAY_TARGET` block: virtual `millis()`/`micros()` from `extern uint64_t g_replay_millis_us`. SDL/wall-clock path is **excluded** when `REPLAY_TARGET` is defined.
- `sim/RenderShim.h` (new file) — `#ifdef REPLAY_TARGET` macros stub every M5GFX draw call (`gdraw.fillSprite`, `gdraw.drawString`, `gdraw.print`, `gdraw.pushSprite`, etc.) to no-ops. Budget: 500 LOC. If the stub layer exceeds budget, regroup and consider linking M5GFX with a `Panel_Null` backend instead.
- `sim/ReplayMain.cpp` (new file, replaces `SimMain.cpp` for this target):
  - `replay_init()` — calls firmware `setup()` once.
  - `replay_set_time(uint64_t millis)` — writes `g_replay_millis_us = millis * 1000`.
  - `replay_loop()` — calls firmware `loop()` once.
  - `replay_inject_byte(char c)` — calls `InjectSerialByte(c)`.
  - `replay_set_displayType(int mode)` — writes `displayType` (lets the JS UI cycle modes without simulating a button press).
- **Accessor exports for ALL FIVE modes' state.** Group by mode:
  - **Always populated** (used by all modes): `displayIAS`, `displayPalt`, `displayPitch`, `displayVerticalG`, `displayPercentLift`, `displayDecelRate`, `Slip`, `PercentLift`, `gOnsetRate`, `IAS`, `Palt`, `IasIsValid`, `displayType`, `iVSI`, `OAT`, `FlightPath`, `Pitch`, `Roll`.
  - **Mode 0 (Energy):** anchors `TonesOnPctLift`, `OnSpeedFastPctLift`, `OnSpeedSlowPctLift`, `StallWarnPctLift`, `PipPctLift`, `FlapsMinDeg`, `FlapsMaxDeg`, `FlapPos`.
  - **Mode 4 (Historic G):** `gHistoryIndex` (int), plus a pointer accessor `replay_get_gHistory_ptr()` returning `float*` to the 300-element ring buffer (JS reads via `HEAPF32.subarray()`).
  - **Spin recovery / data mark:** `SpinRecoveryCue`, `DataMark`.
  - Total: ~28 scalar accessors + 1 array pointer.
- Output: `tools/web/lib/replay/m5sim/onspeed_m5.{js,wasm}` (analogous to `tools/web/lib/wasm/onspeed_core.{js,wasm}` from PR #496). Build script copies the artifact across after `emcc`.
- **Bundle size budget:** target <500 KB total WASM (M5 firmware should be small once M5GFX is stubbed). Measure in PR; if over, file an issue and revisit.

**Verifiable test (Node.js, no browser yet):** ship a small Node test
under `software/OnSpeed-M5-Display/test/test_replay_wasm.js` that:
1. Loads the WASM module.
2. Calls `replay_init()`.
3. Calls `replay_set_time(0)` then injects a known wire frame from `software/Libraries/onspeed_core/test/fixtures/display_frame_v423.bin` byte-by-byte.
4. Advances `replay_set_time(50)` and calls `replay_loop()` 10 times (driving 10 graphics ticks).
5. Asserts `replay_get_Slip()` matches the expected value computed from the fixture's lateral G via `int(-lateralG * 34/0.04)`.
6. Asserts `replay_get_displayIAS()` is still 0 at t=200ms (numbers haven't snapshotted yet).
7. Advances to t=600ms, calls `replay_loop()`, asserts `replay_get_displayIAS()` is now non-zero (numbers snapshotted).
8. Repeats for each of the five modes (`replay_set_displayType(0..4)` then verify mode-specific state populates).

**Sabotage check (mandatory):**
- Comment out `Slip = int(-LateralG * 34 / 0.04)` in `SerialRead.cpp::SerialProcess` — test #5 must fail.
- Flip `updateRateNumbers` from 500 to 50 in `main.cpp` — test #6 must fail (numbers snapshot too early).
- If either sabotage doesn't fail the test, the test isn't actually exercising the production code — fix the test before merging.

### PR 1.5 — `feat(replay): complete LogReplayEngine wire-field coverage` (~half-day)

PR 1 closes Invariant 1 (rendering owned by M5 firmware). PR 1.5
closes Invariant 2 (wire bytes feeding the M5 are complete). Without
this, PR 2's trial run shows zero gauges for `gOnsetRate`,
`turnRateDps`, `oatC`, etc., even though the firmware itself is
correct.

**Why this is its own PR.** During PR 1's design we treated
`LogReplayEngine` as a finished artifact — "PRs #487/#490/#491
landed it." That was wrong. The engine was scoped for *its* original
use case (replay an SD log through algorithm code to verify
behavior), not for *this* one (build complete wire frames for the M5
firmware to consume). Several wire fields were never populated
because nothing depended on them in the original use case. We
discovered this via the `gOnsetRate` gauge symptom; rather than
trickle one issue per missing field, we audit the entire
`DisplayBuildInputs` struct in one PR with a golden-fixture test
that prevents recurrence.

#### Audit deliverable

For each field in `software/Libraries/onspeed_core/src/proto/DisplaySerial.h::DisplayBuildInputs`,
classify it into exactly one bucket:

- **Engine-populated** (from log row or computed via filter at log
  rate). Example: `lateralG` via `accelLatEma_`. PR 1.5 verifies these
  are correct.
- **Engine-populated but currently TODO'd to default zero.** Example:
  `gOnsetRate`, `turnRateDps`, `oatC`. PR 1.5 implements these.
- **wireBridge-supplied at PR-2 time** (from cfg + cached state).
  Example: anchors (`tonesOnPctLift` etc.), `flapsMinDeg`/`flapsMaxDeg`.
  PR 1.5 documents these in `LogReplayEngine.h` with a comment
  pointing forward to PR 2.
- **Constant by design.** Example: `spinRecoveryCue` (always 0
  today, reserved for future use). PR 1.5 documents this.

The classification lives in a comment block at the top of
`LogReplayEngine.h`'s `ReplayStepResult` struct, table-formatted, so
the next reader sees the complete map.

#### Engine-side implementations needed (this PR)

Concrete fields known missing today:

- **`gOnsetRate`** (issue #508): instantiate `GOnsetFilter` (already in
  `onspeed_core/filters/`), feed `accelVertSmoothed` + `dt`, write to
  `out.gOnsetRate`. ~5 lines.
- **`turnRateDps`**: from log column `YawRate` (already on `LogRow` as
  `imuYawRateDps`). One-line copy.
- **`oatC`**: from log column `OAT`. One-line copy. Comment that this
  is only meaningful when the original flight had an OAT sensor.
- **`iasValid`**: this is **NOT** a simple "IAS > 0" check. It's a
  hysteretic state machine in
  `software/Libraries/onspeed_core/src/sensors/IasAlive.h` with a
  20 kt rising threshold and 15 kt falling threshold (5 kt
  hysteresis to prevent chatter at the boundary). Once true, stays
  true above 15 kt; once false, stays false below 20 kt. **State is
  per-flight**, threaded across consecutive rows.

  For replay, two cases:

  **Case 1 (modern logs):** `iasValid` is already a column in the SD
  log (`LogSensor.cpp:540` writes `row.iasValid = g_Sensors.bIasAlive`,
  mirroring the producer's hysteretic state). Engine copies it
  straight through: `out.iasValid = row.iasValid`. Zero drift; the
  state was computed at flight time and persisted.

  **Case 2 (pre-`iasValid`-column logs):** the column doesn't exist
  in the row. Engine recomputes via
  `onspeed::sensors::UpdateIasAlive(prev, row.iasKt)`, threading
  `prev` across rows. Same C++ function the firmware uses — no
  hand-port. Initial state defaults to false (matches firmware boot
  state).

  Engine maintains a `bool iasValidState_` member for case 2 if
  applicable. PR 1.5 audit verifies which path each log fixture
  takes.

If the audit surfaces additional fields, add them here.

#### Test deliverable: `test_replay_wire_completeness.js`

A new Node test at
`software/OnSpeed-M5-Display/test/test_replay_wire_completeness.js`
that:

1. Loads a small fixed-size log fixture (~5 rows, hand-crafted at
   `software/OnSpeed-M5-Display/test/fixtures/wire_completeness_log.csv`).
2. Drives those rows through `LogReplayEngine.step()`.
3. For each `ReplayStepResult`, builds the corresponding
   `DisplayBuildInputs` (using the same wireBridge logic PR 2 will
   use, factored into a small helper).
4. Calls `BuildDisplayFrame()` from onspeed_core WASM, gets 77 wire
   bytes.
5. Asserts every byte of the output matches a hand-crafted golden
   byte array `wire_completeness_golden.bin`.

When a future PR adds a new `DisplayBuildInputs` field (or changes a
filter), the test fails and the maintainer must regenerate the
golden, forcing a conscious "is this change correct?" review.

The fixture log values are chosen to exercise non-zero values for
every classifiable field so the golden actually catches missing
fields (not just default zeros that pass through).

**Sabotage check (mandatory):**
- Zero out `out.gOnsetRate = ...` after the implementation. Test
  must fail on the relevant byte position in the golden.
- Zero out `out.turnRateDps = ...`. Same.
- Add a placeholder field that's never populated. Test must fail.

#### CI integration

Wire the new test into the existing `m5-replay-wasm-test` CI job
(landed in PR 1's CI fix). Same Emscripten setup, same build
sequence; one extra `node` invocation at the end.

#### Bonus deliverable: expose `calculateTone` via embind

Replay's *other* eventual purpose (besides driving the M5 display)
is driving audio tones — what the pilot heard, not just saw. The
inputs `calculateTone()` needs are already in `ReplayStepResult`
(`out.aoa`, `out.flapsIndex` + the cfg held by the engine), and
`onspeed_core/audio/ToneCalc.{h,cpp}` is platform-free and already
in `onspeed_core`. The only missing piece is an embind wrapper.

Add to `software/Libraries/onspeed_core/wasm/bindings.cpp`:
- `tone_calc(aoa, ldmaxAoa, fastAoa, slowAoa, stallWarnAoa)` →
  returns `{ enTone: 'None'|'Low'|'High', pulseFreq, volumeMult }`.
- `tone_calc_muted(aoa, ias, stallWarnAoa, muteUnderIas)` —
  matching `calculateToneMuted` for the mute-button case.

Both are direct passthroughs to the C++ functions. No drift seam.

This unblocks:
- **v0 visualization** (PR 2 if Sam wants it): a "current tone state"
  widget on the replay overlay reads tone state per frame and shows
  e.g. "STALL WARN · HIGH · 20 PPS" or "ON SPEED · LOW · solid". No
  audio playback, just visualization. Adds a Layer-3 component but
  no new state machine.
- **Future audio synthesis PR** (post-v0, separate plan): wrap
  `AudioMixer` in WASM, generate PCM, route to WebAudio. Out of
  scope for v0 and PR 1.5.

**Audio synthesis is explicitly NOT in v0 or PR 1.5.** What's in PR
1.5 is the binding + a smoke test for `tone_calc` (5 lines in
wasm-smoke.mjs). Synthesis is its own plan when you want tones in
the browser/exported video.

### PR 2 — `feat(replay): wire replay through M5 WASM sim, all five modes` (~2-3 days) ⚠️ SUPERSEDED

> **REDIRECTED — see `2026-05-09-replay-retro.md`.** This PR-2 approach
> shipped (#512, unmerged) but the trial run exposed bugs in the JS-side
> data-shape transformations it relied on. The replacement plan adds a
> C++ `LogReplayTask` (lifted from `sketch_common`) as a parallel path,
> with an A/B toggle in the UI so Sam can compare against the JS path
> on real flight data. The retro and `2026-05-09-replay-continue-prompt.md`
> are the canonical guidance for the next agent. The text below is the
> original PR-2 design, kept for historical reference.

Wire the M5 WASM sim into the replay UI for all five modes.

- `tools/web/lib/replay/m5sim/m5sim.js` — JS wrapper around the M5 WASM module. API:
  - `await M5Sim.create()` returns `{ advanceTo(virtualMillis), injectBytes(Uint8Array), setMode(0..4), read() }`.
  - `sim.read()` returns a frozen object with every accessor's current value: `{ displayIAS, displayPalt, ..., gHistory: Float32Array }`.
- `tools/web/lib/replay/wireBridge.js` — given a `ReplayStepResult`, calls `BuildDisplayFrame()` (export from `bindings.cpp`; thin wrapper if not already exposed) → returns 76 wire bytes.
- `tools/web/lib/pages/ReplayPage.js` — adds an opt-in "M5-accurate mode" toggle. When ON, the SVG renders from M5 sim state; when OFF, the existing 22-field-`rec` path runs (preserves A/B comparison ability). PR 2 ships with the toggle; PR 3 makes ON the default.
- **SVG components, one per M5 mode** (in `tools/web/lib/components/svg/m5modes/`):
  - `EnergyMode.js` — already partially exists; refactor to read M5 state-var props (`displayIAS`, `Slip`, `PercentLift`, anchor pcts, `displayPercentLift`, `gOnsetRate`, etc.). Drop `slipFromLateralG` — the M5 gives us `Slip` as an integer.
  - `AttitudeMode.js` — synthetic horizon. Reads `Pitch`, `Roll`, `FlightPath`. Mirrors the layout from `main.cpp`'s Mode 1 dispatch (roughly: artificial horizon ladder + flight-path bird).
  - `IndexerMode.js` — AOA-only page. Reads same anchors as EnergyMode but no surrounding numerics. Mirror Mode 2.
  - `DecelMode.js` — IAS-derivative gauge. Reads `displayDecelRate`. Mirror Mode 3.
  - `HistoricGMode.js` — 60-second scrolling vertical-G trace. Reads `gHistory` (Float32Array) and `gHistoryIndex`. Mirror Mode 4.
- Mode dispatch in the replay UI's overlay: a 5-button toggle row (or arrow keys) writes `sim.setMode(n)`, then renders the matching SVG component.
- The existing 22-field `rec` path stays intact for the "M5-accurate OFF" branch. PR 3 will delete it.

**Verifiable test:** load `~/Downloads/sam_onspeed_aoa_4_11_2026.csv` + `~/Downloads/onspeed2_latest.cfg` + `~/Downloads/cleaned_4_11_2026_sam_aoa.mp4`. Cycle through all five modes. Confirm:
- **Mode 0 (Energy):** IAS digit updates ~2 Hz, altitude steady, ball smooth, AOA bar tracks correctly.
- **Mode 1 (Attitude):** horizon roll/pitch tracks the log's Pitch/Roll columns at 20 Hz; numbers (FlightPath, IAS) snapshot at 2 Hz.
- **Mode 2 (Indexer):** chevron/bar matches Mode 0's indexer minus the surrounding numerics.
- **Mode 3 (Decel):** decel gauge non-zero during decel events; quiet during steady-state cruise.
- **Mode 4 (Historic G):** trace scrolls left-to-right at 5 Hz (300 samples / 60 sec); replay scrubbing rewrites the buffer correctly.

**Sabotage checks:**
- Bypass `BuildDisplayFrame` and inject raw engine floats — Mode 0 ball should regain its current jitter (proving wire quantization is the fix).
- Flip the SVG components to read from the legacy `rec` instead of M5 sim — same jitter visible.

### PR 3 — `chore(replay): delete JS hand-port of M5 logic` (~half-day, after PR 2 bakes 2-3 days) ⚠️ SUPERSEDED

> **REDIRECTED — see retro.** Original framing was "delete JS hand-ports
> of M5 firmware logic" — but the trial run revealed a different,
> larger set of hand-ports (data-shape transformations in `rowObjAt`
> and `wireBridge.js`, not just `slipFromLateralG`). The corrected
> plan in the retro deletes those instead. Original text below.

Once PR 2 is in master and Sam has confirmed all five modes look
correct on real flight data:

- Delete `tools/web/lib/core/slipBall.js::slipFromLateralG` (M5 owns it).
- Delete the percent-lift derivation in JS (M5 owns it).
- Delete `gOnsetRate: 0, // not available in log replay` placeholder in `ReplayPage.js`.
- Delete the `kalmanVsiMps * 196.85` m/s→fpm conversion (M5's `iVSI` is already fpm-from-wire).
- Delete the legacy `rec`-building branch in `ReplayPage.js`.
- Make M5-accurate mode the default and drop the toggle.

Diff is mostly red lines.

**Verifiable test:** rebuild docs site → embedded simulator still
works (it uses the existing `docs` WASM target, untouched). Replay
tool still works. Visual diff vs PR 2's M5-accurate-ON path: zero
change.

## Out of scope (deliberately, not deferred forever)

- **Five-mode UI.** PR 2 wires Energy mode (default). Mode-cycling lives in `displayType`, can be set via accessor. UI to actually cycle is a Layer-1 feature for later. Decel mode in particular would be useful for Sam's flight-test workflow but it's a UI add, not a math add.
- **Audio-tone synthesis.** ToneCalc itself (the tone-selection state) IS exposed in PR 1.5 via embind, so replay can show "what tone the pilot heard" as a state readout. **Generating actual PCM audio** (`AudioMixer` wrap, WebAudio playback, muxing into exported MP4) is out of scope for v0 — separate plan when wanted. The drift-impossible architecture extends naturally to audio: `onspeed_core/audio/AudioMixer.cpp` is platform-free, same as `ToneCalc`, so a future audio PR is "wrap AudioMixer in embind, route output to WebAudio" — no algorithm hand-port.
- **WebSocket-driven `/indexer` page.** Already uses an SVG hand-port. Migrating that to the M5 WASM sim is a separate plan; doesn't block this. (The unified architecture says it should happen eventually.)
- **HUD output for FlySto / TronView.** Out of scope.
- **DataMark detection workflow.** Existing UI keeps working; M5 surfaces `DataMark` as an accessor so the existing detection logic just reads it from M5 instead of the rec.

## Risks and pre-mitigation

**Risk 1: M5GFX/M5Unified dependency too tangled to stub.** If the
firmware's source has implicit M5GFX coupling beyond `gdraw.*` calls
(e.g., `M5.update()` driving touch + button + IMU state that
SerialRead depends on), the stub gets bigger. Mitigation: PR 1
includes a budget — if the stub exceeds 500 LOC, stop, regroup, and
consider linking a Panel_Null backend instead.

**Risk 2: WASM bundle size.** Current `onspeed_core.wasm` is ~199 KB.
Adding the M5 firmware should be small if M5GFX is stubbed; if linked,
M5GFX is large. Mitigation: stub by default. Measure in PR 1; if
>500 KB total, file an issue and revisit.

**Risk 3: Time-virtualization breaks SerialRead's frame-dt
measurement.** SerialRead uses `micros()` to measure frame cadence
for its SavGol divisor. With virtual time, JS must set virtual time
*correctly* between byte injections so the dt is what the M5 would
have seen on real hardware (50 ms between frames). Mitigation: PR 2's
test fixture verifies the SmoothedDecelRate matches the M5 sim's
output for the same wire input over real time. If it doesn't, JS is
advancing virtual time wrong.

**Risk 4: Existing replay UI features (DataMark detection, clip
builder, sync) need to keep working.** They consume the existing
`rec` object. Mitigation: PR 2 introduces M5-accurate mode as
opt-in; other features keep using the legacy `rec`. PR 3 migrates
each feature one-by-one before removing legacy.

**Risk 5: The M5 firmware has `extern "C"` linkage assumptions or
non-`extern` globals.** PR 1 may need to bump some namespace-scope
`const`s to `extern const` (one already happened for `kModeNames`).
Mitigation: scoped to PR 1, scope-local fix.

**Risk 6: Multi-flight / multi-rate logs.** Issue #492 is about
firmware re-opening the log on rate change. Replay's `LogReplayEngine`
is per-rate; the M5 sim is rate-agnostic (it only sees wire bytes). So
this PR doesn't help #492 but doesn't conflict with it either.

## Done definition

After all four PRs land (PR 1 → PR 1.5 → PR 2 → PR 3):

1. The replay tool overlays an indexer that, frame-for-frame, matches
   what the M5 panel showed during the recorded flight, with:
   - Numbers (IAS, alt, G, %, decel) that update every 500 ms exactly.
   - Ball, bar, chevron that update at 20 Hz exactly.
   - Wire-quantized values (no per-row float jitter).
   - Decel rate computed by the M5's own SavGol-on-IAS.
   - **Every wire-format field populated correctly** — no zero
     placeholders, no missing gauges. Verified by the
     `test_replay_wire_completeness` golden-fixture test.
2. The replay tool contains zero rendering math beyond "read M5 state
   var, position SVG element". All math lives in `onspeed_core` or in
   M5 firmware, both compiled to WASM.
3. The X-Plane plugin and the firmware itself remain untouched — they
   already use this code on their own paths.
4. Issue #492 / #485 / #321 / #322 / #324 / #499 / #508 are tracked
   separately (some closed by these PRs, others scope-orthogonal).
5. No JS hand-port of any M5 or `onspeed_core` algorithm remains in
   `tools/web/`.

## Lessons learned (added 2026-05-09 mid-flight)

This plan was a level too thin in its first draft. We focused on
"compile M5 firmware to WASM" (Invariant 1) and assumed
`LogReplayEngine` was a finished artifact that produced complete
wire frames. It wasn't — it was scoped for a different original use
case. We discovered missing fields one by one (`gOnsetRate`,
`turnRateDps`, `oatC`) when the symptoms surfaced, which felt like
"so much could be wrong" even though the architectural fix was
correct.

**The plan should have included a wire-frame field-completeness
audit as a load-bearing step from the start.** When a plan promises
"replay shows what the pilot saw," the audit isn't optional — it's
how you know the promise holds. Without it, every untested field is
a latent zero waiting to surface as a "wait, why is X stuck?" bug at
trial time.

**Generalizable lesson for future plans:** when the deliverable is
"X consumer reproduces what Y producer does, exactly," at least one
PR in the sequence must be a structural completeness audit of the
producer's output schema, with a golden-fixture test that fails on
any new schema field that isn't classified. Otherwise the gap will
ship.

## Dispatch prompts

### PR 1 dispatch — `feat(m5): add replay WASM build target`

```
You are picking up Project B2 PR 1 from the OnSpeed plan set.

READ THESE FILES FIRST, in order, before touching code:
  - docs/superpowers/plans/2026-05-08-replay-INDEX.md (architecture, project ordering)
  - docs/superpowers/plans/2026-05-09-replay-m5-wasm.md (this plan; you are PR 1)
  - software/OnSpeed-M5-Display/CLAUDE.md (M5 firmware structure, build commands)
  - software/OnSpeed-M5-Display/sim/build_wasm.sh (existing docs/live targets — your replay target sits alongside)
  - software/OnSpeed-M5-Display/sim/ArduinoShim.h (existing shim; you add a REPLAY_TARGET branch)
  - software/OnSpeed-M5-Display/sim/SimMain.cpp (existing entry; you add ReplayMain.cpp)
  - software/OnSpeed-M5-Display/src/main.cpp (M5 firmware — read it; this is the state machine you are exposing)
  - software/OnSpeed-M5-Display/src/SerialRead.cpp (wire parser; same)
  - software/Libraries/onspeed_core/wasm/build_wasm.sh and bindings.cpp (the B1 WASM pattern; your B2 build mirrors structure but is independent — different entry point, different exports)

MANDATORY WORKFLOW:
  1. `git fetch origin master && git reset --hard origin/master` before doing anything. Local refs in worktrees go stale.
  2. `git submodule update --init --recursive` after the reset.
  3. Branch off master: `git checkout -b m5-wasm-replay-target`.
  4. Implement the PR 1 changes per the plan's "PR 1" section. Stick to scope: build target + accessor exports + Node test. No JS UI integration (that's PR 2).
  5. Build the WASM artifact locally: `cd software/OnSpeed-M5-Display && pio run -e native` first (populates .pio/libdeps/native), then `bash sim/build_wasm.sh --target replay`.
  6. Run the Node test: it must pass.
  7. Run the sabotage check: comment out the `Slip = ...` line in SerialRead.cpp::SerialProcess and rebuild — the test must FAIL. Then revert. If the test passes despite the sabotage, the test isn't real — fix it before proceeding.
  8. Run the second sabotage: flip updateRateNumbers from 500 to 50 in main.cpp — the 2 Hz snapshot test must fail. Revert.
  9. Open the PR with the standard OnSpeed PR template (see CLAUDE.md root for format). Use `--head <branch-name>` since the worktree workspace has untracked files.

IMPORTANT GOTCHAS:
  - M5GFX library is installed by `pio run -e native` into `.pio/libdeps/native/`. The build script reuses these. If you skip the native build, the WASM build will fail with "M5Unified / M5GFX not found".
  - The replay target should NOT use SDL2. The existing docs/live targets do; yours doesn't. No `-sUSE_SDL=2`. M5GFX rendering is stubbed via RenderShim.h.
  - Bundle size budget: <500 KB total WASM. Measure (`ls -lh sim/build/wasm-replay/onspeed_m5.wasm`) and report in the PR description.
  - LOC budget for RenderShim.h: 500. If you blow through it, stop, post a comment on the issue, and we regroup.
  - `kModeNames[5]` in main.cpp already had `extern` added for the X-Plane plugin. If any other namespace-scope const needs to be exposed for accessor exports, bump it to `extern const` rather than removing the const.
  - The replay target inherits the M5 firmware's strict warnings (`-Werror`). Your new shims must compile clean. No `#pragma`-suppression — fix the underlying issue.

OUT OF SCOPE for PR 1 (do not include):
  - JS UI integration. PR 2.
  - Deleting the JS hand-port. PR 3.
  - SVG components for Modes 1-4. PR 2.
  - The /indexer page. Out of scope for this entire plan; do not touch.

WHEN COMPLETE:
  - The PR description should include: WASM bundle size, list of exported accessors (~28 + 1 array pointer), Node test output (pass), sabotage outcomes (both fail-then-revert, with output proving the test fails when sabotaged).

Bulldog review will run after you're done. They will check that the test ACTUALLY exercises the production code (sabotage results), not that it merely runs to completion.
```

### PR 1.5 dispatch — written after PR 1 lands

The PR 1.5 dispatch prompt is filled in once PR 1 merges. The agent
will:
1. Audit `DisplayBuildInputs` field-by-field, classifying each.
2. Implement engine-side fields known missing today (`gOnsetRate`
   per #508, `turnRateDps`, `oatC`, `iasValid`).
3. Document wireBridge-supplied fields with a forward-pointing
   comment.
4. Build the `wire_completeness_log.csv` fixture + golden binary.
5. Add `test_replay_wire_completeness.js` Node test.
6. Wire into the existing `m5-replay-wasm-test` CI job.
7. Run sabotage checks (zero out each newly-added field; test must
   fail).

### PR 2 dispatch — written after PR 1.5 lands

### PR 3 dispatch — written after PR 2 bakes
