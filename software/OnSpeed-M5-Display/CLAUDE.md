# OnSpeed M5Stack Display

Secondary display firmware for an M5Stack device receiving OnSpeed's `#1`
display-serial frames. Renders five modes (internally numbered 0–4):

- **Primary** (0, default) — AOA indexer + surrounding IAS/G/flap/slip readouts.
- **Attitude** (1) — Backup AI with flight-path marker.
- **Indexer-only** (2) — Same indexer widget, numeric readouts stripped.
- **Energy** (3) — Deceleration gauge in kt/s.
- **G history** (4) — 60-second scrolling G trace.

End-user docs: `docs/site/docs/installation/external-display.md`.

- `src/main.cpp` — entry point, mode dispatch, per-mode render functions
- `src/SerialRead.cpp` — `#1` protocol parser, 20 Hz frame dt measurement
- `lib/GaugeWidgets/` — vendored gauge-drawing primitives (V.R. Little, MIT-ish)
- `lib/onspeed_core/` — vendored copy of `SavGolDerivative.h` for IAS decel

## Library stack

This project uses **M5Unified + M5GFX** (not the deprecated per-board `M5Stack`
library). M5Unified autodetects board at `M5.begin()` and supports Basic,
Core2, CoreS3, Fire, etc. through a single codebase. That's what lets us
target `[env:m5stack-core2]` with the same source once the Core2 port
lands.

`M5Canvas gdraw(&M5.Display);` is the primary 8-bit off-screen sprite.
Every render fills `gdraw`, then `pushSprite(0, 0)` blits it to the panel.

## Text layout conventions (READ BEFORE EDITING DISPLAY CODE)

Porting Gen2's rendering code from the old TFT_eSPI fork to M5GFX broke
several layouts because the two libraries interpret text y-coordinates
differently for different drawing paths. **Follow these rules to keep new
code consistent and avoid re-chasing pixel bugs.**

### Rule 1: The frame default datum is `baseline_left`

Set once per frame at the top of the render loop (`loop()` in `main.cpp`).
Every `setCursor + print/printf` call in the render inherits it.

```cpp
gdraw.setTextDatum(textdatum_t::baseline_left);
```

With `baseline_left`, M5GFX's `print()` path behaves like the old
Adafruit-GFX convention: the `(x, y)` passed to `setCursor` anchors the
glyph's baseline at `y`, with the left edge at `x`. This matches what the
Gen2 code was written against, so existing `setCursor(x, y) + print(...)`
calls produce the layout the original author intended.

### Rule 2: Never mix `drawString` and `setCursor+print` for aligned text

Even at the same nominal datum, M5GFX's two text paths produce slightly
different visual y positions (a few pixels) because of how `y_offset` is
applied differently in each. If two corners of the same row need to
align, they must use the same path.

**Preferred**: use `setCursor + print` for all on-screen text. Compute
right-anchored x via `textWidth()` rather than switching to a `*_right`
datum:

```cpp
constexpr int RIGHT_X = 303;   // pulled 10px inside right edge

// Right-anchored label using the same print() path as left-side text
gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth("ALT"), 60);
gdraw.print("ALT");
```

### Rule 3: Right-side text pulls in from the panel edge

The VSI tick ladder on Modes 1/3 draws to x=319. Right-anchored text at
`RIGHT_X = 303` leaves ~10 pixels of margin so characters don't touch
the ticks. Tune per-page (some pages don't have edge ticks — those can
use `RIGHT_X = 314` for closer-to-edge alignment).

### Rule 4: For centered text, wrap the datum change in a scope

When you legitimately need `middle_*`/`top_*`/`top_center` (e.g., a
centered pitch readout, the G-load header, firmware-update splash
screens), change the datum, draw, then immediately restore
`baseline_left`:

```cpp
gdraw.setTextDatum(textdatum_t::middle_right);
gdraw.drawString(PitchStr, 100, 138);
gdraw.setTextDatum(textdatum_t::baseline_left);   // always restore
```

The restore is load-bearing. Without it, the next `setCursor + print`
inherits the wrong datum and the text lands in the wrong place.

### Rule 5: Layout constants per page, grouped by corner

Each display mode defines its own layout constants at the top of its
block:

```cpp
constexpr int RIGHT_X = 303;
constexpr int LABEL_Y = 90;
constexpr int NUM_Y   = 130;
```

Then draw each corner's label and number together in source, matching the
screen's physical layout. Moving a whole column left is a one-line edit.

### Typical font metrics used by this firmware

Aspirational values only — measure with `gdraw.fontHeight()` if precision
matters. These are what the Gen2 code was tuned against:

- `FSS12` (FreeSans 12pt): ascent ~14 px
- `FSS18` / `FSSB18` (FreeSans[Bold] 18pt): ascent ~21 px

## Protocol

The M5 consumes the OnSpeed `#1` serial protocol at 20 Hz (50 ms):
80-byte ASCII frames on `Serial2` (GPIO 16/17 on Basic, GPIO 13/14 on
Core2), 115200 8N1. See `DISPLAY_SERIAL_PERIOD_MS` in the main
firmware's `Globals.h` for the canonical rate constant.

`SerialRead.cpp` measures actual frame dt from `micros()` and divides
the Savitzky-Golay IAS derivative by that dt. Do not hardcode a rate
here — the main firmware can change its send rate and this code should
remain correct without edits.

## Build

```bash
# M5Stack Basic
pio run -e m5stack-core-esp32 -t upload

# Dummy data mode for bench-testing with no serial input
PLATFORMIO_BUILD_FLAGS='-DDUMMY_SERIAL_DATA' pio run -e m5stack-core-esp32 -t upload
```

Strict-warning build flags match the main firmware (`-Wall -Wextra
-Werror -Wshadow -Wformat=2 -Wunreachable-code -Wnull-dereference`).
`-Wno-error=format-nonliteral` is downgraded because M5Unified's
`WebServer` printf wrappers trip it. **Do not add new `-Wno-error=`
flags without fixing the underlying issue first.**

## Bench testing

Use `tools/m5-replay/replay.py` at the repo root to stream SD-card CSV
logs or scripted synthetic scenarios to the M5 via a USB-to-TTL dongle.
No OnSpeed box required.
