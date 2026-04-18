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

The five modes are selected by pressing **Button B**, which cycles in
order: **Primary → Attitude → Indexer-only → Energy → G history →**
back to Primary. The M5 remembers the last-used mode across power
cycles.

#### Primary — AOA indexer + full readouts

*(Mode 0 — the boot default.)*

!!! info "Screenshot pending"
    <!-- TODO: add assets/images/m5-mode-0-indexer.png -->

The main flight display. A vertical AOA indexer widget occupies the
center of the screen, with supporting readouts around the edges.

**Center — the indexer widget.** Reads from top to bottom and tells
you where your AOA sits relative to the stall/OnSpeed/LDmax setpoints
for the current flap configuration:

- **Top chevrons** (two stylized triangles forming an up-arrow near
  the top of the widget) — *getting slow* warning.
    - **Yellow** when AOA crosses OnSpeedSlow and approaches stall.
    - **Red** past the midpoint between OnSpeedSlow and StallWarn.
    - **Flashing red** above StallWarn — stall imminent.
- **Green donut** (two horizontal arcs + center dot in the middle
  of the widget) — *on-speed* indicator. All three light up when
  AOA is centered in the OnSpeed band.
    - Bottom arc green — lower half of the OnSpeed range.
    - Center dot green — centered (your target on approach).
    - Top arc green — upper half.
- **Bottom chevrons** (down-arrow near the bottom of the widget) —
  *getting fast* warning. **Light blue** when AOA is between LDmax
  and OnSpeedFast (you're a touch fast for an OnSpeed approach —
  pull).
- **White index bar** — a thin horizontal bar that slides up/down
  the widget showing your current AOA on the scale. Small white
  dots on the left and right edges of the widget mark the **LDmax**
  (best-climb / best-glide) reference.

Visual language: *opposite-colored chevrons mean "push toward them."*
Bottom blue → lower the nose. Top yellow/red → raise the nose /
pull. Center green donut → hold what you've got.

**Above the widget — percent lift** (0–99, or "99" when saturated).
A large white number with a black outline — readable against any
background. 50% = LDmax. 66% ≈ middle of the OnSpeed band. 90% = stall
warn.

**Corners:**

- Top-left: current **IAS** in knots (big white number) with a green
  "IAS" label.
- Top-right: current **vertical G** (big white number) with a green
  "G" label.
- Bottom-left: a **flap position** icon (circle with a rotating
  triangular needle representing the flap angle, plus tick marks
  for standard detents) with the numeric flap angle inside the
  circle.
- Bottom-center: the **slip/skid ball** (green when coordinated, red
  and flashing at high AOA with large slip).

**Right edge — G-onset rate tape.** A thin orange bar above or
below a "zero line" ladder at the right side of the screen, showing
instantaneous onset (rate of G change per second). Useful for
spotting sudden pull-ups / push-overs during maneuvering.

#### Attitude — backup AI with AOA

*(Mode 1.)*

!!! info "Screenshot pending"
    <!-- TODO: add assets/images/m5-mode-1-attitude.png -->

Full-width backup artificial horizon driven by OnSpeed's AHRS.
Sky/ground horizon with a pitch ladder (10° increments), roll
indication via a fixed aircraft reference symbol against a rolling
horizon, a magenta flight-path marker (concentric rings with
perpendicular wing bars) showing the aircraft's actual vector through
the air, slip/skid ball at the bottom, and an orange VSI tape on the
right edge with a tick ladder for climb/descent rate.

**Corners** follow the same layout as the Primary mode: IAS /
altitude on top row, G / AOA-percent-lift on bottom row. Numeric
pitch readout sits in a dark rounded rectangle behind the aircraft
symbol at the center.

**When to use:** cross-check against the primary AI (not as a
replacement), or during instrument transitions where you want the
OnSpeed-derived flight path marker overlaid on an independent
attitude source.

#### Indexer-only — panel-minimal

*(Mode 2.)*

!!! info "Screenshot pending"
    <!-- TODO: add assets/images/m5-mode-2-narrow.png -->

The Primary mode's indexer widget (chevrons + donut + index bar),
with all the numeric fields around it **stripped away**. Same stall
warnings, same on-speed cues, same colors.

**When to use:** if you already have an EFIS or dedicated ASI/ALT
display, the numeric readouts on Primary mode are redundant. This
mode keeps only the visual indexer — ideal for a narrow panel cutout
beside the primary ASI.

#### Energy — deceleration gauge

*(Mode 3.)*

!!! info "Screenshot pending"
    <!-- TODO: add assets/images/m5-mode-3-decel.png -->

A vertical "energy tape" gauge in the center showing instantaneous
airspeed deceleration in **knots per second**. A green band marks the
stable-energy zone (around 0 kt/s); red bands above and below flag
rapid acceleration or deceleration. A white horizontal bar on the
tape tracks the current smoothed decel rate.

Numeric readouts in the corners: **IAS** (left), **Kt/s decel rate**
(right). An orange VSI tape runs up the right edge; a slip ball sits
at the bottom.

**When to use:** tuning approach energy. A well-flown stabilized
approach shows small steady decel (tape near the green band); rapid
swings indicate you're pumping the stick or fighting turbulence.
Also useful for gauging flare authority right at touchdown.

#### G history — scrolling G trace

*(Mode 4.)*

!!! info "Screenshot pending"
    <!-- TODO: add assets/images/m5-mode-4-g-history.png -->

A scrolling graph of vertical G over approximately the last 60
seconds. Horizontal reference lines mark each integer G (grey for
±5, ±4, ±3, ±2, −1, −2; bright white for 1G). The trace is color-
coded: **green** when positive-G, **yellow** for 0–1G (unloaded),
**red** for negative G.

**When to use:** post-pattern, post-aerobatic, or post-turbulence
debrief. Shows the full profile rather than just the instantaneous
peak on the other pages. Handy for noticing that approach-to-stall
G unloading you didn't realize you were doing.

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
