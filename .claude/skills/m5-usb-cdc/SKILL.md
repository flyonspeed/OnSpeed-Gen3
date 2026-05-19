---
name: m5-usb-cdc
description: Use when writing a host-side program (Python, Swift, Rust, C, anything) that drives the OnSpeed M5 / huVVer secondary display over USB by speaking the `#1` display-serial wire format. Covers port discovery, baud rate, the Data Source menu requirement, the canonical frame builder/parser, and the gotchas that have bitten every host-side bridge so far (X-Plane plugin, m5-replay).
---

# Driving the M5 / huVVer over USB-CDC

## When to use this skill

A host-side program needs to push live data onto the M5 (or huVVer-AVI)
panel-mount display. Examples that already exist:

- The **X-Plane plugin** streams sim flight state to a tethered M5 so
  the pilot can fly the sim with the real M5 indicator in their hand
  (`software/OnSpeed-XPlane-Plugin/`, end-user docs at
  `docs/site/docs/xplane/m5-tethered.md`).
- The **m5-replay** tool replays an SD-card CSV through a USB-tethered
  M5 for bench triage (`tools/m5-replay/`).

In every case the recipe is the same: open the right `/dev/cu.*` at
115200 8N1, emit valid 77-byte `#1` frames at 20 Hz, and make sure the
M5 is in a Data Source mode that listens to USB.

## The wire format is canonical — don't reimplement it from scratch

The single source of truth is
`software/Libraries/onspeed_core/src/proto/DisplaySerial.h`:

- `kDisplayFrameSizeBytes = 77` (v4.23 layout)
- `kIasInvalidWireSentinel = 9999`
- `DisplayBuildInputs` / `DisplayFrame` structs
- `BuildDisplayFrame()` and `ParseDisplayFrame()`
- `DisplayFrameAccumulator` (byte-stream framing)

**If your host program is C++:** link `onspeed_core` and call
`BuildDisplayFrame()` directly — that's what `m5-replay` does. You get
the wire format and checksum for free, and you never drift from
firmware on a v4.24 protocol bump.

**If your host program is in another language** (Swift, Python, Rust):
mirror the layout from `DisplaySerial.h` exactly. The structure of
fields, scales, widths, and the `%+0Nd` / `%0Nu` format strings is the
contract. The checksum is `util::Checksum8` in `onspeed_core/util/Crc.h`:
sum the bytes of the 73-byte payload, mask to 8 bits, format as two
uppercase ASCII hex digits, append CR LF.

**Pin the v4.23 frame in a comment.** Every host-side bridge that
talks to the M5 should have a header comment that names the protocol
version it targets, so a future protocol bump produces a build error
or a visible mismatch instead of silently corrupting frames.

## Port discovery

The M5 board appears as a USB-serial device with one of these prefixes:

- macOS: `/dev/cu.usbmodem*` (native USB-CDC on ESP32-S3),
  `/dev/cu.usbserial-*` (FTDI / CH9102F bridges),
  `/dev/cu.SLAB_USBtoUART*` (CP2104 bridges on M5Stack Basic),
  `/dev/cu.wchusbserial*` (some Core2 batches with WCH chips)
- Linux: `/dev/ttyACM*` (native CDC) or `/dev/ttyUSB*` (bridge chips)
- Windows: `COM1` through `COM32` (probe via `CreateFile`)

On Sam's machine specifically, the test board is an M5Stack Core2 and
shows up as `/dev/cu.usbserial-5A490687341`. Don't hard-code that —
prefer a CLI argument or a `ls /dev/cu.*` scan.

For a list-and-pick UX, copy what the X-Plane plugin does: enumerate
every known prefix on each platform and present them as a menu. See
`software/OnSpeed-XPlane-Plugin/src/SerialPortList.cpp` for the canonical
implementation.

## Baud and line settings

**115200 8N1.** No negotiation, no auto-baud, no flow control. The
firmware-side configuration is in `software/OnSpeed-M5-Display/src/SerialRead.cpp`:

```cpp
Serial.begin(115200);                                        // USB-CDC path
Serial2.begin(115200, SERIAL_8N1, PORTC_RX, PORTC_TX, false); // UART path
```

Both endpoints share the same baud, so the same producer code drives
either transport.

POSIX serial setup for a host bridge:

```c
int fd = open(path, O_WRONLY | O_NOCTTY | O_NDELAY);
fcntl(fd, F_SETFL, 0);   // clear O_NDELAY so writes block on the queue
struct termios tio;
tcgetattr(fd, &tio);
cfmakeraw(&tio);
cfsetspeed(&tio, B115200);
tio.c_cflag |= (CLOCAL | CREAD);
tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
tio.c_cflag |= CS8;
tcsetattr(fd, TCSANOW, &tio);
```

The `O_NDELAY` clear matters: a USB serial endpoint can be slow if the
M5 isn't draining (e.g., it's still on the splash screen), and writes
need to either block or get short-write handled. Don't lose frames
silently.

## The Data Source menu — the most likely "why doesn't it work"

PR #569 (merged 2026-05-18) added a Settings → Data Source menu with
three options: **AUTO / UART / USB**. This is persisted in NVS under
the `DataSource` key. The runtime late-binding that used to flip the
M5 onto USB-CDC mid-session if `#1` bytes arrived **was removed in the
same PR**. The Data Source has to be set before bytes start flowing.

- **AUTO** (default on fresh NVS) — probes USB-CDC for 2 seconds on
  boot, then UART (Port C, 5 sec each polarity). Bytes flowing on
  USB-CDC during the 2-second window switch the M5 onto USB. **If your
  host program isn't streaming yet when the M5 boots, AUTO will fall
  through to UART and the only fix is to power-cycle the M5.**
- **UART** — Serial2 only, never reads USB. Use when the M5 is on a
  Port C wire in the airplane.
- **USB** — Serial only, never probes UART. Sticky, recommended for
  bench / desktop work where the M5 is always tethered.

For a development workflow where you launch the host program by hand
after the M5 is already booted, **tell the user to set the menu to
USB**. This is the only configuration that works no matter when bytes
start arriving.

How to access the menu:

- **M5Stack Basic / Core2** — hold the center button (BtnB) for ~600 ms
  while the M5 is in any of the five live modes. Cycle through rows
  with BtnA / BtnC, activate with BtnB.
- **huVVer-AVI** — hold the left button (☐) for ~600 ms; activate
  with the right button (○).

In the menu, navigate to **Data Source**, click activate, and watch
the value cycle `AUTO → UART → USB → AUTO`. Long-hold BtnB / click ☐
to exit. Value persists across power cycles.

End-user docs covering this: `docs/site/docs/xplane/m5-tethered.md`.

## Frame cadence

Drive at 20 Hz (50 ms period). The constant is
`kDisplayFramePeriodMs = 50` in `DisplaySerial.h`. The M5's parser
expects fresh frames within 300 ms (`kSerialDataFreshThresholdMs` in
`SerialRead.cpp`) — drift below ~3 Hz and the M5 paints a "NO DATA"
overlay. Drift above 20 Hz and the M5 doesn't care, but you're wasting
bandwidth.

A simple dispatch-timer or `usleep(50000)` loop is fine. The wire is
ASCII, the frame is bytewise-aligned, and the producer-side firmware
itself just hits the wire once per loop tick — there's no clever
buffering on either end.

## Smoke test the frame builder before flashing anything to hardware

Every host bridge that reimplements `BuildDisplayFrame` in another
language should run a one-shot smoke test against a known input to
confirm:

1. Total length is 77 bytes
2. Bytes 0–1 are `#1`
3. Bytes 75–76 are CR LF (0x0D 0x0A)
4. The two ASCII hex digits at offsets 73–74 self-consistently match
   the byte sum over offsets 0–72 masked to 8 bits

If those four checks pass, the M5's parser will accept the frame. The
M5 firmware's `test/test_display_serial/` unit tests pin the same
invariants on the C++ side.

If you want a stronger check, pipe your frame into the firmware's own
parser. The native test environment at
`pio test -e native -f test_display_serial` builds against `onspeed_core`
on the host and can round-trip a frame byte-stream through
`ParseDisplayFrame()`. Faster than flashing an M5 and squinting at
the screen.

## What to fill in when you don't have all the fields

For a partial-data feeder (AirPods → attitude only, log replay →
no live tone state, etc.), the M5 has well-defined "this field is
not available" behaviors. Use them:

- **No air data?** Set `iasValid = false` on the builder input —
  `BuildDisplayFrame` writes `9999` into the IAS slot and `0` into
  percentLift, and the M5 renders `--` for both IAS and percentLift on
  every mode that displays them. Don't try to fake an IAS of 0 — the
  M5 will believe it.
