# Garmin G3X / G3X Touch

The Garmin G3X and G3X Touch are full-featured experimental EFIS systems. OnSpeed can receive flight data from the G3X serial output.

## Serial Setup

- **Baud rate**: 115200
- **Protocol**: Text serial (G3X format)
- **EFIS Type setting**: `GARMING3X`

## Wiring

Connect the G3X serial **TX** output to OnSpeed **RX** (GPIO 11). The G3X has multiple serial ports — use one that isn't already assigned to autopilot, transponder, or other devices.

## Data Available

The G3X provides comprehensive flight data similar to the Dynon SkyView:

- IAS, TAS
- Pitch, roll, heading
- Pressure altitude, VSI
- G-loads
- OAT
- Engine data (if EIS connected)

## Configuration

1. Configure the G3X serial output port (consult the Garmin G3X installation manual)
2. Set **EFIS Type** to `GARMING3X` in the OnSpeed web interface
3. Save and reboot
4. Verify with the `SENSORS` console command

## Display Output to G3X

OnSpeed can optionally output data in G3X format via its display serial port (GPIO 10). Set **Serial Output Format** to `G3X` in the configuration if you want to send OnSpeed data to a G3X display input.
