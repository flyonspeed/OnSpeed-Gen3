# LiveView Five-Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring all five M5 hardware display modes (Primary AOA, Backup AI, Indexer-only, Energy, G-history) to the OnSpeed `/live` web page, replacing the on-device WASM `/indexer` route that reloads on iOS Safari.

**Architecture:** Iterate the SVG/JS rendering in a self-contained browser harness (`tools/liveview-prototype/`) driven by a JS synthetic-data generator and validated visually side-by-side with the existing wasm-live M5 sim build. Once the prototype matches the M5 visually, lift the SVG/JS into `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h`. Then remove the on-device `/indexer` page, the `/sim/*` WASM routes, the binary `BroadcastDisplayFrame` WebSocket producer, and the `embed_sim_prebuild.py` build hook.

**Tech Stack:** Vanilla HTML5 + SVG + ES2020 JS (no framework, no build chain for the prototype). Vitest-style assertion helper for unit tests of pure JS modules. C++17 firmware code in `software/sketch_common/src/web_server/` for the eventual port. PlatformIO for firmware builds.

---

## Spec

Full spec: `docs/superpowers/specs/2026-04-28-liveview-five-modes.md`

Working directory for all tasks: `/Users/sritchie/code/onspeed/onspeed-worktrees/liveview-five-modes/` (the worktree to be created in Stage 0). Branch: `sritchie/liveview-five-modes` off latest master. Do not cd elsewhere.

Key reference files (read-only — do not modify, just study):

- `software/OnSpeed-M5-Display/src/main.cpp` — the M5 firmware. Function `displayAOA()` at line 717 is mode 0; `case 1:` at line 521 is mode 1 (Attitude); `case 2:` at line 626 is mode 2 (Indexer-only); `displayDecelGauge()` at line 1350 is mode 3; `displayGloadHistory()` at line 1437 is mode 4. Functions `drawAOA()` at 889, `drawSlip()` at 1060, `mapPct2Display()` at 1529, `pitchGraph()` at 1284 are referenced from the modes.
- `software/Libraries/onspeed_core/src/aoa/FlapWidgetFrac.h` — pure helper used by the M5's flap circle widget. Has unit tests in `test/test_flap_widget_math/`.
- `software/Libraries/onspeed_core/src/proto/DisplaySerial.h` — reference for the `#1` wire format the wasm-live build consumes.
- `software/OnSpeed-M5-Display/sim/build_wasm.sh` — produces the wasm-live build the prototype embeds for visual A/B.
- `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h` — the target file Stage 7 lifts the prototype into. Today carries an old AOA + Attitude SVG that does not match the M5 geometry; the new version replaces both panels and adds three more.
- `software/OnSpeed-Gen3-ESP32/Web/html_indexer.h` — to be deleted in Stage 8.
- `software/sketch_common/src/web_server/DataServer.cpp` — the WebSocket JSON producer. Stage 7 adds three fields here.
- `software/sketch_common/src/io/DisplaySerial.cpp` — the M5 wire producer. Stage 8 removes the binary WebSocket call.
- `software/sketch_common/src/web_server/ConfigWebServer.cpp` — route handlers; Stage 8 removes `/indexer`, `/sim/index.js`, `/sim/index.wasm` routes and adds an `/indexer` → `/live` redirect.
- `tools/m5-replay/replay.py` — Python frame builder for the `#1` wire format; reference for the JS-side frame builder Stage 0 ports.

---

## File Structure

### New files (created during this plan)

```
tools/liveview-prototype/
├── README.md                             # how to run the harness
├── index.html                            # side-by-side prototype + WASM, mode toggle
├── style.css                             # CSS variables + theme rules
├── lib/
│   ├── colors.js                         # M5 color tokens (M5_TFT_* → CSS vars)
│   ├── geometry.js                       # M5 layout constants extracted from C++
│   ├── pct2y.js                          # mapPct2Display port (pure)
│   ├── chevronColors.js                  # top/bottom chevron region logic (pure)
│   ├── donutColors.js                    # 3-segment donut region logic (pure)
│   ├── slipBall.js                       # slip ball position + flash logic (pure)
│   ├── flapWidget.js                     # flap circle angle from FlapWidgetFrac (pure)
│   ├── frameBuilder.js                   # build #1 ASCII frames for wasm-live (pure)
│   ├── scenarios.js                      # synthetic scenarios (climb / cruise / approach / stall)
│   ├── theme.js                          # light/dark toggle + localStorage
│   └── modes/
│       ├── aoa.js                        # Mode 0: Primary AOA (uses lib/* above)
│       ├── attitude.js                   # Mode 1: Backup AI
│       ├── indexer-only.js               # Mode 2: indexer with no chrome
│       ├── energy.js                     # Mode 3: decel gauge
│       └── ghistory.js                   # Mode 4: G-history strip-chart
├── test/
│   ├── runner.html                       # in-browser test harness
│   ├── pct2y.test.js
│   ├── chevronColors.test.js
│   ├── donutColors.test.js
│   ├── slipBall.test.js
│   ├── flapWidget.test.js
│   ├── frameBuilder.test.js
│   └── scenarios.test.js
└── wasm-live/                            # symlinked / copied from software/OnSpeed-M5-Display/sim/build/wasm-live/
```

### Modified files (during firmware-port stages)

```
software/OnSpeed-Gen3-ESP32/Web/html_liveview.h        # Stage 7: full rewrite
software/OnSpeed-Gen3-ESP32/Web/html_header.h          # Stage 8: remove "Indexer" nav link, drop favicon if any
software/OnSpeed-Gen3-ESP32/Web/html_header_css.h      # Stage 6 / Stage 7: add CSS variables for theme
software/sketch_common/src/web_server/DataServer.cpp   # Stage 7 (add JSON fields), Stage 8 (remove BroadcastDisplayFrame)
software/sketch_common/src/web_server/DataServer.h     # Stage 8: remove BroadcastDisplayFrame prototype
software/sketch_common/src/web_server/ConfigWebServer.cpp  # Stage 8: remove /indexer + /sim/* routes, add /indexer → /live redirect
software/sketch_common/src/io/DisplaySerial.cpp        # Stage 8: remove BroadcastDisplayFrame call
software/sketch_common/src/tasks/AHRS.cpp              # Stage 7: surface gOnsetRate to a global field DataServer can read
software/sketch_common/src/tasks/AHRS.h                # Stage 7: declare gOnsetRate field
docs/site/docs/installation/external-display.md        # Stage 8: redirect docs to /live for tablet pilots
platformio.ini                                          # Stage 8: drop embed_sim_prebuild.py hook
```

### Deleted files (Stage 8)

```
software/OnSpeed-Gen3-ESP32/Web/html_indexer.h
software/OnSpeed-Gen3-ESP32/Web/sim_index_js.h         # generated; check it's gitignored
software/OnSpeed-Gen3-ESP32/Web/sim_index_wasm.h       # generated; check it's gitignored
scripts/embed_sim_for_firmware.sh
scripts/embed_sim_prebuild.py
```

The docs-site WASM embed (`docs/site/docs/installation/external-display.md` iframe) and the build script `software/OnSpeed-M5-Display/sim/build_wasm.sh` STAY — desktop browsers handle the WASM fine, and it remains a useful demo on the docs site.

---

## Stage 0: Prototype harness + spec extraction

This stage builds the development environment we'll iterate in for stages 1-6. By the end of Stage 0, the prototype harness exists, can be served locally, has the wasm-live sim embedded for A/B comparison, and has all the M5 layout constants and color tokens extracted into JS modules with unit tests. No actual rendering work yet.

### Task 0.1: Create the worktree and branch

**Files:** none (git operations only)

- [ ] **Step 1: Create the worktree off latest master**

```bash
cd /Users/sritchie/code/onspeed/OnSpeed-Gen3
git fetch origin master
git worktree add /Users/sritchie/code/onspeed/onspeed-worktrees/liveview-five-modes -b sritchie/liveview-five-modes origin/master
cd /Users/sritchie/code/onspeed/onspeed-worktrees/liveview-five-modes
git submodule update --init --recursive
```

- [ ] **Step 2: Verify clean state**

```bash
git status
git log --oneline -3
```

Expected: working tree clean; HEAD on the latest master commit. Submodules under `software/Libraries/` populated.

- [ ] **Step 3: Build the wasm-live sim once (it's a prerequisite for Task 0.4)**

```bash
cd software/OnSpeed-M5-Display
pio pkg install -e native --project-dir . >/dev/null
./sim/build_wasm.sh --target live
ls sim/build/wasm-live/index.{html,js,wasm}
```

Expected: three files exist, index.wasm is ~1 MB. The build takes ~2 minutes the first time.

- [ ] **Step 4: Commit the worktree state baseline**

No commit needed — the worktree is on master. We don't commit until we have files to commit (Task 0.2).

### Task 0.2: Set up the prototype directory scaffold

**Files:**
- Create: `tools/liveview-prototype/index.html`
- Create: `tools/liveview-prototype/style.css`
- Create: `tools/liveview-prototype/README.md`
- Create: `tools/liveview-prototype/.gitignore`

- [ ] **Step 1: Write `tools/liveview-prototype/.gitignore`**

```
wasm-live/
node_modules/
```

- [ ] **Step 2: Write `tools/liveview-prototype/README.md`**

```markdown
# LiveView Prototype

A standalone browser harness for iterating the OnSpeed five-mode SVG indexer
without flashing firmware. Renders the same SVG that will eventually live in
`software/OnSpeed-Gen3-ESP32/Web/html_liveview.h`, driven by a synthetic
data generator and validated side-by-side against the M5's wasm-live sim.

## Quick start

```bash
# 1. Build the wasm-live sim (once; outputs to sim/build/wasm-live/)
cd software/OnSpeed-M5-Display && ./sim/build_wasm.sh --target live

# 2. Symlink the sim build into the prototype dir
cd tools/liveview-prototype
ln -sfn ../../software/OnSpeed-M5-Display/sim/build/wasm-live wasm-live

# 3. Serve
python3 -m http.server 8080

# 4. Open http://localhost:8080/ in a browser
```

The prototype shows two panels side-by-side: the new SVG indexer (left)
and the wasm-live sim (right). Both consume the same synthetic data feed
(JS objects to the SVG, `#1` ASCII binary frames to the WASM via
`_inject_serial_byte`).

Use the scenario buttons at the top to drive the data through canned
manoeuvres (ground idle, cruise, approach, stall warning, recovery).

## Tests

Open `http://localhost:8080/test/runner.html` to run the unit tests in
the browser. The pure JS modules in `lib/` (color decisions, percent-to-y
mapping, slip ball math, etc.) are all covered.
```

- [ ] **Step 3: Write a placeholder `tools/liveview-prototype/index.html`**

```html
<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OnSpeed LiveView Prototype</title>
<link rel="stylesheet" href="style.css">
</head>
<body>
<header>
  <h1>OnSpeed LiveView Prototype</h1>
  <button id="theme-toggle" type="button">Toggle theme</button>
</header>
<nav id="mode-nav">
  <button data-mode="aoa" type="button">AOA</button>
  <button data-mode="attitude" type="button">AI</button>
  <button data-mode="indexer-only" type="button">Idx</button>
  <button data-mode="energy" type="button">Energy</button>
  <button data-mode="ghistory" type="button">G-Hist</button>
</nav>
<nav id="scenario-nav">
  <button data-scenario="idle" type="button">Idle</button>
  <button data-scenario="cruise" type="button">Cruise</button>
  <button data-scenario="approach" type="button">Approach</button>
  <button data-scenario="stall" type="button">Stall warn</button>
</nav>
<main>
  <section class="panel" id="svg-panel">
    <h2>New SVG</h2>
    <!-- Stage 1+ will fill this with the actual SVG. -->
    <div id="svg-root"></div>
  </section>
  <section class="panel" id="wasm-panel">
    <h2>Wasm-live (M5 sim)</h2>
    <iframe src="wasm-live/index.html" id="wasm-iframe" frameborder="0"></iframe>
  </section>
</main>
<script type="module" src="lib/main.js"></script>
</body>
</html>
```

- [ ] **Step 4: Write a placeholder `tools/liveview-prototype/style.css`**

```css
:root {
  --bg: #111;
  --ink: #eee;
  --panel-bg: #000;
  --green: #07a33f;
  --yellow: #ffe000;
  --red: #cc3837;
  --grey: #888;
  --dark-grey: #444;
  --light-grey: #aaa;
}

[data-theme="light"] {
  --bg: #fafafa;
  --ink: #111;
  --panel-bg: #fafafa;
  --green: #0a8a35;
  --yellow: #c8a800;
  --red: #b22a2a;
  --grey: #555;
  --dark-grey: #aaa;
  --light-grey: #888;
}

body {
  margin: 0;
  background: var(--bg);
  color: var(--ink);
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
}

header, nav {
  display: flex; gap: 8px; align-items: center;
  padding: 8px 12px;
}

