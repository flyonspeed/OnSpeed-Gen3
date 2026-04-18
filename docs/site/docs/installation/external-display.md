# Optional: External Display

OnSpeed can drive an external display via serial output, giving you a
cockpit-mounted visual readout of AOA, airspeed, attitude, flight path,
deceleration, and G-load. The supported option is an **M5Stack Basic**,
which runs the open-source [`OnSpeed-M5-Display`](https://github.com/flyonspeed/OnSpeed-Gen3/tree/master/software/OnSpeed-M5-Display)
firmware.

!!! note "Display is supplemental"
    The external display supplements OnSpeed's audio tones — it does not
    replace them. The primary interface is still aural; your eyes should
    stay outside during approach. The display is most useful for
    post-flight analysis (G-load history), initial familiarization with
    the tones, and as a backup attitude indicator.

## M5Stack Basic display

The M5Stack Basic is a compact self-contained ESP32 development unit
with a 320×240 color TFT, three front buttons, and a USB-C charging
port. It receives OnSpeed's serial stream, parses it, and renders any
of five display modes.

### Buttons

| Button | Action |
|--------|--------|
| **Button A** (left) | Brightness down |
| **Button B** (middle) | Cycle display mode (0 → 4, then wraps back to 0) |
| **Button C** (right) | Brightness up |

If you hold **Button B during boot**, the M5 enters WiFi OTA update
mode instead of running the display — for updating firmware without
needing a USB cable.

### Display modes

The five modes are selected by pressing Button B. The M5 remembers the
last-used mode across power cycles.

#### Mode 0 — Primary AOA indexer

!!! info "Screenshot pending"
    <!-- TODO: add assets/images/m5-mode-0-indexer.png and replace this admonition with the image -->

The main flight display. A vertical AOA scale fills the center of the
screen with numeric IAS (top-left), pressure altitude (top-right),
vertical G (bottom-left), and percent lift (bottom-right) around the
edges.

**How to read the indexer:**

- **White marker bar** — current AOA position on the vertical scale.
  Fixed white dots on the left and right edges mark the **LDmax** (best
  climb) reference.
- **Bottom (down-pointing) chevrons** turn **light blue** when AOA is
  between LDmax and OnSpeedFast — "you're a touch fast, lower the nose."
- **Green donut / arcs** light up when you're in the OnSpeed range
  (between OnSpeedFast and OnSpeedSlow):
    - Bottom arc green → lower half of the range
    - Center dot green → right in the middle — on speed
    - Top arc green → upper half of the range
- **Top (up-pointing) chevrons** turn **yellow** when AOA crosses
  OnSpeedSlow (getting slow), **red** past the midpoint to StallWarn,
  and **flashing red** above StallWarn (stall imminent).

The layout is symmetric top-to-bottom, so the visual cue is always:
*opposite-color chevrons mean "push in that direction."* Bottom blue
→ push nose down. Top yellow/red → push nose down even more firmly.
Center green → hold it.

#### Mode 1 — Attitude indicator with AOA

!!! info "Screenshot pending"
    <!-- TODO: add assets/images/m5-mode-1-attitude.png -->

Backup AI driven from OnSpeed's AHRS. Sky/ground horizon with pitch
ladder, roll indication, slip ball, and VSI tape on the right edge.
Numeric fields around the edges: IAS, pressure altitude, vertical G,
AOA percent-lift, and pitch angle in the center. A magenta flight-path
circle marks the aircraft's actual vector through the air.

**When to use:** cross-check against the primary AI (not as a
replacement), or in low-visibility transitions where you want the
OnSpeed-derived flight path marker.

#### Mode 2 — Narrow AOA indexer

!!! info "Screenshot pending"
    <!-- TODO: add assets/images/m5-mode-2-narrow.png -->

Same AOA scale and chevrons as Mode 0, but without the surrounding
numeric displays. This is the panel-friendly layout — meant for a tall
narrow cutout beside the ASI, where you want the indexer image but
don't need the numbers (your primary display already shows IAS, G,
altitude).

#### Mode 3 — Deceleration gauge

!!! info "Screenshot pending"
    <!-- TODO: add assets/images/m5-mode-3-decel.png -->

