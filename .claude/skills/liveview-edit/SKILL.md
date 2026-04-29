---
name: liveview-edit
description: Use when editing OnSpeed's LiveView (the firmware-served `/indexer` Preact page), iterating on the M5 hardware display in lockstep with the JS rendering, polishing pixel layouts, regenerating the embedded HTML bundle, debugging WebSocket reconnect or data-fields behavior, or working on any of the five SVG modes (Mode 0 AOA, Mode 1 Attitude, Mode 2 Indexer-only, Mode 3 Energy, Mode 4 G-history). Explains the M5↔Preact parallel-iteration workflow and the regenerate-and-commit contract for the firmware-served bundle.
---

# Editing OnSpeed LiveView

The OnSpeed firmware serves a tablet-friendly web page at `/indexer` that mirrors the M5 hardware display's five modes (Mode 0 AOA primary, Mode 1 Backup AI, Mode 2 Indexer-only, Mode 3 Energy / decel gauge, Mode 4 G-load history). Both the M5 hardware renderer (C++ on the M5Stack) and the LiveView (SVG/Preact in the browser) are derived from the same M5 source — `software/OnSpeed-M5-Display/src/main.cpp`. **Edit them in lockstep; the two surfaces are deliberately bit-faithful copies of the same UI.**

Note: the legacy `/live` page (the 2-tab AOA/AHRS view at `software/OnSpeed-Gen3-ESP32/Web/html_liveview.h`) is preserved untouched. The new 5-mode view lives at `/indexer` so pilots can A/B between the two during the soak window.

## The two surfaces

| Surface | Source | Renderer | Where pilots see it |
|---|---|---|---|
| **M5 hardware** | `software/OnSpeed-M5-Display/src/main.cpp` (+ helpers) | M5GFX bitmap blits | The actual M5Stack panel mounted in the airplane |
| **LiveView** | `tools/liveview-prototype/lib/` (Preact components + CSS + vendored Preact bundle) | SVG via Preact | `http://192.168.0.1/indexer` from a tablet/phone on the OnSpeed AP |

The M5 source is the source of truth for layout/coordinates/colors. The LiveView is the SVG/Preact port. The firmware bundle (`software/OnSpeed-Gen3-ESP32/Web/html_indexer.h`) is the prototype concatenated into a single PROGMEM string by `scripts/build_liveview_html.py`.

## Verify against C++ source before editing

When changing layout — pixel positions, font sizes, colors — first read:
1. The relevant `case N:` block in `software/OnSpeed-M5-Display/src/main.cpp` (the truth on real hardware).
2. The corresponding helper (`AiGraph()`, `displayDecelGauge()`, `pitchGraph()`, etc.).
3. The component's existing JS at `tools/liveview-prototype/lib/components.js` (one file with all reusable components) — most have inline citations like `// main.cpp:1437-1493` for the line range they mirror.

## Edit one surface, then propagate

The two surfaces (M5 C++ and LiveView Preact) should stay bit-faithful. Concretely:

- **Edit the M5 C++ first when adding new behavior.** The M5 ships before the LiveView (firmware update goes out, pilots see it on hardware). The C++ is the source of truth.
- **Edit the LiveView second, citing the M5 line numbers in code comments.** Every component already has `// main.cpp:NNNN-MMMM` references — keep this convention.
- **For pure JS-side polish (font scaling, browser sans-serif kerning quirks)** that only matters in the SVG, document the divergence in the file's header comment so it's not mistaken for a missed M5 update.

## Test the firmware preview before committing

`firmware-preview.html` mounts the same Preact `<App/>` against an actual OnSpeed device's WebSocket without flashing:

```bash
# 1. Make sure your laptop is on the OnSpeed AP (SSID "OnSpeed",
#    password "angleofattack").
# 2. Serve from the prototype dir.
cd tools/liveview-prototype && python3 -m http.server 8080
# 3. Open http://localhost:8080/firmware-preview.html
```

Useful for iterating on the WebSocket client + datafields table + status header without a full firmware build cycle.

## The regenerate-and-commit contract