main {
  display: flex; flex-direction: row; gap: 16px;
  padding: 16px;
}

.panel {
  flex: 1;
  background: var(--panel-bg);
  border: 1px solid var(--dark-grey);
  border-radius: 8px;
  padding: 8px;
  min-width: 320px;
}

#svg-root, #wasm-iframe {
  width: 320px;
  height: 240px;
  display: block;
  margin: 0 auto;
}

#wasm-iframe { border: 0; background: #000; }
```

- [ ] **Step 5: Run the server, confirm scaffolding loads**

```bash
cd tools/liveview-prototype
ln -sfn ../../software/OnSpeed-M5-Display/sim/build/wasm-live wasm-live
python3 -m http.server 8080 &
SERVER_PID=$!
sleep 2
curl -sI http://localhost:8080/index.html | head -1
kill $SERVER_PID
```

Expected: `HTTP/1.0 200 OK`. The browser would show the empty scaffold with mode/scenario buttons but no rendering. The wasm-live iframe shows the M5 sim splash. (`lib/main.js` doesn't exist yet — browser will log a 404 for it, fine.)

- [ ] **Step 6: Commit**

```bash
git add tools/liveview-prototype/
git commit -m "liveview-prototype: scaffold harness directory"
```

### Task 0.3: Extract M5 color tokens to `lib/colors.js`

**Files:**
- Create: `tools/liveview-prototype/lib/colors.js`
- Create: `tools/liveview-prototype/test/colors.test.js`

The M5 firmware uses M5GFX color macros (`TFT_BLACK`, `TFT_WHITE`, `TFT_GREEN`, etc.). M5GFX defines them as RGB565 values; for our purposes the relevant ones map to standard hex colors. We export them as a frozen object that the SVG modules consume.

- [ ] **Step 1: Write `tools/liveview-prototype/lib/colors.js`**

```javascript
// M5GFX color tokens, indexed by their TFT_* names.
// Values are CSS variable names so the active theme controls the actual color.
// The hex values in colors.dark / colors.light are tuned to match the M5GFX
// constants closely enough that the rendered output at panel-mount distance
// is indistinguishable from the hardware.
export const colors = Object.freeze({
  TFT_BLACK:      'var(--bg-panel)',
  TFT_WHITE:      'var(--ink)',
  TFT_GREEN:      'var(--green)',
  TFT_YELLOW:     'var(--yellow)',
  TFT_RED:        'var(--red)',
  TFT_GREY:       'var(--grey)',
  TFT_DARKGREY:   'var(--dark-grey)',
  TFT_LIGHTGREY:  'var(--light-grey)',
  TFT_CYAN:       'var(--sky)',
  TFT_BROWN:      'var(--ground)',
});
```

- [ ] **Step 2: Write `tools/liveview-prototype/test/colors.test.js`**

```javascript
import { colors } from '../lib/colors.js';

export function run(assert) {
  assert.equal(colors.TFT_BLACK, 'var(--bg-panel)', 'TFT_BLACK maps to bg var');
  assert.equal(colors.TFT_GREEN, 'var(--green)',     'TFT_GREEN maps to green var');
  assert.equal(Object.isFrozen(colors), true,        'colors is frozen');
}
```

- [ ] **Step 3: Add the test runner page `tools/liveview-prototype/test/runner.html`**

```html
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>Tests</title></head>
<body>
<pre id="output"></pre>
<script type="module">
const out = document.getElementById('output');
const log = (msg, ok) => {
  const line = document.createElement('div');
  line.textContent = (ok ? '✓ ' : '✗ ') + msg;
  line.style.color = ok ? '#070' : '#a00';
  out.appendChild(line);
};
const assert = {
  equal(a, b, msg)  { if (a === b) log(msg, true); else log(`${msg} — expected ${JSON.stringify(b)}, got ${JSON.stringify(a)}`, false); },
  near(a, b, eps, msg) { if (Math.abs(a - b) <= eps) log(msg, true); else log(`${msg} — expected ${b}±${eps}, got ${a}`, false); },
  truthy(a, msg)    { if (a) log(msg, true); else log(`${msg} — expected truthy`, false); },
};
const tests = [
  '../test/colors.test.js',
];
for (const t of tests) {
  try {
    const m = await import(t);
    log(`-- ${t} --`, true);
    m.run(assert);
  } catch (e) {
    log(`${t} — threw ${e}`, false);
  }
}
</script>
</body>
</html>
```

- [ ] **Step 4: Run the test runner**

```bash
cd tools/liveview-prototype
python3 -m http.server 8080 &
SERVER_PID=$!
sleep 1
# Open browser manually, or:
curl -s http://localhost:8080/test/runner.html | head -3
kill $SERVER_PID
```

Expected: 200 OK. Open `http://localhost:8080/test/runner.html` manually to confirm the three colors.test.js assertions pass with green checkmarks.

- [ ] **Step 5: Commit**

```bash
git add tools/liveview-prototype/lib/colors.js tools/liveview-prototype/test/
git commit -m "liveview-prototype: M5 color tokens + test runner"
```

### Task 0.4: Extract M5 layout constants to `lib/geometry.js`

**Files:**
- Create: `tools/liveview-prototype/lib/geometry.js`
- Create: `tools/liveview-prototype/test/geometry.test.js`

We hardcode every layout constant from the M5 source as a JS export. This is tedious but mechanical — each constant gets a comment citing the source line so future readers can cross-reference.

- [ ] **Step 1: Write `tools/liveview-prototype/lib/geometry.js`**

```javascript
// All layout constants extracted from software/OnSpeed-M5-Display/src/main.cpp
// (and helpers from drawAOA/drawSlip/displayAOA). Coordinate system is the
// M5's native 320×240 panel; SVG viewBox matches.
//
// Naming convention: M5_<COMPONENT>_<DIMENSION>.

// ----------------------------------------------------------------------------
// Panel + indexer widget
// ----------------------------------------------------------------------------

export const M5_PANEL_W = 320;
export const M5_PANEL_H = 240;

// displayAOA() at main.cpp:717: wgtX0/Y0/Width/Height set in case 0
// (Primary): wgtWidth=102, wgtHeight=192, wgtX0=(WIDTH-102)/2=109, wgtY0=0.
// drawAOA() at :889 immediately re-centers: X0 = wgtX0 + W/2; Y0 = wgtY0 + H/2.
export const INDEXER_WIDTH  = 102;
export const INDEXER_HEIGHT = 192;
export const INDEXER_X      = (M5_PANEL_W - INDEXER_WIDTH) / 2;  // 109
export const INDEXER_Y      = 0;
export const INDEXER_CX     = INDEXER_X + INDEXER_WIDTH / 2;     // 160
export const INDEXER_CY     = INDEXER_Y + INDEXER_HEIGHT / 2;    // 96

// drawAOA() bounding box: drawRoundRect at :899 with corner radius 5.
export const INDEXER_BOX_RADIUS = 5;

// ----------------------------------------------------------------------------
// Chevron geometry — top + bottom, two halves each (left and right of center).
// drawAOA() :902-:1003. The pre-rotation rect is Px0..Px1 = ±W/12 by Py0..Py1
// = ±H/4. Rotation ±π/8 about each half's center.
// ----------------------------------------------------------------------------

export const CHEVRON_HALF_W = INDEXER_WIDTH / 12;   // 8.5
export const CHEVRON_HALF_H = INDEXER_HEIGHT / 4;   // 48
export const CHEVRON_ROTATION_RAD = Math.PI / 8;    // ±π/8 (±22.5°)

// Half-centers (the X0±W/4, Y0±H/4 anchor points in drawAOA):
export const CHEVRON_TOP_LEFT_CX     = INDEXER_CX - INDEXER_WIDTH / 4;  // 134.5
export const CHEVRON_TOP_LEFT_CY     = INDEXER_CY - INDEXER_HEIGHT / 4; // 48
export const CHEVRON_TOP_RIGHT_CX    = INDEXER_CX + INDEXER_WIDTH / 4;  // 185.5
export const CHEVRON_TOP_RIGHT_CY    = INDEXER_CY - INDEXER_HEIGHT / 4; // 48
export const CHEVRON_BOTTOM_LEFT_CX  = INDEXER_CX - INDEXER_WIDTH / 4;  // 134.5
export const CHEVRON_BOTTOM_LEFT_CY  = INDEXER_CY + INDEXER_HEIGHT / 4; // 144
export const CHEVRON_BOTTOM_RIGHT_CX = INDEXER_CX + INDEXER_WIDTH / 4;  // 185.5
export const CHEVRON_BOTTOM_RIGHT_CY = INDEXER_CY + INDEXER_HEIGHT / 4; // 144

// ----------------------------------------------------------------------------
// Donut — three independently-lit segments.
// drawAOA() :1005-:1031.
// ----------------------------------------------------------------------------

// drawAOA() :1008: bullsEye = H × (65 - 55 - 2) / 200 = 192 × 8 / 200 = 7.68
//   (the C++ uses integer math so it rounds to 7; we use the float here to
//   match the visual size precisely. Either is fine within 1 px.)
export const DONUT_BULLSEYE_R = INDEXER_HEIGHT * 8 / 200; // 7.68
// :1009 black surround radius = bullsEye + H/12 = 7.68 + 16 = 23.68
export const DONUT_BLACK_R = DONUT_BULLSEYE_R + INDEXER_HEIGHT / 12;
// :1012 arc radius = bullsEye + H/16 = 7.68 + 12 = 19.68
export const DONUT_ARC_R = DONUT_BULLSEYE_R + INDEXER_HEIGHT / 16;
// :1013 line width
export const DONUT_ARC_LINEWIDTH = 8;
// :1026 black gap rect between top and bottom arcs
export const DONUT_GAP_X = INDEXER_CX - INDEXER_WIDTH / 3;     // 126
export const DONUT_GAP_Y = INDEXER_CY - INDEXER_HEIGHT / 48;   // 92
export const DONUT_GAP_W = 2 * INDEXER_WIDTH / 3;              // 68
export const DONUT_GAP_H = INDEXER_HEIGHT / 24;                // 8
// :1031 center dot radius = bullsEye + 2
export const DONUT_DOT_R = DONUT_BULLSEYE_R + 2;

// ----------------------------------------------------------------------------
// Index bar (the moving white horizontal that shows current AOA).
// drawAOA() :1037: rect (X0 - W/2, indexY, W, H/24).
// ----------------------------------------------------------------------------

export const INDEX_BAR_X      = INDEXER_X;                     // 109
export const INDEX_BAR_W      = INDEXER_WIDTH;                 // 102
export const INDEX_BAR_H      = INDEXER_HEIGHT / 24;           // 8

// ----------------------------------------------------------------------------
// L/Dmax pip dots — black halo + white inner.
// drawAOA() :1049-:1052.
// ----------------------------------------------------------------------------

export const PIP_LEFT_CX  = INDEXER_X;                          // 109
export const PIP_RIGHT_CX = INDEXER_X + INDEXER_WIDTH - 1;      // 210
export const PIP_HALO_R   = INDEXER_HEIGHT / 24;                // 8
export const PIP_INNER_R  = INDEXER_HEIGHT / 32;                // 6

// ----------------------------------------------------------------------------
// Percent-lift number above the indexer.
// displayAOA() :739-:760.
// ----------------------------------------------------------------------------

export const PCT_LIFT_X = 140;  // PERCENT_X_POS at :739
export const PCT_LIFT_Y = 27;   // PERCENT_Y_POS at :740
// Font: FSSB18 (FreeSans Bold 18pt). Closest CSS: 22 px bold.
export const PCT_LIFT_FONT_SIZE = 22;
// Outline: 9 black copies at ±3 offset.
export const PCT_LIFT_OUTLINE_PX = 3;

// ----------------------------------------------------------------------------
// Corner readouts — IAS top-left, G top-right.
// displayAOA() :764-:786.
// ----------------------------------------------------------------------------

export const CORNER_RIGHT_X  = 303;  // RIGHT_X
export const CORNER_LABEL_Y  = 90;
export const CORNER_NUM_Y    = 130;
export const CORNER_LEFT_X   = 5;
// Label font: FSS18 (FreeSans 18pt). Number font: FSSB18 (FreeSans Bold 18pt).
export const CORNER_LABEL_FONT_SIZE = 18;
export const CORNER_NUM_FONT_SIZE   = 22;

// ----------------------------------------------------------------------------
// Flap circle widget.
// displayAOA() :790-:835.
// ----------------------------------------------------------------------------

export const FLAP_CX = 23;
export const FLAP_CY = 204;
export const FLAP_R  = 16;
// Triangle apex sits at radius FLAP_R + 33.
export const FLAP_TRIANGLE_TIP_R = FLAP_R + 33;
// Visual arc the triangle sweeps (kFlapArcDeg).
export const FLAP_ARC_DEG = 40;

// ----------------------------------------------------------------------------
// Slip ball.
// drawSlip() at :1060, called from displayAOA() :840 with (80, 204, 160, 34).
// ----------------------------------------------------------------------------

export const SLIP_X = 80;
export const SLIP_Y = 204;
export const SLIP_W = 160;
export const SLIP_H = 34;
export const SLIP_CENTER_X = SLIP_X + SLIP_W / 2;  // 160
export const SLIP_CENTER_Y = SLIP_Y + SLIP_H / 2;  // 221
export const SLIP_BALL_R   = SLIP_H / 2 - 1;       // 16
// Ball x offset from center: slipValue × (W - H - 1) / 99 / 2.
// The displayed slip value range is ±99 (clamped).
export const SLIP_BALL_X_RANGE = (SLIP_W - SLIP_H - 1) / 2;  // 62.5

// ----------------------------------------------------------------------------
// G-onset right-edge tape.
// displayAOA() :845-:868.
// ----------------------------------------------------------------------------

export const GONSET_BAR_X      = 313;
export const GONSET_BAR_W      = 7;
// :847: gOnsetHeight = |gOnsetRate × 60|, then constrain(0, 120).
// (60 because main.cpp uses /2 then ×120 — verify in source.)
export const GONSET_HEIGHT_SCALE = 60;
export const GONSET_HEIGHT_MAX   = 120;
export const GONSET_ZERO_Y       = 119;  // :850 if positive: top = 119 - height
// Tick ladder. After PR #351 (pip alignment fix), start=14 step=15.
export const GONSET_TICK_X1   = 313;
export const GONSET_TICK_X2   = 319;
export const GONSET_TICK_FIRST_Y = 14;
export const GONSET_TICK_STEP    = 15;
export const GONSET_TICK_LAST_Y  = 224;
// Zero pip — 3 horizontal 7-px lines.
export const GONSET_PIP_X1 = 306;
export const GONSET_PIP_X2 = 312;
export const GONSET_PIP_Y_TOP    = 118;
export const GONSET_PIP_Y_MIDDLE = 119;
export const GONSET_PIP_Y_BOT    = 120;
```

