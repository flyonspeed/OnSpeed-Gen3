**Status:** Spec — pending review
**Author:** Sam Ritchie
**Date:** 2026-04-28
**Branch:** `sritchie/liveview-five-modes` (to be created off master after PR #351 merges)

# Bring all five M5 display modes to LiveView

## Problem

The OnSpeed `/live` web page today has two modes: **AOA** (a stylized
SVG indexer that loosely resembles the M5 hardware indexer) and
**Attitude** (a backup AI with pitch ladder).  The companion `/indexer`
page wraps the actual M5 firmware as a WebAssembly module so the same
display renders in a phone browser, but in flight on iPhone Safari the
WASM page reloads itself every 30–45 seconds (confirmed: WebKit's
"A problem repeatedly occurred on" sustained-CPU tab kill — a Web
Inspector trace measured 240% sustained CPU in the docs-site embed).
The reload is structural: a 320×240 raster repaint loop at 20 Hz is
hostile to iOS Safari's per-tab CPU budget no matter how many corners
are tuned.

The M5 hardware itself runs five distinct modes — Primary AOA + corner
readouts (mode 0), Backup AI + flight-path marker (mode 1),
Indexer-only (mode 2), Energy / decel gauge (mode 3), and 60 s
G-history (mode 4).  LiveView only mirrors the first two of those, and
the AOA mode it does mirror is a custom SVG that does not match the M5
geometry — chevron colors are static, the donut is a single green
"smile" instead of the M5's three independently-lit segments, and the
M5's percent-lift number, slip ball, flap circle, and right-edge
G-onset tape are absent.

We need pilots to be able to put a phone on the yoke and see the same
five modes the M5 panel-mount unit shows, without any reload.  The
right way to get there is to render the M5 modes natively in HTML/SVG
inside `/live`, driven by the existing WebSocket data feed.  Browsers
repaint only what changed; SVG attribute updates at 20 Hz are
essentially free, where canvas full-frame blits at 20 Hz are
structurally expensive.  Once `/live` carries all five modes the
WASM-on-device path can be removed, freeing about 1 MB of LittleFS.

## Goals

1. All five M5 modes are reachable from `/live` and visually faithful
   to the M5's Primary-mode hardware rendering — same chevron color
   logic, same donut arc structure (three independently-lit
   segments), same percent-lift number, same corner readouts (IAS,
   verticalG), same flap-position circle, same slip ball, same
   right-edge G-onset tape with zero pip.
2. Each mode runs at low CPU (well under iOS Safari's tab-kill
   threshold) by using SVG attribute updates rather than canvas
   repaints.  Target: under ~30% main-thread CPU sustained on
   iPhone Safari.
3. The `/live` page works in both light and dark color schemes.
   Pilots flying with sunglasses want a light theme; pilots in dark
   cockpits want a dark theme.  CSS variables + a toggle button +
   `localStorage` persistence.
4. Every panel renders from the **existing WebSocket JSON broadcast**
   in `DataServer.cpp`, not from a parallel binary frame.  Three
   small additions to that JSON (see Non-goals) cover the gaps.
5. The development loop is browser-only until the visual is right.
   A standalone HTML harness in `tools/liveview-prototype/` runs the
   same SVG/JS off a synthetic data generator, so we can iterate
   pixel-faithfulness without flashing firmware.  Once the prototype
   matches the M5 we lift the SVG/JS into `html_liveview.h`.
6. The on-device `/indexer` route, the WASM serving routes
   (`/sim/index.{js,wasm}`), the binary `BroadcastDisplayFrame`
   WebSocket producer, and the `embed_sim_prebuild.py` build hook
   are removed.  The M5 sim's docs-site embed (in `docs/site/...`)
   stays — desktop browsers can absorb the CPU and it's a useful
   demo there.

## Non-goals

- Pixel-exact bitmap match.  The M5 renders FreeSans bitmap fonts
  through M5GFX with no antialiasing.  Browsers always antialias text.
  We use system fonts (`Helvetica, Arial, sans-serif`) tuned to similar
  sizes; "looks the same at panel-mount glance distance" is the bar,
  not "every pixel matches".
- Changing what the M5 hardware draws.  M5 firmware code in
  `software/OnSpeed-M5-Display/` is unchanged.  This work is purely
  on the Gen3 web side.
- Changing the binary `#1` display-serial wire format.  The Gen3 still
  emits it on Port C for the panel-mount M5; only the WebSocket
  binary broadcast (`BroadcastDisplayFrame`) is removed.
- Adding mode-switch buttons on the M5 hardware to match the new
  LiveView modes.  M5 already has its own 5-mode cycle; no change
  there.
- Pilots flying with iPhone today using `/indexer` get migrated to
  `/live`.  The `/indexer` URL gets a permanent redirect to `/live`
  so old bookmarks still work.

## Required WebSocket JSON additions

Three fields, all already computed in `DisplaySerial.cpp` for the
M5 wire and trivial to add to the JSON in
`DataServer.cpp::BuildDataServerJson`:

| New JSON key      | Type | Source                                          | Used by                                     |
| ----------------- | ---- | ----------------------------------------------- | ------------------------------------------- |
| `flapsMinDeg`     | int  | min of `g_Config.aFlaps[].iDegrees`              | mode 0 / mode 2 flap-circle widget arc range |
| `flapsMaxDeg`     | int  | max of `g_Config.aFlaps[].iDegrees`              | mode 0 / mode 2 flap-circle widget arc range |
| `gOnsetRate`      | float | shared `GOnsetFilter` state (re-init in DataServer or move filter to a shared module) | mode 0 / mode 1 right-edge G-onset tape |

`DecelRate` is already in the JSON (used by the existing `/live` data
sidebar), so mode 3 (Energy) needs no new field.  `verticalGLoad` is
already in the JSON, so mode 4 (G-history) needs no new field —
client-side ring buffer accumulates 60 s worth at the JSON broadcast
cadence (~20 Hz).

For `gOnsetRate` specifically, the cleanest path is to move the
`GOnsetFilter` instance up into a shared location (e.g.,
`g_AHRS.gOnsetRate` updated from the AHRS task at the IMU rate) so
both `DisplaySerial` and `DataServer` read the same filtered value.
That refactor is a one-task prerequisite of stage 1.

## Five modes — what each one renders

All five share the same root SVG element with `viewBox="0 0 320 240"`
matching the M5 panel resolution.  Mode toggle hides/shows top-level
groups within the SVG (or swaps which `<g class="mode-X">` has
`visibility="visible"`).  The data sidebar is shown for modes 0 and 1
and hidden for the more-immersive modes 2/3/4 (matching the M5's
"narrow" Indexer-only convention).

### Mode 0: Primary AOA indexer + corner readouts

References:
- M5 source: `displayAOA()` in `software/OnSpeed-M5-Display/src/main.cpp:717`
- Indexer widget: `drawAOA()` at `:889`
- Slip ball: `drawSlip()` at `:1060`
- Flap-frac math: `onspeed::gauges::FlapWidgetFrac` in
  `software/Libraries/onspeed_core/src/aoa/FlapWidgetFrac.h`
- Index-bar mapping: `mapPct2Display` at `:1529`

Layout (SVG coordinates):
- Indexer widget centered horizontally at `X0=160`, `Y0=96`,
  width 102, height 192 (rounded-rect bounding box at corners
  `(109, 0)` to `(211, 192)`, radius 5).
- Top chevron: two halves (left and right of `X0`), each a rotated
  parallelogram at center `(X0 ± W/4, Y0 - H/4) = (134, 48)` and
  `(186, 48)`, rotated `±π/8`.
- Bottom chevron: same shape mirrored at `Y0 + H/4 = 144`.
- Donut: black surround circle radius 24 at `(160, 96)`; bottom and
  top arcs (radius 20, line width 8) drawn between angle 0..π and
  π..2π; black gap rect `(126, 92, 68, 8)` between them; center
  dot radius 10.
- Index bar: rect at `(109, indexY)`, width 102, height 8 — `indexY`
  computed from current `percentLift` via `mapPct2Display`.
- L/Dmax pip dots: filled circles at `(109, ldmaxY)` and `(210,
  ldmaxY)` — `ldmaxY` computed from `pipPctLift` via
  `mapPct2Display`.  Inner radius 6 white, outer halo radius 8 black.
- Percent-lift number: large white-on-black-outline text, FreeSans
  Bold ~22 CSS px, baseline-left, position `(140, 27)`.
- Corner readouts: `RIGHT_X=303`, `LABEL_Y=90`, `NUM_Y=130`.  IAS
  label "IAS" green at `(5, 90)`, IAS number white at `(7, 130)`,
  G label "G" green right-anchored at `(303, 90)`, G number white
  right-anchored at `(303, 130)` formatted `%+1.1f`.
- Flap circle widget: gray circle radius 16 at `(23, 204)`, gray
  triangle with apex at radius `16+33=49` rotated by
  `FlapWidgetFrac(...) × 40°`, tip blunted 1 px, two stop-mark pixels
  at the arc endpoints, numeric flap angle (FreeSans ~12 CSS px,
  white) drawn middle-center inside the circle.
- Slip ball: 16-px radius green ball at `(160 + slipX, 221)` where
  `slipX = lateralG × 850 / 99 / 2` (from M5's
  `Slip = LateralG × 34 / 0.04` then `× (W-H-1)/99/2`); flashes red
  at `|slip|≥30` and `percentLift≥stallWarn`.  Black/white frame
  bars on either side.
- Right-edge G-onset tape: orange filled rect at
  `(313, gOnsetTop, 7, gOnsetHeight)` where `gOnsetHeight =
  |gOnsetRate × 60|` clamped 0..120 and `gOnsetTop = 119 -
  gOnsetHeight` if positive else `119`.  Tick ladder: 1-px
  horizontal lines from `(313,y)` to `(319,y)` at
  `y ∈ {14, 29, 44, ..., 224}`.  Zero pip: 3 horizontal 7-px lines at
  `(306..312, y ∈ {118, 119, 120})`.

Color logic per the M5 source:
- Top chevron: `chevMid = onSpeedSlow + (stallWarn - onSpeedSlow)/2`.
  AOA in `(onSpeedSlow, chevMid]` → yellow; AOA in `(chevMid,
  stallWarn]` → red; AOA `> stallWarn` and not flashing → red; AOA
  `> stallWarn` and flashing → dark-grey (the flash effect);
  otherwise → dark-grey.
- Bottom chevron: AOA in `[tonesOn, onSpeedSlow)` → green;
  otherwise → dark-grey.
- Donut bottom arc: AOA in `[onSpeedFast, onSpeedFast +
  0.75 × (onSpeedSlow - onSpeedFast)]` → green; else dark-grey.
- Donut top arc: AOA in `[onSpeedFast + 0.25 × (onSpeedSlow -
  onSpeedFast), onSpeedSlow]` → green; else dark-grey.
- Donut center dot: AOA in middle 50% of the OnSpeed band → green;
  else dark-grey.
- Slip ball: green normally; if `|slip|≥30` and `percentLift ≥
  stallWarn`, alternate between black and red with the same `flashFlag`
  cadence used by the chevrons (250 ms).

### Mode 1: Backup AI with flight-path marker

References:
- M5 source: case 1 in `loop()` at
  `software/OnSpeed-M5-Display/src/main.cpp:521`
- Horizon math: `AiGraph()` at `:1087`
- Pitch ladder: `pitchGraph()` at `:1284`

Layout: full-width sky+ground background driven by Pitch and Roll;
fixed aircraft reference symbol at center; flight-path marker
(magenta concentric rings + perpendicular wing bars) at offset
proportional to `flightPath` and `Roll`; pitch ladder with 10°
graduations; right-edge VSI tape (orange bar + tick ladder + zero
pip) showing `kalmanVSI`; same corner readouts as the existing
LiveView Attitude tab (IAS, PALT, G, AOA% — which is a faithful
match to the M5's mode 1 layout already, after PR #351).

The existing `/live` Attitude SVG is geometrically reasonable and
needs only modest refactoring: keep the pitch-ladder graphic, replace
the static yellow aircraft symbol with the M5's design, add the
flight-path marker (this is missing today), add the right-edge VSI
tape (also missing).  Roll and pitch are already wired.

### Mode 2: Indexer-only

Reference: case 2 in `loop()` at `:626` — same `displayAOA()`
function as mode 0 but with `numericDisplay=false`.

Layout: same indexer widget as mode 0 (chevrons, donut, index bar,
pip dots, percent-lift number above), but **no corners, no flap
circle, no slip ball, no right-edge G-onset tape**.  Just the pure
indexer centered on the screen.  The data sidebar is hidden; the
indexer fills more of the viewport.

In SVG terms this is the cheapest mode to add — re-use mode 0's
`<g class="indexer">` group, hide everything outside it, scale the
indexer up to fill the screen.

### Mode 3: Energy / decel gauge

Reference: `displayDecelGauge()` at `:1350`.

Layout: a horizontal arc gauge filling the bottom-center of the
panel, with a needle proportional to `DecelRate` (the existing
WebSocket field, units knots/sec, smoothed Savitzky-Golay
derivative of IAS).  Negative = decelerating (left of center),
positive = accelerating (right of center).  Numeric IAS-decel-rate
readout at the top.

Read the M5 source carefully for the exact geometry; this is a
reasonably involved gauge but every coordinate is a hardcoded
constant in the C++.

### Mode 4: G-history scrolling trace

Reference: `displayGloadHistory()` at `:1437`.

Layout: a 60-second strip-chart of `verticalGLoad`, x-axis time, y-axis
G-load.  M5 source maintains a 300-element ring buffer at 5 Hz.  We
do the same client-side: at every WebSocket message, push the latest
`verticalGLoad`, and once we have ≥300 samples drop the oldest.
Render as an SVG `<polyline points="...">` updated each frame.

Right-edge labels: gridlines at integer Gs.

## CPU and memory targets

- Sustained main-thread CPU on iPhone Safari (measured via Web
  Inspector → Timelines): under 30%, with peaks no more than 50%.
- JS heap stable within ±2 MB after 5 minutes of steady-state operation
  (no upward trend).
- Page never reloads from "A problem repeatedly occurred on..." even
  after 30+ minutes left foregrounded with no user gesture.

These are testable with the same Web Inspector workflow that found the
240% baseline on the WASM `/indexer`.

## Light/dark theme — DROPPED

The original spec called for a light theme alongside dark.  After
implementing the five modes and trying inversion in practice, we
dropped the light theme entirely (2026-04-28).  Reasoning:

1. Avionics displays are bright glyphs on dark panel by convention,
   precisely because that combination cuts through cockpit glare.
   Inverting it puts a mostly-bright-white screen *into* the glare
   envelope rather than punching through it.
2. The five status colors (red stall, yellow caution, green OnSpeed,
   cyan sky, brown ground) are calibrated against the dark panel.
   Dropped onto a light panel they desaturate and lose the contrast
   envelope they need to be quickly readable.
3. Mode 1 (Attitude) breaks particularly badly under inversion — the
   sky/ground horizon metaphor depends on saturated cyan above and
   saturated brown below; pale-blue and pale-tan don't read as
   "above/below the horizon."
4. The wasm-live M5 sim has no light mode, so a light theme would
   diverge from the hardware reference and break side-by-side A/B.
5. ForeFlight, Garmin Pilot, and every other GA cockpit app
   ultimately stayed dark-only for the same reasons.

If a future need surfaces (e.g., ground-school instructor wants a
projector-friendly view), revisit then.  Don't re-add a half-thought
theme without re-evaluating points 1-4.

The CSS variables in `:root` (single block, no per-theme override) still
exist so a future re-introduction would be one CSS block, not a
codebase-wide refactor.

## Browser-iteration prototype

Path: `tools/liveview-prototype/` — a standalone, self-contained HTML
file plus a small synthetic-data generator and (for A/B) a copy of
the wasm-live build artifacts.  Iterate visually side-by-side; once
the SVG matches the WASM, lift the SVG into `html_liveview.h`.

`index.html` layout: two panels side-by-side, top one is the new
SVG, bottom one is the wasm-live sim from
`software/OnSpeed-M5-Display/sim/build/wasm-live/`.  Both consume the
same synthetic data feed (a JS data-source module that emits
`{percentLift, tonesOnPctLift, ...}` records at 20 Hz, encoded as
`#1` ASCII binary frames for the WASM via `_inject_serial_byte` and
delivered as plain JS objects to the SVG).

A small set of "scenario" buttons drives the data-source through
canned manoeuvres: ground idle, climb, level cruise, slow flight to
stall warning, recovery.  These are the same scenarios `tools/m5-replay`
already supports — we lift its scenario generator into JS.

`README.md` in the prototype dir: how to build the wasm-live (`./sim/build_wasm.sh
--target live`), how to start the local server (`python3 -m http.server`),
keyboard shortcuts, what each scenario does.

## Removal at the end

Once the new modes are validated on iPhone, remove:

- `software/OnSpeed-Gen3-ESP32/Web/html_indexer.h` (entire file)
- `software/OnSpeed-Gen3-ESP32/Web/sim_index_js.h` (generated header,
  plus its build-pipeline producer)
- `software/OnSpeed-Gen3-ESP32/Web/sim_index_wasm.h` (generated header,
  plus its build-pipeline producer)
- `scripts/embed_sim_for_firmware.sh` and the
  `scripts/embed_sim_prebuild.py` PIO pre-build hook
- `BroadcastDisplayFrame()` definition and prototype in
  `software/sketch_common/src/web_server/DataServer.{cpp,h}`
- The call to `BroadcastDisplayFrame()` in
  `software/sketch_common/src/io/DisplaySerial.cpp:402`
- The `/sim/index.js` and `/sim/index.wasm` route handlers in
  `software/sketch_common/src/web_server/ConfigWebServer.cpp` and
  the `/indexer` route handler too
- Add a redirect from `/indexer` → `/live` so old bookmarks still
  work
- Update `docs/site/docs/installation/external-display.md` and the
  rest of the docs site to point pilots flying with iPhone at
  `/live` rather than `/indexer`
- Delete the docs-site embed too?  **No.**  The desktop docs page
  (`/latest/installation/external-display/`) embeds the WASM via
  iframe.  Desktop CPU is fine, and the demo is genuinely useful in
  the docs.  Leave the docs-site embed in place; only remove the
  on-device serving.

## Testing strategy

Visual: A/B against the wasm-live sim using the prototype harness.
Run synthetic scenarios (ground idle, on-speed approach, slow flight
to stall warning) and check that every SVG element matches the M5's
behavior — chevrons light up at the same percent-lift threshold, donut
arcs change at the same edges of the OnSpeed band, slip ball flashes
at the same combined-condition trigger, etc.

Unit (Vitest or plain ES module + a small assertion helper, no build
chain): pure JS modules in `tools/liveview-prototype/lib/` covered by
tests in `tools/liveview-prototype/test/`:

- `mapPct2Display(percent, anchors) → y` matches the C++
  `mapPct2Display` byte-for-byte over a representative anchor set.
- `chevronColors({percent, anchors, flashFlag}) → {top, bottom}`
  produces the same color decisions as the M5 across the full
  percent range.
- `donutColors({percent, anchors}) → {topArc, bottomArc, dot}`.
- `slipBallColors({slip, percent, stallWarn, flashFlag}) →
  {fillColor, visible}`.
- `flapWidgetAngle({flapPos, flapsMin, flapsMax}) → degrees` — same
  contract as `onspeed::gauges::FlapWidgetFrac`.

After porting back to the firmware: re-run the iPhone Safari Web
Inspector capture, confirm sustained CPU under 30%, and leave the
page foregrounded for 30+ minutes to confirm no reload.

## Stage breakdown

1. **Stage 0 — prototype harness + spec extraction.** Build the
   `tools/liveview-prototype/` directory with `index.html`,
   synthetic-data generator, side-by-side panel scaffolding, and a
   "color tokens + theme toggle" baseline.  Extract every M5
   coordinate / threshold / color decision into a single JS module
   that all later stages consume.  Unit tests for the pure functions
   live alongside.
2. **Stage 1 — Mode 0 (Primary AOA + corner readouts).**  Faithful
   chevron colors, three-segment donut, percent-lift number, corner
   readouts, flap circle, slip ball, G-onset tape.  All driven by
   the synthetic data feed.  Visual A/B with WASM.
3. **Stage 2 — Mode 1 (Attitude).**  Sky/ground horizon, pitch
   ladder, fixed aircraft symbol, flight-path marker, VSI tape, same
   corner readouts.
4. **Stage 3 — Mode 2 (Indexer-only).**  Trivial — re-use Stage 1's
   indexer group, hide everything else.
5. **Stage 4 — Mode 3 (Energy / decel gauge).**
6. **Stage 5 — Mode 4 (G-history).**
7. **Stage 6 — Light/dark theme toggle.**  Wire CSS variables, add
   the toggle button, persist to `localStorage`.
8. **Stage 7 — Port to `html_liveview.h`.**  Lift the SVG/JS into
   the firmware-served page.  Update the mode-toggle UI to a 5-state
   selector.  Add the missing JSON keys (`flapsMinDeg`,
   `flapsMaxDeg`, `gOnsetRate`).
9. **Stage 8 — Remove `/indexer` + WASM-on-device.**  Routes,
   handlers, header generators, build hook, binary WebSocket producer,
   docs updates.

Stages 1 through 6 happen entirely in the browser on synthetic data —
no firmware build required.  Stage 7 is the only stage that touches
firmware code.  Stage 8 is mechanical removal once Stage 7 has soaked.

Each stage produces a working artifact (a runnable prototype HTML file
or a working firmware build) that we can validate before moving on.

## Open questions

1. **Mode-toggle UI**: 5 buttons in a row?  A horizontal scroll-snap
   strip?  Hamburger menu?  The current `/live` page uses a 2-state
   `switch-field`.  5-state should still fit on iPhone if we keep
   labels short ("AOA / AI / Idx / Energy / G").  Lean toward a row
   of 5 buttons.
2. **Hidden modes**: should `/live` remember which mode the pilot
   last selected (`localStorage`) so a reload returns to the same
   mode?  Default yes, matching the M5's behavior of remembering its
   last mode across power cycles.
3. **Wakelock**: stays best-effort.  Pilots flying with iPhone should
   set Auto-Lock to "Never" in iOS Settings (already documented in
   PR #351).  Do we add an in-page nag that detects the wakelock
   request rejection and surfaces a small "screen will sleep —
   set Auto-Lock to Never" banner?  Lean yes, low priority.
4. **gOnsetRate refactor**: move the filter into AHRS or keep two
   independent filters (one in DisplaySerial, one in DataServer)?
   Two filters are simpler but slightly disagree on transient values.
   Lean: move into AHRS as `g_AHRS.gOnsetRate`, both consumers read.
5. **Pixel-perfect FreeSans?**  We default to system fonts for
   simplicity.  If the visual gap is unacceptable, embed FreeSans
   numerals as a small WOFF2 (~5 KB) in a follow-up.  Out of scope
   for this work.

## References

- M5 source: `software/OnSpeed-M5-Display/src/main.cpp`
- M5 protocol: `software/Libraries/onspeed_core/src/proto/DisplaySerial.h`
- Existing LiveView SVG: `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h`
- Existing indexer (to be removed): `software/OnSpeed-Gen3-ESP32/Web/html_indexer.h`
- WebSocket producer: `software/sketch_common/src/web_server/DataServer.cpp`
- Display-serial producer: `software/sketch_common/src/io/DisplaySerial.cpp`
- M5 sim build script: `software/OnSpeed-M5-Display/sim/build_wasm.sh`
- M5 replay tool (scenario generator to lift): `tools/m5-replay/replay.py`
