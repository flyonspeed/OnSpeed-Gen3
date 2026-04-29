# Stage 7: Port LiveView Prototype to Firmware `/live`

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to execute this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the existing `/live` page (currently 2 modes: AOA + Attitude) with the 5-mode SVG prototype built in Stages 1-5, served from firmware.

**Architecture:** Keep the prototype source in `tools/liveview-prototype/` as the source of truth. A new build script reads the prototype JS modules and CSS, concatenates them in dependency order, and emits `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h` as a single PROGMEM string. The script runs as a PlatformIO pre-build hook so `pio run` always produces a fresh `html_liveview.h`.

**Tech stack:**
- Python 3 build script (no extra dependencies — stdlib only)
- PlatformIO `extra_scripts` hook for auto-generation
- C++ header file (generated)
- Existing WebSocket JSON broadcast in `DataServer.cpp` (already has `DecelRate`; need to add 2 fields)

---

## Pre-flight context (read this first)

### What we're replacing

`software/OnSpeed-Gen3-ESP32/Web/html_liveview.h` is a 615-line single-string PROGMEM file containing:
- Inline `<style>` block (~20 lines)
- Inline `<script>` (~370 lines):
  - WebSocket connection + reconnect on staleness
  - `onMessage` JSON parser feeding ~12 SVG/text fields
  - `pct2y` percent-lift mapping (replicated client-side)
  - `updateAttitude` rotates the existing simple attitude SVG
  - 500 ms gated text refresh of all numeric fields
  - 3 s staleness reconnect fallback
- Inline SVGs:
  - `#aoaindexer` SVG (the legacy AOA indicator)
  - `#attitude` SVG (the legacy attitude indicator)
- Inline `<div id="datafields_aoa">` + `<div id="datafields_att">` data tables (showing AOA, Der AOA, FltPath, IAS, PAlt, iVSI, Vert G, Lat G, Pitch, Roll, DataMark, Flaps, Age)
- Footer with status indicator + AOA/AHRS toggle radio + "NOT SAFE FOR FLIGHT" warning

The prototype at `tools/liveview-prototype/` has 5 SVG modes but does NOT yet have:
- WebSocket connection logic
- 3-second reconnect fallback
- Status indicator
- "Age N sec" timer
- Numeric data-fields table (the prototype just has the SVG modes)

The firmware port has to bolt these onto the prototype.

### What's already in the WebSocket JSON

`software/sketch_common/src/web_server/DataServer.cpp:296-303` broadcasts:

```
AOA, Pitch, Roll, IAS, PAlt, verticalGLoad, lateralGLoad,
flapsPos, flapIndex, coeffP, dataMark, kalmanVSI, flightPath,
PitchRate, DecelRate, OAT, DerivedAOA,
percentLift, tonesOnPctLift, onSpeedFastPctLift, onSpeedSlowPctLift,
stallWarnPctLift, pipPctLift
```

Already has:
- `DecelRate` ✅ (Mode 3 input)
- `flapsPos` ✅ (Mode 0 flap circle reads this)
- All Mode 0/1/4 inputs ✅

Missing for the prototype:
- `flapsMinDeg` and `flapsMaxDeg` — Mode 0's flap circle widget normalizes the triangle sweep over `[min, max]` (the prototype's `flapWidget.js::flapWidgetFrac` reads these)
- `gOnsetRate` — Mode 0's right-edge orange tape

### What's NOT in the prototype that the existing /live has

- WebSocket reconnect logic (`reconnect()`, `connectWebSocket()`)
- `onClose` handler with retry storm protection
- `updateAge()` 3-second staleness detector
- Status text (`"CONNECTING..."` / `"CONNECTED"` / `"Reconnecting..."`)
- N/A sentinel handling (AOA = -100 → display "N/A")
- `fmtFixed` helper (sign-collapse for ±0.0 values)
- Datafields table

### What the prototype has that existing /live doesn't

