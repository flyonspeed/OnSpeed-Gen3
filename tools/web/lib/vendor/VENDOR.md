# Vendored dependencies

OnSpeed's LiveView (`/indexer`) renders its SVG modes using Preact + htm,
served from this vendored bundle. The bundle is a verbatim copy of the
`htm/preact/standalone` package and is committed so the firmware build
has no Internet dependency.

## Files

| File | Source | Size | License |
|---|---|---:|---|
| `preact-standalone.js` | https://unpkg.com/htm/preact/standalone.module.js | ~14 KB | MIT |
| `chartist.js` | https://github.com/gionkunz/chartist-js v0.11.4 | ~40 KB | WTFPL or MIT |
| `chartist.css` | https://github.com/gionkunz/chartist-js v0.11.4 | ~11 KB | WTFPL or MIT |
| `regression.js` | https://github.com/Tom-Alexander/regression-js v2.0.1 | ~4 KB | MIT |
| `sgfilter.js` | https://github.com/mljs/savitzky-golay-generalized | ~4 KB | MIT |

The chartist, regression, and sgfilter sources are the same minified
bundles the firmware previously served at `/js/chartist.js`,
`/js/regression.js`, `/js/sgfilter.js` (PROGMEM blobs in
`software/OnSpeed-Gen3-ESP32/Web/javascript_chartist*.h`,
`javascript_regression.h`, `sg_filter.h`).  Pulled out of the C++
header strings into real `.js` files so the cal-wizard rewrite can
import them by path; the bundler concatenates them into the shared
`/static/app-<sha>.js` blob.

Chartist usage: only the `Chartist.Line` / `Chartist.AutoScaleAxis`
/ `Chartist.Interpolation.simple` API surface for the cal-wizard
review chart.  Regression usage: `regression.polynomial(data,
{order, precision})` for the IAS-to-AOA fit and CP-to-AOA fit.
Sgfilter usage: not yet — the cal-wizard rewrite drops the legacy
client-side smoothingAlpha; sgfilter.js stays vendored as a future
hook for the planned offline log-replay analyses.

## What it provides

- **Preact** (~3 KB): a 3 KB React-API-compatible virtual-DOM library.
  https://github.com/preactjs/preact
- **htm**: a tagged-template-literal bridge to Preact's `h()` so we
  don't need a JSX compile step. https://github.com/developit/htm

We use `useState`, `useEffect`, `useRef`, and the `html` template tag.
None of the React DOM, Suspense, hooks beyond the basics, etc.

## Why vendor instead of CDN

The firmware bundle (`static_app_js.h`) is served from PROGMEM by the
ESP32 over the captive-portal AP. Pilots' tablets are connected to the
OnSpeed AP, NOT the public Internet. A CDN-loaded Preact would 404.

## Refresh procedure

```bash
curl -sL https://unpkg.com/htm/preact/standalone.module.js \
  -o tools/web/lib/vendor/preact-standalone.js

# Re-add the leading vendor comment block (preserves dep + license note).
# Then verify:
python3 scripts/build_web_bundle.py
pio run -e esp32s3-v4p
```

## License

Both Preact and htm are MIT-licensed. Reproduced below for the
audit-able license trail.

```
The MIT License (MIT)

Copyright (c) Jason Miller (https://jasonformat.com/) and contributors

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## Why these specific packages

Other lightweight reactive frameworks were considered:

- **Solid.js**: requires JSX + a build step. We chose runtime-only.
- **Mithril**: ~8 KB, larger than Preact + htm combined.
- **Hyperapp**: ~3 KB. Maintenance signals less active than Preact.
- **Lit**: ~5 KB. Web-component idioms over functional components.
  Could work; Preact wins on familiarity for React-experienced
  contributors.
- **van.js**: ~1 KB. Reactive primitives only — too minimal for our
  five-mode UI.

If a future maintainer wants to swap the framework, the components in
`lib/modes.js`, `lib/components/svg/`, `lib/pages/`, and `lib/shell/`
are the files that depend on `html` and `render`. Replace those
imports + adjust the JSX-equivalent template syntax; everything else
(geometry, colors, math helpers, scenarios, WebSocket client) is
framework-free.