- [ ] **Step 2: Write `tools/liveview-prototype/test/geometry.test.js`**

```javascript
import * as G from '../lib/geometry.js';

export function run(assert) {
  // Sanity: indexer is centered horizontally on the panel.
  assert.equal(G.INDEXER_CX, G.M5_PANEL_W / 2, 'indexer center horizontally on panel');
  // Indexer extents fit inside the panel.
  assert.truthy(G.INDEXER_X >= 0, 'indexer left edge in panel');
  assert.truthy(G.INDEXER_X + G.INDEXER_WIDTH <= G.M5_PANEL_W, 'indexer right edge in panel');
  // Donut center coincides with indexer widget center.
  assert.equal(G.DONUT_GAP_X + G.DONUT_GAP_W / 2, G.INDEXER_CX, 'donut gap centered on indexer');
  // Slip ball center matches displayAOA() drawSlip(80, 204, 160, 34) call.
  assert.equal(G.SLIP_CENTER_X, 160, 'slip center x = 160');
  assert.equal(G.SLIP_CENTER_Y, 221, 'slip center y = 221');
  // G-onset zero pip is 3-px tall centered on y=119.
  assert.equal(G.GONSET_PIP_Y_MIDDLE, 119, 'G-onset zero pip middle y = 119');
  // After PR #351 fix, the first ladder tick is at 14, step 15, so tick at y=119 exists.
  const ticksOnPipMiddle = (G.GONSET_PIP_Y_MIDDLE - G.GONSET_TICK_FIRST_Y) % G.GONSET_TICK_STEP;
  assert.equal(ticksOnPipMiddle, 0, 'a ladder tick lands on the zero pip center');
}
```

- [ ] **Step 3: Add the test to the runner**

Edit `tools/liveview-prototype/test/runner.html`, append `'../test/geometry.test.js'` to the tests array.

- [ ] **Step 4: Run the test runner; confirm both files pass**

Open `http://localhost:8080/test/runner.html` in a browser; expect green checkmarks for every assertion.

- [ ] **Step 5: Commit**

```bash
git add tools/liveview-prototype/lib/geometry.js tools/liveview-prototype/test/geometry.test.js tools/liveview-prototype/test/runner.html
git commit -m "liveview-prototype: extract M5 layout constants"
```

### Task 0.5: Port `mapPct2Display` to `lib/pct2y.js`

**Files:**
- Create: `tools/liveview-prototype/lib/pct2y.js`
- Create: `tools/liveview-prototype/test/pct2y.test.js`

The C++ function at main.cpp:1529 is the heart of the indexer — it converts a percent-lift value (0..99) plus the per-flap anchor array into the y-coordinate where the index bar sits. We port it byte-for-byte and unit-test against representative inputs.

- [ ] **Step 1: Write `tools/liveview-prototype/lib/pct2y.js`**

```javascript
// Port of mapPct2Display() from software/OnSpeed-M5-Display/src/main.cpp:1529.
// Maps a percent-lift value (0..99) to a y-coordinate within the indexer
// widget (1..192). The y-coordinate is in the M5's native 320×240 panel
// space; the SVG viewBox matches, so this y is also the SVG y.
//
// `anchors` is the same 8-element array the C++ uses (PctAnchors), where:
//   anchors[0] = 0  (alpha_0 floor; always zero in percent space)
//   anchors[2] = tonesOnPctLift   (operational; chevron + audio gate)
//   anchors[3] = onSpeedFastPctLift  (donut bottom edge)
//   anchors[4] = onSpeedSlowPctLift  (donut top edge / top-chevron lower gate)
//   anchors[6] = pipPctLift          (visual L/Dmax pip)
//   anchors[7] = stallWarnPctLift    (top-chevron flash threshold)
// Slots 1 and 5 are unused.
//
// Y values: 192 = bottom of widget, 1 = top.
export function mapPct2Display(aoaPct, anchors) {
  if      (aoaPct <= anchors[0])                              return 192;
  else if (aoaPct >  anchors[0] && aoaPct <= anchors[3])      return map2int(aoaPct, anchors[0], anchors[3], 192, 115);
  else if (aoaPct >  anchors[3] && aoaPct <= anchors[4])      return map2int(aoaPct, anchors[3], anchors[4], 115,  78);
  else if (aoaPct >  anchors[4] && aoaPct <= anchors[7])      return map2int(aoaPct, anchors[4], anchors[7],  78,   1);
  else                                                         return 1;
}

// Linear interpolation matching the C++ map2int. Returns an integer.
function map2int(x, inLow, inHigh, outLow, outHigh) {
  if (inHigh === inLow) return outLow;
  return Math.round((x - inLow) * (outHigh - outLow) / (inHigh - inLow) + outLow);
}
```

- [ ] **Step 2: Write `tools/liveview-prototype/test/pct2y.test.js`**

```javascript
import { mapPct2Display } from '../lib/pct2y.js';

// Representative anchor set (RV-10 full flaps, from SerialRead.cpp:198-203).
// Slot 0 is the alpha_0 floor (always 0); slot 2 is L/Dmax (pctOf(-2.24)),
// slot 3 is OnSpeedFast, slot 4 is OnSpeedSlow, slot 7 is StallWarn.
// Numbers below approximate what the firmware would emit for that flap.
const anchors = [0, 0, 33, 51, 64, 0, 33, 80];

export function run(assert) {
  assert.equal(mapPct2Display(0,   anchors), 192, 'percent 0 -> y 192 (bottom)');
  assert.equal(mapPct2Display(51,  anchors),  78, 'OnSpeedSlow edge -> y 78');
  assert.equal(mapPct2Display(64,  anchors),  78, 'percent at slow upper -> y 78');
  assert.equal(mapPct2Display(80,  anchors),   1, 'StallWarn -> y 1 (top)');
  assert.equal(mapPct2Display(99,  anchors),   1, 'above StallWarn -> y 1');
  // Mid-band linear: percent 33 (= OnSpeedFast) -> y 115.
  assert.equal(mapPct2Display(33,  anchors), 115, 'OnSpeedFast edge -> y 115');
  // Halfway between fast and slow.
  assert.equal(mapPct2Display(42,  anchors), Math.round(115 + (78-115) * (42-33)/(51-33)), 'mid-donut linear');
}
```

- [ ] **Step 3: Add the test to the runner; run; confirm pass**

Append `'../test/pct2y.test.js'` to runner.html. Open in browser, all green.

- [ ] **Step 4: Commit**

```bash
git add tools/liveview-prototype/lib/pct2y.js tools/liveview-prototype/test/pct2y.test.js tools/liveview-prototype/test/runner.html
git commit -m "liveview-prototype: port mapPct2Display"
```

### Task 0.6: Port chevron color logic to `lib/chevronColors.js`

**Files:**
- Create: `tools/liveview-prototype/lib/chevronColors.js`
- Create: `tools/liveview-prototype/test/chevronColors.test.js`

drawAOA() lines 905-965 in main.cpp encode the M5 chevron color rules. Port them verbatim, return an object keyed by chevron-half so the SVG module can just `.setAttribute('fill', ...)` per element.

- [ ] **Step 1: Write `tools/liveview-prototype/lib/chevronColors.js`**

```javascript
import { colors } from './colors.js';

// Chevron color decisions for the M5 indexer.
// Returns { top, bottom } where each is one of TFT_DARKGREY / TFT_YELLOW /
// TFT_RED / TFT_GREEN.
//
// Both chevron halves on a row share the same color (same gate fires for
// both). Source: drawAOA() in main.cpp:905-1003.
export function chevronColors({ percentLift, anchors, flashFlag }) {
  const onSpeedSlow = anchors[4];
  const stallWarn   = anchors[7];
  const tonesOn     = anchors[2];
  const chevMid     = onSpeedSlow + (stallWarn - onSpeedSlow) / 2;

  // Top chevron — escalates yellow → red as AOA approaches stall.
  let top = colors.TFT_DARKGREY;
  if      (percentLift > onSpeedSlow && percentLift <= chevMid)   top = colors.TFT_YELLOW;
  else if (percentLift > chevMid     && percentLift <= stallWarn) top = colors.TFT_RED;
  else if (percentLift > stallWarn   && !flashFlag)               top = colors.TFT_RED;
  // else (above stallWarn AND flashing) → stays DARKGREY (the "off" half of the flash).

  // Bottom chevron — green when audio low tone is playing (in [tonesOn, onSpeedSlow)).
  let bottom = colors.TFT_DARKGREY;
  if (percentLift >= tonesOn && percentLift < onSpeedSlow) bottom = colors.TFT_GREEN;

  return { top, bottom };
}
```

- [ ] **Step 2: Write `tools/liveview-prototype/test/chevronColors.test.js`**

```javascript
import { chevronColors } from '../lib/chevronColors.js';
import { colors } from '../lib/colors.js';

const anchors = [0, 0, 33, 51, 64, 0, 33, 80];  // tonesOn=33, fast=51, slow=64, stall=80

export function run(assert) {
  // Below tonesOn — both gray.
  let r = chevronColors({ percentLift: 20, anchors, flashFlag: false });
  assert.equal(r.top,    colors.TFT_DARKGREY, 'low AOA: top gray');
  assert.equal(r.bottom, colors.TFT_DARKGREY, 'low AOA: bottom gray');
  // In bottom-chevron green range.
  r = chevronColors({ percentLift: 50, anchors, flashFlag: false });
  assert.equal(r.bottom, colors.TFT_GREEN, 'in [tonesOn,slow): bottom green');
  assert.equal(r.top,    colors.TFT_DARKGREY, 'in [tonesOn,slow): top gray');
  // Past slow but below chevMid (=72).
  r = chevronColors({ percentLift: 68, anchors, flashFlag: false });
  assert.equal(r.top, colors.TFT_YELLOW, 'past slow: top yellow');
  // Past chevMid but at stallWarn.
  r = chevronColors({ percentLift: 75, anchors, flashFlag: false });
  assert.equal(r.top, colors.TFT_RED, 'past chevMid: top red');
  // Above stallWarn, not flashing.
  r = chevronColors({ percentLift: 90, anchors, flashFlag: false });
  assert.equal(r.top, colors.TFT_RED, 'above stallWarn, not flash: top red');
  // Above stallWarn, flashing — top goes dark for the off-half.
  r = chevronColors({ percentLift: 90, anchors, flashFlag: true });
  assert.equal(r.top, colors.TFT_DARKGREY, 'above stallWarn, flash: top dark');
}
```

- [ ] **Step 3: Add to runner; run; confirm pass**

Append to runner.html, verify in browser.

- [ ] **Step 4: Commit**

```bash
git add tools/liveview-prototype/lib/chevronColors.js tools/liveview-prototype/test/chevronColors.test.js tools/liveview-prototype/test/runner.html
git commit -m "liveview-prototype: chevron color decisions"
```

