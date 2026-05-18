# M5 Replay — stream OnSpeed data to the M5Stack display without the box

A Python tool that replays OnSpeed SD-card flight logs (or synthetic
scenarios) to an M5Stack display over a USB-to-TTL serial dongle. Lets you
bench-test the M5 display firmware at your desk without needing the Gen3
OnSpeed controller connected.

This is the companion to the M5 display firmware at
[`software/OnSpeed-M5-Display/`](../../software/OnSpeed-M5-Display/).

---

## What this is for

You have an M5Stack Basic or Core2 and want to test the five OnSpeed
display modes (Energy Display, Attitude, Indexer, Decel Display,
Historic G) at your desk. The OnSpeed Gen3 box is mounted in the
plane, but the M5 is powered from USB-C and just needs a stream of valid
`#1` frames on its RX pin.

`replay.py` generates those frames from:
- **A real flight log CSV** (recorded by the Gen3 firmware to the SD card), or
- **A deterministic synthetic scenario** that cycles through cruise →
  slow flight → approach → stall → recovery → g-turns so you can
  regression-test every display mode.

The tool reads your OnSpeed config file (`.cfg`) to pick up per-flap
setpoints — the frames sent to the M5 use your aircraft's real LDmax,
OnSpeedFast/Slow, and StallWarn AOAs, so the display colors and
chevron positions look exactly like what you'd see in the plane.

---

## Quick start

```bash
# 1. Flash the M5 firmware (one time) — pick the env for your board
cd ../../software/OnSpeed-M5-Display
pio run -e m5stack-core-esp32 -t upload    # M5Stack Basic
# pio run -e m5stack-core2 -t upload       # M5Stack Core2

# 2. Wire the dongle to the M5 (see "Hardware setup" below)

# 3. Start the replay (plays your real flight log)
cd ../../tools/m5-replay
uv run replay.py \
    --port /dev/cu.usbserial-XXXX \
    --input ~/Downloads/sam_onspeed_aoa_4_11_2026.csv

# Or start with the deterministic synthetic scenario
uv run replay.py --port /dev/cu.usbserial-XXXX --synthetic
```

The M5 should boot into "Looking for Serial data…", auto-detect your
replay stream, and switch to the AOA chevron display. Press the middle
button (**Button B**) on the M5 face to cycle through the five display
modes. **Button A** dims the screen, **Button C** brightens it.

---

## Hardware setup

### What to buy

| Item | Purpose | Approx. cost |
|------|---------|--------------|
| USB-to-TTL serial dongle (FTDI FT232RL, CP2102, or CH340) | Laptop → M5 serial bridge | $6–15 |
| 3× female-to-female jumper wires | Dongle ↔ M5 Port C header | $3 |
| USB-C cable (for the M5) | Power + flashing | (you probably have one) |

Any 3.3 V (or 3.3/5 V switchable) TTL dongle works. I've tested with:
- Adafruit FTDI Friend
- SparkFun FTDI Basic
- Generic "CP2102 USB to TTL" from Amazon

Keep the dongle on **3.3 V** — the M5's Port C RX pin is 3.3 V logic.

### Wiring

**Port C** is the red 4-pin grove connector on the right side of the
M5 base. The RX/TX GPIOs differ between the two boards:

| Board | Port C RX | Port C TX |
|-------|-----------|-----------|
| M5Stack Basic | GPIO 16 | GPIO 17 |
| M5Stack Core2 | GPIO 13 | GPIO 14 |

Pinout for the Port C connector (same physical pin order on both
boards; only the MCU GPIO mapping changes):

```
Port C
  [GND] [5V] [TX] [RX]
```

Connect (either board):

```
Dongle TX  ─────────►  M5 Port C RX
Dongle GND ─────────►  M5 GND
```

That's it. Leave the dongle's RX line unconnected — we're one-way. The M5
is powered through its own USB-C, independently of the dongle.

> **Tip:** if you already have an M5 Port C breakout cable (e.g. the
> 4-pin Grove cable that ships with some M5 kits), you can plug it
> directly into the dongle pins with the jumpers.

### Finding the serial port

**macOS:**
```bash
ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART 2>/dev/null
```

