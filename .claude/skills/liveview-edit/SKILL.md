---
name: liveview-edit
description: Use when editing OnSpeed's LiveView (the firmware-served `/live` web page), iterating on the M5 hardware display in lockstep with the JS rendering, polishing pixel layouts in either, regenerating the embedded HTML bundle, debugging LiveView WebSocket reconnect or data-fields behavior, or working on any of the five SVG modes (Mode 0 AOA, Mode 1 Attitude, Mode 2 Indexer-only, Mode 3 Energy, Mode 4 G-history). Explains the M5↔JS parallel-iteration workflow and the regenerate-and-commit contract for the firmware-served bundle.
---

# Editing OnSpeed LiveView

The OnSpeed firmware serves a tablet-friendly web page at `/live` that mirrors the M5 hardware display's five modes (Mode 0 AOA primary, Mode 1 Backup AI, Mode 2 Indexer-only, Mode 3 Energy / decel gauge, Mode 4 G-load history). Both the M5 hardware renderer (C++ on the M5Stack) and the LiveView (SVG/JS in the browser) are derived from the same M5 source — `software/OnSpeed-M5-Display/src/main.cpp`. **Edit them in lockstep; the two surfaces are deliberately bit-faithful copies of the same UI.**

## The three surfaces

| Surface | Source | Renderer | Where pilots see it |
|---|---|---|---|
| **M5 hardware** | `software/OnSpeed-M5-Display/src/main.cpp` (+ helpers) | M5GFX bitmap blits | The actual M5Stack panel mounted in the airplane |
| **LiveView prototype** (browser harness) | `tools/liveview-prototype/` (ES modules + CSS) | SVG via JavaScript | `python3 -m http.server` over the prototype dir, used for iteration |
| **LiveView firmware bundle** | Generated from the prototype by `scripts/build_liveview_html.py` | Same SVG/JS, served by ESP32 firmware at `http://192.168.0.1/live` | The OnSpeed device's WiFi captive page from a tablet/phone in the cockpit |

The M5 source is the source of truth for layout/coordinates/colors. The prototype is the SVG/JS port. The firmware bundle is the prototype concatenated into a single PROGMEM string.

## Iteration workflow

### 1. Use the side-by-side measure harness

`tools/liveview-prototype/measure.html` shows the JS prototype on the left and a wasm-live build of the M5 firmware on the right, both at exact 320×240 with a 10 px / 50 px grid overlay. **Use this for every visual A/B**:

```bash
# One-time: build the wasm-live sim
cd software/OnSpeed-M5-Display
./sim/build_wasm.sh --target live
ln -sfn ../../software/OnSpeed-M5-Display/sim/build/wasm-live ../../tools/liveview-prototype/wasm-live

# Each iteration:
cd tools/liveview-prototype
python3 -m http.server 8080
# Open http://localhost:8080/measure.html
```

Toggle the grid (button in the toolbar) for color comparisons; leave it on for pixel measurements. The two panels both consume the same synthetic data feed (JS objects to the SVG, `#1` ASCII binary frames to the WASM via `_inject_serial_byte`), so any disagreement is a rendering difference, not a data difference.

### 2. Verify against C++ source AND wasm-live before editing

When changing layout — pixel positions, font sizes, colors — first read:
1. The relevant `case N:` block in `software/OnSpeed-M5-Display/src/main.cpp` (the truth on real hardware).
2. The corresponding helper (`AiGraph()`, `displayDecelGauge()`, `pitchGraph()`, etc.).
3. The widget's existing JS at `tools/liveview-prototype/lib/widgets/<name>.js` — usually has inline citations like `// main.cpp:1437-1493` for the line range it mirrors.

If the user reports a delta ("the bars are too thick"), check both:
- Does the M5 wasm-live agree with the user's reading? → real bug; fix it.
- Does wasm-live disagree with the user? → user is comparing against real hardware (which can diverge from wasm-live in edge cases like 8 px body padding on the wasm-live shell). Trust real hardware; ask if unsure.

This rule has saved hours during the 5-mode port; do not skip.

### 3. Edit one surface, then propagate

The two surfaces (M5 C++ and LiveView SVG/JS) should stay bit-faithful. Concretely:

- **Edit the M5 C++ first when adding new behavior.** The M5 ships before the LiveView (firmware update goes out, pilots see it on hardware). The C++ is the source of truth.
- **Edit the LiveView second, citing the M5 line numbers in code comments.** Every widget/mode file already has `// main.cpp:NNNN-MMMM` references — keep this convention. Future readers cross-check both.
- **For pure JS-side polish (font scaling, browser sans-serif kerning quirks)** that only matters in the SVG, document the divergence in the file's header comment so it's not mistaken for a missed M5 update.

### 4. Test the firmware preview before committing

After editing prototype JS/CSS, two paths exist for testing:

```bash
# Path A: synthetic-scenario harness (no hardware needed)
cd tools/liveview-prototype && python3 -m http.server 8080
# open http://localhost:8080/

# Path B: firmware preview against a real OnSpeed device
# (requires you to be on the OnSpeed AP)
cd tools/liveview-prototype && python3 -m http.server 8080
# open http://localhost:8080/firmware-preview.html
# WS_URI in lib/firmware/wsClient.js targets ws://192.168.0.1:81
```

Path A exercises modes against scenarios. Path B exercises the firmware-side shell (WebSocket reconnect, data-fields table, status header) against real data without flashing.

## The regenerate-and-commit contract

