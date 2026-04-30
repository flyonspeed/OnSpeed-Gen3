# OnSpeed firmware web UI

Source for the firmware-served web pages.  The actual artifacts the
firmware embeds —
`software/OnSpeed-Gen3-ESP32/Web/static_app_js.h`,
`static_app_css.h`, `html_stubs.h` — are **generated** from this
directory by `scripts/build_web_bundle.py`.  The directory under
`tools/web/` is the source of truth; the headers are the build product.

## What this directory is, conceptually

It's two things at once.  Useful to keep both in mind while editing:

1. **The firmware's web UI.** Pages served from PROGMEM at flight time
   over the OnSpeed AP, driven by live WebSocket frames at 20 Hz.
2. **A JS implementation of OnSpeed's flight-data pipeline that runs
   in any browser.** Same components, same algorithms.  Run via the
   dev-server below, fed by mock JSON, recorded WebSocket replay
   (NDJSON), or eventually parsed SD logs.

That second hat matters as the rewrite progresses.  Future pages
anticipated:

- **Log analysis** — pilot uploads an SD log, marks calibration runs,
  the page replays them through the same JS algorithms that would
  have run live.  Confirms a calibration without re-flying.
- **Offline cal-wizard replay** — feed a captured decel run through
  the cal wizard's JS as if it were live; iterate on stall-detection
  parameters and re-run.
- **Bench-test pages** — drive firmware-side algorithms from canned
  inputs, surface drift against expected outputs.

What this means for working in `tools/web/`:

- Components in `lib/components/svg/` and `lib/components/form/` are
  pure functions of props.  No module-level state.  No direct
  WebSocket subscriptions.  Pages own data sources; components own
  rendering.  Lets the same `<DecelGauge>` render live in `/indexer`
  Mode 3 AND offline against parsed SD-log rows.
- Pure-JS flight algorithms live in `lib/core/`.  Where there's a C++
  equivalent in `software/Libraries/onspeed_core/`, both ends should
  be unit-tested against the same fixtures.
- The dev-server is meant to grow.  Today: static + mock HTTP +
  WebSocket replay.  Adding a "drag in an SD log" workflow is a
  natural extension.

The bundler is a small Python+regex script with no npm/Vite/Rollup
dependency.  `git clone && node tools/web/dev-server/server.mjs` is
the entire local-dev install.

## Quick start

Run the dev server with mock data (default port `8080`):

```bash
node tools/web/dev-server/server.mjs --mock
```

Open `http://localhost:8080/indexer` for the 5-mode indexer page or
`http://localhost:8080/live` for the legacy two-tab AOA / AHRS view.
Both pages connect to the dev server's WebSocket at `/ws`, which loops
through `dev-server/replay/cruise.ndjson` (one JSON frame per line).

Other modes:

```bash
# Static-only with a "no backend configured" fallback:
node tools/web/dev-server/server.mjs

# Mock + a synthetic scenario from lib/scenarios.js:
node tools/web/dev-server/server.mjs --mock --scenario approach

# Mock + a custom NDJSON replay:
node tools/web/dev-server/server.mjs --mock --replay path/to/log.ndjson

# Proxy /api/* to a real OnSpeed device (WS goes browser->device direct):
node tools/web/dev-server/server.mjs --proxy http://192.168.0.1
```

Or via npm: `npm run dev` is a thin wrapper around the `--mock` form.

The dev server has zero dependencies — `git clone && node ...` is
sufficient.  Node 20+ is required.

## Source layout

```
tools/web/
├── README.md                          this file
├── package.json                       npm scripts (no dependencies)
├── public/
│   ├── style.css                      synthetic-scenario harness page styles
│   └── scenarios.html                 the offline harness (separate from firmware)
├── lib/
│   ├── entry.js                       bundle entry; selects page from data-page attr
│   ├── modes.js                       the 5 indexer SVG modes (Mode0..Mode4)
│   ├── scenarios.js                   synthetic data generators for the offline harness
│   ├── scenarios-main.js              entry for public/scenarios.html
│   ├── components/svg/index.js        Preact SVG components (Indexer, Horizon, etc.)
│   ├── core/                          framework-free math + tokens
│   │   ├── colors.js, geometry.js, pct2y.js,
│   │   ├── donutColors.js, chevronColors.js,
│   │   ├── slipBall.js, flapWidget.js
│   ├── pages/
│   │   ├── IndexerPage.js             /indexer
│   │   └── LivePage.js                /live
│   ├── shell/
│   │   ├── PageShell.js               global nav, dropdowns, footer
│   │   ├── PageShell.css              chrome + page-specific styles
│   │   ├── nav.js                     single-source-of-truth nav manifest
│   │   └── apiClient.js               fetch wrapper with proxy/mock/firmware base
│   ├── ws/wsClient.js                 useWebSocket hook + frameToRecord
│   └── vendor/preact-standalone.js    Preact + htm bundle (vendored)
├── dev-server/
│   ├── server.mjs                     the dev server (mock / proxy / static)
│   ├── capture.mjs                    record device WS frames -> NDJSON
│   ├── mocks/                         JSON fixtures for /api/* in mock mode
│   │   ├── livedata.json
│   │   ├── api-sample-aoa.json        ... one fixture per /api/* endpoint;
│   │   ├── api-logs.json                  see "API mock layout" below.
│   │   └── api-...
│   └── replay/
│       └── cruise.ndjson              NDJSON frames to drive the WS replay loop
└── test/
    ├── geometry-invariants.mjs        SVG geometry + helpers
    ├── render-smoke.mjs               page-import + render-into-mock-DOM smoke
    └── api-schema.mjs                 validates /api/* mock fixtures
```

### API mock layout

