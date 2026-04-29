# LiveView Prototype — Lessons Learned (Stage 1 → Stage 2+)

This memo captures what we figured out building Mode 0 (Primary AOA + corner
readouts) so Stages 2–5 don't re-litigate the same questions.  Anyone
implementing the next mode should read this **before** writing code.

## The widget pattern is non-negotiable

Every visible element on screen lives in `lib/widgets/X.js` as a
`mountX(parent, config) → { el, update(record) }` function.  Widgets are
self-contained: they own the SVG elements they create, and `update()`
sets attributes on those elements per frame.  Modes (`lib/modes/Y.js`)
compose widgets — they never inline `<rect>` or `<text>` directly.

Existing widgets ready to reuse for Stages 2–5:

| Widget | What it draws | Used by |
| --- | --- | --- |
| `widgets/indexer.js` | Bounding rect + 4 chevron halves + 3-segment donut + index bar + L/Dmax pips | Mode 0, Mode 2 |
| `widgets/percentLiftNumber.js` | Big two-digit number with black halo | Mode 0, Mode 2 |
| `widgets/cornerReadout.js` | Label + bold number pair (configurable color, anchor) | Mode 0, Mode 1, Mode 3 |
| `widgets/flapCircle.js` | Gray circle + rotating triangle + stop-mark dots + numeric flap angle | Mode 0 |
| `widgets/slipBall.js` | Tick frame + ball with stall-flash logic | Mode 0, Mode 1, Mode 3 |
| `widgets/edgeTape.js` | Right-edge bar + tick ladder + zero pip | Mode 0 (G-onset), Mode 1 (VSI), Mode 3 (VSI) |

If a new mode needs a kind of widget that doesn't exist, add it to
`lib/widgets/`.  Don't draw shapes inline in a mode file; that's how the
codebase becomes hostile to refactor.

## Read C++ source AND GaugeWidgets first, every time

For every Mode 1+, before writing one line of SVG:

1. Read the M5 source case (`case 1:` for Attitude, `case 2:` for
   Indexer-only, `case 3:` for Energy, `case 4:` for G-history) at
   `software/OnSpeed-M5-Display/src/main.cpp` to find what's drawn.
2. Read the helpers it calls (`AiGraph()`, `pitchGraph()`,
   `displayDecelGauge()`, `displayGloadHistory()`).
3. Skim `software/OnSpeed-M5-Display/lib/GaugeWidgets/GaugeWidgets.{h,cpp}`
   to see if the M5 already has primitive helpers (`drawArc`,
   `drawTriangle`, `gradMarks…`) we should mirror in JS.
4. Cross-reference any layout constants in the M5 source against the
   nominal values cited in plan stubs.  When the plan and source
   disagree, prefer source — multiple plan stubs had off-by-one or
   mis-cited line numbers that were caught by careful reading.

## CSS variables, not hardcoded colors

Every fill / stroke goes through `colors.TFT_*` which maps to a CSS
variable.  Don't hardcode `#fff` or `#000` in widget code.  This makes
the eventual light-theme work (Stage 6) a CSS-only change.

The CSS variables that exist today (in `style.css` `:root` and
`[data-theme="light"]`):

```
--panel-bg   (TFT_BLACK)
--white      (TFT_WHITE — pure #fff, distinct from --ink which is body text)
--green      (TFT_GREEN — exact rgb(0,255,58) per user measurement)
--yellow     (TFT_YELLOW — rgb(255,253,64))
--red        (TFT_RED — rgb(255,0,24))
--grey       (TFT_GREY)
--dark-grey  (TFT_DARKGREY — rgb(107,109,84) per user measurement)
--light-grey (TFT_LIGHTGREY)
```