The firmware-served `/live` page lives at `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h` as a PROGMEM string. **It is generated** from the prototype by `scripts/build_liveview_html.py`. The generated header is **committed to the repo** so Arduino IDE contributors (who don't run PlatformIO and never fire the pre-build hook) compile against an up-to-date version.

### When you edit prototype source

ALWAYS regenerate and commit the bundle alongside your prototype change:

```bash
# After editing tools/liveview-prototype/lib/**/*.js, style.css, or lib/firmware/**
python3 scripts/build_liveview_html.py

# Verify size hasn't ballooned (should be roughly 130-150 KB)
wc -c software/OnSpeed-Gen3-ESP32/Web/html_liveview.h

# Commit prototype edit + regenerated bundle in the same commit
git add tools/liveview-prototype/ software/OnSpeed-Gen3-ESP32/Web/html_liveview.h
git commit
```

The CI workflow `.github/workflows/ci.yml` has a `liveview-bundle-fresh` job that runs the generator and `git diff --exit-code`s the result. **A forgotten regeneration fails PR checks.** This is intentional — Arduino IDE flashing the stale committed header would silently break the page for that contributor.

### When you edit only firmware-side code (DataServer.cpp, AHRS, etc.)

No regenerate needed. The bundle reads JSON over WebSocket; firmware-side changes that adjust the JSON shape need:
1. Update `tools/liveview-prototype/lib/firmware/wsClient.js`'s field map.
2. Regenerate (per above) so the firmware bundle picks up the new field.

### PlatformIO contributors get auto-regen

PlatformIO build environments register `scripts/build_liveview_html.py` as a `pre:` extra_script. `pio run` regenerates on demand (skip-if-fresh — only if a prototype source mtime is newer than the output). So the typical PIO flow is:

```bash
pio run -e esp32s3-v4p   # regenerates html_liveview.h if needed, then builds firmware
```

## Layout / pixel polish patterns

Picked up across the 5-mode port (full reference: `tools/liveview-prototype/LESSONS.md`):

### Grid units

The user describes pixel-level positioning in **10-px grid units** (1 unit = 10 px on the 320×240 panel). When they say "the digit caps should be 1.8 units tall" → that's an 18 px cap height target. Convert at a glance.

### Font-size table (browser Helvetica/Arial cap height)

| `font-size` | Cap height | Grid units |
|---:|---:|---:|
| 16 | ~12 px | 1.2 |
| 20 | ~14.5 px | 1.45 |
| 23 | ~17 px | 1.7 |
| 33 | ~24 px | 2.4 |
| 35 | ~25 px | 2.5 |
| 36 | ~26 px | 2.6 |
| 40 | ~29 px | 2.9 |
| 45 | ~33 px | 3.3 |

Use this to plan size adjustments without round-tripping through the browser. Target user spec (e.g., "2.5 units tall") → look up `font-size` directly.

### Dominant-baseline mapping

M5GFX uses `setCursor(x, y)` (alphabetic baseline) and datums like `middle_center`. SVG translates as:

| M5 call | SVG `text-anchor` | SVG `dominant-baseline` |
|---|---|---|
| `setCursor(x, y); print(...)` | `start` | `alphabetic` |
| `middle_center` datum | `middle` | `central` |
| `middle_right` datum | `end` | `central` |
| `top_left` datum | `start` | `hanging` |

### When two modes share rendering

If the M5 dispatches two modes through one C++ function with a flag (Mode 0 + Mode 2 both call `displayAOA()` with `numericDisplay = true/false`), mirror the structure — one mode file with the option, plus a thin wrapper for the alternate. Don't copy-paste widget mounts. Mode 2's `lib/modes/indexer-only.js` is a 3-line wrapper around `mountAoa(rootEl, { numericDisplay: false })`.

## When NOT to touch the LiveView

- **Don't edit `html_liveview.h` directly.** It says `AUTO-GENERATED` at the top. Your changes will be overwritten on the next build. Edit prototype source, regenerate.
- **Don't add a light theme.** The avionics palette is dark-only by design — saturated colors lose semantic contrast on light. Mode 1's sky/ground horizon especially breaks. See spec section "Light/dark theme — DROPPED" for the full rationale.
- **Don't add module-level `import` from outside the prototype source tree.** The build script bundles `tools/liveview-prototype/lib/**/*.js` only. Outside-tree imports would break the bundler.
- **Don't add `export default`.** The bundler's IIFE-wrap strategy doesn't support default exports. Use named exports.

## Files to know

- `tools/liveview-prototype/README.md` — full prototype docs (how to iterate, how to test, file layout)
- `tools/liveview-prototype/LESSONS.md` — pixel-iteration patterns from Stages 1-5
- `tools/liveview-prototype/lib/colors.js` — TFT_* color tokens → CSS variables
- `tools/liveview-prototype/lib/geometry.js` — every layout constant for all 5 modes
- `tools/liveview-prototype/lib/widgets/*.js` — reusable building blocks (12 widgets)
- `tools/liveview-prototype/lib/modes/*.js` — five mode compositions
- `tools/liveview-prototype/lib/firmware/*` — firmware-only shell (WS client, datafields, entry point)
- `scripts/build_liveview_html.py` — bundler. Read its top docstring for transformation rules.
- `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h` — generated PROGMEM bundle. Never edit by hand.
- `software/OnSpeed-M5-Display/src/main.cpp` — M5 hardware renderer (the source of truth for layout)
- `docs/superpowers/specs/2026-04-28-liveview-five-modes.md` — the design spec
- `docs/superpowers/plans/2026-04-28-liveview-five-modes.md` — the implementation plan with task breakdown
- `docs/superpowers/plans/2026-04-28-liveview-stage7-firmware-port.md` — the firmware-port plan
