# Using the Indexer

The indexer is a floating X-Plane window that runs the OnSpeed M5
indexer firmware in-process and draws its framebuffer into a
GL-textured quad. Same renderer as a real M5Stack — same chevrons,
same donut, same percent-lift index bar, same fonts.

## Show / hide

Two paths:

- **Plugins → Fly On Speed → Indexer: Show/Hide** toggles the window.
- The window honors X-Plane's standard floating-window chrome —
  drag by the titlebar to move, use the close button to dismiss.

The first time you show the indexer in a session, the plugin
lazy-initializes SDL, the M5 firmware's `setup()`, the OpenGL
texture, and the panel framebuffer. Expect a fraction of a second
of latency on first show; subsequent shows are immediate.

## Cycling modes

Two ways to change which of the five modes is on screen:

- **Click anywhere inside the indexer body.** Each click steps the
  mode forward; mode 4 wraps back to mode 0. The titlebar is reserved
  for drag, so clicks there don't cycle.
- **Plugins → Fly On Speed → Indexer Mode N** jumps directly to mode
  *N*. The window auto-shows if it isn't already visible.

The mode index is the same `displayType` the M5 firmware uses; the
plugin's `SetMode()` writes the M5's global state directly so a
plugin-side mode change behaves identically to a Button-B press on a
physical M5.

## The five modes

The same five modes the M5 firmware ships. For full descriptions of
each mode (what every readout means, when to use it), see the
[external display modes section](../installation/external-display.md#display-modes)
— the plugin runs the same C++ source.

### Mode 0 — AOA + Numbers (Primary)

The default after first show. Vertical AOA indexer in the center,
flanked by IAS, vertical-G, flap-position icon, percent-lift number,
slip ball, and a G-onset rate tape on the right edge.

The indexer widget itself shows:

- **Top chevrons** — getting-slow warning, yellow → red → flashing red
  as AOA crosses OnSpeedSlow, midpoint, and StallWarn.
- **Green donut** (two horizontal arcs + center dot) — on-speed
  indicator, lit when AOA is in the OnSpeed band.
- **Bottom chevron** — fast-but-safe (between LDmax and OnSpeedFast).
- **White index bar** — current AOA on the percent-lift scale.

### Mode 1 — Attitude

Backup artificial horizon driven by X-Plane's pitch and roll. Pitch
ladder, magenta flight-path marker (concentric rings + perpendicular
wing bars), slip/skid ball at the bottom, orange VSI tape on the
right edge, IAS / pressure-altitude / vertical-G / percent-lift
readouts in the corners.

### Mode 2 — Narrow AOA

Mode 0's indexer widget with all the numeric fields stripped away.
Same chevrons, donut, and index bar; nothing else.

### Mode 3 — Decel

Vertical "energy tape" gauge showing instantaneous airspeed
deceleration in knots per second. Green band marks the stable-energy
zone; red bands above and below flag rapid acceleration or
deceleration. Useful for tuning approach energy.

### Mode 4 — G History

Rolling 1-minute strip-chart of vertical G (the on-screen header
reads `G-LOAD [1 min]`). Useful for spotting how hard you pulled in
a maneuver after the fact.

## What feeds the indexer

The plugin's `IndexerWindow::Tick()` callback runs every X-Plane
flight loop. It reads X-Plane datarefs, builds a display-serial
`#1` frame in the same wire format the firmware emits, and feeds
that frame into the M5's serial parser. The M5 renderer then runs
its loop normally, drawing into the panel's framebuffer.

The percent-lift anchors (LDmax, OnSpeedFast, OnSpeedSlow, StallWarn)
come from the four AOA setpoints in the audio control window —
edit them there to recalibrate the indexer's chevron and donut
gates. See [Per-Aircraft Settings](settings.md).