Stages 2–5 may need new tokens (`--sky` / `TFT_CYAN` for Mode 1's sky,
`--ground` / `TFT_BROWN` for Mode 1's ground, etc.).  Add them to BOTH
themes when introducing a new color.

## Font sizing: ~1.25× the M5 nominal

Browser sans-serif (Helvetica/Arial) has a cap height around 73% of
its `font-size`.  M5GFX's FreeSans bitmap renders larger glyphs at the
same nominal point size.  Empirical scaling factor we landed on:

| M5 font | M5 ascent (px) | CSS `font-size` |
| --- | ---: | ---: |
| FSS12  | ~14 | 23 (flap angle), 18 (other small labels) |
| FSS18  | ~21 | 33 (corner labels) |
| FSSB18 | ~21 | 33 (corner numbers), 36 (percent-lift number) |

Always use `dominant-baseline: alphabetic` for `setCursor`-style M5
calls, `dominant-baseline: central` for `middle_center` calls.  Keep
the same baseline-y values the C++ specifies — M5GFX renders text at
`y` exactly, and so does SVG with `alphabetic`.

## Numeric text update gate

Numeric readouts update at the M5's `updateRateNumbers = 500 ms`
cadence (main.cpp:156, gated at :491-503).  Bars, indexer position,
chevron / donut colors, slip-ball position, and animated tape bars
update every frame.  In `lib/modes/aoa.js` this is implemented as a
`numLastUpdateMs` timer that gates only the text `update()` calls.

Mode 1 (Attitude) follows the same convention: pitch ladder, FPV
marker, horizon — every frame.  IAS / PALT / G / AOA% numeric text —
500 ms gate.

## Paint order matters; SVG paints by document order

Two stable patterns:

1. The **percent-lift number** must paint on top of any element that
   crosses its bounding box (the index bar at high AOA).  The
   `mountPercentLiftNumber` widget exposes a `bringToFront()` method
   and the mode file calls it once per frame.  Belt-and-suspenders
   even though appending order should already place it last.

2. The **gOnset/VSI tape** paints bar FIRST, ticks LAST so the ticks
   stay visible across the bar — matches main.cpp:845-857 ordering.

When introducing a new mode, ask for each composed widget: should it
paint on top of, or under, the surrounding widgets?  If a widget needs
to be "always on top," design it with an explicit `bringToFront()`
escape hatch instead of relying on mount order.

## Animations: CSS transitions on SVG attributes

The gOnset bar uses `style="transition: y 100ms linear, height 100ms
linear;"` so 20 Hz data steps render as smooth motion.  Same pattern
will work for Mode 1's flight-path marker, Mode 3's decel needle, and
Mode 4's polyline — but be careful: transitions on `points` attribute
of `<polyline>` don't animate (browsers don't interpolate path data).
For polylines, just update the points each frame at 20 Hz; the eye
fills it in.

## The measure harness is your iteration tool

`tools/liveview-prototype/measure.html` shows the new SVG and the
wasm-live M5 sim side-by-side at exact 320×240 with a 10 px / 50 px
grid overlay.  Use it for every visual A/B.  The grid toggle button
lets you remove gridlines for color comparison.

When the M5 wasm-live disagrees with real hardware (one known case:
the slip ball appears below the visible canvas in wasm-live because
the shell adds 8 px body padding, but on real hardware the ball
touches the bottom edge), trust **real hardware**, not wasm-live.
The user has the real hardware in front of them; ask if you're not
sure which is the truth.

## Browser caching gotcha

Python `http.server` serves no cache headers.  When iterating, the
browser holds onto old ES module imports.  Always do **Cmd-Shift-R**
(hard reload) after firmware-side or scenario-side changes, otherwise
you'll be debugging stale code.  Module-level `import` won't pick up
file changes via cache-busting query params either.

## When the user says "X is wrong"

Verify against the M5 source and the wasm-live sim before changing
anything.  A few times in Stage 1 the user described a delta that
turned out to be (a) a miscount on the gridlines, (b) a wasm-live
bug rather than a our-SVG bug, or (c) a request to override the M5
behavior (e.g., flap circle background = chevron background, which
the C++ doesn't do).  In each case the right answer was different:

- Real bug → fix it.
- Wasm-live disagrees with real hardware → trust real hardware.
- User wants override → make the change but document it as an
  intentional divergence from C++ in a comment.

Don't chase pixel feedback without confirming the source delta.

## Files to know about

- `tools/liveview-prototype/lib/colors.js` — CSS variable mapping
- `tools/liveview-prototype/lib/geometry.js` — every layout constant
- `tools/liveview-prototype/lib/scenarios.js` — synthetic data drivers
- `tools/liveview-prototype/lib/main.js` — pump + mode dispatch
- `tools/liveview-prototype/lib/widgets/*.js` — reusable building blocks
- `tools/liveview-prototype/lib/modes/aoa.js` — Mode 0 composition (reference)
- `tools/liveview-prototype/measure.html` — A/B harness
- `tools/liveview-prototype/test/runner.html` — unit-test runner
