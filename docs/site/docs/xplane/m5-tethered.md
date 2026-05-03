# Tethering a Physical M5

The plugin can drive a USB-tethered M5Stack with the same wire frames
that drive the embedded indexer. A Core2 plugged into the sim PC
behaves the same way it behaves plugged into the panel-mounted Gen3.

## Setup

1. Flash the [`OnSpeed-M5-Display`](../installation/external-display.md)
   firmware onto your M5Stack (Basic or Core2 — same source, separate
   build target). Any Gen3-compatible M5 build works.
2. Plug the M5 into a USB-C port on the sim PC.
3. In X-Plane: **Plugins → Fly On Speed → Serial output → \<port path\>**.
   Pick the port that corresponds to your M5.

The plugin opens the port at 115200 8N1 — the same baud rate and
framing the firmware's display-serial output uses. From there, every
display-serial frame the embedded indexer consumes is also pushed out
the wire.

## Picking the port

The serial submenu lists every USB-CDC port the OS exposes:

- **macOS** — `/dev/cu.usbmodem*`, `/dev/cu.usbserial*`
- **Linux** — `/dev/ttyACM*`, `/dev/ttyUSB*`
- **Windows** — `COM3` through `COM256`

The M5 typically shows up as the only newly-appeared entry after you
plug it in. If the list is empty when you open the menu, the OS
hadn't enumerated the device yet — close the menu, click
**Refresh ports**, and reopen.

The selected port path is persisted in the per-aircraft `.prf` file
(see [Per-Aircraft Settings](settings.md)). Loading the same aircraft
later reopens the same port automatically. Pick **Off (no serial
output)** to stop streaming and clear the saved path.

## Verifying it works

After selecting the port, the M5 should start drawing the same
indexer the embedded window draws. If the M5 was running its
"waiting for serial" splash, that goes away within ~1 second of the
first frame arriving.

The plugin emits frames at 20 Hz — same rate as the firmware. There
is no acknowledgment / handshake; the M5 just consumes whatever
arrives. If the M5 freezes mid-flight, unplug and replug the USB
cable — the plugin retries the open every two seconds while the
saved port is still configured, so the M5 picks back up automatically
once it re-enumerates. To stop streaming entirely, pick **Off (no
serial output)** from the menu.

## Troubleshooting

### Port doesn't appear in the menu

- Confirm the M5 is enumerating at the OS level. On macOS:
  `ls /dev/cu.usbmodem*`. On Linux: `dmesg | tail` after plugging
  in. On Windows: Device Manager → Ports.
- Some M5 firmware builds present as a different USB chipset
  (CP2104 on Basic, CH9102F on Core2). Both register as USB-CDC and
  show up as `cu.usbmodem*` (or `ttyACM*` / `COMn`); if the OS
  exposes them as `cu.usbserial*` instead, they still appear in the
  refresh list — the plugin enumerates both prefixes on macOS.
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