### Task 0.7: Port donut color logic to `lib/donutColors.js`

**Files:**
- Create: `tools/liveview-prototype/lib/donutColors.js`
- Create: `tools/liveview-prototype/test/donutColors.test.js`

drawAOA() lines 1011-1031 encode the three-segment donut: bottom arc (0..π), top arc (π..2π), and center dot. Each lights up green only when the percent-lift is in a specific sub-range of the OnSpeed band.

- [ ] **Step 1: Write `tools/liveview-prototype/lib/donutColors.js`**

```javascript
import { colors } from './colors.js';

// Donut segment colors. Three independent segments per drawAOA() :1011-1031.
// Returns { topArc, bottomArc, dot }, each TFT_GREEN or TFT_DARKGREY.
//
// Range gates inside the OnSpeed band [fast, slow]:
//   bottom arc: [fast, fast + 0.75 × range]    — lower 75%
//   top arc:    [fast + 0.25 × range, slow]    — upper 75%
//   center dot: [fast + 0.25 × range, slow - 0.25 × range]  — middle 50%
// (range = slow - fast)
export function donutColors({ percentLift, anchors }) {
  const fast = anchors[3];
  const slow = anchors[4];
  const range = slow - fast;

  const bottomArc = (percentLift >= fast && percentLift <= fast + 0.75 * range)
    ? colors.TFT_GREEN : colors.TFT_DARKGREY;
  const topArc = (percentLift >= fast + 0.25 * range && percentLift <= slow)
    ? colors.TFT_GREEN : colors.TFT_DARKGREY;
  const dot = (percentLift >= fast + 0.25 * range && percentLift <= slow - 0.25 * range)
    ? colors.TFT_GREEN : colors.TFT_DARKGREY;

  return { topArc, bottomArc, dot };
}
```

- [ ] **Step 2: Write `tools/liveview-prototype/test/donutColors.test.js`**

```javascript
import { donutColors } from '../lib/donutColors.js';
import { colors } from '../lib/colors.js';

const anchors = [0, 0, 33, 51, 64, 0, 33, 80];  // fast=51, slow=64, range=13

export function run(assert) {
  // Below fast — all gray.
  let r = donutColors({ percentLift: 30, anchors });
  assert.equal(r.bottomArc, colors.TFT_DARKGREY, 'below fast: bottom arc gray');
  assert.equal(r.topArc,    colors.TFT_DARKGREY, 'below fast: top arc gray');
  assert.equal(r.dot,       colors.TFT_DARKGREY, 'below fast: dot gray');
  // At fast (lower edge): bottom arc on, top arc off, dot off.
  r = donutColors({ percentLift: 51, anchors });
  assert.equal(r.bottomArc, colors.TFT_GREEN,    'at fast: bottom on');
  assert.equal(r.topArc,    colors.TFT_DARKGREY, 'at fast: top off');
  assert.equal(r.dot,       colors.TFT_DARKGREY, 'at fast: dot off');
  // Centered (= 51 + 0.5 × 13 = 57.5): all three on.
  r = donutColors({ percentLift: 57.5, anchors });
  assert.equal(r.bottomArc, colors.TFT_GREEN, 'centered: bottom on');
  assert.equal(r.topArc,    colors.TFT_GREEN, 'centered: top on');
  assert.equal(r.dot,       colors.TFT_GREEN, 'centered: dot on');
  // At slow (upper edge): top arc on, bottom off, dot off.
  r = donutColors({ percentLift: 64, anchors });
  assert.equal(r.bottomArc, colors.TFT_DARKGREY, 'at slow: bottom off');
  assert.equal(r.topArc,    colors.TFT_GREEN,    'at slow: top on');
  assert.equal(r.dot,       colors.TFT_DARKGREY, 'at slow: dot off');
}
```

- [ ] **Step 3: Add to runner; run; confirm pass**

- [ ] **Step 4: Commit**

```bash
git add tools/liveview-prototype/lib/donutColors.js tools/liveview-prototype/test/donutColors.test.js tools/liveview-prototype/test/runner.html
git commit -m "liveview-prototype: donut color decisions"
```

### Task 0.8: Port slip ball logic to `lib/slipBall.js`

**Files:**
- Create: `tools/liveview-prototype/lib/slipBall.js`
- Create: `tools/liveview-prototype/test/slipBall.test.js`

drawSlip() at main.cpp:1060 plus its caller at :840. Slip is computed in SerialRead.cpp:269 as `LateralG × 850`, clamped to ±99. This module accepts the raw `lateralG` (from the WebSocket JSON) and returns ball position + color decisions.

- [ ] **Step 1: Write `tools/liveview-prototype/lib/slipBall.js`**

```javascript
import { colors } from './colors.js';
import {
  SLIP_CENTER_X,
  SLIP_CENTER_Y,
  SLIP_BALL_X_RANGE,
} from './geometry.js';

// Slip from lateral G (SerialRead.cpp:269): Slip = LateralG × 34 / 0.04 = ×850.
// Clamped to ±99 to match the wire-format range.
export function slipFromLateralG(lateralG) {
  const v = Math.round(lateralG * 850);
  return Math.max(-99, Math.min(99, v));
}

// Ball position (SVG cx) and fill color.
// Flash logic (drawSlip :1069-1071): when high-AOA AND high-slip, alternate
// between black and red with the chevron's flashFlag cadence.
export function slipBall({ slip, percentLift, stallWarn, flashFlag }) {
  // Position: drawSlip() draws ball at CenterX + slipValue × (W-H-1) / 99 / 2.
  const cx = SLIP_CENTER_X + slip * SLIP_BALL_X_RANGE / 99;
  const cy = SLIP_CENTER_Y;

  let fill = colors.TFT_GREEN;
  if (Math.abs(slip) >= 30 && percentLift >= stallWarn) {
    fill = flashFlag ? colors.TFT_BLACK : colors.TFT_RED;
  }
  return { cx, cy, fill };
}
```

- [ ] **Step 2: Write `tools/liveview-prototype/test/slipBall.test.js`**

```javascript
import { slipFromLateralG, slipBall } from '../lib/slipBall.js';
import { colors } from '../lib/colors.js';
import { SLIP_CENTER_X, SLIP_CENTER_Y } from '../lib/geometry.js';

export function run(assert) {
  // SerialRead.cpp:269: Slip = LateralG × 850, clamp ±99.
  assert.equal(slipFromLateralG(0),     0,   'zero lateral G -> zero slip');
  assert.equal(slipFromLateralG(0.05), 43,   '0.05 G -> 43 slip');
  assert.equal(slipFromLateralG(1.0),  99,   '1 G -> clamped 99');
  assert.equal(slipFromLateralG(-1.0), -99,  '-1 G -> clamped -99');

  // Ball position centered at zero slip.
  let r = slipBall({ slip: 0, percentLift: 50, stallWarn: 80, flashFlag: false });
  assert.equal(r.cx, SLIP_CENTER_X, 'zero slip: centered');
  assert.equal(r.cy, SLIP_CENTER_Y, 'cy always center');
  assert.equal(r.fill, colors.TFT_GREEN, 'normal: green');

  // Off-center but not stalling: still green.
  r = slipBall({ slip: 50, percentLift: 50, stallWarn: 80, flashFlag: false });
  assert.equal(r.fill, colors.TFT_GREEN, 'high slip but not stalling: green');

  // Stalling AND high slip: flashes red/black.
  r = slipBall({ slip: 50, percentLift: 90, stallWarn: 80, flashFlag: false });
  assert.equal(r.fill, colors.TFT_RED, 'stalling+slipping flash on: red');
  r = slipBall({ slip: 50, percentLift: 90, stallWarn: 80, flashFlag: true });
  assert.equal(r.fill, colors.TFT_BLACK, 'stalling+slipping flash off: black');
}
```

- [ ] **Step 3: Add to runner; run; confirm pass**

- [ ] **Step 4: Commit**

```bash
git add tools/liveview-prototype/lib/slipBall.js tools/liveview-prototype/test/slipBall.test.js tools/liveview-prototype/test/runner.html
git commit -m "liveview-prototype: slip ball logic"
```

### Task 0.9: Port flap widget angle math to `lib/flapWidget.js`

**Files:**
- Create: `tools/liveview-prototype/lib/flapWidget.js`
- Create: `tools/liveview-prototype/test/flapWidget.test.js`

The flap circle widget rotates a triangle from `0` (clean) to `40°` (full flaps), proportional to `FlapWidgetFrac(flapPos, min, max)` from `software/Libraries/onspeed_core/src/aoa/FlapWidgetFrac.h`. That helper has a tested contract; we port it to JS.

Read the C++ helper to see the exact contract (handles reflex flaps with negative min, etc.).

- [ ] **Step 1: Read the C++ source for `FlapWidgetFrac` and any tests**

```bash
cat software/Libraries/onspeed_core/src/aoa/FlapWidgetFrac.h
ls test/test_flap_widget_math/
cat test/test_flap_widget_math/test_flap_widget_math.cpp | head -80
```

Note the contract carefully — particularly the reflex-flap handling and the min==max degenerate case.

- [ ] **Step 2: Write `tools/liveview-prototype/lib/flapWidget.js`**

```javascript
// Port of onspeed::gauges::FlapWidgetFrac from
// software/Libraries/onspeed_core/src/aoa/FlapWidgetFrac.h.
//
// Maps a current flap deg (`flapPos`) and the configured min/max range
// to a fraction in [0, 1] suitable for visual widget arc sweep. Handles:
//   - Reflex flaps (negative `flapsMin`): clamp at 0 below min, at 1 above max.
//   - Degenerate `flapsMin === flapsMax`: return 0 (no travel = no movement).
export function flapWidgetFrac(flapPos, flapsMin, flapsMax) {
  if (flapsMin === flapsMax) return 0;
  const frac = (flapPos - flapsMin) / (flapsMax - flapsMin);
  return Math.max(0, Math.min(1, frac));
}

// Convert frac → SVG transform value for the rotating triangle.
// drawAOA() :797-:805 uses kFlapArcDeg = 40°, so frac × 40° is the angle.
export function flapTriangleTransform(frac) {
  // SVG rotates clockwise about the circle center (FLAP_CX, FLAP_CY).
  // Caller composes the full transform; this just returns degrees.
  return frac * 40;
}
```

- [ ] **Step 3: Write `tools/liveview-prototype/test/flapWidget.test.js`**

The test cases should mirror the C++ tests under `test/test_flap_widget_math/` (read those first and copy the assertions into JS).

```javascript
import { flapWidgetFrac, flapTriangleTransform } from '../lib/flapWidget.js';

export function run(assert) {
  // Conventional 0..30 range.
  assert.equal(flapWidgetFrac( 0,  0, 30), 0,    'clean = 0');
  assert.equal(flapWidgetFrac(30,  0, 30), 1,    'full = 1');
  assert.equal(flapWidgetFrac(15,  0, 30), 0.5,  'mid = 0.5');
  // Below min clamps to 0.
  assert.equal(flapWidgetFrac(-5,  0, 30), 0,    'below min clamps to 0');
  // Above max clamps to 1.
  assert.equal(flapWidgetFrac(50,  0, 30), 1,    'above max clamps to 1');
  // Reflex flaps (negative min).
  assert.equal(flapWidgetFrac(-10, -10, 30), 0,  'reflex full reflex = 0');
  assert.equal(flapWidgetFrac( 30, -10, 30), 1,  'reflex full extend = 1');
  assert.equal(flapWidgetFrac( 10, -10, 30), 0.5, 'reflex mid = 0.5');
  // Degenerate: min==max returns 0.
  assert.equal(flapWidgetFrac(15,  0,  0), 0,    'no travel returns 0');

  // Triangle transform.
  assert.equal(flapTriangleTransform(0),    0,  'frac 0 -> 0°');
  assert.equal(flapTriangleTransform(0.5), 20,  'frac 0.5 -> 20°');
  assert.equal(flapTriangleTransform(1),   40,  'frac 1 -> 40°');
}
```

- [ ] **Step 4: Add to runner; run; confirm pass**

- [ ] **Step 5: Commit**

```bash
git add tools/liveview-prototype/lib/flapWidget.js tools/liveview-prototype/test/flapWidget.test.js tools/liveview-prototype/test/runner.html
git commit -m "liveview-prototype: flap widget angle math"
```

### Task 0.10: JS frame builder for wasm-live (`lib/frameBuilder.js`)

**Files:**
- Create: `tools/liveview-prototype/lib/frameBuilder.js`
- Create: `tools/liveview-prototype/test/frameBuilder.test.js`

The wasm-live build consumes `#1` ASCII binary frames via `_inject_serial_byte`. We need a JS function that takes a synthetic-data record and produces the 76-byte frame to feed in. Reference: `tools/m5-replay/replay.py` lines 96-131.

- [ ] **Step 1: Read the Python reference**

```bash
sed -n '96,135p' tools/m5-replay/replay.py
```