- 5 SVG modes (vs 2)
- Synthetic scenarios (irrelevant for firmware — strip these out)
- A wasm-live A/B harness (also irrelevant for firmware)

### Constraints

- **No bundler.** ES modules concatenated by hand into a single string. After concatenation, `import`/`export` declarations have to be removed.
- **PROGMEM size.** Current `html_liveview.h` is ~22 KB after C-string escaping. The new bundle (5 SVG modes + bolt-on shell + minified WebSocket logic) will be larger; estimate 40-60 KB. Confirm fits in flash.
- **No HTTPS, no CORS.** The page is served from the same origin as the WebSocket. No fetch necessary.
- **R-string escaping.** A C++ R-string `R"=====(...)====="` must not contain the literal sequence `)=====`. The build script must scan generated content for `)=====` and either re-pick the delimiter or escape via concatenation.
- **CSP-clean inline.** ESP32 doesn't ship a strict CSP, so `<script>` and `<style>` tags inline are fine.

---

## File structure (what we'll create / modify)

**New files:**
- `OnSpeed-Gen3/scripts/build_liveview_html.py` — generates `html_liveview.h` from prototype source. ~200 lines.
- `OnSpeed-Gen3/test/test_build_liveview/test_build_liveview.py` — Python unit tests for the bundler. ~80 lines.

**Modified files:**
- `OnSpeed-Gen3/platformio.ini` — register `build_liveview_html.py` as a pre-build hook.
- `OnSpeed-Gen3/software/sketch_common/src/web_server/DataServer.cpp` — add `flapsMinDeg`, `flapsMaxDeg`, `gOnsetRate` to the JSON output.
- `OnSpeed-Gen3/software/OnSpeed-Gen3-ESP32/Web/html_liveview.h` — auto-generated, no longer hand-edited. (Add a leading comment in the generated file warning future editors not to edit by hand.)
- `OnSpeed-Gen3/.gitignore` — gitignore the generated `html_liveview.h`? **Open question — see below.**

**New prototype additions** (these go in `tools/liveview-prototype/lib/`):
- `lib/firmware/wsClient.js` — WebSocket connection logic, reconnect, age timer
- `lib/firmware/dataFields.js` — datafields table widget (preserves existing /live behavior)
- `lib/firmware/main.js` — firmware-specific entry point (no scenarios, uses real WebSocket)
- `lib/firmware/style.css` — firmware-specific CSS (mode-button styling, datafields table layout)
- `tools/liveview-prototype/firmware-preview.html` — local preview that uses `lib/firmware/main.js` against a local WebSocket OR a saved JSON-replay file (so we can test the firmware-mode JS without flashing)

---

## Open questions to resolve before starting

**Q7.1: Gitignore the generated `html_liveview.h`?**
- **Pro of gitignoring:** generated artifact, source of truth is prototype, no review noise on regeneration
- **Pro of committing:** firmware contributors who don't touch the prototype see the file; CI doesn't need to run the generator; existing-/live diffability for review

**Recommendation:** Commit it. Match the pattern of `lib/version/buildinfo.cpp` (which is *also* generated by a pre-build hook AND gitignored — but `buildinfo.cpp` is regenerated from git state on EVERY build, while `html_liveview.h` only changes when prototype source does). Since contributors who don't touch the prototype shouldn't have to install Python or run the generator, commit it. Add a test that verifies the committed file matches what the generator would produce.

**Q7.2: Where does `gOnsetRate` come from in DataServer?**
- Option A: duplicate the GOnsetFilter inside DataServer.cpp (state per WS frame). Faster to implement but creates two filter instances (DisplaySerial + DataServer) on the same input.
- Option B: Move GOnsetFilter into AHRS so it's a single instance, and both consumers (DisplaySerial + DataServer) read `g_AHRS.gOnsetRate`. Cleaner but cross-cutting refactor.

**Recommendation:** Option B. The cost is ~50 lines across AHRS.h/cpp and DisplaySerial.cpp; the win is one source of truth for the value. But this can be **deferred**: ship Stage 7 with Option A (5 lines in DataServer.cpp) and file a follow-up task for B.