**Linux:**
```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

**Windows:** open Device Manager → Ports (COM & LPT), look for a new
COM port when you plug the dongle in. Use `COM4` (or whatever it shows)
as the `--port` argument.

---

## Usage

### Real flight log

```bash
uv run replay.py --port /dev/cu.usbserial-XXXX --input path/to/log.csv
```

Any CSV written by the OnSpeed Gen3 firmware to its SD card works. The
CSV must have these columns (standard OnSpeed log format — see
[`LogSensor.cpp:303`](../../software/sketch_common/src/tasks/LogSensor.cpp)):
`timeStamp, Pitch, Roll, IAS, Palt, AngleofAttack, flapsPos, VSI,
VerticalG, LateralG, YawRate, OAT, FlightPath, DataMark`.

### Synthetic scenario

```bash
uv run replay.py --port /dev/cu.usbserial-XXXX --synthetic
```

Cycles forever through this 5-minute script:

| Time (s) | Phase | What happens |
|----------|-------|--------------|
| 0–60 | Cruise | IAS 140 kt, AOA ≈ 2°, flaps 0 |
| 60–120 | Slow flight sweep | IAS 140 → 90 kt, AOA 2 → 8° |
| 120–180 | Approach | IAS 80 kt, flaps 16, AOA at OnSpeedSlow, gentle roll |
| 180–210 | Approach-to-stall | AOA climbs past StallWarn to AlphaStall |
| 210–240 | Recovery | IAS builds back, AOA drops, G excursion |
| 240–300 | G-turns | ±45° rolls, G up to 1.8 — exercises Historic G mode |

After 5 minutes, loops back to cruise.

### Fast-forwarding in a long log

```bash
uv run replay.py --port $PORT --input log.csv --skip 120
```

Skips the first 120 seconds (useful for jumping past taxi/takeoff).

### Faster/slower playback

```bash
uv run replay.py --port $PORT --input log.csv --speed 2.0   # 40 Hz stream
uv run replay.py --port $PORT --input log.csv --speed 0.5   # 10 Hz stream
```

> **Note:** the M5 firmware measures its own frame period and compensates.
> The `DecelRate` display will still show realistic values at any speed
> because the M5's DecelRate = SavGol(IAS) / measured_dt.

### Looping forever

```bash
uv run replay.py --port $PORT --input log.csv --loop
```

### Custom config file

```bash
uv run replay.py --port $PORT --synthetic --config path/to/your-config.cfg
```

Defaults to `~/Dropbox/N720AK/OnSpeed Cals/2_20_26_config.cfg`. The tool
reads per-flap setpoints (LDMAX, OnSpeedFast/Slow, StallWarn, alpha_0,
alpha_stall) from the config so PercentLift calculations match what the
real firmware would produce for your aircraft.

### All options

```
--port PATH       Serial port (e.g. /dev/cu.usbserial-XXXX). Required.
--input CSV_PATH  OnSpeed SD-card CSV log to replay.
--synthetic       Use deterministic demo scenario instead of a CSV.
--config PATH     OnSpeed .cfg file with flap setpoints.
                  Default: ~/Dropbox/N720AK/OnSpeed Cals/2_20_26_config.cfg
--skip SECS       Skip N seconds from the CSV start.
--speed MULT      Playback speed multiplier (default 1.0 = real-time).
--loop            Restart from the beginning when the stream ends.
```

---

## Testing

Run the unit tests (validates frame format, field offsets, CRC, clamping):

```bash
pio run -e native      # build the firmware-parser harness
uv run test_replay.py  # run the test suite
```

Expected: `11/11 passed`.

The tests run in two layers:

**Layer 1 — Self-referential (Python only).** Fast checks of the Python
builder: total length is 83 bytes (v4.24 wire), `#1` header, CRC
matches the sum-of-payload-bytes convention, every field round-trips
through the documented byte offsets, signed fields keep their signs,
the honest percent-lift formula tracks `ComputePercentLift`,
out-of-range values clamp, NaN/Inf don't corrupt the frame length.

**Layer 2 — Firmware-parser interop.** The decisive test: build a frame
in Python, pipe it into the native `parse_frame` binary (which links
the same `onspeed_core::ParseDisplayFrame` that runs on the M5), and
assert every parsed field matches the original input within wire
resolution. Also covers a corrupt-CRC reject test and a wrong-size
test that catches stale builders against new firmware. If the
harness binary isn't built, these tests skip with a clear message;
CI always builds it.

Without the Layer 2 tests, a wire-format change between Python and the
firmware would slip through silently — Layer 1 only knows about the
Python builder.

---

## The wire protocol

Frames are **83 bytes** total (v4.24 wire): 79-byte payload + 2-byte
CRC + CRLF, at 115200 8N1. Full byte-level reference (offsets, scale
factors, sign conventions, parser recommendations) lives in
[`docs/site/docs/reference/serial-protocol.md`](../../docs/site/docs/reference/serial-protocol.md).
The canonical builder/parser is
[`software/Libraries/onspeed_core/src/proto/DisplaySerial.{h,cpp}`](../../software/Libraries/onspeed_core/src/proto/DisplaySerial.h)
— `replay.py`'s output must round-trip through that parser
byte-for-byte, which is what the Layer 2 tests verify.