- [ ] **Step 2: Write `tools/liveview-prototype/lib/frameBuilder.js`**

```javascript
// Build OnSpeed `#1` display-serial ASCII frames in JavaScript.
// Mirrors tools/m5-replay/replay.py::Frame.to_bytes().
//
// 76-byte frame: 72-byte ASCII payload + 2-byte ASCII hex CRC + CR + LF.
// Used to feed the wasm-live M5 sim with synthetic data via
// `Module.cwrap('inject_serial_byte', null, ['number'])`.
//
// All numeric formatting matches snprintf in
// software/Libraries/onspeed_core/src/proto/DisplaySerial.cpp.

const PAYLOAD_LEN = 72;
const FRAME_LEN   = 76;

function clampInt(v, lo, hi) {
  v = Math.round(v);
  return Math.max(lo, Math.min(hi, v));
}
function clampUint(v, lo, hi) {
  v = Math.round(v);
  return Math.max(lo, Math.min(hi, v));
}

// Format helpers — all produce fixed-width ASCII matching the printf specs
// in DisplaySerial.cpp.
function intStr(v, width, signed) {
  const s = signed ? (v >= 0 ? '+' + Math.abs(v).toString().padStart(width - 1, '0')
                              : '-' + Math.abs(v).toString().padStart(width - 1, '0'))
                    : v.toString().padStart(width, '0');
  return s;
}

export function buildFrame(f) {
  const payload =
    '#1' +
    intStr(clampInt(f.pitchDeg * 10, -999, 999), 4, true) +
    intStr(clampInt(f.rollDeg  * 10, -9999, 9999), 5, true) +
    intStr(clampUint(f.iasKt   * 10, 0, 9999), 4, false) +
    intStr(clampInt(f.paltFt,        -99999, 99999), 6, true) +
    intStr(clampInt(f.turnRateDps * 10, -9999, 9999), 5, true) +
    intStr(clampInt(f.lateralG    * 100, -99, 99), 3, true) +
    intStr(clampInt(f.verticalG   *  10, -99, 99), 3, true) +
    intStr(clampUint(f.percentLift,      0, 99),    2, false) +
    intStr(clampInt(f.vsiFpm / 10,  -999, 999), 4, true) +
    intStr(clampInt(f.oatC,          -99, 99),  3, true) +
    intStr(clampInt(f.flightPathDeg * 10, -999, 999), 4, true) +
    intStr(clampInt(f.flapsDeg,      -99, 99),  3, true) +
    intStr(clampUint(f.tonesOnPctLift,    0, 99), 2, false) +
    intStr(clampUint(f.onSpeedFastPctLift, 0, 99), 2, false) +
    intStr(clampUint(f.onSpeedSlowPctLift, 0, 99), 2, false) +
    intStr(clampUint(f.stallWarnPctLift,   0, 99), 2, false) +
    intStr(clampInt(f.flapsMinDeg,    -99, 99), 3, true) +
    intStr(clampInt(f.flapsMaxDeg,    -99, 99), 3, true) +
    intStr(clampInt(f.gOnsetRate * 100, -999, 999), 4, true) +
    intStr(clampInt(f.spinCue,         -9, 9),   2, true) +
    intStr(clampUint(f.dataMark,        0, 99), 2, false) +
    intStr(clampUint(f.pipPctLift,      0, 99), 2, false);

  if (payload.length !== PAYLOAD_LEN) {
    throw new Error(`payload length ${payload.length} != ${PAYLOAD_LEN}: "${payload}"`);
  }

  // ASCII-byte sum (& 0xFF) → two-digit uppercase hex.
  let crc = 0;
  for (let i = 0; i < payload.length; i++) crc += payload.charCodeAt(i);
  crc &= 0xFF;
  const crcHex = crc.toString(16).toUpperCase().padStart(2, '0');

  const full = payload + crcHex + '\r\n';
  if (full.length !== FRAME_LEN) {
    throw new Error(`frame length ${full.length} != ${FRAME_LEN}`);
  }
  // Return as Uint8Array (each char is ASCII).
  const out = new Uint8Array(FRAME_LEN);
  for (let i = 0; i < FRAME_LEN; i++) out[i] = full.charCodeAt(i);
  return out;
}
```

- [ ] **Step 3: Write `tools/liveview-prototype/test/frameBuilder.test.js`**

```javascript
import { buildFrame } from '../lib/frameBuilder.js';

const exampleRecord = {
  pitchDeg: 5.0, rollDeg: 0.0, iasKt: 100.0, paltFt: 2500,
  turnRateDps: 0.0, lateralG: 0.0, verticalG: 1.0, percentLift: 50,
  vsiFpm: 0, oatC: 70, flightPathDeg: 0, flapsDeg: 33,
  tonesOnPctLift: 33, onSpeedFastPctLift: 51, onSpeedSlowPctLift: 64,
  stallWarnPctLift: 80, flapsMinDeg: 0, flapsMaxDeg: 33,
  gOnsetRate: 0, spinCue: 0, dataMark: 0, pipPctLift: 33,
};

export function run(assert) {
  const buf = buildFrame(exampleRecord);
  assert.equal(buf.length, 76, 'frame length 76');
  // First two bytes are '#' '1'.
  assert.equal(buf[0], 0x23, "byte 0 is '#'");
  assert.equal(buf[1], 0x31, "byte 1 is '1'");
  // Last two bytes CRLF.
  assert.equal(buf[74], 0x0D, 'byte 74 is CR');
  assert.equal(buf[75], 0x0A, 'byte 75 is LF');
  // CRC at offsets 72-73 should be valid hex.
  const crcStr = String.fromCharCode(buf[72], buf[73]);
  assert.truthy(/^[0-9A-F]{2}$/.test(crcStr), `crc is hex: "${crcStr}"`);
  // Recompute crc and verify match.
  let crc = 0;
  for (let i = 0; i < 72; i++) crc += buf[i];
  crc &= 0xFF;
  assert.equal(crc, parseInt(crcStr, 16), 'CRC matches recomputed');
}
```

- [ ] **Step 4: Add to runner; confirm pass**

- [ ] **Step 5: Commit**

```bash
git add tools/liveview-prototype/lib/frameBuilder.js tools/liveview-prototype/test/frameBuilder.test.js tools/liveview-prototype/test/runner.html
git commit -m "liveview-prototype: JS #1 frame builder"
```

### Task 0.11: Synthetic scenarios + main.js bootstrap

**Files:**
- Create: `tools/liveview-prototype/lib/scenarios.js`
- Create: `tools/liveview-prototype/lib/main.js`

The scenario module emits `{percentLift, ...}` records at 20 Hz; main.js subscribes both the SVG (TBD in Stage 1) and the wasm-live iframe (via `_inject_serial_byte`) to the same stream.

- [ ] **Step 1: Write `tools/liveview-prototype/lib/scenarios.js`**

A small set of named scenarios, each a function `(t_ms) => record`. Start with three: `idle` (constant ground state), `cruise` (constant level flight), `approach` (a sweep from L/Dmax through OnSpeed to stall warning over 30 seconds, then back).

```javascript
// Synthetic scenarios — each one is a function that takes elapsed milliseconds
// since the scenario started and returns a complete record matching the
// frameBuilder schema.

const RV10_FULL_FLAPS = {
  // From SerialRead.cpp:198-203: alpha_0 = -9.21, alpha_stall = 11.57.
  // L/Dmax = -2.24°, OnSpeedFast = 2.19°, OnSpeedSlow = 4.09°, StallWarn = 7.94°.
  // Convert to percent-lift: pct = (aoaDeg - alpha_0) / (alpha_stall - alpha_0) * 100.
  alpha0: -9.21,
  alphaStall: 11.57,
  aoaToPct(aoaDeg) {
    return Math.max(0, Math.min(99, Math.round((aoaDeg - this.alpha0) / (this.alphaStall - this.alpha0) * 100)));
  },
};

const ANCHORS_FULL_FLAPS = {
  tonesOnPctLift:     RV10_FULL_FLAPS.aoaToPct(-2.24),  // ~33
  onSpeedFastPctLift: RV10_FULL_FLAPS.aoaToPct( 2.19),  // ~55
  onSpeedSlowPctLift: RV10_FULL_FLAPS.aoaToPct( 4.09),  // ~64
  stallWarnPctLift:   RV10_FULL_FLAPS.aoaToPct( 7.94),  // ~83
  pipPctLift:         RV10_FULL_FLAPS.aoaToPct(-2.24),  // L/Dmax
};

function record(overrides) {
  return {
    pitchDeg: 0, rollDeg: 0, iasKt: 0, paltFt: 0,
    turnRateDps: 0, lateralG: 0, verticalG: 1.0, percentLift: 0,
    vsiFpm: 0, oatC: 70, flightPathDeg: 0, flapsDeg: 33,
    tonesOnPctLift: 0, onSpeedFastPctLift: 0, onSpeedSlowPctLift: 0,
    stallWarnPctLift: 0, flapsMinDeg: 0, flapsMaxDeg: 33,
    gOnsetRate: 0, spinCue: 0, dataMark: 0, pipPctLift: 0,
    ...ANCHORS_FULL_FLAPS,
    ...overrides,
  };
}

export const scenarios = {
  idle: (_t) => record({ iasKt: 0, percentLift: 0 }),
  cruise: (_t) => record({ iasKt: 130, paltFt: 4500, percentLift: 30, pitchDeg: 1.5 }),
  approach: (t) => {
    // 30 s sweep: percent-lift goes from 30 to 90 over 15 s, back to 30 over 15 s.
    const phase = (t / 15000) % 2;  // 0..1..2..0
    const frac = phase <= 1 ? phase : (2 - phase);
    return record({
      iasKt: 75 - 15 * frac,            // 75 → 60
      paltFt: 800,
      percentLift: Math.round(30 + 60 * frac),
      pitchDeg: 4 + 4 * frac,
      flightPathDeg: -3,
      vsiFpm: -500,
    });
  },
  stall: (t) => {
    // 10 s climb past stallWarn into flashing red, then recovery.
    const phase = (t / 10000) % 1;  // 0..1
    const pct = phase < 0.7 ? Math.round(50 + 50 * (phase / 0.7))   // climb 50→100
                            : Math.round(100 - 80 * ((phase - 0.7) / 0.3));  // recover 100→20
    return record({
      iasKt: 65,
      paltFt: 3000,
      percentLift: Math.min(99, pct),
      pitchDeg: 8,
      verticalG: 1.0,
    });
  },
};
```

- [ ] **Step 2: Write `tools/liveview-prototype/lib/main.js`**

```javascript
import { scenarios } from './scenarios.js';
import { buildFrame } from './frameBuilder.js';

// State.
let currentScenario = 'idle';
let scenarioStart = performance.now();
let currentMode = 'aoa';

// Subscribers — Stage 1+ register these to receive { record } messages.
const subscribers = [];
export function subscribe(fn) { subscribers.push(fn); return () => {
  const i = subscribers.indexOf(fn);
  if (i >= 0) subscribers.splice(i, 1);
}; }

// 20 Hz tick — emit current scenario's record to all subscribers.
function tick() {
  const t = performance.now() - scenarioStart;
  const fn = scenarios[currentScenario];
  if (!fn) return;
  const r = fn(t);
  for (const s of subscribers) s(r);

  // Also push the record into the wasm-live iframe via inject_serial_byte.
  pushToWasm(r);
}

setInterval(tick, 50);  // 20 Hz

// Wasm-live driver. Wait for the iframe's Module to be ready, then call
// inject_serial_byte for every byte of every frame.
let injectFn = null;
const wasmIframe = document.getElementById('wasm-iframe');
if (wasmIframe) {
  wasmIframe.addEventListener('load', () => {
    const iwin = wasmIframe.contentWindow;
    // Module.cwrap may not be ready yet; poll briefly.
    const tryBind = () => {
      try {
        if (iwin.Module && iwin.Module.cwrap) {
          injectFn = iwin.Module.cwrap('inject_serial_byte', null, ['number']);
          console.log('[prototype] WASM bridge bound');
          return;
        }
      } catch (e) { /* cross-origin? same-origin since both served by us */ }
      setTimeout(tryBind, 200);
    };
    setTimeout(tryBind, 200);
  });
}

function pushToWasm(record) {
  if (!injectFn) return;
  const buf = buildFrame(record);
  for (let i = 0; i < buf.length; i++) injectFn(buf[i]);
}

// ----- UI wiring -----

document.querySelectorAll('#scenario-nav button[data-scenario]').forEach(btn => {
  btn.addEventListener('click', () => {
    currentScenario = btn.dataset.scenario;
    scenarioStart = performance.now();
  });
});

document.querySelectorAll('#mode-nav button[data-mode]').forEach(btn => {
  btn.addEventListener('click', () => {
    currentMode = btn.dataset.mode;
    document.querySelectorAll('[data-mode-panel]').forEach(p => {
      p.style.display = p.dataset.modePanel === currentMode ? '' : 'none';
    });
  });
});