The firmware-served `/indexer` page lives at `software/OnSpeed-Gen3-ESP32/Web/html_indexer.h` as a PROGMEM string. **It is generated** from the prototype by `scripts/build_liveview_html.py`. The generated header is **committed to the repo** so Arduino IDE contributors (who don't run PlatformIO and never fire the pre-build hook) compile against an up-to-date version.

### When you edit prototype source

ALWAYS regenerate and commit the bundle alongside your prototype change:

```bash
# After editing tools/liveview-prototype/lib/**/*.js, style.css, or lib/firmware/**
python3 scripts/build_liveview_html.py

# Verify size hasn't ballooned (typical: ~95 KB)
wc -c software/OnSpeed-Gen3-ESP32/Web/html_indexer.h

# Commit prototype edit + regenerated bundle in the same commit
git add tools/liveview-prototype/ software/OnSpeed-Gen3-ESP32/Web/html_indexer.h
git commit
```

The CI workflow `.github/workflows/ci.yml` has an `indexer-bundle-fresh` job that runs the generator and `git diff --exit-code`s the result. **A forgotten regeneration fails PR checks.** This is intentional — Arduino IDE flashing the stale committed header would silently break the page for that contributor.

### When you edit only firmware-side code (DataServer.cpp, AHRS, etc.)

If you change the JSON wire shape:
1. Update `tools/liveview-prototype/lib/firmware/wsClient.js`'s field map.
2. Regenerate (per above) so the firmware bundle picks up the new field.

### PlatformIO contributors get auto-regen

PlatformIO build environments register `scripts/build_liveview_html.py` as a `pre:` extra_script. `pio run` regenerates on demand (skip-if-fresh — only if a prototype source mtime is newer than the output).

```bash
pio run -e esp32s3-v4p   # regenerates html_indexer.h if needed, then builds firmware
```

## Layout / pixel polish patterns

### Grid units

Pixel-level positioning is described in **10-px grid units** (1 unit = 10 px on the 320×240 panel). "The digit caps should be 1.8 units tall" → 18 px cap height target.

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

### Dominant-baseline mapping

M5GFX uses `setCursor(x, y)` (alphabetic baseline) and datums like `middle_center`. SVG translates as:

| M5 call | SVG `text-anchor` | SVG `dominant-baseline` |
|---|---|---|
| `setCursor(x, y); print(...)` | `start` | `alphabetic` |
| `middle_center` datum | `middle` | `central` |
| `middle_right` datum | `end` | `central` |
| `top_left` datum | `start` | `hanging` |

### When two modes share rendering

If the M5 dispatches two modes through one C++ function with a flag (Mode 0 + Mode 2 both call `displayAOA()` with `numericDisplay = true/false`), mirror the structure — one mode component with the option, plus a thin wrapper for the alternate. Mode 2 in `lib/modes.js` is `<Mode0 r=${r} stale=${stale} numericDisplay=${false} />`.

## When NOT to touch the LiveView

- **Don't edit `html_indexer.h` directly.** It says `AUTO-GENERATED` at the top. Your changes will be overwritten on the next build. Edit prototype source, regenerate.
- **Don't add a light theme.** The avionics palette is dark-only by design — saturated colors lose semantic contrast on light. Mode 1's sky/ground horizon especially breaks.
- **Don't add module-level `import` from outside the prototype source tree.** The build script bundles `tools/liveview-prototype/lib/**/*.js` only. Outside-tree imports would break the bundler.
- **Don't add `export default`.** The bundler doesn't support default exports. Use named exports.

## Files to know

- `tools/liveview-prototype/README.md` — full prototype docs (how to iterate, how to test, file layout)
- `tools/liveview-prototype/lib/colors.js` — TFT_* color tokens → CSS variables
- `tools/liveview-prototype/lib/geometry.js` — every layout constant for all 5 modes
- `tools/liveview-prototype/lib/components.js` — 16 reusable Preact components
- `tools/liveview-prototype/lib/modes.js` — five mode compositions
- `tools/liveview-prototype/lib/firmware/App.js` — top-level `<App/>` for /indexer
- `tools/liveview-prototype/lib/vendor/preact-standalone.js` — vendored Preact + htm (MIT)
- `scripts/build_liveview_html.py` — bundler. Read its top docstring for transformation rules.
- `software/OnSpeed-Gen3-ESP32/Web/html_indexer.h` — generated PROGMEM bundle. Never edit by hand.
- `software/OnSpeed-M5-Display/src/main.cpp` — M5 hardware renderer (the source of truth for layout)
