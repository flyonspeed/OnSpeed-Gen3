# Optional: External Display

OnSpeed can drive an external display via serial output, giving you a
cockpit-mounted visual readout of AOA, airspeed, attitude, flight path,
deceleration, and G-load. Two M5Stack units are supported — the
**M5Stack Basic** and the **M5Stack Core2** — both running the same
open-source [`OnSpeed-M5-Display`](https://github.com/flyonspeed/OnSpeed-Gen3/tree/master/software/OnSpeed-M5-Display)
firmware from a single codebase.

!!! note "Display is supplemental"
    The external display supplements OnSpeed's audio tones — it does not
    replace them. The primary interface is still aural; your eyes should
    stay outside during approach. The display is most useful for
    post-flight analysis (the Historic G page), initial familiarization
    with the tones, and as a backup attitude indicator.

## Try it in your browser

Here's the live display firmware running in your browser — same C++
source that runs on the real M5Stack hardware, rendered at its
native 320×240 resolution.

<iframe src="../../assets/sim/index.html"
        width="340" height="320"
        style="display: block; margin: 1.5em auto; border: 0;"
        title="OnSpeed M5 display simulator"
        loading="lazy"></iframe>

Click the display, then press <kbd>↓</kbd> to cycle through the five
modes (the one you're looking at is the primary AOA indexer; there
are four more). The data feed is a synthetic AOA ramp that sweeps
from −4° up through past stall and back, covering the alpha_0 floor,
the L/D~MAX~ / ONSPEED tone bands, and the stall-warning region where
the top chevrons flash red. The on-screen percent-lift digit
saturates at 99 in the stall region (the saturation convention,
"never reads 100") — same as on the real device. The wire carries
sub-percent resolution, so the underlying index bar position
advances smoothly even when the digit holds at 99. It's not a
recorded flight, but everything the firmware draws is drawn the
same way on the panel.

See [Display modes](#display-modes) below for what each mode means.

## Supported M5Stack units

Both supported units have the same 320×240 ILI9342C color TFT at 40 MHz
and receive OnSpeed's 20 Hz serial stream over Port C. You can pick
either one; the firmware is a single binary per board (built from the
same source) and the on-screen layout is identical.

| | **M5Stack Basic** | **M5Stack Core2** |
|--|--|--|
| USB-serial chip | CP2104 | CH9102F |
| Port C pins | GPIO 16 (RX) / GPIO 17 (TX) | GPIO 13 (RX) / GPIO 14 (TX) |
| Buttons | Three physical buttons | Three capacitive touch zones below the screen |
| IMU | MPU9250 | MPU6886 |
| Power | Direct 5 V | AXP192 PMIC (handled by M5Unified) |
| PlatformIO env | `m5stack-core-esp32` | `m5stack-core2` |

Either unit works — the Core2 adds a built-in LiPo battery and
capacitive touch, while the Basic is slightly cheaper and has tactile
buttons. Wiring, serial protocol, and mounting options are the same.

### Buttons

| Button | Action |
|--------|--------|
| **Button A** (left) | Brightness down |
| **Button B** (middle) | Cycle display mode (0 → 4, then wraps back to 0) |
| **Button C** (right) | Brightness up |

On the Core2, these are capacitive touch zones below the screen (the
three dots printed on the bezel); the Basic has physical push buttons.
The firmware uses M5Unified's unified `M5.BtnA/B/C` API, so behavior is
identical on both units.

Brightness and display mode are both persisted to NVS on every adjust,
so the M5 boots back into whichever brightness and page the pilot last
selected. A fresh M5 with no saved preferences defaults to full
brightness and Mode 0 (Energy Display).

If you hold **Button B during boot**, the M5 enters WiFi OTA update
mode instead of running the display — for updating firmware without
needing a USB cable.

## Display modes

The five modes are selected by pressing **Button B**, which cycles in
order: **Energy Display → Attitude → Indexer → Decel Display →
Historic G →** back to Energy Display. The last-used mode persists
across power cycles; a fresh M5 with no saved preference comes up
on Energy Display (Mode 0).

To preview each mode, scroll back up to the in-browser simulator and
press <kbd>↓</kbd> to step through them. The C++ source that runs the
sim is the same source that runs on the real M5Stack hardware, so the
layout you see in the browser is the layout you get on the panel.

### Energy Display — AOA indexer + full readouts

*(Mode 0 — the boot default.)*

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
  *fast but safe* indicator. **Green** when AOA is between LDmax
  and OnSpeedFast (you're a touch fast for an OnSpeed approach —
  ease off if you want to nail on-speed).
- **White index bar** — a thin horizontal bar that slides up/down
  the widget showing your current AOA on the scale. Small white
  dots on the left and right edges of the widget mark the **LDmax**
  (best-climb / best-glide) reference.

Visual language follows the standard FAA green/yellow/red
progression. Green covers the whole safe band — bottom chevron
(fast but safe) and the central donut (on-speed). Yellow and red
escalate on the top chevrons as AOA rises toward stall.

**Above the widget — percent lift** (0–99, saturating at 99 in the
stall region). A large white number with a black outline — readable
against any background. The number is the honest envelope fraction:
0% at zero-lift body angle, 99% at stall. Where each band edge falls
on the scale (L/Dmax, OnSpeed, stall warning) varies per flap because
the underlying calibration varies per flap — typically L/Dmax lands
in the low 30s on clean flaps and the mid-50s on full flaps.
Compare values by AOA region (silent / pulsing / on-speed / stall),
not by exact percent.

**Corners:**

- Top-left: current **IAS** in knots (big white number) with a green
  "IAS" label, plus a small two-digit **DataMark** counter at the very
  top-left corner — wraps mod 100, increments each time the pilot
  presses the data-mark button. Mirrors the same counter on the SD log
  and the JS indexer.
- Top-right: current **vertical G** (big white number) with a green
  "G" label.
- Bottom-left: a **flap position** icon (circle with a rotating
  triangular needle representing the flap angle, with stop marks at
  the configured travel endpoints) and the numeric flap angle inside
  the circle.
- Bottom-center: the **slip/skid ball** (green when coordinated, red
  and flashing at high AOA with large slip).

Energy Display does not show altitude — flip to Attitude mode (next) for
a numeric pressure-altitude readout.

**Right edge — G-onset rate tape.** A thin yellow bar growing up
(positive onset, G load increasing — pulling into a turn or
pull-up) or down (negative onset, G unloading — push-over) from
a "zero line" ladder at the right side of the screen.  Saturates
at ±2 g/s.  The signal is the smoothed first derivative of
vertical-G, computed by feeding `AccelVertFilter` through a 250 ms
low-pass kernel (`GOnsetFilter` in `onspeed_core/filters/`).
Useful as a smoothness cue: a steady long pull-up shows a small
bar; a sudden yank shows a tall bar that dies back as the rate
stabilizes.

### Attitude — synthetic horizon with AOA

*(Mode 1.)*

Full-width backup artificial horizon driven by OnSpeed's AHRS.
Sky/ground horizon with a pitch ladder (10° increments), roll
indication via a fixed aircraft reference symbol against a rolling
horizon, a magenta flight-path marker (concentric rings with
perpendicular wing bars) showing the aircraft's actual vector through
the air, slip/skid ball at the bottom, and a white VSI tape on the
right edge with a tick ladder for climb/descent rate.  The bar fills
at ±600 fpm — saturates beyond, so a hard climb pegs the top — and
the 20-pixel ladder ticks give a visual reference for fine reads
in the gentle-cruise band where small trends matter most.

**Corners:**

- Top-left: current **IAS** in knots with an "IAS" label.
- Top-right: current **pressure altitude** with a "PALT" label. ISA
  1013.25 hPa reference; this is not panel-altimeter altitude — no
  Kollsman/QNH correction is applied, so it matches the panel altimeter
  only when the altimeter is set to 29.92.
- Bottom-left: current **vertical G** with a "G" label.
- Bottom-right: current **percent lift** (the same 0–99 envelope
  fraction shown above the Energy Display indexer) with an "AOA" label.

Numeric pitch readout sits in a dark rounded rectangle behind the
aircraft symbol at the center.

**When to use:** cross-check against the primary AI (not as a
replacement), or during instrument transitions where you want the
OnSpeed-derived flight path marker overlaid on an independent
attitude source.

### Indexer — panel-minimal

*(Mode 2.)*

The Energy Display's indexer widget (chevrons + donut + index bar),
with all the numeric fields around it **stripped away**. Same stall
warnings, same on-speed cues, same colors. The **DataMark** counter
stays at the top-left so the pilot can confirm a press registered
even on this minimal layout.

**When to use:** if you already have an EFIS or dedicated ASI/ALT
display, the numeric readouts on Energy Display are redundant. This
mode keeps only the visual indexer — ideal for a narrow panel cutout
beside the primary ASI.

### Decel Display — deceleration gauge

*(Mode 3.)*

A vertical "energy tape" gauge in the center showing instantaneous
airspeed deceleration in **knots per second**. A green band marks the
stable-energy zone (around 0 kt/s); red bands above and below flag
rapid acceleration or deceleration. A white horizontal bar on the
tape tracks the current smoothed decel rate.

Numeric readouts in the corners: **IAS** (left), **Kt/s decel rate**
(right). A white VSI tape runs up the right edge (same ±600 fpm
scale as Mode 1); a slip ball sits at the bottom.

**When to use:** tuning approach energy. A well-flown stabilized
approach shows small steady decel (tape near the green band); rapid
swings indicate you're pumping the stick or fighting turbulence.
Also useful for gauging flare authority right at touchdown.

### Historic G — scrolling G trace

*(Mode 4.)*

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

| OnSpeed pin              | Direction | M5 Basic Port C RX | M5 Core2 Port C RX |
|--------------------------|-----------|--------------------|--------------------|
| GPIO 10 (DISPLAY_SER_TX) | →         | GPIO 16            | GPIO 13            |
| GND                      | ↔         | GND                | GND                |

Port C on the Basic uses GPIO 16/17; on the Core2 it moves to
GPIO 13/14. The M5 firmware compiles the correct pins in automatically
per env — you only need to match the physical Port C pin on whichever
unit you have.

The OnSpeed serial output can be TTL or RS-232 level. The M5 firmware
auto-detects on boot by trying three common port configurations (TTL
non-inverted, RS-232 inverted, and a bench-simulator TTL variant).
Once detected, the port mapping is saved to the M5's NVS flash so
subsequent boots come up faster.

- **Baud rate:** 115200 8N1
- **Frame rate:** 20 Hz (every 50 ms). Each frame is 77 ASCII bytes
  (v4.23 wire) including `#1` header, fields, CRC, and CRLF.

See [Display Serial Protocol](../reference/serial-protocol.md) for the
complete wire-format reference (byte offsets, field semantics, parsing
recommendations).

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

There are three ways to get firmware onto the M5 display, in increasing
order of complexity:

1. **OTA update** (no cable) — already-flashed M5, WiFi-capable update
2. **USB flash from a released binary** — new M5, or recovery flash
3. **Build + flash from source** — developers and advanced users

### 1. OTA update (existing M5, no USB cable)

Once an M5 has been flashed at least once with OnSpeed firmware, future
updates can happen over WiFi — no USB cable required.

1. Download `firmware.bin` for your board from the [latest release page](https://github.com/flyonspeed/OnSpeed-Gen3/releases/latest). Look for:
    - `onspeed-m5-X.Y.Z-basic-firmware.bin` (M5Stack Basic)
    - `onspeed-m5-X.Y.Z-core2-firmware.bin` (M5Stack Core2)
2. Hold **Button B** while powering the M5 on. It will boot into
   firmware-update mode and display its WiFi SSID, password, and the
   URL to browse to. The on-screen information is authoritative — use
   those values rather than the defaults below if they differ.
3. Connect your laptop or phone to the **`OnSpeedDisplay`** WiFi
   network using the password **`angleofattack`**.
4. Open **`http://192.168.0.2/upgrade`** in a browser.
5. Click **Choose file**, select the `firmware.bin` you downloaded,
   then click **Update**.
6. The M5 reboots into the new firmware automatically.

### 2. USB flash from a released binary (new M5)

For a freshly-purchased M5 that's never run OnSpeed firmware, flash it
once via USB. After that, future updates can use the OTA path above.

**Download the release assets** for your board from the [latest release](https://github.com/flyonspeed/OnSpeed-Gen3/releases/latest):

- `onspeed-m5-X.Y.Z-basic-firmware.bin` or `-core2-firmware.bin`
- `onspeed-m5-X.Y.Z-basic-bootloader.bin` or `-core2-bootloader.bin`
- `onspeed-m5-X.Y.Z-basic-partitions.bin` or `-core2-partitions.bin`

**Flash with esptool** (install via `pip install esptool`):

```bash
# Replace PORT with your M5's USB-serial device:
#   Basic: /dev/cu.usbserial-* (CP2104) on macOS, COMn on Windows
#   Core2: /dev/cu.usbserial-* (CH9102F) on macOS, COMn on Windows

esptool.py --chip esp32 --port PORT --baud 921600 write_flash \
    0x1000   onspeed-m5-X.Y.Z-BOARD-bootloader.bin \
    0x8000   onspeed-m5-X.Y.Z-BOARD-partitions.bin \
    0x10000  onspeed-m5-X.Y.Z-BOARD-firmware.bin
```

Replace `BOARD` with `basic` or `core2` and `X.Y.Z` with the release
version.

Alternatively, [M5Burner](https://docs.m5stack.com/en/download) is a
GUI tool from M5Stack that can flash custom firmware. Use its
"Custom firmware" tab and point it at the three release `.bin` files.

### 3. Build and flash from source (developers)

Prerequisites:

- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation.html)
  installed
- M5Stack Basic or Core2 connected to your laptop via USB-C

```bash
cd software/OnSpeed-M5-Display

# M5Stack Basic
pio run -e m5stack-core-esp32 -t upload

# M5Stack Core2
pio run -e m5stack-core2 -t upload
```

Tail the M5's own USB serial for debug output:

```bash
pio device monitor
```

### Getting firmware for an unreleased change (reviewers and testers)

**From a PR:** every pull request that touches M5 display code builds
both board variants in CI and posts a sticky comment with direct
download links to the `.zip` artifacts. Look for the "Firmware
Artifacts" comment on the PR; it includes a table with Basic and
Core2 rows.

**From master:** the latest successful master CI run's artifacts are
listed at [nightly.link for OnSpeed-Gen3 CI on master](https://nightly.link/flyonspeed/OnSpeed-Gen3/workflows/ci/master)
(no GitHub login required). Look for `m5-display-basic-…zip` or
`m5-display-core2-…zip`. The same shortcut is on the **master
artifacts** badge in the [README](https://github.com/flyonspeed/OnSpeed-Gen3#readme).

CI artifacts expire 30 days after the run.

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

- **ONSPEED** — native `#1` format (73-byte payload + CRC + CRLF = 77
  bytes total, v4.23 wire) carrying attitude, IAS, pressure altitude,
  percent-lift (tenths of a percent), the per-flap percent anchors that
  drive an indexer (L/D~MAX~ audio gate, OnSpeedFast, OnSpeedSlow,
  stall-warn, plus the separate visual L/D~MAX~ pip), G-loading, and
  flap travel range.
  Use this with `OnSpeed-M5-Display` or any third-party
  display reading the full data set. Full byte-level wire-format
  reference: [Display Serial Protocol](../reference/serial-protocol.md).
- **G3X** — Garmin G3X-compatible subset for use with Garmin panel
  displays. Drops AOA setpoints and aerodynamic anchors; carries only
  what a stock G3X-compatible PFD knows how to render.

## Mounting

Mount the display where it's visible during approach without blocking
your view of primary instruments. Common locations:

- Panel cutout (flush-mount, Indexer mode's narrow layout is designed for this)
- Glareshield mount on a RAM-style ball joint
- Side console near the throttle quadrant