Actually scratch that — looking at `DisplaySerial.cpp:163`, the GOnsetFilter there is a function-static. Moving it to AHRS makes both consumers see exactly the same filtered value (matters because the M5 hardware indicator and the web indicator should agree on what the bar shows). Pick **Option B** — do it as Task 7.2 below.

**Q7.3: Compute decel rate client-side or use the wire field?**
DataServer.cpp already broadcasts `DecelRate` (g_Sensors.fDecelRate). Use it. (No change needed; just wire the prototype's Mode 3 to read it.)

**Q7.4: Datafields table — keep, drop, or make collapsible?**
The existing `/live` shows it always. The prototype was designed for an SVG-only view. User direction (this session): "preserve the existing live view text fields" — they're useful diagnostically. Keep them as a collapsible panel beneath the SVG. Default to collapsed in Mode 0/1/2/3 (the SVG-rich modes), default to expanded in Mode 4 (the strip chart has more whitespace next to it).

Actually scratch that — variability in default state will frustrate users. **Always collapsed by default**, with a state stored in localStorage. One control, one behavior.

---

## Tasks

### Task 7.1: Create the build script `build_liveview_html.py`

**Files:**
- Create: `OnSpeed-Gen3/scripts/build_liveview_html.py`
- Create: `OnSpeed-Gen3/scripts/build_liveview_html_README.md` (one-line: "Run `python3 scripts/build_liveview_html.py` to regenerate. Auto-runs as a pre-build hook in platformio.ini.")

The script's job: read the prototype source, produce a single `html_liveview.h` file.

Inputs (read from `tools/liveview-prototype/`):
- `style.css` (the dark-mode CSS variables and base styles)
- `lib/colors.js`, `lib/geometry.js`, `lib/slipBall.js`, `lib/pct2y.js`, `lib/chevronColors.js`, `lib/donutColors.js`, `lib/flapWidget.js` (helpers)
- `lib/widgets/*.js` (12 widget files)
- `lib/modes/*.js` (5 mode files)
- (NEW) `lib/firmware/wsClient.js`, `lib/firmware/dataFields.js`, `lib/firmware/main.js`, `lib/firmware/style.css`

Output:
- `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h` — single PROGMEM string

Algorithm:
1. Read all CSS files (prototype `style.css` + firmware `style.css`), concatenate, wrap in `<style>...</style>`.
2. Read all JS files in topological order based on `import` statements. Since ES modules can't cross-import in a single-script context, the build script must:
   - Strip all `import` lines.
   - Strip all `export` keywords from declarations (`export function X` → `function X`, `export const Y` → `const Y`, `export default` is uncommon — fail-fast if encountered).
   - Concatenate into one big `<script>...</script>` block.
3. Generate the HTML scaffold:
   - DOCTYPE, head with meta, title.
   - Inline CSS block.
   - `<body>` with `<div id="container">`, `<div id="status">`, `<div id="modes">` (5 buttons), 5 `<div data-mode-panel="...">` slots, `<div id="datafields">`, footer with "NOT SAFE FOR FLIGHT" warning.
   - Inline JS block.
4. Wrap the whole HTML in `R"=====(...)====="` C-string syntax.
5. Add a leading comment to the C header: `// AUTO-GENERATED by scripts/build_liveview_html.py — DO NOT EDIT BY HAND.`
6. Verify generated content has no `)=====` substring (would break the R-string). If found, fail with a clear error.
7. Verify total bundle size is under 256 KB (PROGMEM headroom). Report the size on success.

**Steps:**

- [ ] **Step 1: Stub the script**
  Create the file with a `__main__` block that prints "build_liveview_html.py: not yet implemented" and exits 0. Wire it into platformio.ini so we know the hook fires.

- [ ] **Step 2: CSS concatenation**
  Read `tools/liveview-prototype/style.css` and produce the inline `<style>` block. Verify the existing dark-mode styles are present. Test via `python3 scripts/build_liveview_html.py --dry-run` — script should print the CSS to stdout.

- [ ] **Step 3: JS dependency resolver**
  Build a tiny resolver that reads all `*.js` files under `lib/` and `lib/widgets/` and `lib/modes/`, parses each file's `import` statements, and produces a topological order. Topo-sort with cycle detection (we don't expect cycles but fail loudly if one appears). Write a unit test for this.

