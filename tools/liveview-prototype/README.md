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

## Firmware bundle

This prototype is the source of truth for the firmware's `/live`
page. The actual firmware artifact —
`software/OnSpeed-Gen3-ESP32/Web/html_liveview.h` — is **generated**
by `scripts/build_liveview_html.py` from the JS modules and CSS in
this directory plus the firmware-side shell at `lib/firmware/`.

### Editing prototype source

When you change anything under `tools/liveview-prototype/lib/`,
`tools/liveview-prototype/style.css`, or
`tools/liveview-prototype/lib/firmware/`, regenerate
`html_liveview.h` and commit BOTH:

```bash
python3 scripts/build_liveview_html.py
git add tools/liveview-prototype/... software/OnSpeed-Gen3-ESP32/Web/html_liveview.h
git commit
```

The generated header is committed to the repo so Arduino IDE
contributors (who don't run PlatformIO and won't fire the pre-build
hook) compile against the latest version automatically. CI verifies
the header matches what the script would produce — a forgotten
regeneration fails PR checks.

PlatformIO users: a `pio run` regenerates the header on demand
(skip-if-fresh) so contributors who DON'T touch prototype source pay
no overhead.

### The firmware-side shell (`lib/firmware/`)

Three files only the firmware bundle uses:

- `wsClient.js` — WebSocket connection to `ws://192.168.0.1:81`
  with the legacy /live's reconnect logic. Maps the firmware's JSON
  schema to the record shape the modes consume.
- `dataFields.js` — Collapsible 13-row data-fields table preserved
  from the legacy /live for diagnostic continuity.
- `main.js` — Entry point. Mounts panels, subscribes them to the
  WebSocket, wires the mode-toggle buttons.
- `style.css` — Page chrome (status header, mode-button row,
  datafields toggle, footer warning).

The browser harness's `index.html` doesn't load these — it uses
`lib/main.js` and synthetic scenarios.

### Local firmware preview without flashing

`firmware-preview.html` mounts the same firmware-side modules with
real ES module imports against an actual OnSpeed device's
WebSocket. Useful for iterating on the WebSocket client + datafields
table without flashing:

```bash
# 1. Make sure your laptop is on the OnSpeed AP (SSID "OnSpeed",
#    password "angleofattack").
# 2. Serve from the prototype dir.
cd tools/liveview-prototype && python3 -m http.server 8765
# 3. Open http://localhost:8765/firmware-preview.html
```

### Don't edit the generated header

`html_liveview.h` starts with a `// AUTO-GENERATED` comment. If you
find yourself editing it, stop — your changes will be erased on the
next build. Edit the prototype source instead and regenerate.
