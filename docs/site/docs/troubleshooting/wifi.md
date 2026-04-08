# Troubleshooting: WiFi Issues

Can't connect to the OnSpeed WiFi network, or the web pages aren't loading.

## Can't See the "OnSpeed" Network

### Controller Not Powered

The WiFi AP takes ~10 seconds after power-on to start. Verify:

- Power is connected and the circuit breaker is on
- The heartbeat LED is blinking (confirms the controller is running)

### Too Far Away

The ESP32-S3 WiFi has limited range — typically 10–30 feet in open air, less through aircraft structure (metal fuselage, instrument panel).

- Move your device closer to the controller
- Try from inside the cockpit if the controller is behind the panel

### WiFi Interference

If you're at an airport or hangar with many WiFi networks, interference can make the OnSpeed AP hard to find.

- Try scanning for networks multiple times
- Make sure you're looking for `OnSpeed` (exact spelling, capital O and S)

## Can't Connect (Wrong Password)

The default WiFi credentials are:

- **SSID**: `OnSpeed`
- **Password**: `angleofattack`

The password is case-sensitive and all lowercase.

## Connected But Pages Won't Load

### Try the IP Address

If `http://onspeed.local` doesn't work, try the direct IP:

**`http://192.168.0.1`**

The mDNS name doesn't work on all devices (especially older Android devices).

### Browser Caching

If you recently updated firmware, your browser may be serving cached old pages:

- Try a hard refresh (Ctrl+Shift+R or Cmd+Shift+R)
- Try an incognito/private browsing window
- Clear your browser cache for the OnSpeed IP

### Too Many Connections

The ESP32's web server can handle a limited number of simultaneous connections. If you have multiple devices connected, try disconnecting all but one.

### Pages Load Slowly

The ESP32 serves pages over WiFi with limited bandwidth. Large pages (especially the calibration wizard with its JavaScript libraries) may take several seconds to load.

- Be patient — wait for the page to fully load before interacting
- Stay close to the controller (stronger signal = faster transfer)
- Close other browser tabs/apps using the OnSpeed WiFi

## WiFi Reflash

If the firmware is corrupted and you can't access the web interface, you can reflash firmware via:

1. **USB serial** — connect a USB cable and use `esptool` to flash a new firmware binary
2. **OTA over WiFi** — if the WiFi AP is still working, navigate to `/upgrade` and upload a new firmware binary

See [Flashing Firmware](../software/flashing.md) for USB-based recovery.
