# Gen3 hardware-test sketch — design

## Purpose

A standalone PlatformIO environment that flashes a manufacturing/diagnostic
test sketch onto a Gen3 ESP32-S3 board. Two intended uses:

- **Bench**: run on freshly assembled boards before flashing production
  firmware. Fast iteration, eyeball the output, decide whether the box is
  good.
- **Field diagnostic**: flash onto a customer's returned box to triage a
  hardware issue. Must not destroy SD card contents or configuration files.

This supersedes PR #43, which targeted a Feb-18 master state that has since
been substantially refactored (`Globals.h` → `HardwareMap.h`, `[esp32s3_common]`
env split, MCP3202 driver rewrite, gyro full-scale fix in #316). Rather than
rebase across that churn, this rebuilds from current master.

Out of scope:
- M5-side hardware-test sketch (M5 is stock M5Stack hardware; we only test
  the wired interconnect from the Gen3 side)
- Automated test fixtures or CI integration
- JTAG / boundary-scan
- Persistent factory test records (would write to customer SD cards — unsafe
  in the field-diagnostic use case)

## Architecture

A new sibling sketch at:

```
software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino
```

Plus two new envs in `platformio.ini`:

- `[env:hardware_test]` — V4P variant (default)
- `[env:hardware_test-v4b]` — V4B variant

Both inherit from `[esp32s3_common]` so they pick up the same flash mode,
partition layout, USB CDC settings, and warning regime as production firmware.
A pre-script (`scripts/hwtest_srcdir.py`) redirects `PROJECT_SRC_DIR` to the
test sketch folder so only the test `.ino` compiles. The sketch:

- `#include`s `HardwareMap.h` from the firmware sketch tree (single source of
  truth for pins; no duplication, no `#define` drift)
- Links `onspeed_core` (for `CountsToPsi`, `BuildFrame`, etc.) and `SdFat`
- Uses the same MCP3202 control-word format as
  `src/drivers/Mcp3202Adc.cpp::Mcp3202Read`

## Tests

The sketch runs an 8-test loop on a 10-second cycle. Tests are classified as:

- **AUTO** — programmatic pass/fail
- **PROMPT** — asks the operator y/n/skip with 5-second timeout, default skip
- **MANUAL** — operator watches/listens, no automatic fail

| # | Test | Class | What it checks |
|---|------|-------|----------------|
| 1 | Pressure (Pitot, AOA, Static) | AUTO | SPI read on each pressure sensor; status bits = 0; counts in HSC valid window. Uses `onspeed::sensors::CountsToPsi` for the conversion. |
| 2 | IMU | AUTO | WHO_AM_I = 0x6B (ISM330DHCX) or 0x6C (LSM6DSOX); accel + gyro readback. Gyro scaled at 250 dps full-scale (matches PR #316 fix to `GYRO_RES`). |
| 3 | MCP3202 ADC pots | AUTO | 3-byte MCP3202 transaction matching `Mcp3202Adc.cpp` (start byte `0x01`, config byte `0xA0 \| (channel << 6)`). Channel 0 = Flap, channel 1 = Volume (matches `kAdcChFlap`/`kAdcChVolume` in `HardwareMap.h`). PASS if both reads land strictly in (0, 4095). |
| 4 | Serial loopback | PROMPT | Asks "Is the GPIO 10 ↔ GPIO 3 jumper installed? [y/n, 5s default=skip]". If `y`: send "HWTEST\n" on Display TX (Serial1 at 115200), read back on Boom RX, PASS on roundtrip within 200ms. If `n` or timeout: SKIP (not FAIL). |
| 5 | SD card | AUTO | Init SdFat at SD-card SPI bus, log card size, write+delete `_hwtest.tmp` at filesystem root. Safe on configured boxes — does not touch `.cfg` files or logs. |
| 6 | Audio | MANUAL | 1.5s 400 Hz sine on left channel only, then 1.5s 1600 Hz sine on right channel only, via I2S DAC. Operator listens. |
| 7 | LED ramp | MANUAL | PWM ramp 0→255 over 1s then 255→0 over 1s on `kPinLedKnob` (GPIO 13). Operator watches the external LED. |
| 8 | M5 display wire | MANUAL | 10 seconds of valid `#1` DisplayFrame protocol at 20 Hz on Display TX (GPIO 10) at 115200 8N1. Frames sweep pitch −10°→+10° and ramp PercentLift through OnSpeed/StallWarn bands. Built via `onspeed::proto::BuildFrame`. Operator confirms the M5 wakes, displays a moving attitude indicator, and the AOA chevron rises through the bands. |

## Output format

Two output streams to USB serial (921600 baud), both human-readable in real
time. Per-test output is unstructured prose; a single machine-parseable
`RESULT,...` line at the end of each cycle gives a one-line summary suitable
for grep, copy/paste into an issue, or scraping with a test harness.

### Banner

```
=======================================
  OnSpeed Gen3 Hardware Test
  Build: 4.21.0-2-gabc123 (2026-04-26)
  Hardware variant: V4P
=======================================
```

Build version pulled from `BuildInfo::version` and `BuildInfo::gitShortSha`
(same mechanism the production firmware uses).

### Per-test prose

Mirrors the existing PR's format — section header in brackets, then
indented status lines:

```
[Pressure Sensors]
  Pitot:    PASS  status=0  counts=2046  +0.000 PSI
  AOA:      PASS  status=0  counts=2046  +0.000 PSI
  Static:   PASS  status=0  counts=3245  +14.234 PSI  981.4 mb

[IMU]
  IMU:    PASS  WHO_AM_I=0x6B
          Accel: X=+0.012  Y=-0.005  Z=+0.998 g
          Gyro:  X=+0.12   Y=-0.05   Z=+0.08 dps

[Serial Loopback]
  Jumpered? [y/n, 5s default=skip]: skip
  Serial1 loopback (TX=10 -> RX=3): SKIP

[M5 Display Wire]
  Streaming 200 frames over Display TX at 20 Hz...
  M5 should show pitch sweep -10..+10 and AOA chevron rising through
  OnSpeed/StallWarn bands.
```

### Summary line (last line of every cycle)

```
RESULT,fw=4.21.0-2-gabc123,hw=V4P,pressure=PASS,imu=PASS,adc=PASS,loopback=SKIP,sd=PASS,audio=MANUAL,led=MANUAL,m5=MANUAL,overall=PASS
```

Rules:

- One line. No internal newlines.
- Field separator is `,`. Field is `<key>=<value>`.
- Per-test values: `PASS`, `FAIL`, `SKIP`, or `MANUAL`.
- `overall=PASS` iff every AUTO test passed. `SKIP` and `MANUAL` do not
  count as failures.

## Build & flash

```bash
pio run -e hardware_test              # build V4P (default)
pio run -e hardware_test -t upload    # build + flash V4P
pio run -e hardware_test-v4b          # build V4B
pio device monitor                    # 921600 baud
```

The two envs differ only in `-DHW_V4P` vs `-DHW_V4B`; everything else
inherits from `[esp32s3_common]`.

## Hardware fixture

For the loopback test (PROMPT, optional):

- **Jumper wire** between Gen3 J1 pin for Display TX (GPIO 10) and Boom RX
  (GPIO 3). A single male-to-male jumper is sufficient.

For the M5 wire test (MANUAL):

- Production cable from Gen3 to M5 Port C, M5 powered on its own USB-C.

For the SD test (AUTO):

- microSD card inserted (any FAT32 card — does not need to be formatted by
  OnSpeed).

For the audio test (MANUAL):

- Speaker or audio panel connected, or 3.5mm headphones via the test jack.
  If none connected, the test still runs (drives I2S) — operator just
  doesn't hear anything, which is not a failure.

## Risks called out

**onspeed_core link blast radius.** Linking onspeed_core means a wire-format
change in `proto/DisplaySerial` rebuilds the hwtest. This is acceptable —
it's the drift we want to catch. The alternative (hand-rolling a 74-byte
frame in the sketch) would let the hwtest silently desync from the
firmware's wire format.

**HardwareMap.h coupling.** The hwtest sketch is now coupled to a header in
the firmware sketch tree. If the firmware moves `HardwareMap.h`, the hwtest
breaks. Acceptable — it's a hard build-time error, not silent drift, and
the file's path is stable enough.

**Field-diagnostic safety.** The SD test only writes/deletes `_hwtest.tmp`
at root; this doesn't collide with `.cfg` or log filenames (logs are date-
stamped, configs are `OnSpeedConfig.cfg`). The loopback prompt's
default-skip protects against a false FAIL on a non-jumpered customer box.
No SD writes beyond the temp file. No NVS writes. No flash erase.

