# Optional: External Display

OnSpeed can drive an external display via serial output, providing a
cockpit-mounted visual readout of AOA, airspeed, attitude, and
deceleration. The most common option is an **M5Stack Basic**, which
runs the open-source [`OnSpeed-M5-Display`](https://github.com/flyonspeed/OnSpeed-Gen3/tree/master/software/OnSpeed-M5-Display)
firmware ported from the Gen2 project.

## M5Stack Basic display

The M5Stack Basic is a compact self-contained ESP32 development unit
with a 320×240 color TFT, three front buttons, and a USB-C charging
port. It receives OnSpeed's serial stream, parses it, and renders any
of five display modes.

### Display modes

Press Button A / B / C on the front of the M5 to cycle between:

| Mode | What it shows |
|------|--------------|
| **AOA chevron** | Large animated chevron with the current AOA colored by region (green / yellow / red), plus numeric AOA, IAS, and percent-lift. |
| **Attitude indicator** | Mini artificial horizon driven from OnSpeed's pitch/roll/flight-path angle; useful as a backup AI. |
| **Narrow AOA** | Vertical tape layout for AOA, sized for a small panel cutout. |
| **Decel gauge** | Deceleration rate in knots per second — useful for gauging energy during approach. |
| **G-load history** | Scrolling graph of vertical G over time. Handy after maneuvering or turbulence. |

### Wiring

The M5 talks to OnSpeed over a one-way serial link. Connect:

| OnSpeed pin       | Direction | M5 pin        |
|-------------------|-----------|---------------|
| GPIO 10 (DISPLAY_SER_TX) | → | M5 Port C RX (GPIO 16) |
| GND               | ↔         | M5 GND        |

The OnSpeed serial output can be TTL or RS-232 level. The M5 firmware
auto-detects on boot by trying three common port configurations (TTL
direct, RS-232 inverted, and a simulator-friendly variant). Once
detected, the port mapping is saved to the M5's NVS flash so subsequent
boots come up faster.

- **Baud rate:** 115200 8N1
- **Frame rate:** 20 Hz (every 50 ms). Each frame is 80 ASCII bytes
  including `#1` header, fields, CRC, and CRLF.

### Powering the M5

The M5 can be powered from:

- **Its own USB-C port** — ideal for bench testing or a permanent
  panel installation if you have avionics USB power.
- **Panel 5 V supply** wired into the M5 base's red power pin on
  Port C. Check the M5Stack documentation for current ratings before
  wiring to ship power.

Do **not** try to power the M5 from OnSpeed's GPIO pins — they don't
supply enough current for the display.

### Serial output format

The OnSpeed firmware's serial output format is configurable via the
`SERIALOUTFORMAT` field in the configuration:

- **ONSPEED** — native format (76-byte payload with AOA, setpoints,
  attitude, flap position, etc.). Use this with `OnSpeed-M5-Display`.
- **G3X** — Garmin G3X-compatible subset for use with Garmin panel
  displays.

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

You can test the M5 display on your desk before connecting it to
OnSpeed. Use the Python replay tool under
[`tools/m5-replay/`](https://github.com/flyonspeed/OnSpeed-Gen3/tree/master/tools/m5-replay)
with a cheap USB-to-TTL dongle:

```bash
cd tools/m5-replay

# Real flight log
uv run replay.py --port /dev/cu.usbserial-XXXX \
    --input /path/to/OnSpeed_log.csv

# Deterministic synthetic demo (cruise → approach → stall → g-turns)
uv run replay.py --port /dev/cu.usbserial-XXXX --synthetic
```

See `tools/m5-replay/README.md` for the full hardware shopping list,
wiring diagram, and protocol details.

## Mounting

Mount the display where it's visible during approach without blocking
your view of primary instruments. Common locations:

- Panel cutout (flush-mount)
- Glareshield mount on a RAM-style ball joint
- Side console

!!! note "Display is supplemental"
    The external display provides additional visual information. The
    primary OnSpeed interface remains the audio tones — your eyes
    should still be outside during approach.
