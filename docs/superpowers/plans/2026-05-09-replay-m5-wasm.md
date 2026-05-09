---
date: 2026-05-09
owner: Sam
status: design — ready to dispatch PR 1
supersedes_in_replay: PLAN_VIDEO_OVERLAY Layer 0+ rendering path
relates_to:
  - 2026-05-08-replay-INDEX.md
  - 2026-05-08-video-overlay-replay.md
  - 2026-05-08-firmware-log-replay-parity.md
---

# Project B2 — M5 firmware compiled to WASM (state-machine layer)

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

## The story in one paragraph

The replay tool's overlay must paint **what the pilot saw on the M5
screen during that flight**, frame-for-frame. Today the replay tool
hand-rolls a "build a 22-field record from log row, render SVG
directly" path that bypasses the M5's wire-rate downsampling, wire
quantization, 2 Hz text snapshot, and locally-computed decel rate.
Result: ball jitter, altitude flicker, text updating too fast, decel
gauge stuck at zero. The fix is the architectural close-out we've been
pointing at: **compile the M5-Display firmware itself to WASM and run
it as the replay tool's state engine.** JS feeds wire frames in at
20 Hz; the M5 firmware code (literally the same source that flashes to
the panel) decides what numbers to display, when to snapshot, what
the ball position is. JS reads M5 state vars and renders SVG from
them. Drift is impossible because the M5 firmware *is* the replay
engine.

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

### PR 2 — `feat(replay): wire replay through M5 WASM sim, all five modes` (~2-3 days)

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

### PR 3 — `chore(replay): delete JS hand-port of M5 logic` (~half-day, after PR 2 bakes 2-3 days)

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
- **Audio-tone replay.** ToneCalc lives in firmware too but isn't part of the M5 display sim. Routing the engine's `tonesOnPctLift` through ToneCalc to play tones in the browser is a fun separate plan.
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

After all three PRs land:

1. The replay tool overlays an indexer that, frame-for-frame, matches
   what the M5 panel showed during the recorded flight, with:
   - Numbers (IAS, alt, G, %, decel) that update every 500 ms exactly.
   - Ball, bar, chevron that update at 20 Hz exactly.
   - Wire-quantized values (no per-row float jitter).
   - Decel rate computed by the M5's own SavGol-on-IAS.
2. The replay tool contains zero rendering math beyond "read M5 state
   var, position SVG element". All math lives in `onspeed_core` or in
   M5 firmware, both compiled to WASM.
3. The X-Plane plugin and the firmware itself remain untouched — they
   already use this code on their own paths.
4. Issue #492 / #485 / #321 / #322 / #324 / #499 are unaffected (still
   open, separately scoped).
5. No JS hand-port of any M5 or `onspeed_core` algorithm remains in
   `tools/web/`.

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

### PR 2 dispatch — written after PR 1 lands

### PR 3 dispatch — written after PR 2 bakes
