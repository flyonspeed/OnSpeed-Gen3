# Tethering a Physical M5

The plugin can drive a USB-tethered M5Stack with the same wire frames
that drive the embedded indexer. A Core2 plugged into the sim PC
behaves the same way it behaves plugged into the panel-mounted Gen3.

This is the right setup for pilots who want to fly the sim with the
real M5 indexer in their hand instead of (or alongside) the embedded
indexer window.

## What you need

- An M5Stack Basic or Core2 with the OnSpeed-M5-Display firmware
  flashed (the same firmware you'd use in the airplane).
- A USB-C cable to the sim PC.
- The X-Plane plugin installed (see [Install](install.md)).

## Step 1 — Flash M5 firmware

The M5 firmware needs the USB-CDC support that landed in the same PR
as this feature. If you've flashed your M5 from a build older than
that, reflash from current `master`.

From a terminal on the sim PC, with the M5 plugged in via USB:

```bash
cd software/OnSpeed-M5-Display
pio run -e m5stack-core2 -t upload    # M5Stack Core2
# or
pio run -e m5stack-core-esp32 -t upload   # M5Stack Basic
```

PlatformIO auto-detects the connected device. After flashing, the M5
reboots into the OnSpeed splash, then enters serial-detect mode.

## Step 2 — What the M5 shows during boot

After flash + reboot, the M5 cycles through three states:

1. **OnSpeed splash** — color logo, ~3 seconds.
2. **"Looking for Serial data / Please wait..."** — white text on
   black. The firmware probes USB-CDC first (2 seconds, only if the
   USB host is sending traffic), then the three Port C UART
   variants (5 seconds each). Total ~17 seconds of probing.
3. **"No Serial Stream Detected / Is OnSpeed running?"** — red text.
   Reached if the firmware found nothing in the 30-second probe
   window.

If you launch X-Plane and start streaming before the probe ends, the
firmware switches into USB-CDC mode immediately. If you're slow and
hit the "No Serial" splash, that's fine too — the firmware also
late-binds at runtime: any time bytes start arriving on USB-CDC with
the `#1` frame-start signature, the M5 switches into USB-CDC mode and
starts displaying without needing a reboot.

## Step 3 — Pick the port in X-Plane

In X-Plane: **Plugins → Fly On Speed → Serial output → \<port path\>**.
Pick the port that corresponds to your M5.

The plugin opens the port at 115200 8N1 — the same baud rate the
firmware's display-serial output uses. From there, every display-
serial frame the embedded indexer consumes is also pushed out the
wire.

Within ~1 second the M5 should switch from its splash / no-data
state to displaying live X-Plane state.

## Picking the port

The serial submenu lists every USB-CDC port the OS exposes:

- **macOS** — `/dev/cu.usbmodem*`, `/dev/cu.usbserial*`,
  `/dev/cu.SLAB_USBtoUART*` (SiLabs CP2104), `/dev/cu.wchusbserial*`
  (WCH CH9102F)
- **Linux** — `/dev/ttyACM*`, `/dev/ttyUSB*`
- **Windows** — `COM1` through `COM32` (probed via `CreateFile`)

The M5 typically shows up as the only newly-appeared entry after you
plug it in. If the list is empty when you open the menu, the OS
hadn't enumerated the device yet — close the menu, click
**Refresh ports**, and reopen.

The selected port path is persisted in the per-aircraft `.prf` file
(see [Per-Aircraft Settings](settings.md)). Loading the same aircraft
later reopens the same port automatically. Pick **Off (no serial
output)** to stop streaming and clear the saved path.

## Step 4 — Use the indexer in the sim

With data flowing, the M5 behaves identically to its in-airplane
configuration:

- **BtnA (left)** — display brightness down.
- **BtnB (middle)** — cycle display modes (AOA + numbers, attitude,
  narrow AOA, decel gauge, G history).
- **BtnC (right)** — display brightness up.

See [Using the indexer](indexer.md) for the per-mode descriptions.
Every mode is the same as the in-airplane M5; the data behind it is
just X-Plane's flight model instead of your AOA sensor.

The plugin emits frames at 20 Hz — same rate the firmware emits in
the airplane.  The display behaves accordingly: smooth chevron
sweeps during AOA changes, ball-and-pitch reactivity matching what
you'd see in real flight.

## Reconnecting after a disconnect

If the M5 freezes or the USB cable bumps loose: unplug and replug.
The plugin retries the open every two seconds while the saved port
is still configured, so the M5 picks back up automatically once it
re-enumerates — no menu action required.

To stop streaming entirely, pick **Off (no serial output)** from the
menu.

## Persistence across sessions

The selected port path is persisted in the per-aircraft `.prf` file
(see [Per-Aircraft Settings](settings.md)). Loading the same
aircraft later reopens the same port automatically. You don't need
to re-pick the port on every X-Plane launch — only when the port
path changes (M5 unplugged into a different USB port, fresh OS USB
enumeration after a reboot, etc.).

The plugin's auto-retry handles routine plug-out / plug-in; the
saved path stays.  Only an explicit **Off** clears it.

## Troubleshooting

### Port doesn't appear in the menu

- Confirm the M5 is enumerating at the OS level. On macOS:
  `ls /dev/cu.usbmodem*`. On Linux: `dmesg | tail` after plugging
  in. On Windows: Device Manager → Ports.
- M5Stack Basic and Core2 use external USB-to-UART bridge chips
  (CP2104 on Basic, CH9102F on some Core2 batches). These enumerate
  on macOS as `cu.usbserial-*`, `cu.SLAB_USBtoUART*`, or
  `cu.wchusbserial*` — NOT as `cu.usbmodem*` (that prefix is
  reserved for native USB-CDC peripherals like the ESP32-S3).
  The plugin scans all four prefixes on macOS, so the device gets
  picked up regardless of chipset.
- Click **Refresh ports** after replugging.

### Port appears but the M5 stays blank

- Confirm the M5 is running OnSpeed-M5-Display firmware, not stock
  M5Stack demo software. Boot the M5 — the OnSpeed splash screen
  should appear before the wire data starts.
- Confirm baud rate. The firmware expects 115200 8N1; the plugin
  opens at the same. There is no negotiation, so a mismatched baud
  rate produces a dead link with no error.

### Port opens, M5 lights up briefly, then disconnects

Common on Linux when ModemManager grabs the port. Disable it for
the M5's vendor:product ID, or uninstall ModemManager if you don't
need it.

### Selecting a port silently does nothing

Open the X-Plane `Log.txt` (sim root) and search for
`FlyOnSpeed:`. The plugin logs open / close transitions and any
`open()` / `CreateFile()` errors. The most common cause is a stale
saved port path — the previously connected M5 is on a different
node now. Pick **Off**, then re-pick the current port from the
freshly enumerated list.
