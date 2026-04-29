# Future direction: shared firmware-wide JS bundle

This is a one-page design note for **future** work, not a deliverable
of this PR. The Preact `/indexer` rewrite that ships in this PR builds
the foundation; the architectural opportunity below comes into focus
once OTHER firmware pages (calibration wizard, AOA config, log
management, etc.) get rewritten in Preact too.

## What we have today

The firmware serves several HTML pages from PROGMEM:

| Route | Header file | Approx size |
|---|---|---:|
| `/live` | `html_liveview.h` | 26 KB (legacy 2-tab page) |
| `/indexer` | `html_indexer.h` | 97 KB (new Preact 5-mode) |
| `/calibration` | `html_calibration.h` + `javascript_calibration.h` | 24 KB |
| `/aoaconfig` | inline in `ConfigWebServer.cpp` | ~50 KB of inline JS |
| `/sensorconfig` | inline | ~10 KB |
| `/logs`, `/format`, `/upgrade`, ... | inline | small each |

Each page bundles its own JS, CSS, and HTML — no sharing. Page
navigation re-downloads. Cache is per-route via `SendWithETag`
(version-keyed by `BuildInfo::version`).

The `/indexer` Preact bundle has reusable building blocks the other
pages would benefit from once they're Preact-ified:
- WebSocket reconnect + age timer (any live-data page wants this)
- Decel gauge component (cal wizard has a parallel imperative one)
- Number formatting helpers (`fmt(v, digits)` with sign-collapse)
- Color tokens, geometry constants
- The `<App/>` shell pattern (header status line, mode buttons, footer)

## What we'd ship eventually

**One bundle, many pages.**

```
/static/app-<version>.js     ← 100-150 KB Preact + all components,
                                served Cache-Control: immutable
/live                         ← tiny HTML stub: <div id="app" data-page="live">
                                 + <script src="/static/app-...js">
/indexer                      ← stub w/ data-page="indexer"
/calibration                  ← stub w/ data-page="calibration"
/aoaconfig                    ← stub w/ data-page="aoaconfig"
```

The shared `app.js` switches on `data-page` to pick the right
top-level Preact component:

```js
const PAGES = {
  indexer:     IndexerApp,
  calibration: CalibrationWizard,
  aoaconfig:   AoaConfigForm,
  ...
};
const root = document.getElementById('app');
render(html`<${PAGES[root.dataset.page]} />`, root);
```

## Build pipeline

`scripts/build_liveview_html.py` extends to two outputs:

- `software/OnSpeed-Gen3-ESP32/Web/static/app_js.h` — full bundle
  as PROGMEM byte array.
- `software/OnSpeed-Gen3-ESP32/Web/static/app_js_etag.h` — content
  hash so the route handler emits an immutable Cache-Control header.

Per-page HTML stubs (still in `html_indexer.h` etc.) become tiny —
roughly 200 bytes each instead of 100 KB.

`ConfigWebServer.cpp` adds one route:

```cpp
CfgServer.on("/static/app.js", HTTP_GET, []() {
    CfgServer.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
    CfgServer.send_P(200, "application/javascript",
                     reinterpret_cast<PGM_P>(app_js), app_js_len);
});
```

The page stubs reference `/static/app.js?v=<sha>` so cache busting
is automatic on firmware OTA.

## Wins

- **Code sharing.** WebSocket logic, decel gauge, formatters,
  components — written once, used everywhere.
- **Cache once, navigate cheap.** First page visit downloads
  ~150 KB. Every subsequent page navigation is a 200-byte HTML stub.
  iPhone Safari over the captive portal AP loves this.
- **Single source of truth for shared widgets.** The cal wizard's
  decel gauge becomes `<DecelGauge range=${[-4, 2]} bands=${...} />`,
  same component as `/indexer`'s Mode 3.

## Costs

- **Bundle size grows.** 100 KB → 150-200 KB once cal wizard +
  aoaconfig fold in. Still well under PROGMEM budget but bigger
  than today's per-page bundles individually.
- **Preact-ification of the other pages.** Significant work.
  Calibration wizard alone (`html_calibration.h` + 465 lines of
  JS in `javascript_calibration.h`) is several hundred lines to
  port. AOA config with its huge form is bigger still.
- **Build complexity.** Bundler emits two artifacts, route handler
  for `/static/app.js`, content-hash injection.

## When to do it

**Trigger condition: when the SECOND firmware page goes Preact.**

If only `/indexer` is Preact, sharing infrastructure is pure
overhead. The moment the cal wizard (or any other page) becomes a
Preact component, the shared-bundle architecture pays for itself
immediately because the cal wizard imports `<DecelGauge>` from
the existing components.

## Recommended sequence

1. **PR #354 (this one)**: ship `/indexer` Preact, gather Vac's
   flight A/B data on hardware. Get the new view validated.
2. **Follow-up PR**: Preact-ify the calibration wizard. Two-step:
   - Step A: in-place rewrite of `javascript_calibration.h` to use
     `<DecelGauge>` from `lib/components.js`. Still its own page,
     still its own bundle.
   - Step B: extract the shared bundle. `/indexer` and
     `/calibration` both reference `/static/app.js`. Per-page
     bundles shrink to stubs.
3. **Subsequent PRs**: AOA config, sensor config, log management
   — each one folds into the shared bundle as it gets Preact-ified.

By PR #3 or #4, the shared-bundle architecture is the default and
adding a new firmware page is "write a top-level component, register
it under PAGES, add a tiny stub route." That's the desired end-state.

## Why not now

- Only `/indexer` is Preact. Sharing requires a second consumer.
- The cal wizard rewrite is its own meaningful body of work.
- Doing it speculatively would block PR #354's hardware test.