document.getElementById('theme-toggle').addEventListener('click', () => {
  const cur = document.documentElement.dataset.theme || 'dark';
  const next = cur === 'dark' ? 'light' : 'dark';
  document.documentElement.dataset.theme = next;
  localStorage.setItem('liveview-theme', next);
});

const savedTheme = localStorage.getItem('liveview-theme');
if (savedTheme) document.documentElement.dataset.theme = savedTheme;
```

- [ ] **Step 3: Manually test the harness**

```bash
cd tools/liveview-prototype
python3 -m http.server 8080 &
SERVER_PID=$!
# Open http://localhost:8080/ in a browser
# Click each scenario button; the wasm-live iframe should respond
# (it will show splash for 3 s then start rendering its sim).
# Check browser DevTools console for "WASM bridge bound" message.
kill $SERVER_PID
```

Expected: console shows the WASM bridge bound after the iframe loads. The wasm-live panel renders Mode 0 with synthetic data. Scenario buttons change the data feed visibly. The svg-root panel is empty (no SVG yet — that's Stage 1).

- [ ] **Step 4: Commit**

```bash
git add tools/liveview-prototype/lib/scenarios.js tools/liveview-prototype/lib/main.js
git commit -m "liveview-prototype: synthetic scenarios + WASM bridge"
```

### Task 0.12: Stage 0 finish — push the branch + smoke check

- [ ] **Step 1: Push the branch**

```bash
git push -u origin sritchie/liveview-five-modes
```

- [ ] **Step 2: Re-run all tests in browser**

Open `http://localhost:8080/test/runner.html`. Confirm every assertion is green.

Stage 0 is complete when all tests pass and the harness loads the wasm-live sim through the iframe + receives synthetic frames.

---

## Stage 1: Mode 0 — Primary AOA + corner readouts

The new SVG mounts inside `#svg-root`. By the end of Stage 1, the left panel of the prototype shows a faithful M5 mode 0 indexer that visually matches the wasm-live sim on the right under every scenario.

### Task 1.1: Static SVG skeleton (chevrons, donut, index bar, pip dots, percent number)

**Files:**
- Create: `tools/liveview-prototype/lib/modes/aoa.js`
- Modify: `tools/liveview-prototype/index.html` (add `#svg-root` SVG container with mode panel attribute)
- Modify: `tools/liveview-prototype/lib/main.js` (mount aoa mode on subscribe)

This task creates the static SVG layout — every M5 element is in the right position with placeholder colors. Animation comes in Task 1.2.

- [ ] **Step 1: Write `tools/liveview-prototype/lib/modes/aoa.js`**

The module exports a `mount(rootEl, { subscribe })` function. It builds the SVG once, returns an `update(record)` callback the subscriber pump invokes per frame. (Show the full SVG markup with every M5 element from spec; use geometry.js constants. Code-step required — show the full source.)

```javascript
import * as G from '../geometry.js';
import { colors } from '../colors.js';
import { mapPct2Display } from '../pct2y.js';
import { chevronColors } from '../chevronColors.js';
import { donutColors } from '../donutColors.js';
import { slipBall, slipFromLateralG } from '../slipBall.js';
import { flapWidgetFrac, flapTriangleTransform } from '../flapWidget.js';

// Build the mode-0 SVG once. Returns { el, update(record) }.
//
// Geometry references inline cite main.cpp source lines so future readers
// can cross-check with the M5 firmware. SVG uses `shape-rendering="crispEdges"`
// on the chevrons and pip elements where pixel sharpness matters; text
// nodes use system sans-serif at sizes tuned to FreeSans visually.
export function mountAoa(rootEl) {
  const SVG_NS = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', `0 0 ${G.M5_PANEL_W} ${G.M5_PANEL_H}`);
  svg.setAttribute('xmlns', SVG_NS);
  svg.style.background = colors.TFT_BLACK;
  svg.style.width  = '100%';
  svg.style.height = '100%';

  // ---- Helper: construct + append element with attrs ----
  const mk = (tag, attrs, parent = svg) => {
    const e = document.createElementNS(SVG_NS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    parent.appendChild(e);
    return e;
  };

  // ---- Indexer bounding rounded rect ----
  mk('rect', {
    x: G.INDEXER_X,
    y: G.INDEXER_Y,
    width: G.INDEXER_WIDTH,
    height: G.INDEXER_HEIGHT,
    rx: G.INDEXER_BOX_RADIUS,
    fill: 'none',
    stroke: colors.TFT_DARKGREY,
    'stroke-width': 1,
  });

  // ---- Chevrons (4 halves, each is a rotated rectangle drawn as <rect> with transform) ----
  // Use rect with rotation transform anchored at each half-center.
  const chevronEls = {};
  const drawChevronHalf = (key, cx, cy, signRad) => {
    const angleDeg = (signRad * 180 / Math.PI);
    return mk('rect', {
      x: cx - G.CHEVRON_HALF_W,
      y: cy - G.CHEVRON_HALF_H,
      width: G.CHEVRON_HALF_W * 2,
      height: G.CHEVRON_HALF_H * 2,
      transform: `rotate(${angleDeg} ${cx} ${cy})`,
      fill: colors.TFT_DARKGREY,
      'shape-rendering': 'crispEdges',
    });
  };
  // top-left: rotate +π/8; top-right: rotate -π/8; bottom inversed.
  chevronEls.topLeft     = drawChevronHalf('topLeft',     G.CHEVRON_TOP_LEFT_CX,     G.CHEVRON_TOP_LEFT_CY,    -G.CHEVRON_ROTATION_RAD);
  chevronEls.topRight    = drawChevronHalf('topRight',    G.CHEVRON_TOP_RIGHT_CX,    G.CHEVRON_TOP_RIGHT_CY,    G.CHEVRON_ROTATION_RAD);
  chevronEls.bottomLeft  = drawChevronHalf('bottomLeft',  G.CHEVRON_BOTTOM_LEFT_CX,  G.CHEVRON_BOTTOM_LEFT_CY,  G.CHEVRON_ROTATION_RAD);
  chevronEls.bottomRight = drawChevronHalf('bottomRight', G.CHEVRON_BOTTOM_RIGHT_CX, G.CHEVRON_BOTTOM_RIGHT_CY, -G.CHEVRON_ROTATION_RAD);

  // ---- Donut: black surround, then top/bottom arcs, then center dot ----
  // Draw black surround circle as a rect of fillCircle radius DONUT_BLACK_R.
  mk('circle', { cx: G.INDEXER_CX, cy: G.INDEXER_CY, r: G.DONUT_BLACK_R, fill: colors.TFT_BLACK });
  // Bottom arc: 0 to π means starting at angle 0 (right side) sweeping CCW.
  // SVG arc path: M (start) A rx ry rotation large-arc-flag sweep-flag (end).
  const donutBottomArc = mk('path', {
    d: arcPath(G.INDEXER_CX, G.INDEXER_CY, G.DONUT_ARC_R, 0, Math.PI),
    fill: 'none',
    stroke: colors.TFT_DARKGREY,
    'stroke-width': G.DONUT_ARC_LINEWIDTH,
  });
  const donutTopArc = mk('path', {
    d: arcPath(G.INDEXER_CX, G.INDEXER_CY, G.DONUT_ARC_R, Math.PI, 2 * Math.PI),
    fill: 'none',
    stroke: colors.TFT_DARKGREY,
    'stroke-width': G.DONUT_ARC_LINEWIDTH,
  });
  // Black gap between arcs.
  mk('rect', {
    x: G.DONUT_GAP_X, y: G.DONUT_GAP_Y,
    width: G.DONUT_GAP_W, height: G.DONUT_GAP_H,
    fill: colors.TFT_BLACK,
  });
  // Center dot.
  const donutDot = mk('circle', {
    cx: G.INDEXER_CX, cy: G.INDEXER_CY,
    r: G.DONUT_DOT_R, fill: colors.TFT_DARKGREY,
  });

  // ---- Index bar (white moving horizontal) ----
  const indexBar = mk('rect', {
    x: G.INDEX_BAR_X, y: 192, width: G.INDEX_BAR_W, height: G.INDEX_BAR_H,
    fill: colors.TFT_WHITE,
    stroke: colors.TFT_BLACK,
    'stroke-width': 0.5,
  });

  // ---- L/Dmax pip dots (left + right) ----
  const pipLeftHalo  = mk('circle', { cx: G.PIP_LEFT_CX,  cy: 192, r: G.PIP_HALO_R,  fill: colors.TFT_BLACK });
  const pipLeftInner = mk('circle', { cx: G.PIP_LEFT_CX,  cy: 192, r: G.PIP_INNER_R, fill: colors.TFT_WHITE });
  const pipRightHalo  = mk('circle', { cx: G.PIP_RIGHT_CX, cy: 192, r: G.PIP_HALO_R,  fill: colors.TFT_BLACK });
  const pipRightInner = mk('circle', { cx: G.PIP_RIGHT_CX, cy: 192, r: G.PIP_INNER_R, fill: colors.TFT_WHITE });

  // ---- Percent-lift number above indexer ----
  // Black outline (drawn as 9 copies at ±3 offset is overkill in SVG; use stroke).
  const pctLiftText = mk('text', {
    x: G.PCT_LIFT_X, y: G.PCT_LIFT_Y,
    'font-family': 'Helvetica, Arial, sans-serif',
    'font-weight': 'bold',
    'font-size': G.PCT_LIFT_FONT_SIZE,
    fill: colors.TFT_WHITE,
    stroke: colors.TFT_BLACK,
    'stroke-width': G.PCT_LIFT_OUTLINE_PX,
    'paint-order': 'stroke',
    'dominant-baseline': 'alphabetic',
    'text-anchor': 'start',
  });
  pctLiftText.textContent = '00';

  // ---- Corner readouts ----
  const iasLabel = mk('text', {
    x: G.CORNER_LEFT_X, y: G.CORNER_LABEL_Y,
    'font-family': 'Helvetica, Arial, sans-serif', 'font-size': G.CORNER_LABEL_FONT_SIZE,
    fill: colors.TFT_GREEN, 'text-anchor': 'start',
  });
  iasLabel.textContent = 'IAS';
  const iasNum = mk('text', {
    x: G.CORNER_LEFT_X + 2, y: G.CORNER_NUM_Y,
    'font-family': 'Helvetica, Arial, sans-serif', 'font-weight': 'bold',
    'font-size': G.CORNER_NUM_FONT_SIZE, fill: colors.TFT_WHITE, 'text-anchor': 'start',
  });
  iasNum.textContent = '0';
  const gLabel = mk('text', {
    x: G.CORNER_RIGHT_X, y: G.CORNER_LABEL_Y,
    'font-family': 'Helvetica, Arial, sans-serif', 'font-size': G.CORNER_LABEL_FONT_SIZE,
    fill: colors.TFT_GREEN, 'text-anchor': 'end',
  });
  gLabel.textContent = 'G';
  const gNum = mk('text', {
    x: G.CORNER_RIGHT_X, y: G.CORNER_NUM_Y,
    'font-family': 'Helvetica, Arial, sans-serif', 'font-weight': 'bold',
    'font-size': G.CORNER_NUM_FONT_SIZE, fill: colors.TFT_WHITE, 'text-anchor': 'end',
  });
  gNum.textContent = '+1.0';

  // ---- Flap circle widget ----
  mk('circle', { cx: G.FLAP_CX, cy: G.FLAP_CY, r: G.FLAP_R, fill: colors.TFT_GREY });
  const flapTriangle = mk('path', {
    fill: colors.TFT_GREY,
    'shape-rendering': 'crispEdges',
  });
  const flapAngleText = mk('text', {
    x: G.FLAP_CX, y: G.FLAP_CY + 4,
    'font-family': 'Helvetica, Arial, sans-serif', 'font-size': 12,
    fill: colors.TFT_WHITE, 'text-anchor': 'middle',
  });
  flapAngleText.textContent = '0';

  // ---- Slip ball ----
  // Frame ticks (black/white bars on either side of the ball).
  // drawSlip() :1078-1081: 4 rects, 2 black + 2 white, total 16 px wide each side.
  mk('rect', { x: G.SLIP_CENTER_X - G.SLIP_H/2 - 9, y: G.SLIP_Y, width: 10, height: G.SLIP_H, fill: colors.TFT_BLACK });
  mk('rect', { x: G.SLIP_CENTER_X - G.SLIP_H/2 - 7, y: G.SLIP_Y, width: 6,  height: G.SLIP_H, fill: colors.TFT_WHITE });
  mk('rect', { x: G.SLIP_CENTER_X + G.SLIP_H/2,     y: G.SLIP_Y, width: 10, height: G.SLIP_H, fill: colors.TFT_BLACK });
  mk('rect', { x: G.SLIP_CENTER_X + G.SLIP_H/2 + 2, y: G.SLIP_Y, width: 6,  height: G.SLIP_H, fill: colors.TFT_WHITE });
  const slipBallEl = mk('circle', {
    cx: G.SLIP_CENTER_X, cy: G.SLIP_CENTER_Y, r: G.SLIP_BALL_R, fill: colors.TFT_GREEN,
  });

  // ---- G-onset right-edge tape ----
  // Tick ladder: lines at every TICK_STEP starting from FIRST_Y.
  for (let y = G.GONSET_TICK_FIRST_Y; y <= G.GONSET_TICK_LAST_Y; y += G.GONSET_TICK_STEP) {
    mk('line', {
      x1: G.GONSET_TICK_X1, y1: y, x2: G.GONSET_TICK_X2, y2: y,
      stroke: colors.TFT_GREY, 'stroke-width': 1,
    });
  }
  // Zero pip: 3 horizontal 7-px lines at y=118, 119, 120.
  for (const y of [G.GONSET_PIP_Y_TOP, G.GONSET_PIP_Y_MIDDLE, G.GONSET_PIP_Y_BOT]) {
    mk('line', {
      x1: G.GONSET_PIP_X1, y1: y, x2: G.GONSET_PIP_X2, y2: y,
      stroke: colors.TFT_GREY, 'stroke-width': 1,
    });
  }
  // Onset bar — fillRect that we update each frame.
  const onsetBar = mk('rect', {
    x: G.GONSET_BAR_X, y: G.GONSET_ZERO_Y, width: G.GONSET_BAR_W, height: 0,
    fill: colors.TFT_YELLOW,
  });

  rootEl.appendChild(svg);

  // ---- Update function (called per data tick in Task 1.2) ----
  function update(rec) {
    // Stub for now — Task 1.2 fills this in.
  }

  return { el: svg, update };
}

// SVG arc path helper — circle arc from angle start to end (radians).
function arcPath(cx, cy, r, startRad, endRad) {
  const x1 = cx + r * Math.cos(startRad);
  const y1 = cy + r * Math.sin(startRad);
  const x2 = cx + r * Math.cos(endRad);
  const y2 = cy + r * Math.sin(endRad);
  const largeArc = (endRad - startRad) > Math.PI ? 1 : 0;
  const sweep = 1;  // CCW in SVG (y-down)
  return `M ${x1} ${y1} A ${r} ${r} 0 ${largeArc} ${sweep} ${x2} ${y2}`;
}
```