The dev server's `--mock` mode resolves `/api/<segments>` to
`mocks/api-<segments-with-dashes>.json`.  Examples:

| HTTP path                | Mock file                               |
|--------------------------|-----------------------------------------|
| `/api/sample/aoa`        | `mocks/api-sample-aoa.json`             |
| `/api/sample/flaps-raw`  | `mocks/api-sample-flaps-raw.json`       |
| `/api/audiotest/status`  | `mocks/api-audiotest-status.json`       |
| `/api/logs`              | `mocks/api-logs.json`                   |
| `/api/logs/delete-bulk`  | `mocks/api-logs-delete-bulk.json`       |
| `/api/format/status`     | `mocks/api-format-status.json`          |
| `/api/calwiz/state`      | `mocks/api-calwiz-state.json`           |
| `/api/version`           | `mocks/api-version.json`                |

The dev server returns the same JSON for both GET and POST against
the same path; for trigger-style POSTs (audiotest, vnochime, reboot)
the fixture is `{ok: true}`.

The mock files are validated by `node tools/web/test/api-schema.mjs`,
which pins the documented JSON shape for every endpoint.

## Page → component map

| Route       | Component        | What it does                                                                  |
|-------------|------------------|-------------------------------------------------------------------------------|
| `/indexer`  | `IndexerPage`    | 5-mode SVG view (AOA / Attitude / Indexer / Energy / G-history)               |
| `/live`     | `LivePage`       | Two-tab AOA / AHRS read-only page; preserves the legacy `/live` info density  |

The page list is the source of truth in three places that must stay
in sync:

- `tools/web/lib/entry.js` — the `PAGES` map.
- `scripts/build_web_bundle.py` — the `PAGES` table the bundler iterates over.
- `tools/web/dev-server/server.mjs` — the `PAGES` array the dev server matches against.

## Adding a new page

1. Write `lib/pages/MyPage.js`, exporting a Preact component whose
   render output is wrapped in `<PageShell active="my">`.
2. Register it in `lib/entry.js`'s `PAGES` map.
3. Add an entry to `PAGES` in `scripts/build_web_bundle.py` so a stub
   gets emitted in `html_stubs.h`.
4. Add a `CfgServer.on("/my", HTTP_GET, HandleMy)` route in
   `ConfigWebServer.cpp`, with `HandleMy()` calling
   `ServePageStub(htmlStub_my)`.
5. Add the route to `dev-server/server.mjs`'s `PAGES` array.
6. Optionally extend `lib/shell/nav.js` so the nav surfaces it.

## Capturing real WebSocket data

```bash
node tools/web/dev-server/capture.mjs ws://192.168.0.1:81 \
  > tools/web/dev-server/replay/my-flight.ndjson
```

Connect for ~30 seconds, Ctrl-C to flush.  Each frame becomes one
NDJSON line:
`{"tDelay": <ms-since-previous-frame>, "frame": <parsed-JSON>}`.  Use
`--replay tools/web/dev-server/replay/my-flight.ndjson` to play it
back through the dev server.

## Firmware bundle

`scripts/build_web_bundle.py` walks `lib/`, concatenates all JS files
in topological order with `import`/`export` keywords stripped, and
emits three PROGMEM headers:

- `static_app_js.h` — gzipped JS bundle, content-hashed `etag` (12 hex chars).
- `static_app_css.h` — gzipped CSS bundle, separate etag.
- `html_stubs.h` — one `htmlStub_<page>` per page, each a complete
  HTML document referencing `/static/app-<etag>.{js,css}`.

ConfigWebServer routes:

- `GET /indexer`, `GET /live` → `ServePageStub(htmlStub_…)`
- `GET /static/app-*.js`  → `HandleStaticAppJs`  (immutable cache)
- `GET /static/app-*.css` → `HandleStaticAppCss` (immutable cache)

The Preact bundle is special-cased in the bundler: wrapped in an IIFE
that returns its named exports as an object, so Preact's single-letter
internals (e, n, _, t, h, ...) don't collide with our top-level
scope.

### Editing source

After changing anything under `tools/web/lib/`,
`tools/web/lib/shell/PageShell.css`, or
`scripts/build_web_bundle.py`, regenerate and commit BOTH the source
and the headers:

```bash
python3 scripts/build_web_bundle.py
git add tools/web/... \
        software/OnSpeed-Gen3-ESP32/Web/static_app_js.h \
        software/OnSpeed-Gen3-ESP32/Web/static_app_css.h \
        software/OnSpeed-Gen3-ESP32/Web/html_stubs.h \
        software/OnSpeed-Gen3-ESP32/Web/html_indexer.h
git commit
```

CI's `web-bundle-fresh` job verifies the committed headers match what
the script would produce — a forgotten regeneration fails the PR check.

PlatformIO users: `pio run` regenerates the headers on demand (the
script is registered as a pre-build extra_script with skip-if-fresh).

### Don't edit the generated headers

`static_app_js.h`, `static_app_css.h`, and `html_stubs.h` start with
an `// AUTO-GENERATED` comment.  If you find yourself editing them,
stop — your changes will be erased on the next build.  Edit the
source under `lib/` and regenerate.

## Tests

```bash
node tools/web/test/geometry-invariants.mjs
node tools/web/test/render-smoke.mjs
```

Both run on Node 20 with no dependencies.  CI runs them on every PR.

## Source plan

The full multi-PR plan that this directory is implementing is at
`local-plans/PLAN_WEB_PREACT_REWRITE.md` (outside the repo).  Phase 1
covers the infrastructure + a `/live` port; later phases extend the
JSON API, port `/aoaconfig` and `/calwiz`, and clean up the legacy
inline-HTML pages.
