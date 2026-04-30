---
name: liveview-edit
description: Use when editing OnSpeed's LiveView (the firmware-served `/indexer` Preact page), iterating on the M5 hardware display in lockstep with the JS rendering, polishing pixel layouts, regenerating the embedded HTML bundle, debugging WebSocket reconnect or data-fields behavior, or working on any of the five SVG modes (Mode 0 AOA, Mode 1 Attitude, Mode 2 Indexer-only, Mode 3 Energy, Mode 4 G-history). Explains the M5↔Preact parallel-iteration workflow and the regenerate-and-commit contract for the firmware-served bundle.
---

# Editing OnSpeed LiveView

The OnSpeed firmware serves a tablet-friendly web page at `/indexer` that mirrors the M5 hardware display's five modes (Mode 0 AOA primary, Mode 1 Backup AI, Mode 2 Indexer-only, Mode 3 Energy / decel gauge, Mode 4 G-load history). Both the M5 hardware renderer (C++ on the M5Stack) and the LiveView (SVG/Preact in the browser) are derived from the same M5 source — `software/OnSpeed-M5-Display/src/main.cpp`. **Edit them in lockstep; the two surfaces are deliberately bit-faithful copies of the same UI.**

The legacy `/live` page (the 2-tab AOA/AHRS view) is now served from a Preact rewrite at `tools/web/lib/pages/LivePage.js`; the bundler emits its stub into `software/OnSpeed-Gen3-ESP32/Web/html_stubs.h` alongside the indexer stub.

## The two surfaces

| Surface | Source | Renderer | Where pilots see it |
|---|---|---|---|
| **M5 hardware** | `software/OnSpeed-M5-Display/src/main.cpp` (+ helpers) | M5GFX bitmap blits | The actual M5Stack panel mounted in the airplane |
| **LiveView** | `tools/web/lib/` (Preact components + CSS + vendored Preact bundle) | SVG via Preact | `http://192.168.0.1/indexer` from a tablet/phone on the OnSpeed AP |

The M5 source is the source of truth for layout/coordinates/colors. The LiveView is the SVG/Preact port. The firmware bundle (`software/OnSpeed-Gen3-ESP32/Web/static_app_js.h`, `static_app_css.h`, `html_stubs.h`) is the JS+CSS+stubs concatenated into PROGMEM by `scripts/build_web_bundle.py`.

## Verify against C++ source before editing

When changing layout — pixel positions, font sizes, colors — first read:
1. The relevant `case N:` block in `software/OnSpeed-M5-Display/src/main.cpp` (the truth on real hardware).
2. The corresponding helper (`AiGraph()`, `displayDecelGauge()`, `pitchGraph()`, etc.).
3. The component's existing JS at `tools/web/lib/components/svg/index.js` (one file with all reusable components) — most have inline citations like `// main.cpp:1437-1493` for the line range they mirror.

## Edit one surface, then propagate

The two surfaces (M5 C++ and LiveView Preact) should stay bit-faithful. Concretely:

- **Edit the M5 C++ first when adding new behavior.** The M5 ships before the LiveView (firmware update goes out, pilots see it on hardware). The C++ is the source of truth.
- **Edit the LiveView second, citing the M5 line numbers in code comments.** Every component already has `// main.cpp:NNNN-MMMM` references — keep this convention.
- **For pure JS-side polish (font scaling, browser sans-serif kerning quirks)** that only matters in the SVG, document the divergence in the file's header comment so it's not mistaken for a missed M5 update.

## Iterate in the dev server

`tools/web/dev-server/server.mjs` runs the page out of source files (no rebundling needed for dev), so edits are reflected on reload. Three modes:

```bash
# Mock data — replays an NDJSON file at 20 Hz over WS:
node tools/web/dev-server/server.mjs --mock
# Open http://localhost:8080/indexer or /live

# Mock + a synthetic scenario from lib/scenarios.js:
node tools/web/dev-server/server.mjs --mock --scenario approach

# Proxy /api/* to a real OnSpeed AP (WS goes browser→device direct):
node tools/web/dev-server/server.mjs --proxy http://192.168.0.1
```

Capture WS frames from a real device for replay:

```bash
node tools/web/dev-server/capture.mjs ws://192.168.0.1:81 \
  > tools/web/dev-server/replay/my-flight.ndjson
```

## The regenerate-and-commit contract

The firmware-served pages reference `/static/app-<sha>.js` + `/static/app-<sha>.css`, where `<sha>` is the bundle's content hash.  Both blobs (and the per-page HTML stubs) live in `software/OnSpeed-Gen3-ESP32/Web/static_app_js.h`, `static_app_css.h`, and `html_stubs.h`.  **They are generated** from `tools/web/lib/` by `scripts/build_web_bundle.py`. The generated headers are **committed to the repo** so Arduino IDE contributors (who don't run PlatformIO and never fire the pre-build hook) compile against an up-to-date version.

### When you edit web source

ALWAYS regenerate and commit the bundle alongside your source change:

```bash
# After editing anything under tools/web/lib/ or tools/web/lib/shell/PageShell.css
python3 scripts/build_web_bundle.py

# Commit source edit + regenerated headers in the same commit
git add tools/web/ \
        software/OnSpeed-Gen3-ESP32/Web/static_app_js.h \
        software/OnSpeed-Gen3-ESP32/Web/static_app_css.h \
        software/OnSpeed-Gen3-ESP32/Web/html_stubs.h \
        software/OnSpeed-Gen3-ESP32/Web/html_indexer.h
git commit
```

The CI workflow `.github/workflows/ci.yml` has a `web-bundle-fresh` job that runs the generator and `git diff --exit-code`s the result. **A forgotten regeneration fails PR checks.** This is intentional — Arduino IDE flashing stale committed headers would silently break the pages for that contributor.

### When you edit only firmware-side code (DataServer.cpp, AHRS, etc.)

If you change the JSON wire shape:
1. Update `tools/web/lib/ws/wsClient.js`'s `frameToRecord` field map.
2. Regenerate (per above) so the firmware bundle picks up the new field.

### PlatformIO contributors get auto-regen

PlatformIO build environments register `scripts/build_web_bundle.py` as a `pre:` extra_script. `pio run` regenerates on demand (skip-if-fresh — only if a source mtime is newer than the output).

```bash
pio run -e esp32s3-v4p   # regenerates the bundle if needed, then builds firmware
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

- **Don't edit the generated headers directly.** They say `AUTO-GENERATED` at the top. Your changes will be overwritten on the next build. Edit source under `tools/web/lib/`, regenerate.
- **Don't add a light theme.** The avionics palette is dark-only by design — saturated colors lose semantic contrast on light. Mode 1's sky/ground horizon especially breaks.
- **Don't add module-level `import` from outside the source tree.** The build script bundles `tools/web/lib/**/*.js` only. Outside-tree imports break the bundler.
- **Don't add `export default`.** The bundler doesn't support default exports. Use named exports.

## Files to know

- `tools/web/README.md` — full source docs (how to iterate, how to test, file layout)
- `tools/web/lib/core/colors.js` — TFT_* color tokens → CSS variables
- `tools/web/lib/core/geometry.js` — every layout constant for all 5 modes
- `tools/web/lib/components/svg/index.js` — reusable Preact SVG components
- `tools/web/lib/modes.js` — five mode compositions
- `tools/web/lib/pages/IndexerPage.js` — top-level component for `/indexer`
- `tools/web/lib/pages/LivePage.js` — top-level component for `/live`
- `tools/web/lib/shell/PageShell.js` — global nav + footer chrome
- `tools/web/lib/vendor/preact-standalone.js` — vendored Preact + htm (MIT)
- `scripts/build_web_bundle.py` — bundler. Read its top docstring for transformation rules.
- `software/OnSpeed-Gen3-ESP32/Web/static_app_js.h` — generated PROGMEM JS bundle. Never edit by hand.
- `software/OnSpeed-Gen3-ESP32/Web/static_app_css.h` — generated PROGMEM CSS bundle. Same.
- `software/OnSpeed-Gen3-ESP32/Web/html_stubs.h` — generated per-page HTML stubs. Same.
- `software/OnSpeed-M5-Display/src/main.cpp` — M5 hardware renderer (source of truth for layout)