- [ ] **Step 2: Modify `tools/liveview-prototype/index.html`**

Wrap the `#svg-root` div in a `<div data-mode-panel="aoa">` so the mode-toggle code in main.js can show/hide it. Repeat for the other modes (placeholder divs, populated in later stages).

```html
<section class="panel" id="svg-panel">
  <h2>New SVG</h2>
  <div data-mode-panel="aoa" id="svg-root"></div>
  <div data-mode-panel="attitude" style="display:none;"></div>
  <div data-mode-panel="indexer-only" style="display:none;"></div>
  <div data-mode-panel="energy" style="display:none;"></div>
  <div data-mode-panel="ghistory" style="display:none;"></div>
</section>
```

- [ ] **Step 3: Modify `tools/liveview-prototype/lib/main.js`**

Import `mountAoa` and mount it once on load. Subscribe its `update` to the data pump.

Append to top of main.js:
```javascript
import { mountAoa } from './modes/aoa.js';
```

After scenario/mode wiring at end of main.js, add:
```javascript
const svgRoot = document.getElementById('svg-root');
const aoaPanel = mountAoa(svgRoot);
subscribe(rec => aoaPanel.update(rec));
```

- [ ] **Step 4: Visual smoke test**

Open `http://localhost:8080/`. The new SVG panel on the left should show all the static M5 elements at the correct geometry, even though they don't animate yet (Task 1.2 wires the update). The wasm-live panel on the right shows the actual M5 sim animating. They should look geometrically similar at zoom level.

Compare side-by-side in the browser. If any element is visibly mis-positioned (wrong corner, wrong size), revisit `geometry.js` for the offending coordinate and re-sync against the M5 source line cited in the comment.

- [ ] **Step 5: Commit**

```bash
git add tools/liveview-prototype/lib/modes/aoa.js tools/liveview-prototype/lib/main.js tools/liveview-prototype/index.html
git commit -m "liveview-prototype: static AOA SVG layout"
```

### Task 1.2: Wire dynamic updates into the AOA SVG

**Files:**
- Modify: `tools/liveview-prototype/lib/modes/aoa.js`

Fill in the `update(rec)` stub from Task 1.1. Every dynamic element gets one attribute set per frame.

- [ ] **Step 1: Replace `update(rec)` body with full wire-up**

```javascript
function update(rec) {
  // Build anchors array in the slot convention pct2y / chevron / donut expect.
  const anchors = [
    0,                          // [0] alpha_0 floor (always 0)
    0,                          // [1] unused
    rec.tonesOnPctLift,         // [2]
    rec.onSpeedFastPctLift,     // [3]
    rec.onSpeedSlowPctLift,     // [4]
    0,                          // [5] unused
    rec.pipPctLift,             // [6]
    rec.stallWarnPctLift,       // [7]
  ];
  // 250 ms flash flag — same as M5's flashRate / flashFlag in main.cpp.
  const flashFlag = (Math.floor(performance.now() / 250) % 2) === 1;

  // ----- Index bar -----
  const indexY = mapPct2Display(rec.percentLift, anchors);
  indexBar.setAttribute('y', indexY);

  // ----- L/Dmax pips -----
  const ldmaxY = mapPct2Display(rec.pipPctLift, anchors);
  pipLeftHalo.setAttribute('cy', ldmaxY);
  pipLeftInner.setAttribute('cy', ldmaxY);
  pipRightHalo.setAttribute('cy', ldmaxY);
  pipRightInner.setAttribute('cy', ldmaxY);

  // ----- Chevron colors -----
  const chev = chevronColors({ percentLift: rec.percentLift, anchors, flashFlag });
  chevronEls.topLeft.setAttribute('fill',     chev.top);
  chevronEls.topRight.setAttribute('fill',    chev.top);
  chevronEls.bottomLeft.setAttribute('fill',  chev.bottom);
  chevronEls.bottomRight.setAttribute('fill', chev.bottom);

  // ----- Donut colors -----
  const donut = donutColors({ percentLift: rec.percentLift, anchors });
  donutBottomArc.setAttribute('stroke', donut.bottomArc);
  donutTopArc.setAttribute('stroke', donut.topArc);
  donutDot.setAttribute('fill', donut.dot);

  // ----- Percent-lift number -----
  pctLiftText.textContent = String(rec.percentLift).padStart(2, '0');

  // ----- Corner readouts -----
  iasNum.textContent = String(Math.round(rec.iasKt));
  gNum.textContent = (rec.verticalG >= 0 ? '+' : '') + rec.verticalG.toFixed(1);

  // ----- Flap widget -----
  const frac = flapWidgetFrac(rec.flapsDeg, rec.flapsMinDeg, rec.flapsMaxDeg);
  const flapAngleDeg = flapTriangleTransform(frac);
  // Triangle: from G.FLAP_CX,FLAP_CY, sweep angle 0 → flapAngleDeg.
  // Apex at radius FLAP_TRIANGLE_TIP_R, base radius G.FLAP_R perpendicular to apex.
  const angleRad = flapAngleDeg * Math.PI / 180;
  const apexX = G.FLAP_CX + Math.cos(angleRad) * G.FLAP_TRIANGLE_TIP_R;
  const apexY = G.FLAP_CY + Math.sin(angleRad) * G.FLAP_TRIANGLE_TIP_R;
  const topX  = G.FLAP_CX + Math.sin(angleRad) * G.FLAP_R;
  const topY  = G.FLAP_CY - Math.cos(angleRad) * G.FLAP_R;
  const botX  = G.FLAP_CX - Math.sin(angleRad) * G.FLAP_R;
  const botY  = G.FLAP_CY + Math.cos(angleRad) * G.FLAP_R;
  flapTriangle.setAttribute('d', `M ${topX} ${topY} L ${apexX} ${apexY} L ${botX} ${botY} Z`);
  flapAngleText.textContent = String(rec.flapsDeg);

  // ----- Slip ball -----
  const slip = slipFromLateralG(rec.lateralG);
  const slipPos = slipBall({ slip, percentLift: rec.percentLift, stallWarn: rec.stallWarnPctLift, flashFlag });
  slipBallEl.setAttribute('cx', slipPos.cx);
  slipBallEl.setAttribute('fill', slipPos.fill);

  // ----- G-onset bar -----
  const gOnsetHeight = Math.min(120, Math.abs(rec.gOnsetRate * G.GONSET_HEIGHT_SCALE));
  const gOnsetTop    = rec.gOnsetRate > 0 ? G.GONSET_ZERO_Y - gOnsetHeight : G.GONSET_ZERO_Y;
  onsetBar.setAttribute('y', gOnsetTop);
  onsetBar.setAttribute('height', gOnsetHeight);
}
```

- [ ] **Step 2: Visual A/B test**

Refresh `http://localhost:8080/`. Click each scenario button in turn. The new SVG panel should now animate in lockstep with the wasm-live panel. Compare:

- Index bar moves up/down with percent-lift in the same y range.
- Chevrons turn yellow → red → flashing-red as the approach scenario's percent-lift climbs through the OnSpeed band into stall warning.
- Donut arcs light up green when in the OnSpeed band.
- IAS and G corner numbers track the data feed.
- Flap widget triangle rotates with `flapsDeg`.
- Slip ball stays centered when `lateralG=0`; in the cruise scenario it stays put.
- G-onset bar grows up from the zero pip as `gOnsetRate` becomes positive.

Any visible drift between the two panels means a logic bug or a geometry constant is wrong. Adjust `geometry.js` or the relevant `lib/*.js` until the two match.

- [ ] **Step 3: Commit**

```bash
git add tools/liveview-prototype/lib/modes/aoa.js
git commit -m "liveview-prototype: wire AOA SVG to live data"
```

### Task 1.3: Stage 1 review checkpoint

Capture screenshots of both panels in each scenario. Document any remaining visual deltas in a follow-up note (e.g., "FreeSans bitmap differs slightly from system Helvetica — accept" or "donut arc start angle is 1° off — fix in Task 1.4").

- [ ] **Step 1: Save side-by-side screenshots**

```bash
mkdir -p docs/superpowers/screenshots/2026-04-28-liveview-five-modes
# Use Playwright or browser screenshot tool, saving each scenario:
#   stage-1-idle.png, stage-1-cruise.png, stage-1-approach-mid.png,
#   stage-1-approach-warning.png, stage-1-stall.png
```

(If the agent has Playwright MCP available, capture programmatically; otherwise note in the README that a human reviewer should take screenshots.)

- [ ] **Step 2: Commit screenshots**

```bash
git add docs/superpowers/screenshots/
git commit -m "liveview-prototype: stage-1 visual capture"
```

- [ ] **Step 3: Push branch + open draft PR for review**

```bash
git push origin sritchie/liveview-five-modes
gh pr create --draft --head sritchie/liveview-five-modes \
  --title "[WIP] LiveView five modes — Stage 1: Mode 0 prototype" \
  --body "Stage 1 of docs/superpowers/plans/2026-04-28-liveview-five-modes.md. Prototype only — no firmware changes. Screenshots: docs/superpowers/screenshots/2026-04-28-liveview-five-modes/"
```

---

## Stage 2: Mode 1 — Backup AI with flight-path marker

[detailed task breakdown TBD by Stage 2 agent — same pattern as Stage 1: extract M5 source for the case-1 inline render and `AiGraph()`/`pitchGraph()` helpers, build a static SVG, wire the update path, A/B against wasm-live]

The agent for Stage 2 should follow the same Task pattern as Stage 1: read main.cpp `case 1:` and `AiGraph()` / `pitchGraph()` carefully, extract every constant into geometry.js, build a `mountAttitude(rootEl)` function in `lib/modes/attitude.js`, wire the update.

Critical M5 source ranges:
- `case 1:` block at main.cpp:521-624
- `AiGraph()` at main.cpp:1087-1283
- `pitchGraph()` at main.cpp:1284-1349