- [ ] **Step 4: Strip module syntax**
  Walk each JS file's text and:
  - Drop `import {...} from '...';` lines.
  - Replace `export function` → `function`, `export const` → `const`, `export class` → `class`.
  - Skip `export default` with a clear error message (we don't use it).
  - Verify the stripped file still parses as valid JS by running it through `node --check` if Node is available (best-effort — skip if Node isn't installed; CI installs Node).

- [ ] **Step 5: Concatenate + wrap**
  Produce the full HTML scaffold. Inline CSS at top of `<head>`, inline JS at bottom of `<body>`. Verify R-string delimiter doesn't appear in content.

- [ ] **Step 6: Output**
  Write to `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h`. Print final size to stdout.

- [ ] **Step 7: Test against the existing /live route**
  Run `pio run -e esp32s3-v4p`. Build should succeed. Total firmware size should be under flash limits.

- [ ] **Step 8: Commit**
  ```
  git add scripts/build_liveview_html.py scripts/build_liveview_html_README.md platformio.ini
  git commit -m "Add build_liveview_html.py pre-build hook"
  ```

### Task 7.2: Move GOnsetFilter from DisplaySerial to AHRS

**Files:**
- Modify: `OnSpeed-Gen3/software/sketch_common/src/tasks/AHRS.h`
- Modify: `OnSpeed-Gen3/software/sketch_common/src/tasks/AHRS.cpp`
- Modify: `OnSpeed-Gen3/software/sketch_common/src/io/DisplaySerial.cpp`
- Modify: `OnSpeed-Gen3/test/test_ahrs/test_ahrs.cpp` (or wherever AHRS unit tests live)

**Context:** `DisplaySerial.cpp:163` has `static GOnsetFilter sGOnsetFilter(0.25f)` as a function-static. We want both DisplaySerial AND DataServer to read the same filtered g-onset value, so move the filter into AHRS.

**Steps:**

- [ ] **Step 1: Add `gOnsetRate` to AHRS state**
  In `AHRS.h`, add `float gOnsetRate = 0.0f;` to the public state. In `AHRS.cpp`, add a `GOnsetFilter` instance as a class member, initialized with the same 0.25 sec time constant DisplaySerial uses. Update once per IMU sample (whatever the AHRS update cadence is — likely 200 Hz).

- [ ] **Step 2: Update DisplaySerial.cpp**
  Replace the function-static `sGOnsetFilter` and its `Update()` calls with `g_AHRS.gOnsetRate`. Confirm the M5 wire format is identical pre/post change with a manual test (compare hex dumps of frames).

- [ ] **Step 3: Add `gOnsetRate` to DataServer JSON**
  In `DataServer.cpp:296`, append `,"gOnsetRate":%.2f` to the format string. Add the corresponding `g_AHRS.gOnsetRate` argument to `snprintf`.

- [ ] **Step 4: Build + native test**
  `pio run -e native -e esp32s3-v4p`. Tests should still pass.

- [ ] **Step 5: Commit**
  ```
  git commit -m "Move GOnsetFilter into AHRS"
  ```

### Task 7.3: Add `flapsMinDeg`/`flapsMaxDeg` to DataServer JSON

**Files:**
- Modify: `OnSpeed-Gen3/software/sketch_common/src/web_server/DataServer.cpp`

**Context:** Mode 0's flap circle widget reads `flapsMinDeg` and `flapsMaxDeg` to normalize the triangle sweep. These come from `g_Config.aFlaps[]` — the min/max across all configured detents. Compute them on the wire frame so the JS doesn't have to walk the array.

**Steps:**

- [ ] **Step 1: Compute min/max during the flap snapshot**
  After the existing flap-vector copy loop in DataServer.cpp (~:367), iterate over the snapshot and compute min/max `fSetting` (the angle, in degrees). Default to 0 / 33 if no detents are configured (matches the prototype's defaults).

- [ ] **Step 2: Append to format string**
  Add `,"flapsMinDeg":%i,"flapsMaxDeg":%i` to `szFormat`. Add the corresponding args to `snprintf`.

- [ ] **Step 3: Test on real hardware**
  Connect to the OnSpeed via WiFi, hit `ws://192.168.0.1:81`, capture a frame, verify the new fields are present and reasonable.

- [ ] **Step 4: Commit**

### Task 7.4: Build the firmware-side JS shell

**Files:**
- Create: `tools/liveview-prototype/lib/firmware/wsClient.js`
- Create: `tools/liveview-prototype/lib/firmware/dataFields.js`
- Create: `tools/liveview-prototype/lib/firmware/main.js`
- Create: `tools/liveview-prototype/lib/firmware/style.css`
- Create: `tools/liveview-prototype/firmware-preview.html` (testable in a local browser without flashing)

**Steps:**

- [ ] **Step 1: Port `wsClient.js`**
  Lift the WebSocket logic from existing `html_liveview.h:24-345` (init, reconnect, connectWebSocket, onOpen, onClose, onMessage, onError, updateAge, writeToStatus). Strip out the legacy AOA/Attitude SVG manipulation — only keep the connection lifecycle, age tracker, and JSON parser. Export functions: `connect(url, opts)` returning a subscriber-style object with `subscribe(fn)` and `disconnect()`. Map `onMessage` JSON to the same record shape the prototype's modes consume.

- [ ] **Step 2: Port `dataFields.js`**
  Lift the existing `<div id="datafields_aoa">` table into a JS-driven widget. Constructor: `mountDataFields(parent) → { update(rec) }`. Render rows: AOA, DerAOA, FltPath, IAS, PAlt, iVSI, VertG, LatG, Pitch, Roll, DataMark, Flaps, Age. Default-collapsed via a "data ▶" toggle button. State persists in localStorage key `liveview-datafields-expanded`.

- [ ] **Step 3: Write `firmware/main.js`**
  Mirror `lib/main.js` but (a) replace the synthetic-scenario pump with `wsClient.connect(...)` and (b) mount the dataFields widget alongside the mode panels. Mode-button HTML lives in this file's static template.

- [ ] **Step 4: Write `firmware/style.css`**
  Add styles for the mode-button row (5 buttons), the datafields toggle, the "NOT SAFE FOR FLIGHT" warning, and any layout tweaks needed for tablets/phones.

- [ ] **Step 5: Write `firmware-preview.html`**
  Local-test harness: same DOM as the firmware-served page, but loads `lib/firmware/main.js` directly via ES modules. Reads from `ws://localhost:8765/` if running, or replays a captured JSON file from `lib/firmware/sample-data.jsonl` (one JSON record per line, replayed at 20 Hz) if no socket is open. This lets us iterate on the firmware-mode JS in a browser without flashing.

- [ ] **Step 6: Verify**
  Open `firmware-preview.html` in a browser. Cycle through modes. Confirm the data-fields table looks identical to the production /live's. Toggle the table.

- [ ] **Step 7: Commit**

### Task 7.5: Wire the build script to platformio.ini

**Files:**
- Modify: `OnSpeed-Gen3/platformio.ini`

**Steps:**

- [ ] **Step 1: Add `extra_scripts` entry**
  In each ESP32 build env block (`[env:esp32s3-v4p]`, `[env:esp32s3-v4b]`), add:
  ```
  extra_scripts = pre:scripts/build_liveview_html.py
  ```

- [ ] **Step 2: Update the script to be a SCons hook**
  PlatformIO's `extra_scripts` API gives us a SCons env. The script should regenerate only if any prototype source file is newer than the output (skip-if-fresh). On first run or when prototype files change, regenerate.

- [ ] **Step 3: Build**
  `pio run -e esp32s3-v4p`. Confirm `html_liveview.h` regenerates on first build, NOT on subsequent rebuilds (unless prototype source changed).

- [ ] **Step 4: Commit**

### Task 7.6: Generate the new `html_liveview.h`

**Files:**
- Modify: `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h` (regenerated)

**Steps:**

- [ ] **Step 1: Run `python3 scripts/build_liveview_html.py`**

- [ ] **Step 2: Inspect the generated header**
  Open in an editor. Confirm:
  - Leading comment says "AUTO-GENERATED — DO NOT EDIT".
  - PROGMEM string is well-formed.
  - All 5 modes are present.
  - WebSocket logic is present.
  - Datafields table is present.
  - Total size under 256 KB.

- [ ] **Step 3: Build firmware**
  `pio run -e esp32s3-v4p`. Confirm builds.

- [ ] **Step 4: Commit**

### Task 7.7: Hardware test (USER-RUN)

**Files:** none

**Steps (executed by user, not assistant):**

- [ ] **Step 1:** Flash to device: `pio run -e esp32s3-v4p -t upload`
- [ ] **Step 2:** Connect to "OnSpeed" WiFi (password: `angleofattack`).
- [ ] **Step 3:** Navigate to `http://192.168.0.1/live` in Safari/Chrome.
- [ ] **Step 4:** Verify the 5-mode toggle works (cycle through AOA / Attitude / Indexer / Energy / G-history).
- [ ] **Step 5:** Verify each mode renders with live data (rotate the OnSpeed unit by hand to see attitude change; this is bench-test sufficient).
- [ ] **Step 6:** Open the data-fields toggle. Confirm all 13 rows update.
- [ ] **Step 7:** Disconnect / reconnect WiFi. Confirm "Reconnecting..." appears in status, then "CONNECTED" returns within 3-5 seconds.
- [ ] **Step 8:** **iPhone Safari soak test.** Open `/live` on an iPhone, leave foregrounded for 10 minutes. Confirm no reload, no white-screen tab kill. Compare to the legacy `/indexer` which reloaded every 30-45s.
- [ ] **Step 9:** **CPU profile.** Open Web Inspector → Timelines → CPU. Sustained main-thread CPU should be under 30% for the typical mode (AOA). Mode 4 (G-history) is the most expensive due to per-frame circle updates; should still be under 50%.
- [ ] **Step 10:** Report any visual/behavioral differences from the prototype side-by-side comparison done in Stages 1-5.

### Task 7.8: Drop the old "Indexer" navigation entry

**Files:**
- Modify: `software/OnSpeed-Gen3-ESP32/Web/html_header.h`

**Context:** The legacy `/indexer` route is still in the navigation menu (`html_header.h:46`). Once Stage 7 ships, the new `/live` covers everything `/indexer` did (Modes 0/2 are the indexer). Remove the menu entry.

Note: this does NOT delete the `/indexer` route or `html_indexer.h` — that's Stage 8's job. We just stop linking to it from the nav so users discover the new `/live`.

**Steps:**

- [ ] **Step 1: Remove the indexer `<li>` from the nav.**

- [ ] **Step 2: Build, flash, verify nav.**

- [ ] **Step 3: Commit**

### Task 7.9: Document the new /live in `docs/site/`

**Files:**
- Modify (or create): `OnSpeed-Gen3/docs/site/docs/configuration/liveview.md` (or wherever the existing /live is documented)

**Steps:**

- [ ] **Step 1: Update the /live documentation**
  Document the 5 modes, the data-fields table, the toggle behavior. Include screenshots from the prototype harness.

- [ ] **Step 2: Build docs locally and verify.**

- [ ] **Step 3: Commit**

---

## Risk register

**R1: Bundle size exceeds PROGMEM headroom.**
- Likelihood: low. Total prototype JS is ~2000 lines, ~80 KB ungzipped. PROGMEM has plenty of headroom.
- Mitigation: minify-on-emit if necessary (drop comments + collapse whitespace). Don't pre-minify by hand — the build script can do it.

**R2: Firmware build hooks loop.**
- Likelihood: low. The script reads from `tools/` and writes to `software/`. PlatformIO doesn't watch `tools/` so no rebuild loop.
- Mitigation: skip-if-fresh in the script.

**R3: Generated `html_liveview.h` diff is huge and unreviewable.**
- Likelihood: high. 600+ lines replaced wholesale, nearly all of it generated.
- Mitigation: Land Task 7.1 (build script) first WITHOUT regenerating the header. Then regenerate as a separate commit (Task 7.6) so the bulk-replace is a single isolated commit. Reviewers can audit the generator and trust its output.

**R4: ES module concatenation breaks because of name collisions.**
- Likelihood: medium. Several widget files have local `mk(...)` helpers with the same name.
- Mitigation: wrap each module in an IIFE during concatenation. The build script generates `(function() { /* module content */ })();` blocks. Exports become assignments to a shared `liveview` namespace.

**R5: WebSocket reconnect logic interferes with prototype subscribers.**
- Likelihood: low. The prototype's subscribe pattern is clean; wsClient just calls subscribers with a record.
- Mitigation: write a unit test for `wsClient.connect` that simulates close + reopen and verifies subscriber count is stable.

**R6: User can't easily revert if Stage 7 breaks.**
- Likelihood: medium.
- Mitigation: Task 7.6 isolates the `html_liveview.h` regeneration into a single commit. `git revert <commit>` restores the legacy /live cleanly without touching the build script.

---

## Success criteria

1. `pio run -e esp32s3-v4p` builds without warnings.
2. Generated `html_liveview.h` is < 256 KB.
3. All 5 modes render on real hardware with live data.
4. Data-fields table (collapsible) shows all 13 historical fields.
5. WebSocket reconnects work as before (3-second staleness fallback).
6. iPhone Safari soak test: 10 minutes foregrounded, no reload.
7. Web Inspector CPU profile: under 30% sustained main-thread CPU on Mode 0.
8. The legacy `/indexer` menu entry is removed (Task 7.8); the route still exists (Stage 8 removes it).

---

## Out of scope for Stage 7

- Removing `/indexer` route entirely (Stage 8).
- Removing `html_indexer.h`, `sim_index_js.h`, `sim_index_wasm.h`, `embed_sim_for_firmware.sh` (Stage 8).
- Collapsing the calibration-wizard decel gauge with the Mode 3 widget (deferred follow-up after Stage 7 ships — see Task 7.5 in the original 5-mode plan).
- Light theme. Dropped per Stage 6 decision.
- New widget polish for the existing 5 modes. Land Stage 7 as-is.
- Per-mode data-fields contextual filtering (e.g., showing only relevant fields per mode). Future enhancement.

---

## Execution sequence

The cleanest order minimizes "everything is broken at once":

1. **Task 7.1** (build script) — produces an unused tool, doesn't change firmware behavior.
2. **Task 7.2** (GOnsetFilter relocation) — backend refactor, doesn't touch UI.
3. **Task 7.3** (JSON additions) — backend, doesn't touch UI.
4. **Task 7.4** (firmware JS shell) — adds prototype source files, doesn't change firmware.
5. **Task 7.5** (platformio hook) — wires it all up.
6. **Task 7.6** (regenerate html_liveview.h) — THIS is the moment /live changes.
7. **Task 7.7** (hardware test by user).
8. **Task 7.8** (remove /indexer from nav).
9. **Task 7.9** (docs).

Tasks 1-5 can ship as a single PR ("Add liveview build infrastructure") that doesn't change /live behavior. Task 6 is the cutover commit. Tasks 7-9 are post-cutover validation + cleanup.
