# AGENT_CONTEXT — Video Replay Tool

**Last updated:** 2026-05-09 — Sam.

**START HERE if you've never seen this codebase before.** If you're
looking for the index of plan documents and reading order, see
`2026-05-08-replay-INDEX.md`.

> **🛑 READ THE RETRO.** `2026-05-09-replay-retro.md` documents bugs
> that survived the foundation PRs (#507, #511) and the trial-run
> findings on PR #512. Architectural correction inside. The continue
> prompt at `2026-05-09-replay-continue-prompt.md` is the runway for
> the next agent. **Skipping the retro means re-making the same
> JS-hand-port mistakes.** The "no hand-ports" goal in this doc was
> defined too narrowly — it covered algorithm hand-ports but missed
> data-shape hand-derivations (`rowObjAt`, `buildDisplayInputs`).
> Both are equally drift-prone.

This is the briefing every sub-agent working on the video-replay tool
needs before touching code.

## The unified architecture (memorize this)

```
                          ┌──────────────────────────────┐
                          │   software/Libraries/        │
                          │   onspeed_core/              │  ← THE SPEC
                          │   (platform-free C++)        │
                          └──────────────┬───────────────┘
                                         │
        ┌──────────────┬──────────────┬──┴──┐
        │              │              │     │
        ▼              ▼              ▼     ▼
   Firmware      X-Plane       host_main  onspeed_core.wasm
   (ESP32)       plugin        CLI binary (algorithm artifact —
                 (native C++)  (Python    Python via host_main,
                               subprocess)  CI regression)

  Layer 2 (state machine over wire): software/OnSpeed-M5-Display

                ┌───────────────────────────────────────────┐
                │  M5-Display firmware                      │
                │  imports onspeed_core (Layer 1).          │
                │  adds time + state machine:               │
                │  20 Hz graphics, 2 Hz numbers, Slip,      │
                │  SmoothedDecelRate, gHistory, modes.      │
                │  THE source of truth for "what the M5     │
                │  pilot saw".                              │
                └─────┬───────┬─────────┬──────────┬────────┘
                      │       │         │          │
   native build       │       │ Emscripten WASM    │ direct C++ link
   ┌──────────────────┘       │         │          └──────────────────┐
   ▼                          │         ▼                             ▼
   M5 hardware /              │   Replay tool                   X-Plane plugin
   huVVer-AVI /               │   (browser)                     (Mac/Win/Linux)
   tools/m5-replay            │   feeds wire bytes,
                              │   reads M5 state,
                              │   paints SVG / 5 modes
                              │
                              ▼
                       (out of scope: /indexer page —
                       works fine over WebSocket JSON,
                       could migrate later)
```

**End state (after Project A + B1 + B2 land):** every consumer
either compiles `onspeed_core` directly, compiles M5 firmware
directly (which links `onspeed_core`), shells out to host_main, or
loads a WASM artifact. **No hand-ports anywhere.** Drift impossible
by construction.

**Pre-WASM (today)**: the Replay tool has TWO hand-ports —
`tools/web/lib/replay/*.js` reimplements algorithm code (B1
replaces this, partially landed via PR #496) AND it reimplements
M5 state-machine logic in `slipBall.js`, `reassemble.js`, and the
22-field `rec` builder in `ReplayPage.js` (B2 replaces this; see
`2026-05-09-replay-m5-wasm.md`).

**Pre-Python-consolidation (today)**: the Python tools have their
own algorithm code at `tools/onspeed_py/`. That code is replaced by
host_main subprocess wrappers once `PLAN_PYTHON_CONSOLIDATION.md`
lands.

**Carved out: the `/indexer` page.** Works today over WebSocket
JSON. Conceptually a sibling Layer-2 consumer (could feed M5
firmware WASM via WebSocket bytes), but **explicitly out of scope**
for this plan set. It ships unchanged. If/when we revisit, fresh
plan.

**Deploy targets:**
- **Firmware (Gen3)** — native C++ on the ESP32. Real airplane.
- **X-Plane plugin** — native C++. Links both `onspeed_core` and M5 firmware source.
- **M5 hardware variants** (Basic, Core2, huVVer-AVI) — native build of M5 firmware.
- **`tools/m5-replay`** — Python harness driving M5 firmware via USB-TTL.
- **Docs-site embedded sim** — full WASM build of M5 firmware (existing `--target docs` from `build_wasm.sh`).
- **host_main** — native CLI binary. Used by Python wrappers + CI regression.
- **onspeed_core.wasm** — algorithm artifact. Used by host_main consumers (Python via subprocess) and CI parity check. The Replay tool consumes `onspeed_core` transitively, through the M5 firmware WASM (B2).
- **M5 firmware WASM** — full-firmware artifact (`--target replay`). Loaded by Replay tool. Same source that flashes to M5/huVVer hardware.

 The plans listed below cover *what* to build. This doc covers *what
tripped me up while building it*, so future agents don't pay the same
cost twice.

---

## Read this first if you're new

The video-replay tool lives at `/replay` on the dev-server. It's
`tools/web/lib/pages/ReplayPage.js` plus `tools/web/lib/replay/*.js`.
**It is NOT in the firmware bundle.** The bundler skips it via a
regex in `scripts/build_web_bundle.py`.

Six plans govern this work (plus this doc, which is orientation —
not a plan):

1. **`2026-05-08-python-consolidation.md`** (this directory) —
   **DO FIRST.** Migrate Python algorithm code to `host_main`
   subprocess wrappers. Sam has explicitly OK'd breaking changes.
   Sequenced before any other layer work.
2. **`2026-05-08-firmware-log-replay-parity.md`** (this directory) —
   fix firmware LogReplay so SD-log replay produces flight-equivalent
   wire output. **STATUS: merged** (PRs #487, #490, #491). Must
   precede WASM Step 2 / Project B2.
3. **`2026-05-08-wasm-core.md`** — Project B1: compile `onspeed_core`
   (algorithm layer) to WebAssembly. Steps 0/1/2 merged; Steps 3-5
   pending. Used by Python tools (via host_main) and CI regression.
4. **`2026-05-09-replay-m5-wasm.md`** — Project B2: compile the
   **M5-Display firmware** (state-machine layer over the wire) to
   WebAssembly. Same source that flashes to M5/huVVer hardware. The
   Replay tool feeds wire bytes in, reads display state vars out,
   paints SVG from those values. **Required by C; pre-req
   firmware-log-replay-parity is already merged.** STATUS: design,
   PR 1 ready to dispatch.
5. **`2026-05-08-video-overlay-replay.md`** (this directory) — the
   Replay tool roadmap. Layer 0–5 architecture, sequencing, dispatch
   templates. Layer 1+ presumes Python consolidation + B1 + B2 are done.
6. **`2026-05-08-cross-impl-drift-prevention.md`** (this directory) —
   one-paragraph summary plus a tiny WASM-vs-native parity CI step.
   Most of this doc is historical, explaining why we no longer need
   a streaming-goldens CI gate (the WASM compile collapses it).

---

## Things that will mislead you if you don't know them

### 1. The body-angle convention is load-bearing

Throughout the OnSpeed codebase, a value called "AOA" is **body
angle** (fuselage-to-wind), not wing AOA. `alpha_0` is the body
angle at zero wing lift, **typically negative** on aircraft with
positive wing incidence (RV-10 at -3.7°, RV-4 with flap-0 at -4°).

The percent-lift formula:

```
percent_lift = (body_angle - alpha_0) / (alpha_stall - alpha_0) * 100
```

The floor is `alpha_0`, not zero. **Body angles between alpha_0 and 0
produce small POSITIVE fractions** because the wing IS lifting.

If you find yourself writing `body_angle / alpha_stall * 100` you're
wrong. The canonical implementation lives at:

- **C++ (the spec)**: `software/Libraries/onspeed_core/src/aoa/PercentLift.cpp::ComputePercentLift`
- **Python**: pre-`PLAN_PYTHON_CONSOLIDATION.md` Step 1 = a hand-port at
  `tools/onspeed_py/percent_lift.py::compute_percent_lift`.
  Post-Step 1 = a thin subprocess wrapper around `host_main percent_lift`.
- **JS**: pre-`PLAN_WASM_CORE.md` Step 1 = a hand-port at
  `tools/web/lib/replay/percentLift.js::computePercentLift`.
  Post-Step 1 = a thin wrapper that calls into the WASM build of
  onspeed_core. **Same compiled algorithm as firmware. No drift.**

Project-wide CLAUDE.md `/Users/sritchie/code/onspeed/CLAUDE.md` has
the full body-angle convention block. Read it.

### 2. The repo has TWO config formats — both must be supported

OnSpeed `.cfg` XML files come in two shapes:

- **V2 (current firmware)**: `<FLAP_POSITION>` blocks per detent.
  Each block has nested `<DEGREES>`, `<POT_VALUE>`, `<LDMAXAOA>`,
  `<ONSPEEDFASTAOA>`, `<ONSPEEDSLOWAOA>`, `<STALLWARNAOA>`. Newer
  builds also write `<ALPHA0>`, `<ALPHASTALL>`, `<KFIT>`. Older V2
  builds (e.g. Sam's `2_11_26_config.cfg`) **don't write the
  alpha_0 / alpha_stall / kFit block** — those default to 0.0 / 0.0
  / 0.0, which makes percent-lift math wrong. The C++ loader handles
  this; pre-WASM Python and JS hand-ports mirror it.
- **V1 (Gen2 / pre-PR-#320)**: top-level `<FLAPDEGREES>0,16,33</FLAPDEGREES>`,
  `<FLAPPOTPOSITIONS>...</FLAPPOTPOSITIONS>`, `<SETPOINT_LDMAXAOA>...</SETPOINT_LDMAXAOA>`,
  etc. V1 has no per-flap alpha_0 / alpha_stall / kFit. Loader
  defaults: `alpha_0 = 0.0`, `alpha_stall = stallwarn + 1.5`,
  `kFit = 0.0`.

V1 also has digit-prefixed tags (`<3DAUDIO>`) that aren't valid XML.
Browsers' DOMParser rejects them; the firmware's tinyxml2 doesn't.
The XML preprocessing (`<3DAUDIO>` → `<_3DAUDIO>` regex rewrite)
lives in `onspeed_core/config/Config.cpp`. Pre-`PLAN_WASM_CORE.md`
Step 1, the Python and JS loaders re-implement the same trick;
post-Step 1 they delegate to the C++ implementation via host_main /
WASM.

When testing, prefer `~/Downloads/onspeed2_latest.cfg` (newer V2 with
ALPHA0 populated). The older `2_11_26_config.cfg` works but produces
visibly-wrong percent-lift in the lower band.

### 3. Log carries raw IMU; wire carries AHRS-filtered IMU; replay must rate-adjust

**The actual data flow** (verified by reading the code, not by
reasoning from constants):

- The IMU chip (LSM6) is configured for **208 Hz output rate** with
  a **67 Hz analog LPF** (`IMU330.cpp:77`, `:89`).
- The AHRS engine (`onspeed_core/ahrs/Ahrs.cpp:317/341`) runs an EMA
  on `accelLatCorr_` at IMU update rate with **α=0.060899**.
  Continuous-time τ ≈ 0.079 s.
- The wire's `lateralG` reads from `g_AHRS.AccelLatFilter.get()`
  (`io/DisplaySerial.cpp:368`) which is `seed()`-mirrored from
  `core_.accelLatSmoothedG()` once per AHRS tick (`AHRS.cpp:112`).
  So the wire IS the engine's filter output.
- The SD log writes raw `g_pIMU->Ay` into the `LateralG` column
  (`LogSensor.cpp:552`) at 50 Hz log rate. **Unfiltered.**
- Same story for `VerticalG` and `ForwardG`. Other channels:
  `pitchDeg` / `rollDeg` are log-side already-smoothed (`g_AHRS.SmoothedPitch/Roll`);
  AOA is also log-side already-smoothed (`g_Sensors.AOA` post-EMA).
  Lateral / vertical / forward G are uniquely the channels where the
  wire is filtered and the log is raw.

**What replay must do**: the log captures one in every ~four IMU
samples, unfiltered. Replay can't perfectly reproduce the wire (the
high-frequency content the firmware filter rejected isn't in the
log), but it CAN come as close as 50 Hz data permits — by applying
a **rate-adjusted EMA** whose continuous-time τ matches the
firmware's. At 50 Hz with τ=0.079s, that's α'=0.224.

That's the v3 fix. See `PLAN_FIRMWARE_LOG_REPLAY_PARITY.md`.

**What replay does NOT do**:

- **Doesn't add log columns.** Schema is preserved; old offline
  tools keep reading old logs.
- **Doesn't re-run AHRS at 50 Hz.** Filtering the raw channel
  directly with rate-adjusted α is strictly better than running AHRS
  on under-sampled data.
- **Doesn't over-smooth for video readability.** Replay shows what
  the M5 saw. The M5 IS jittery in real flight — 30+ mg of
  sample-to-sample noise during turns and maneuvering. That jitter
  is real flight; replay should show it. The current JS
  `KACC_TAU_S = 0.50` is a workaround that produces output 6× more
  smoothed than the firmware actually emits; it's deleted in
  Sub-task 2.

**The historical PR #475 mistake**: claimed the firmware ran an EMA
at 208 Hz and LogReplay drove that same filter at the wrong rate.
False premise — there are TWO EMAs and only one runs at 208 Hz.
Bulldog caught it (the AOA EMA actually runs at 50 Hz; the 208 Hz
filter is the AHRS accel EMA which lives in the engine, not in
`LogReplay.cpp`). PR closed.

**The historical "log the smoothed channel" v2 plan**: drafted but
never implemented. Schema changes are expensive (every offline tool
that reads OnSpeed logs has to update); when the same fix can be
done in math (rate-adjusted filter), prefer math.

### 4. Old SD logs don't carry `flapsRawADC`

Logs from before ~PR #221 don't have a raw flap-pot ADC column.
Without it, the L/Dmax pip jumps at every detent transition (because
firmware's `FlapsDetector` snaps to the nearest detent and the log
only writes the snapped integer).

**Today's situation (pre-`PLAN_FIRMWARE_LOG_REPLAY_PARITY.md`)**: JS
synthesizes a fake ADC sweep across detent transitions — smoothstep
windows centered on the snap tick (mirrors firmware's
flip-at-midpoint physics). The implementation in
`lib/replay/logReplay.js::synthLeverSweep` is **two-pass**:
1. **First pass**: fill every row with the *current* detent's pot
   value. Without this pass, rows outside the smoothstep windows get
   stuck at the initial value forever.
2. **Second pass**: paint smoothstep windows over each transition,
   centered on the snap tick.

I shipped the single-pass version first and discovered the bug
during real-flight verification. Don't simplify it back.

**Target situation (post-firmware-LogReplay-parity + WASM)**:
firmware LogReplay detects the missing column and synthesizes the
sweep itself, in C++, inside `LogReplayEngine`. The Replay tool
inherits this via the WASM build. The JS `synthLeverSweep` function
gets deleted.

**The two-pass JS shape does NOT translate to the engine directly.**
The JS implementation walks the entire row list twice. The firmware-
side LogReplay task reads SD logs row-by-row, streaming. A 95-min
flight at 50 Hz is 286k rows × ~100 bytes = ~30 MB — far beyond ESP32
heap. **The C++ synth must be streaming with a bounded lookahead
window**, not batch two-pass.

The right shape: keep a circular buffer of `±half_window` rows
(~200 rows = ~20 KB at 50 Hz), step lags by `half_window` ticks but
both edges of any transition are visible inside the buffer. Output is
identical to the batch version because smoothstep windows are local
(they never need rows outside `±half_window` of the transition tick).
Latency = `half_window / 50 Hz` ≈ 2 sec, acceptable for offline
replay. PR #488 shipped a batch design; closed and re-spec'd.

### 5. Auto-detect prefers ROTATION; crosswind turn is the fallback

(Earlier versions of this doc said the opposite. That was wrong.)

Rotation is the unambiguous moment the wheels release: VSI-positive
after IAS-alive for ~1 s. It happens within seconds of takeoff every
flight, regardless of departure direction or pattern shape, and
auto-detects reliably. The pilot uses the **nudge buttons** + the
**Pause/Attach UI** to fine-tune from rotation to a more precise
event (often the first crosswind turn) — but rotation is the right
*initial guess.*

The detector lives in `lib/replay/syncDetect.js::detectRotation`:
IAS ≥ 30 kt then sustained VSI > 200 fpm for 1 s; walk back to
rotation IAS. Crosswind turn (`detectFirstCrosswindTurn`) is the
fallback for log excerpts that start mid-flight or otherwise lack
a rotation event.

### 6. Don't break `/indexer` when extracting widgets

The live `/indexer` page (driven by the airplane's WiFi AP on bench)
shares SVG components with the replay tool. Both render the same
`Mode0` / `Mode1` / `Mode3` from `lib/modes.js`, which compose
subcomponents from `lib/components/svg/index.js`.

When Layer 2 extracts widgets into `lib/replay/widgets/`, **don't
move the source code** out of `lib/components/svg/`. Re-export it
from the new location:

```js
// tools/web/lib/replay/widgets/HudSlipBall.js
import { SlipBall } from '../../components/svg/index.js';
export { SlipBall };
```

That way `IndexerPage.js` keeps importing from the original location
and the live page doesn't change. The replay-side widgets get a
consistent flat namespace.

### 7. SVGs ignore CSS `aspect-ratio` on size-constrained children

I lost an hour to this in Phase 2. An `<svg viewBox="0 0 320 240">`
child of an absolutely-positioned container does **not** honor
`aspect-ratio: 4/3` — it stretches to fill whatever box the parent
gives it.

The fix: wrap the SVG in a div, set `aspect-ratio` on the div,
let the SVG fill the wrapper at 100% × 100%. SVG's
`preserveAspectRatio="xMidYMid meet"` (the default) does the right
thing inside a correctly-sized box. The replay tool's
`.replay-overlay-frame` wrapper exists for this reason.

### 8. The test data is gitignored — don't expect it in the repo

`test-data/` is in `.gitignore`. Real flight logs + cockpit MP4s are
too big for git. If a fresh clone needs to verify the tool, copy:

- `~/Downloads/sam_onspeed_aoa_4_11_2026.csv` (76 MB log)
- `~/Downloads/cleaned_4_11_2026_sam_aoa.mp4` (17 GB video)
- `~/Downloads/onspeed2_latest.cfg` (config with ALPHA0 populated)

into `$WORKTREE/test-data/`. The MP4 can be a symlink — Playwright
respects symlinks for file uploads.

---

## How to drive the tool from a Playwright agent

The dev-server runs at `node tools/web/dev-server/server.mjs --mock --port 9001`.
Visit `http://localhost:9001/replay`.

**File-pickers via Playwright MCP:**

```js
// 1. Click the file label
await mcp.browser_click({ element: 'Log file', target: 'e22' });

// 2. Provide the path
await mcp.browser_file_upload({
  paths: ["/Users/sritchie/code/onspeed/onspeed-worktrees/video-overlay/test-data/sam_onspeed_aoa_4_11_2026.csv"]
});
```

**File access denied error**: Playwright is sandboxed to the
worktree root. Files outside (e.g. `~/Downloads`) are rejected.
Solution: copy or symlink them into `$WORKTREE/test-data/`.

**Programmatic video seek** (faster than scrubbing the UI):

```js
await mcp.browser_evaluate({
  function: '() => { document.querySelector("video").currentTime = 1234.5; return null; }'
});
```

**Sync persists across reloads** via localStorage. To force a fresh
auto-detect, clear it:

```js
await mcp.browser_evaluate({
  function: '() => Object.keys(localStorage).filter(k => k.startsWith("replay-sync-v1:")).forEach(k => localStorage.removeItem(k))'
});
```

---

## Existing infrastructure to reuse, don't duplicate

| Thing | Path | What it does |
|---|---|---|
| **Snapshot regression harness** | `tools/regression/{host_main.cpp,run_snapshot.py,fixtures/}` | Runs `host_main.cpp` linked against current `onspeed_core` over `short_replay.csv`, diffs against `golden.csv`. **C++ only today.** `PLAN_PYTHON_CONSOLIDATION.md` Step 0 extends host_main into a multi-subcommand CLI; `PLAN_DRIFT_PREVENTION.md` adds a tiny WASM-vs-native parity check that runs the same harness on both builds. |
| **Python replay** | `tools/onspeed_py/{config,percent_lift,log_replay,frame}.py` + `tests/` | **PRE-CONSOLIDATION**: hand-port of firmware algorithms. **POST-`PLAN_PYTHON_CONSOLIDATION.md`**: thin subprocess wrappers around `host_main`. Algorithm code lives in C++. The wrapper interface stays; the body becomes a `subprocess.run(['host_main', ...])` call. |
| **M5 WASM simulator** | `software/OnSpeed-M5-Display/sim/` | M5 firmware compiled to a 320×240 canvas via Emscripten + SDL2. Existing precedent for compiling `onspeed_core` to WebAssembly — the algorithm-only WASM build (`PLAN_WASM_CORE.md`) is a strict subset (no rendering). |
| **render-smoke tests** | `tools/web/test/render-smoke.mjs` | Renders Mode0/1/3 against fixed records; asserts text content. Layer 2 widget tests follow the same pattern. |
| **Live `/indexer` page** | `tools/web/lib/pages/IndexerPage.js` | The live indexer. Shares SVG components with replay. **Don't break this when extracting widgets — re-export rather than moving source.** |

---

## Build / dev commands

| Command | Purpose |
|---|---|
| `cd $WORKTREE && node tools/web/dev-server/server.mjs --mock --port 9001` | Run the dev-server. The `/replay` page is at `/replay`. |
| `cd $WORKTREE/tools/web && npm test` | JS test suites: render-smoke, geometry-invariants, etc. |
| `cd $WORKTREE && pio test -e native` | C++ native tests (1000+ today). |
| `cd $WORKTREE && pio run -e esp32s3-v4p` | Build firmware for V4P (catches firmware-side regressions). |
| `cd $WORKTREE && ./scripts/check_core_purity.sh` | Verifies `onspeed_core` stays platform-free. |
| `cd $WORKTREE && ./tools/regression/run_snapshot.py` | C++ snapshot regression. |
| `cd $WORKTREE/tools/onspeed_py && uv run --with pytest pytest` | Python tests (50+). |

The `/replay` page is dev-server-only; there's no production build
yet. Layer-5 / form-factor work will add a static-build step.

---

## Anti-patterns I've seen creep in

### Don't write percent-lift math inline

If you're computing percent-lift in a new file, **import**
`computePercentLift` from `lib/replay/percentLift.js`. Don't reinvent
the formula; the alpha_0 floor is easy to forget.

### Don't add the replay code back to the firmware bundle

`scripts/build_web_bundle.py` skips `lib/replay/*` and
`pages/ReplayPage.js`. The replay tool is dev-server-only. If you
think you need it on the airplane, you don't — the live `/indexer`
serves that.

### Don't fork SVG components into the replay tree

If you find yourself copying an SVG from `lib/components/svg/index.js`
into `lib/replay/widgets/`, stop. Re-export from the original
location. The live page and replay must render identically.

### Don't drop kAccSmoothing tuning

If a future bug report says "the slip ball lags too much" and you
think to lower `KACC_TAU_S`, test on real data first. The current
0.50 is already 4× stricter than the firmware's nominal continuous-
time τ; lowering it brings the noise back. The "lag" is real but
small and reflects the wire-format quantization the firmware applies.

### Don't put HUD widgets in `lib/components/svg/`

HUD widgets (HudAirspeedTape, HudAltitudeTape, etc.) are
**replay-tool-only**. They live under `lib/replay/widgets/`, not
`lib/components/svg/`. The bundler skip-list excludes `lib/replay/`
from firmware; if you put widgets in `lib/components/svg/` they'll
ship in the firmware bundle and bloat flash. **The M5 hardware does
NOT need HUD widgets** — it has its 5 built-in modes already.

If a future need arises to use a HUD widget on the live `/indexer`
page, lift it OUT of `lib/replay/` deliberately, with the cost
(firmware flash usage) understood.

### Don't have JS or Python parse CSV their own way

Post-`PLAN_PYTHON_CONSOLIDATION` Step 0 + `PLAN_WASM_CORE` Step 2,
**`host_main` (and its WASM twin) is the canonical CSV parser**.
Python wrappers shell out to host_main; the Replay tool calls the
WASM build. Neither rolls its own parser. Subtle differences in
column-name aliasing, NaN handling, malformed-row tolerance lead to
silent bugs that no CI gate would catch (because both sides see the
same well-formed input). The cure is "compile, don't port."

### Don't load the whole video into memory during export

Cockpit videos are routinely 10-20 GB. WebCodecs export must use
**streaming reads** from the File System Access API
(`FileSystemFileHandle.getFile().slice()` to pull byte ranges).
MP4Box.js demux is built around streaming — use it correctly. An
agent that does `await file.arrayBuffer()` on a 17 GB video will
crash the tab.

### The M5 WASM simulator is "by-construction-correct" only for ALGORITHMS

The simulator compiles the same C++ as the firmware, so percent-lift,
EMA, anchors, etc. behave identically. **But its rendering** (M5GFX
behavior on Emscripten/WASM) may differ from real-hardware behavior
in subtle ways (font metrics, pixel rounding). Don't claim
WASM-vs-real-M5 pixel parity. It's algorithm parity, which is what
matters for the spec.

### Don't bypass the snapshot regression goldens

The C++ snapshot regression at `tools/regression/run_snapshot.py`
catches behavior changes in `onspeed_core`. When a PR intentionally
changes behavior, regenerate the golden as part of the same commit
(`./tools/regression/run_snapshot.py --update-golden`). Don't
disable or sidestep the check.

Post-`PLAN_WASM_CORE.md` Step 0+1, the same regression also runs
against the WASM build (`tools/regression/run_snapshot_wasm.py`) —
the WASM-vs-native parity check from `PLAN_DRIFT_PREVENTION.md`. If
both diverge from the golden, regenerate; if only one diverges, the
WASM build differs from native — investigate the emcc settings, do
NOT regenerate.

Python tools and the Replay tool are **automatically correct by
construction**: Python shells out to host_main; the Replay tool
loads the WASM build. Neither has separate algorithm code to gate.

---

## Where to ask questions

If you're stuck on a decision the plans don't answer:

1. Re-read `PLAN_VIDEO_OVERLAY.md`'s "Sequencing roadmap" section.
   Most layer ambiguity is resolved there.
2. Check `CLAUDE.md` at the repo root — body-angle, worktree usage,
   PR style, banned phrasings.
3. The CLAUDE.md memory file at `~/.claude/projects/-Users-sritchie-code-onspeed/memory/`
   has Sam's preferences — aircraft assignments, build quirks,
   etc. Worth a glance.
4. If the question is genuinely unresolved, **stop and ask Sam**
   rather than guessing. He'd rather have a clarifying question
   than a confident wrong choice.

---

## Resumable from here — pickup notes for the next session

If you're picking up this work in a new session (Sam OR an agent),
the open work items are, IN STRICT DEPENDENCY ORDER:

**FOUNDATION (do these first, in parallel after host_main exists):**

0a. **Python consolidation Step 0**: extend host_main to a
    multi-subcommand CLI. See `2026-05-08-python-consolidation.md`.
    Required by both 0b and 0c. ~1-2 days.

0b. **Python consolidation Steps 1-7** (parallel after 0a):
    migrate Python algorithm code to host_main subprocess wrappers.
    7 small PRs.

0c. **WASM compile** (parallel after 0a): compile onspeed_core to
    a browser-loadable WASM module. See `2026-05-08-wasm-core.md`.
    5 small PRs. After this, the JS Replay tool has zero hand-port —
    it loads the same compiled algorithms the firmware runs.

1. **WASM-vs-native parity check** (after 0c lands): one CI step
   that runs the existing snapshot regression against both the
   native host_main and the WASM build. See
   `PLAN_DRIFT_PREVENTION.md`. ~half-day.

**REPLAY TOOL FEATURES (after foundation lands):**

2. **Layer 1: file-handle persistence + sticky config + sync
   nudge + keyboard frame-by-frame + all 5 modes.** See
   `PLAN_VIDEO_OVERLAY.md` Step 2 (expanded). ~1 day.
   Independent of WASM/Python projects — could parallelize.

3. **Layer 2: HUD widget gallery + catalog.** See PLAN Step 3.
   Visual iteration with Sam — build the gallery page first
   (`tools/web/widget-gallery.html`), then design widgets one at
   a time. OnSpeed-original styling, NOT G3X chrome.

4. **Layer 3, 4, 5**: see PLAN. Each independent once Layer 2 ships.

5. **Step 6.5 (deploy)**: build_static.sh + GitHub Actions hook
   into docs deploy → dev.flyonspeed.org/replay. Co-located with
   the WASM build artifacts.

6. **Step 7 (docs)**: user-facing replay.md, design spec,
   reference docs.

### What's already-fixed-and-pushed but not yet user-tested

- Auto-detect now prefers rotation (was crosswind-first; that was a
  misread of Sam's intent). The status text says "log rotation at
  NNN.NNs"; users fine-tune via nudge.

### What I would have shipped next if context didn't get expensive

- One-line rename: the `LogTimeline` "shift+click to override anchor"
  semantics are now redundant with the planned nudge buttons. Could
  drop shift-click. Defer until Layer 1 ships.
- DataMark in the original log file we tested with had zero entries
  (Sam never pressed the panel button). The synthetic `test_with_marks.csv`
  has 3 marks for verification. Real-flight DataMark testing requires
  a flight where Sam actively bumps the mark — schedule for next flight.

### The single source of truth, in priority order

1. **`PLAN_VIDEO_OVERLAY.md`** — what to build, in what order.
2. **`PLAN_DRIFT_PREVENTION.md`** — how the four implementations stay in sync.
3. **`AGENT_CONTEXT_VIDEO_OVERLAY.md`** (this file) — gotchas, anti-patterns, build commands.
4. **CLAUDE.md** at the repo root — body-angle convention, worktree usage, banned phrasings, project-wide rules.
5. **The code itself** — when the docs disagree with the code, the code is what runs; update the docs.