Mode 1 panels in the M5 source:
- Sky/ground horizon driven by Pitch + Roll
- Pitch ladder (10° graduations)
- Fixed yellow aircraft reference symbol at center (a "+W" shape per drawString)
- Magenta flight-path marker (concentric rings + perpendicular wing bars), offset proportional to FlightPath × pitch-ratio
- Slip ball at the bottom (same as mode 0 but with different drawSlip dimensions: `(80, 215, 160, 20)` per main.cpp:1418)
- Right-edge VSI tape (orange bar + tick ladder + zero pip) showing kalmanVSI
- Same corner readouts as mode 0 (IAS + PALT top, G + AOA% bottom — note this differs from mode 0's IAS + G top, no readouts bottom)

Tasks 2.1–2.N follow the Stage 1 pattern. Acceptance: the new SVG attitude panel matches the wasm-live attitude rendering side-by-side under the cruise + approach scenarios (which exercise pitch + roll + flight-path).

---

## Stage 3: Mode 2 — Indexer-only

By far the cheapest stage. Mode 2 is the same indexer widget as mode 0 but without corners, without flap circle, without slip ball, without G-onset tape — just the indexer.

**User confirmation (2026-04-28, post-Stage-2):** "the Indexer-only page should be easy and share a ton with mode 0." The indexer/percent-lift composition is already encapsulated in `widgets/indexer.js` + `widgets/percentLiftNumber.js`; Mode 2's mode file should just compose those two widgets and skip everything else.

### Task 3.1: Add `lib/modes/indexer-only.js`

**Implementation note (post-Stage-2):** The C++ source dispatches Mode 0
and Mode 2 through the same `displayAOA()` body, gated by
`numericDisplay = true/false` (main.cpp:511-635, gate at :755). Mirror
that exactly — `mountAoa()` takes a `{ numericDisplay = true }` option,
and `mountIndexerOnly` is a 3-line wrapper that calls
`mountAoa(rootEl, { numericDisplay: false })`. Do NOT copy-paste widget
mount blocks; the prototype's source structure should match the M5's.

**Files:**
- Modify: `tools/liveview-prototype/lib/modes/aoa.js` (add option flag,
  gate corner readouts + flap circle behind it)
- Create: `tools/liveview-prototype/lib/modes/indexer-only.js` (wrapper)
- Modify: `tools/liveview-prototype/lib/main.js` (mount + subscribe)

- [ ] **Step 1: Add `numericDisplay` option to `mountAoa`** — gate the
      ias/g/flap mount calls and their per-frame updates behind it.
- [ ] **Step 2: Write `modes/indexer-only.js`** — 3-line wrapper.
- [ ] **Step 3: Wire into main.js subscribe.**
- [ ] **Step 4: Visual A/B in the browser.**
- [ ] **Step 5: Commit.**

---

## Stage 4: Mode 3 — Energy / decel gauge

Read `displayDecelGauge()` at main.cpp:1350 carefully. The mode shows a horizontal arc gauge with a needle proportional to `DecelRate` (knots/sec, already in JSON). Negative = decelerating (left of center), positive = accelerating (right of center).

**User confirmation (2026-04-28, post-Stage-2):** "the positioning of the IAS and knots per second and their values should be quite similar to IAS and g on mode zero, so that should be a SLAM dunk." The IAS / kt-per-sec corner readouts use the same `cornerReadout` widget + the same Mode 0 geometry (`CORNER_LEFT_X`, `CORNER_RIGHT_X`, `CORNER_LABEL_Y`, `CORNER_NUM_Y`). Re-use `mountCornerReadout` directly without re-deriving constants.

### Tasks 4.1–4.N

[Same Stage-1 pattern: extract layout, build static SVG, wire update, A/B.]

Critical M5 source: main.cpp:1350-1435 (about 85 lines). Constants are:
- Arc gauge centered horizontally; sweep angle range and needle length encoded in the source
- Numeric IAS / DecelRate corner readouts (re-use Mode 0's geometry)
- Same right-edge G-onset tape as Mode 0 (re-use `widgets/edgeTape.js`)
- Same slip ball as Mode 0 (re-use `widgets/slipBall.js`)
- The only new widget is the horizontal decel arc + needle

After Stage 1+2 the only mode-specific work is the arc-gauge widget itself. Everything else is widget composition.

---

## Stage 5: Mode 4 — G-history

Read `displayGloadHistory()` at main.cpp:1437. 60-second strip-chart of `verticalGLoad`. Client-side ring buffer.

**User confirmation (2026-04-28, post-Stage-2):** "I'm guessing the g history is gonna be much easier for you to do." The mode is structurally simple: an axis frame, gridlines, and a single SVG `<polyline>` whose points are updated each frame from a ring buffer. No font scaling, no bank rotation, no layered widgets — should be the fastest stage to ship.

### Tasks 5.1–5.N

[Same pattern.]

The polyline can be updated by `setAttribute('points', ...)` per frame. 300 samples × 5 Hz = 60 s buffer. Even at 20 Hz updates, the cost is one string allocation + one attribute set per frame — comfortably cheap.

---

## Stage 6: Light/dark theme — DROPPED

After Stages 1-5 the user and assistant agreed the light theme is
wrong for an avionics app and removed it.  Rationale documented in
the spec's "Light/dark theme — DROPPED" section.

Files cleaned up (2026-04-28):
- `tools/liveview-prototype/index.html`: removed `data-theme="dark"`
  attribute and the toggle button.
- `tools/liveview-prototype/style.css`: removed the
  `[data-theme="light"]` block (replaced with a comment explaining why).
- `tools/liveview-prototype/lib/main.js`: removed the toggle handler
  and `localStorage` lookup.

Stage 6 is a no-op going forward.  Stage 7 picks up directly.

---

## Stage 7: Port prototype back to `html_liveview.h`

This is the only stage that touches firmware code. By the end, `/live` carries all five modes (replacing the existing AOA + Attitude implementations) and the WebSocket JSON has the three additional fields the new modes need.

### Task 7.1: Add `flapsMinDeg`, `flapsMaxDeg`, `gOnsetRate` to the WebSocket JSON

**Files:**
- Modify: `software/sketch_common/src/web_server/DataServer.cpp`
- Possibly modify: `software/sketch_common/src/tasks/AHRS.cpp`, `software/sketch_common/src/tasks/AHRS.h` (to surface gOnsetRate as `g_AHRS.gOnsetRate`)

[Detailed steps: read the existing DataServer.cpp JSON producer; add three fields to the szFormat string and the snprintf args; for gOnsetRate, decide between (a) duplicate the GOnsetFilter in DataServer or (b) move it into AHRS so both DisplaySerial and DataServer read the same filtered value. Plan recommends (b) per the spec's open question.]

### Task 7.2: Lift the SVG/JS from prototype into `html_liveview.h`

**Files:**
- Modify: `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h`

The single PROGMEM string contains the full new HTML. Carefully escape any C-string-special chars in the SVG (the existing file already handles this; follow its pattern). The JS bundles into the same string — no separate `<script src=>`.

[Detailed steps: copy the prototype's index.html (sans the wasm-live iframe), inline all CSS, inline all `lib/*.js` modules concatenated. The result is one big `R"=====(...)====="` raw string.]

### Task 7.3: Update mode toggle UI from 2-state to 5-state

**Files:**
- Modify: `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h` (the mode buttons)
- Test on the OnSpeed device (Stage 7 is where firmware testing starts)

### Task 7.4: Verify on real OnSpeed hardware

- [ ] **Step 1: Build firmware**: `pio run -e esp32s3-v4p`
- [ ] **Step 2: Flash and reboot**: `pio run -e esp32s3-v4p -t upload`
- [ ] **Step 3: Connect WiFi**, navigate to `http://192.168.0.1/live`
- [ ] **Step 4: Cycle through all 5 modes**, confirm each renders.
- [ ] **Step 5: Open on iPhone**, leave foregrounded for 10 minutes. Confirm no reload.
- [ ] **Step 6: Web Inspector CPU profile** — sustained CPU should be under 30% main thread.
- [ ] **Step 7: Commit + push.**

### Task 7.5 (deferred): Collapse the cal-wizard decel gauge into the liveview widget

Once Stage 7 lands and `/live` is firmware-served, both the in-flight
liveview and the calibration wizard are running in the same firmware
HTML environment. At that point the duplicate decel-gauge implementations
in `lib/widgets/decelGauge.js` (this branch) and `html_calibration.h` /
`javascript_calibration.h` (cal wizard) become a real maintenance burden
— same firmware, same audience, two parallel SVG decel gauges.

User confirmed (2026-04-28): "we're eventually going to move all this
stuff so it's served by the firmware. Why should there be two
implementations of this stuff?" Defer this collapse to a follow-up PR
after Stage 7 ships; in scope: parameterize `decelGauge.js` to support
the cal-wizard's range (-4..+2), 3-band layout, and 160×340 footprint,
then have `html_calibration.h` consume the same JS module instead of
inlining its own SVG.

Out of scope right now: the prototype's job is to clone the M5 first;
the consolidation story is a separate change once both consumers live
in the same place.

---

## Stage 8: Remove `/indexer` + WASM-on-device

Once Stage 7 has soaked for a few flight-test sessions, this stage is mechanical removal.

### Task 8.1: Remove `/indexer` route + html_indexer.h

**Files to delete**:
- `software/OnSpeed-Gen3-ESP32/Web/html_indexer.h`
- `software/OnSpeed-Gen3-ESP32/Web/sim_index_js.h` (generated, gitignored)
- `software/OnSpeed-Gen3-ESP32/Web/sim_index_wasm.h` (generated, gitignored)
- `scripts/embed_sim_for_firmware.sh`
- `scripts/embed_sim_prebuild.py`

**Files to modify**:
- `software/sketch_common/src/web_server/ConfigWebServer.cpp` — remove the three route handlers (`/indexer`, `/sim/index.js`, `/sim/index.wasm`); add a redirect from `/indexer` to `/live` with HTTP 301.
- `platformio.ini` — drop the embed_sim_prebuild.py extra_script entry.

### Task 8.2: Remove `BroadcastDisplayFrame` binary WebSocket producer

**Files to modify**:
- `software/sketch_common/src/web_server/DataServer.cpp` — remove `BroadcastDisplayFrame()` definition.
- `software/sketch_common/src/web_server/DataServer.h` — remove `BroadcastDisplayFrame()` prototype.
- `software/sketch_common/src/io/DisplaySerial.cpp` — remove the call to `BroadcastDisplayFrame()` (around line 402). The serial-port `#1` frame for the M5 hardware stays intact.

### Task 8.3: Update docs to point pilots at `/live`

**Files to modify**:
- `docs/site/docs/installation/external-display.md` — replace any "use /indexer on iPhone" guidance with "use /live on iPhone".
- The docs-site's WASM iframe stays (it's a desktop demo).

### Task 8.4: Final firmware test, commit, push, ship

- [ ] **Step 1: Confirm flash size dropped by ~1 MB**: `pio run -e esp32s3-v4p` and check the LittleFS partition usage.
- [ ] **Step 2: All five modes still work after removal.**
- [ ] **Step 3: `/indexer` URL redirects to `/live`.**
- [ ] **Step 4: Commit + push.**

---

## Acceptance criteria summary

By the end of Stage 8, every item below must pass:

1. `/live` carries all five M5 modes, switchable via a 5-button toggle.
2. Each mode renders faithfully against the M5's hardware behavior — chevron color rules, donut arc structure, slip ball flash, etc.
3. Light + dark themes both work; choice persists in `localStorage`.
4. iPhone Safari sustained CPU on `/live` is under 30% main-thread, peaks under 50%.
5. iPhone Safari leaves `/live` foregrounded for 30+ minutes without reloading (no "A problem repeatedly occurred on" message).
6. JS heap on `/live` does not grow more than ±2 MB after 5 minutes of steady-state operation.
7. The on-device `/indexer` route, `/sim/*` routes, `BroadcastDisplayFrame`, and `embed_sim_prebuild.py` build hook are all removed.
8. The docs-site WASM embed at `docs/site/.../external-display/` still works on desktop browsers (unchanged).
9. The serial `#1` wire format for the M5 hardware over Port C is unchanged — M5Stack hardware continues to display correctly.
10. Total flash usage on the V4P firmware drops by ≥ 800 KB versus pre-Stage-8.

---

## Risk register

- **Visual fidelity**: SVG text rendering may not exactly match FreeSans bitmap. Mitigation: tune font sizes; if unacceptable, embed FreeSans numerals as small WOFF2 (out of scope, follow-up).
- **iOS Safari WebSocket buffer**: 20 Hz JSON updates may pressure WebSocket buffer differently than the binary frame. Mitigation: stage 7 includes a real-device soak test.
- **Theme color contrast in light mode**: M5 colors are tuned for black background; light-mode equivalents may need extra tuning per element. Mitigation: stage 6 includes a per-element audit.
- **Removing `BroadcastDisplayFrame` breaks docs-site WASM** (it's the producer the docs-site iframe consumes via WebSocket): the docs-site WASM is `DUMMY_SERIAL_DATA` mode and consumes its own synthetic feed — it does not consume the WebSocket. Removing the binary producer is safe. Verify by serving the docs site locally before+after Stage 8.
- **iPhone Safari might still reload despite SVG**: low risk — the existing `/live` page already runs on iPhone Safari indefinitely without reload. SVG attribute updates are well-trodden. Mitigation: stage 7 real-device soak.