- **No AOA configuration?** Set the band-edge percents
  (`tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`,
  `stallWarnPctLift`) to 0. The index bar collapses to an empty scale
  but the rest of the panel still renders.
- **No flap input?** Set `flapsDeg = 0`, `flapsMinDeg = 0`,
  `flapsMaxDeg = 0`. The flap dial reads as "clean" and doesn't
  contribute to band-edge interpolation.
- **No OAT?** Set `oatC = -99` (any value at the edge of the range).
  PR #575 added consumer-side gating so OAT-disabled frames render
  cleanly.
- **No turn rate / G load / VSI?** Zero. The corresponding gauges
  show needle-at-center and that's correct for a stationary
  bench / sim aircraft.

The pattern is: leave everything but the fields you actually have
zeroed. The wire format is forgiving as long as the checksum is right.

## Troubleshooting

### M5 stays on splash / "No Serial Stream Detected"

Most likely: Data Source is set to UART (or AUTO + you started
streaming after the 2-second window passed). Set it to USB explicitly.
See the menu instructions above.

Next: confirm the M5 is on the OnSpeed firmware, not stock M5Stack
demo. The OnSpeed splash with the gauge logo (~3 seconds at boot)
should appear before any wire data is sought.

### Frames flowing but the M5 ignores them

Almost always a checksum bug. Run the four-check smoke test above
locally. If your builder is in Swift / Python and uses `String(format:)`
or `%` formatting, watch out for signed-zero in `%+04d` — `-0` is not
a valid wire value (the firmware emits `+000`).

Next likely: wrong frame length. v4.21 was 74 bytes; v4.22 was 76;
v4.23 (current) is 77. If you have an older host program tucked away,
re-derive the layout from `DisplaySerial.h` rather than blindly using
an old known-good frame.

### M5 lights up briefly then disconnects

On Linux: ModemManager grabbed the port. Disable it for the M5's
vendor/product ID or uninstall it.

On macOS: rare, but a Bluetooth-paired device using the same chipset
ID can cause this. Unpair / unplug the conflicting device.

### Mid-session flip from UART to USB doesn't work

Expected — PR #569 removed runtime late-binding. The Data Source is
read at `serialSetup()` time only. To switch sources mid-session you
have to reboot the M5 after changing the menu.

## Reference layout

The 77-byte frame, copied verbatim from `DisplaySerial.h`:

```
Offset  Width  Field               Format   Scale   Notes
------  -----  ------------------  -------  ------  ----------------------
 0       2     magic               literal  —       "#1"
 2       4     pitchDeg            %+04d    ×10     signed
 6       5     rollDeg             %+05d    ×10     signed
11       4     iasKt               %04u     ×10     9999 = sentinel
15       6     paltFt              %+06d    ×1      signed
21       5     turnRateDps         %+05d    ×10     signed
26       3     lateralG            %+03d    ×100    body-frame, signed
29       3     verticalG           %+03d    ×10     signed
32       3     percentLift         %03u     ×10     tenths of percent
35       4     vsiFpm10            %+04d    ×1      vsi/10
39       3     oatC                %+03d    ×1      signed
42       4     flightPathDeg       %+04d    ×10     signed
46       3     flapsDeg            %+03d    ×1      signed
49       2     tonesOnPctLift      %02u     ×1      0–99
51       2     onSpeedFastPctLift  %02u     ×1      0–99
53       2     onSpeedSlowPctLift  %02u     ×1      0–99
55       2     stallWarnPctLift    %02u     ×1      0–99
57       3     flapsMinDeg         %+03d    ×1      signed
60       3     flapsMaxDeg         %+03d    ×1      signed
63       4     gOnsetRate          %+04d    ×100    signed
67       2     spinRecoveryCue     %+02d    ×1      −9..+9
69       2     dataMark            %02u     ×1      0–99
71       2     pipPctLift          %02u     ×1      0–99
73       2     checksum            ASCII hex        sum of bytes 0–72 & 0xFF
75       2     terminator          CR LF    —       0x0D 0x0A
```

If you ever need to bump the layout for a v4.24 wire change, the
single PR that does so updates `DisplaySerial.h`, all
host-side bridges that mirror the layout, and the doc at
`docs/site/docs/reference/serial-protocol.md` — anything that drifts
out of step quietly corrupts the M5's display.