Frame rate on the real firmware is **20 Hz** (50 ms), controlled by
`kDisplaySerialPeriodMs` in
[`HardwareMap.h`](../../software/OnSpeed-Gen3-ESP32/HardwareMap.h). The M5
firmware does not assume this rate — it measures the actual inter-frame
interval with `micros()` each frame, so this replay tool can stream at
any cadence and the M5's DecelRate display will still be correct.

---

## Troubleshooting

### M5 stays on "Looking for Serial data"

- **Verify wiring**: dongle TX → M5 Port C RX pin (GPIO 16 on Basic,
  GPIO 13 on Core2), not the TX pin. Shared GND.
- **Verify dongle TX is alive**: on a fresh USB insertion most FTDI
  dongles have a TX LED that blinks when data flows.
- **Check for OS permissions on macOS**: the first time you plug in a
  cheap CH340 dongle, macOS may require you to approve the driver in
  System Settings → Privacy & Security.
- **Serial port in use**: make sure another program isn't holding the
  port (close `pio device monitor`, Arduino Serial Monitor, etc.).

### M5 shows "ONSPEED CRC Failed"

The frame reached the M5 but the CRC didn't match. Most likely causes:

- **Wrong baud**: the M5 expects 115200. The dongle must match.
- **Signal inversion**: if you're using an RS-232 dongle (DB9 style),
  try a plain TTL dongle instead. The M5 auto-detects both, but only
  one per-port mapping is tried at a time.

Run `uv run test_replay.py` to confirm the tool's own CRCs are well
formed. All tests should pass.

### M5 boots into the wrong serial-port mode

The M5 remembers the last successful port in ESP32 NVS flash. If you
plug into a different physical port, hold Button A while the M5 boots —
that triggers `checkSerial()` to re-probe. (If this isn't working, check
`main.cpp` setup code in the M5 firmware.)

### `WARN: unexpected frame dt=...` on the M5's USB serial monitor

The M5 flags frame periods outside 20 ms–200 ms. Causes:

- **`--speed` very different from 1.0**: intentional; the warning is
  just informational.
- **Laptop CPU load** causing Python's `time.sleep` to jitter: mostly
  harmless — the next frame will usually land on time.
- **Real delivery issue** (flaky cable, dongle overheating): investigate.

### Python can't find `serial` module

Use `uv run replay.py` (not `python replay.py`). The PEP-723 header in
the script declares the `pyserial` dependency and `uv` installs it
automatically in an ephemeral environment.

---

## How it works

```
                  ┌─────────────────────────────┐
                  │  replay.py                  │
 OnSpeed .cfg ───►│  • parses flap setpoints    │
 SD-card CSV  ───►│  • reads rows at 20 Hz      │
                  │  • computes PercentLift     │
                  │  • builds 83-byte frame     │
                  │  • writes to serial port    │
                  └──────────────┬──────────────┘
                                 │ /dev/cu.usbserial-XXXX
                                 │ (115200 8N1)
                                 ▼
                  ┌─────────────────────────────┐
                  │  USB-to-TTL dongle          │
                  │  (FTDI / CP2102)            │
                  └──────────────┬──────────────┘
                                 │ 3.3 V TTL
                                 ▼
                  ┌─────────────────────────────┐
                  │  M5Stack Basic or Core2     │
                  │  • SerialRead.cpp parses    │
                  │  • measures frame dt        │
                  │  • renders display mode     │
                  └─────────────────────────────┘
```

The tool never tries to *emulate* the OnSpeed box — it just faithfully
replays valid frames. This means:

- **Your real flight data shows up as it was captured**, not a simulation.
- **Bugs in the M5 firmware surface in the same way as in flight**, so
  bench testing catches real issues.
- **The M5 can't tell the difference** between replayed frames and live
  ones, so you can develop and iterate without the plane.

---

## File layout

```
tools/m5-replay/
├── replay.py          # Main tool (single file, PEP-723 deps)
├── test_replay.py     # Unit tests for the frame builder
└── README.md          # This file
```

## Related

- M5 display firmware: [`../../software/OnSpeed-M5-Display/`](../../software/OnSpeed-M5-Display/)
- Firmware side that sends these frames: [`../../software/sketch_common/src/io/DisplaySerial.cpp`](../../software/sketch_common/src/io/DisplaySerial.cpp)
- M5-side parser: [`../../software/OnSpeed-M5-Display/src/SerialRead.cpp`](../../software/OnSpeed-M5-Display/src/SerialRead.cpp)
- Log column format: [`../../software/sketch_common/src/tasks/LogSensor.cpp`](../../software/sketch_common/src/tasks/LogSensor.cpp)
