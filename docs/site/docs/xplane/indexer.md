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

- **Click the MODE button** at the bottom-center of the indexer
  window.  Each click steps the mode forward; mode 4 wraps back to
  mode 0.  The label updates to "MODE 0", "MODE 1", etc.  Clicks on
  the indexer body itself, the titlebar, or anywhere outside the
  button are pass-through — they don't cycle the mode and don't
  steal focus from whatever you were doing.
- **Plugins → Fly On Speed → Indexer Mode N** jumps directly to mode
  *N*. The window auto-shows if it isn't already visible.

The mode index is the same `displayType` the M5 firmware uses; the
plugin's `SetMode()` writes the M5's global state directly so a
plugin-side mode change behaves identically to a Button-B press on a
physical M5.

## Window appearance and behavior

### Letterboxing

The indexer texture is 320×240 pixels (the M5Stack's native LCD
resolution).  When you drag the X-Plane window to a different shape,
the texture stays at 4:3 aspect — it's centered with letterbox bars
filling whatever extra space you've given it.  Drag the window
proportionally bigger and the texture scales together; drag wide
or tall and the un-needed area shows the chrome bg.

This avoids the "stretched smooshed indexer" you'd otherwise get
from non-4:3 windows.

### Sticky position across launches

The indexer window remembers, per-aircraft, whether you had it
visible, what mode it was on, where it was on the screen, what
size, and whether it was in pop-out mode (own OS window, draggable
to other monitors).  All of that comes back the next time you load
the same aircraft.

The restore happens during X-Plane's "Preparing world" phase —
same timing as stock G1000 / X1000 windows snapping into their
saved spots.  Position-on-disk lives in
`Output/preferences/AOA-Tone-FlyOnSpeed-<aircraft>.prf` alongside
the audio settings.  See [Per-Aircraft Settings](settings.md) for
the file's full schema.

### Pop-out mode (multi-monitor)

The pop-out button on the X-Plane window's chrome (top-right)
detaches the indexer into its own OS window — at which point you
can drag it to a different monitor, snap it to a corner, etc.,
exactly like any other OS window.

The pop-out position persists separately from the floating
position, so:

- Drag the floating indexer to spot A, pop out, drag to monitor 2,
  un-pop → indexer returns to spot A.
- Pop back out → returns to monitor 2 spot.
- Quit X-Plane and relaunch in either mode → restores wherever
  you last had it.

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

#### G-onset tape (right-edge yellow bar)

The yellow bar on the right edge is the **rate of change of vertical
G** — how aggressively you're loading or unloading the airframe right
now. Bar grows up from the center for positive onset (G load
increasing — pulling into a turn or pull-up), grows down for negative
onset (G unloading — pushing over, releasing back-pressure).
Saturates at ±2 g/s. The center pip and ladder ticks give you a
visual reference. The bar is computed by feeding the smoothed
vertical-G signal through a 250 ms low-pass derivative — same
filter the firmware feeds the wire from `AHRS::Update`.

Useful as an "am I loading the airframe smoothly?" cue: a steady
long pull-up shows a small bar; a sudden yank shows a tall bar
that dies back as the rate stabilizes.

### Mode 1 — Attitude

Backup artificial horizon driven by X-Plane's pitch and roll. Pitch
ladder, magenta flight-path marker (concentric rings + perpendicular
wing bars), slip/skid ball at the bottom, white VSI tape on the
right edge, IAS / pressure-altitude / vertical-G / percent-lift
readouts in the corners.

#### VSI tape (right-edge white bar)

The white bar on the right edge is **vertical speed** — how fast
you're climbing or descending. Bar grows up from the center for
climbs, down for descents. Saturates at ±600 fpm: a steady 300 fpm
descent fills half the bar; a 700 fpm climb pegs it to the top.
The ±600 fpm range is tuned for the gentle-cruise band where small
trends matter most — for larger excursions read the IAS / FPA
numerics in the corners. The center pip and 20-pixel ladder ticks
give a visual reference; the bar drops to the ladder color (zero)
in level flight.

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