Round analog-style gauge with a needle showing deceleration in **knots
per second**, plus the same indexer widget. Positive (accelerating)
readings sit on one side; negative (decelerating, e.g. during flare)
sit on the other. Smoothed to remove jitter.

**When to use:** tuning approach energy. A stabilized approach should
show small steady decel; rapid swings mean you're pumping the stick.

#### Mode 4 — G-load history

!!! info "Screenshot pending"
    <!-- TODO: add assets/images/m5-mode-4-g-history.png -->

Scrolling graph of vertical G over approximately the last 60 seconds,
with horizontal reference lines at 1G, 2G, etc. Shows cumulative
G-loading at a glance.

**When to use:** post-aerobatic or post-pattern debrief — see the full
profile rather than just the peak G on the main display.

### Status indicators across all modes

| Condition | How it appears |
|-----------|---------------|
| **No data from OnSpeed for >300 ms** | Red diagonal X across the screen with "NO DATA" text in the center |
| **Stall warning** (AOA > StallWarn) | Top chevrons flash red |
| **Large slip/skid at high AOA** | Slip ball flashes red |
| **Low brightness** | Press Button C to brighten |

## Hardware setup

### Wiring

The M5 talks to OnSpeed over a one-way serial link. Only three
connections are needed.

| OnSpeed pin              | Direction | M5 pin                 |
|--------------------------|-----------|------------------------|
| GPIO 10 (DISPLAY_SER_TX) | →         | M5 Port C RX (GPIO 16) |
| GND                      | ↔         | M5 GND                 |

The OnSpeed serial output can be TTL or RS-232 level. The M5 firmware
auto-detects on boot by trying three common port configurations (TTL
non-inverted, RS-232 inverted, and a bench-simulator TTL variant).
Once detected, the port mapping is saved to the M5's NVS flash so
subsequent boots come up faster.

- **Baud rate:** 115200 8N1
- **Frame rate:** 20 Hz (every 50 ms). Each frame is 80 ASCII bytes
  including `#1` header, fields, CRC, and CRLF.

### Powering the M5

The M5 can be powered from:

- **Its own USB-C port** — ideal for bench testing or a permanent
  panel installation if you have avionics USB power (1 A at 5 V).
- **Panel 5 V supply** wired into the M5 base's VCC pin on Port C.
  Check the M5Stack documentation for current ratings before wiring
  to ship power; ~120 mA typical at full brightness.

Do **not** try to power the M5 from OnSpeed's GPIO pins — they don't
supply enough current for the display's backlight.

## Flashing the M5 firmware

Prerequisites:

- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation.html)
  installed
- M5Stack Basic connected to your laptop via USB-C

Flash:

```bash
cd software/OnSpeed-M5-Display
pio run -e m5stack-core-esp32 -t upload
```

Tail the M5's own USB serial for debug output:

```bash
pio device monitor
```

## Bench testing without the OnSpeed box

You can test the M5 display on your desk before connecting it to a
running OnSpeed box. Use the Python replay tool under
[`tools/m5-replay/`](https://github.com/flyonspeed/OnSpeed-Gen3/tree/master/tools/m5-replay)
with an inexpensive USB-to-TTL dongle:

```bash
cd tools/m5-replay

# Real flight log
uv run replay.py --port /dev/cu.usbserial-XXXX \
    --input /path/to/OnSpeed_log.csv

# Deterministic synthetic demo (cruise → approach → stall → g-turns)
uv run replay.py --port /dev/cu.usbserial-XXXX --synthetic
```

See [`tools/m5-replay/README.md`](https://github.com/flyonspeed/OnSpeed-Gen3/blob/master/tools/m5-replay/README.md)
for the hardware shopping list, wiring diagram, and protocol details.

## Serial output format

The OnSpeed firmware's serial output format is configurable via the
`SERIALOUTFORMAT` field in the configuration:

- **ONSPEED** — native format (76-byte payload with AOA, setpoints,
  attitude, flap position, setpoints, G-loading, etc.). Use this with
  `OnSpeed-M5-Display`.
- **G3X** — Garmin G3X-compatible subset for use with Garmin panel
  displays.

## Mounting

Mount the display where it's visible during approach without blocking
your view of primary instruments. Common locations:

- Panel cutout (flush-mount, Mode 2 narrow layout is designed for this)
- Glareshield mount on a RAM-style ball joint
- Side console near the throttle quadrant