**Audio test on bare PCB without speaker.** Test still runs (drives I2S
DAC), operator just doesn't hear it. Not coded around — would add
complexity for no diagnostic value. Same applies to the M5 wire test on
a Gen3 box that's not connected to an M5 — the bytes go into the void;
no automatic check.

**Loopback prompt blocking the loop.** If the operator never responds, the
5-second timeout default-skips and the loop continues. No way to hang.

## What this replaces

PR #43 (`sritchie/hardware-test`) is the prior attempt at this. It targeted
a master state from Feb 18 that has since been heavily refactored. Concrete
problems with PR #43 vs current master:

- Wrong ADC chip (PR uses MCP3204 protocol; board has MCP3202; confirmed
  in `Phils_OnSpeed_Gen2v4_PWB_27Jan24_v0f` pick-and-place CSV)
- ADC channel assignment swapped (PR: vol=0/flap=1; firmware: flap=0/vol=1)
- Gyro full-scale wrong (PR: 245 dps; correct: 250 dps per PR #316)
- EFIS RX pin wrong (PR: GPIO 9; firmware: GPIO 11)
- Pin defs reference `Globals.h` which no longer exists (now `HardwareMap.h`)
- `[esp32s3_common]` env layout now exists; PR's standalone env doesn't
  inherit shared flash/USB settings
- PR's "self-contained" choice predates onspeed_core, which is now the
  natural place for shared logic

This rebuild adopts onspeed_core, includes `HardwareMap.h` directly, fixes
all hardware constants, and adds the M5 wire test that wasn't in #43.
