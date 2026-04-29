# Vendored dependencies

OnSpeed's LiveView (`/indexer`) renders its SVG modes using Preact + htm,
served from this vendored bundle. The bundle is a verbatim copy of the
`htm/preact/standalone` package and is committed so the firmware build
has no Internet dependency.

## Files

| File | Source | Size | License |
|---|---|---:|---|
| `preact-standalone.js` | https://unpkg.com/htm/preact/standalone.module.js | ~14 KB | MIT |

## What it provides

- **Preact** (~3 KB): a 3 KB React-API-compatible virtual-DOM library.
  https://github.com/preactjs/preact
- **htm**: a tagged-template-literal bridge to Preact's `h()` so we
  don't need a JSX compile step. https://github.com/developit/htm

We use `useState`, `useEffect`, `useRef`, and the `html` template tag.
None of the React DOM, Suspense, hooks beyond the basics, etc.

## Why vendor instead of CDN

The firmware bundle (`html_indexer.h`) is served from PROGMEM by the
ESP32 over the captive-portal AP. Pilots' tablets are connected to the
OnSpeed AP, NOT the public Internet. A CDN-loaded Preact would 404.

## Refresh procedure

```bash
curl -sL https://unpkg.com/htm/preact/standalone.module.js \
  -o tools/liveview-prototype/lib-preact/vendor/preact-standalone.js

# Re-add the leading vendor comment block (preserves dep + license note).
# Then verify:
python3 scripts/build_liveview_html.py
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
`lib-preact/modes/` and `lib-preact/components/` are the only files
that depend on `html` and `render`. Replace those imports + adjust
the JSX-equivalent template syntax; everything else (geometry, colors,
math helpers, scenarios, WebSocket client) is framework-free.
