# LiveView Prototype

Browser-iterable Preact source for the firmware's `/indexer` web page.

The actual firmware artifact —
`software/OnSpeed-Gen3-ESP32/Web/html_indexer.h` — is **generated**
from this directory by `scripts/build_liveview_html.py`. The
prototype is the source of truth; the header is the build product.

## Local iteration

```bash
cd tools/liveview-prototype
python3 -m http.server 8080
# open http://localhost:8080/   for synthetic-scenario testing
```

The synthetic-scenario harness (`index.html`) mounts the same Preact
components the firmware uses, driven by canned scenarios (idle,
cruise, approach, stall warn). Useful for visual changes without
needing a real OnSpeed device.

To preview the firmware UI against real WebSocket data, connect your
laptop to the OnSpeed AP (SSID `OnSpeed`, password `angleofattack`)
and open `http://localhost:8080/firmware-preview.html` — the
WebSocket client targets `ws://192.168.0.1:81`.

## Architecture

The prototype is a Preact app. Each of the five modes (AOA primary,
Backup AI, Indexer-only, Energy, G-history) is a Preact component
that takes a record `r` (the WebSocket payload shape) and returns an
SVG tree. Components are pure functions of props — no imperative DOM
management, no `update()` closures.

```
lib/
├── colors.js, geometry.js, pct2y.js,    ← framework-free math + tokens
│   chevronColors.js, donutColors.js,
│   slipBall.js, flapWidget.js
├── components.js                         ← 16 reusable Preact components
├── modes.js                              ← Mode0..Mode4 compositions
├── main.js                               ← Synthetic-scenario harness entry
├── scenarios.js                          ← Synthetic data generator
├── firmware/
│   ├── App.js                            ← Top-level <App/> — firmware UI
│   ├── wsClient.js                       ← WebSocket reconnect + age timer
│   └── style.css                         ← Page chrome (header/nav/footer)
└── vendor/
    ├── preact-standalone.js              ← Preact + htm, MIT licensed
    └── VENDOR.md                         ← License + refresh procedure
```

Component-level conventions:

- Components live in `lib/components.js`. They take props, return
  an htm template literal, and are imported by `lib/modes.js`.
- Modes (in `lib/modes.js`) compose components against a record
  `r`. Each mode is a 10-30 line Preact component.
- The firmware entry point (`lib/firmware/App.js`) owns one
  `<App/>` component, manages mode/datafields state via `useState`,
  subscribes to the WebSocket via `useEffect`, and re-renders at
  20 Hz.

## Firmware bundle

`scripts/build_liveview_html.py` walks `lib/`, concatenates all JS
in topological order with `import`/`export` keywords stripped, and
emits the result as a single PROGMEM string at
`software/OnSpeed-Gen3-ESP32/Web/html_indexer.h`.

The Preact bundle is special-cased: wrapped in an IIFE that returns
its named exports as an object so Preact's single-letter internals
(e, n, _, t, h, ...) don't collide with our top-level scope.

### Editing prototype source

When you change anything under `tools/liveview-prototype/lib/` or
`tools/liveview-prototype/style.css`, regenerate `html_indexer.h`
and commit BOTH:

```bash
python3 scripts/build_liveview_html.py
git add tools/liveview-prototype/... software/OnSpeed-Gen3-ESP32/Web/html_indexer.h
git commit
```

The generated header is committed to the repo so Arduino IDE
contributors (who don't run PlatformIO and won't fire the pre-build
hook) compile against the latest version automatically. CI verifies
the header matches what the script would produce — a forgotten
regeneration fails PR checks.

PlatformIO users: `pio run` regenerates the header on demand
(skip-if-fresh).

### Don't edit the generated header

`html_indexer.h` starts with a `// AUTO-GENERATED` comment. If you
find yourself editing it, stop — your changes will be erased on the
next build. Edit the prototype source instead and regenerate.
