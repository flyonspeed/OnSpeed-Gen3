# AGENT_CONTEXT — Video Replay Tool

**Last updated:** 2026-05-08 — Sam.

This is the briefing every sub-agent working on the video-replay tool
needs before touching code. The plans (`2026-05-08-video-overlay-replay.md`,
`2026-05-08-cross-impl-drift-prevention.md`) cover *what* to build.
This doc covers *what tripped me up while building it*, so future
agents don't pay the same cost twice.

---

## Read this first if you're new

The video-replay tool lives at `/replay` on the dev-server. It's
`tools/web/lib/pages/ReplayPage.js` plus `tools/web/lib/replay/*.js`.
**It is NOT in the firmware bundle.** The bundler skips it via a
regex in `scripts/build_web_bundle.py`.

Three plans govern this work:

1. **`2026-05-08-video-overlay-replay.md`** (this directory) — the
   roadmap. Layer 0–5 architecture, sequencing, dispatch templates.
   **Read all of it before doing anything.**
2. **`2026-05-08-cross-impl-drift-prevention.md`** (this directory) —
   how the firmware C++, Python tools, JS replay, and M5 simulator
   stay in sync. Spec-fixture-driven CI gate.
3. **THIS document** — gotchas, war stories, paths to land.

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

- C++: `software/Libraries/onspeed_core/src/aoa/PercentLift.cpp::ComputePercentLift`
- Python: `tools/onspeed_py/percent_lift.py::compute_percent_lift`
- JS: `tools/web/lib/replay/percentLift.js::computePercentLift`

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
  / 0.0, which makes percent-lift math wrong. The Python loader
  handles this; the JS loader mirrors it.
- **V1 (Gen2 / pre-PR-#320)**: top-level `<FLAPDEGREES>0,16,33</FLAPDEGREES>`,
  `<FLAPPOTPOSITIONS>...</FLAPPOTPOSITIONS>`, `<SETPOINT_LDMAXAOA>...</SETPOINT_LDMAXAOA>`,
  etc. V1 has no per-flap alpha_0 / alpha_stall / kFit. Loader
  defaults: `alpha_0 = 0.0`, `alpha_stall = stallwarn + 1.5`,
  `kFit = 0.0`.

V1 also has digit-prefixed tags (`<3DAUDIO>`) that aren't valid XML.
Browsers' DOMParser rejects them; the firmware's tinyxml2 doesn't.
**Both Python and JS loaders preprocess the file: regex-rewrite
`<3DAUDIO>` → `<_3DAUDIO>` before parsing.**

When testing, prefer `~/Downloads/onspeed2_latest.cfg` (newer V2 with
ALPHA0 populated). The older `2_11_26_config.cfg` works but produces
visibly-wrong percent-lift in the lower band.

### 3. The SD log writes RAW IMU values; firmware EMA happens on the wire

The firmware reads IMU at 208 Hz, applies EMA smoothing
(α=0.060899), and emits the smoothed values on the M5 wire / JSON.
**The SD log captures the raw pre-EMA `Ay` / `Az` / `Ax` values at
50 Hz** (`g_pIMU->Ay`, etc., in `tasks/LogSensor.cpp`).

For replay to look like what the M5 displayed in flight, you have to
re-apply the smoothing client-side. Variable-dt EMA at the log's
actual sample rate. The continuous-time τ ≈ 0.0741 s reproduces the
firmware's bandwidth — but at 50 Hz log rate, that's not enough to
hide the aliased noise the 208 Hz filter would have rejected.
Empirically tuned: `KACC_TAU_S = 0.50` in `lib/replay/logReplay.js`.

**If you're tempted to lower KACC_TAU_S, test against a real
calm-cruise segment of a flight video before merging.** The slip
ball will visibly twitch otherwise.

### 4. Old SD logs don't carry `flapsRawADC`

Logs from before ~PR #221 don't have a raw flap-pot ADC column.
Without it, the L/Dmax pip jumps at every detent transition (because
firmware's `FlapsDetector` snaps to the nearest detent and the log
only writes the snapped integer). To make replay look smooth, JS
synthesizes a fake ADC sweep across detent transitions — smoothstep,
centered on the snap tick (mirrors firmware's flip-at-midpoint
physics).

The synth-sweep implementation in `lib/replay/logReplay.js::synthLeverSweep`
is **two-pass**:
1. **First pass**: fill every row with the *current* detent's pot
   value. Without this pass, rows outside the smoothstep windows get
   stuck at the initial value forever.
2. **Second pass**: paint smoothstep windows over each transition,
   centered on the snap tick.

I shipped the single-pass version first and discovered the bug
during real-flight verification. Don't simplify it back.

### 5. The auto-detect anchor is the FIRST CROSSWIND TURN, not rotation

Pilots naturally sync against the first bank into crosswind because
both video (wing drop) and log (sharp roll-step) are unambiguous.
Rotation is the fallback when no crosswind turn is detected.

The detector lives in `lib/replay/syncDetect.js::detectFirstCrosswindTurn`:
sustained `|roll| ≥ 20°` at `IAS ≥ 30 kt` for `0.5 s`. If absent,
falls back to `detectRotation` (IAS+VSI heuristic).

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
| **Snapshot regression harness** | `tools/regression/{host_main.cpp,run_snapshot.py,fixtures/}` | Runs `host_main.cpp` linked against current `onspeed_core` over `short_replay.csv`, diffs against `golden.csv`. **C++ only today.** Drift-prevention plan extends to all four languages. |
| **Python replay** | `tools/onspeed_py/{config,percent_lift,log_replay,frame}.py` + `tests/` | The Python implementation that the JS replay was ported from. Authoritative when JS has a question. |
| **M5 WASM simulator** | `software/OnSpeed-M5-Display/sim/` | M5 firmware compiled to a 320×240 canvas via Emscripten + SDL2. Same renderer, different driver. **A future agent can write a "JS replay vs WASM sim" parity test.** |
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

### Don't bypass the spec fixtures (once they exist)

When `PLAN_DRIFT_PREVENTION` PR 1 lands, every algorithm change must
ship with a JSON fixture update. Don't merge a behavior change in
just JS or just Python — the CI gate exists for a reason.

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
