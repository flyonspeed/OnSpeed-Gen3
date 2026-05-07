# OnSpeed Display (M5Stack + huVVer-AVI + native sim)

Secondary display firmware that consumes OnSpeed's `#1` display-serial
frames. The directory name `OnSpeed-M5-Display/` is historical — this
codebase now builds for **three production targets and one sim**:

| Target | Build | Hardware |
|---|---|---|
| M5Stack Basic | `pio run -e m5stack-core-esp32` | ESP32 + 320×240 ILI9342C |
| M5Stack Core2 | `pio run -e m5stack-core2` | ESP32 + 320×240 ILI9342C + capacitive touch |
| huVVer-AVI | `pio run -e huvver-avi` | ESP32 + 240×320 ST7789, panel-mount avionics instrument |
| Native sim (SDL2) | `pio run -e native` | macOS / Linux desktop, 320×240 SDL window |

All four share the same renderer source. Per-target wiring is in
`sim/ArduinoShim.h` (native) and `sim/HuvverShim.h` (huVVer);
`#ifdef ESP_PLATFORM` gates Arduino-only code.

Renders five modes (numbered 0–4; numbers are stable across renames
because the X-Plane plugin's `indexerMode` per-aircraft pref stores
the integer):

- **Energy Display** (0, default on a fresh M5) — AOA indexer + surrounding IAS/G/flap/slip readouts. Last-used mode persists in NVS, so subsequent boots come up on whichever mode the pilot last selected.
- **Attitude** (1) — Synthetic horizon with flight-path marker.
- **Indexer** (2) — Same indexer widget, numeric readouts stripped (AOA-only page).
- **Decel Display** (3) — IAS-derivative gauge in kt/s.
- **Historic G** (4) — 60-second scrolling vertical-G trace.

Names follow Vac's canonical terminology (VAF threads 228078,
225345). The canonical UI list lives in
`tools/web/lib/pages/IndexerPage.js`; `kModeNames[5]` in `src/main.cpp`
mirrors it.

End-user docs: `docs/site/docs/installation/external-display.md`.
Attribution / licensing: `CREDITS.md` in this directory.

- `src/main.cpp` — entry point, mode dispatch, per-mode render functions
- `src/SerialRead.cpp` — `#1` protocol parser, 20 Hz frame dt measurement
- `lib/GaugeWidgets/` — V.R. Little's gauge-drawing primitives, vendored.
  Licensed under V.R. Little's non-commercial license (NOT MIT — see CREDITS.md).
- `sim/ArduinoShim.h` — host-side stubs for the SDL2 native sim env.
- `sim/HuvverShim.h` — huVVer-AVI compat layer: custom LGFX_Device
  for the ST7789 panel + GPIO buttons + DAC mute, mimicking the
  M5Unified `M5_t` surface so renderer code is target-agnostic.
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
gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth("PALT"), 60);
gdraw.print("PALT");
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
77-byte ASCII frames on `Serial2` (GPIO 16/17 on Basic, GPIO 13/14 on
Core2), 115200 8N1 (v4.23 wire; v4.22 was 76 bytes). See
`kDisplaySerialPeriodMs` in the main firmware's `HardwareMap.h` for
the canonical rate constant. Wire format details:
`docs/site/docs/reference/serial-protocol.md`.

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

## Flashing the huVVer-AVI

The huVVer-AVI talks to the host through a CH340 USB-serial bridge (no
native USB on the ESP32-classic it uses). On macOS the device shows up
as `/dev/cu.usbserial-<10-char-serial>`; Linux gives it
`/dev/ttyUSB0` (or higher if other CH340 devices are present).

**First-light recipe** — bench test with no wire input attached:

```bash
PLATFORMIO_BUILD_FLAGS='-DDUMMY_SERIAL_DATA' \
  pio run -e huvver-avi -t upload \
  --upload-port /dev/cu.usbserial-<your-serial> \
  --project-dir software/OnSpeed-M5-Display
```

After the auto-reset the panel shows the "Fly OnSpeed" splash, then a
synthetic AOA ramp sweeping 0→99→0 every 30 seconds with all five
display modes accessible via the round button.

**Real-data recipe** — once wired to an OnSpeed-Gen3 producing `#1`
display-serial frames on Connector A pin 2 (RX1, IO21):

```bash
pio run -e huvver-avi -t upload \
  --upload-port /dev/cu.usbserial-<your-serial> \
  --project-dir software/OnSpeed-M5-Display
```

### Upload speed

PlatformIO's default of 460800 baud is reliable through the CH340
bridge. Bumping `upload_speed` to 921600 fails with `Invalid head of
packet (0xA6): Possible serial noise or corruption.` after the chip
ID handshake — the CH340 datasheet supports the higher rate but the
combination with a long USB cable / unpowered hub does not.

### NVS reset after replacing other huVVer firmware

If the huVVer was previously running V.R. Little's stock firmware (or
the TBX transponder-controller App), residual NVS state from that
firmware can produce odd behavior on first boot of the OnSpeed
display. Clear it once: power-cycle while holding the **Menu (square)
+ Select (round) buttons together**. The boot menu reads this combo
as "restore system settings to factory defaults" — fine because the
OnSpeed display firmware doesn't persist any settings to NVS today.

### Buttons

Pin map (per HuvverShim.h, matching V.R. Little's `my_custom_setup.h`):

| Role | Physical | GPIO | M5 alias |
|---|---|---|---|
| Menu (dim) | square, top-left | IO39 | `BtnA` |
| Select (mode cycle) | round, top-right | IO36 | `BtnB` |
| Forward (bright) | right triangle | IO34 | `BtnC` |
| Back (unused) | left triangle | IO35 | `BtnD` |

The huVVer schematic mounts SW1–SW4 with internal-pull-up GPIOs, but
3 of the 4 (39, 36, 34, 35) are ESP32 input-only pins where
`INPUT_PULLUP` silently does nothing. The pins idle clean HIGH only
because the board carries external pull-up provisioning; rely on
that, not on `pinMode()`. Software debounce in `HuvverShim::poll()`
handles bounce; do not need to add debounce inside the call sites.

Strict-warning build flags: `-Wall -Wextra -Werror -Wshadow -Wformat=2
-Wunreachable-code -Wnull-dereference`. M5 project code and its includes
(M5Unified, M5GFX, WebServer, WiFi) are clean under strict `-Werror=shadow`
— stricter than the main Gen3 firmware, which has to downgrade shadow
because ghostl (pulled in via arduinoWebSockets) and Arduino Network
headers shadow members we can't fix upstream.
`-Wno-error=format-nonliteral` is downgraded because the Arduino
framework's `WebServer` and `WiFi` headers trip it (included by
`main.cpp` directly — M5Unified doesn't bundle a WebServer).
**Do not add new `-Wno-error=` flags without fixing the underlying
issue first.**

## Bench testing

Use `tools/m5-replay/replay.py` at the repo root to stream SD-card CSV
logs or scripted synthetic scenarios to the M5 via a USB-to-TTL dongle.
No OnSpeed box required.
