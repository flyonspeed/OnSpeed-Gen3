# Garmin G5

The Garmin G5 is a compact, standalone electronic flight instrument. OnSpeed can receive flight data from the G5's serial output.

## Serial Setup

- **Baud rate**: 115200
- **Protocol**: Text serial
- **EFIS Type setting**: `GARMING5`

## Wiring

Connect the G5 serial **TX** output to OnSpeed **RX** (GPIO 11). Consult the G5 installation manual for the serial output pin location on the rear connector.

## Data Available

The G5 provides:

- IAS
- Pitch and roll angles
- Pressure altitude
- Vertical speed

!!! note "Limited data compared to SkyView"
    The G5 is a simpler instrument than the Dynon SkyView. Some data fields (OAT, engine parameters, percent lift) may not be available depending on the G5 configuration.

## Configuration

1. Configure the G5 serial output (consult the Garmin G5 installation manual)
2. Set **EFIS Type** to `GARMING5` in the OnSpeed web interface
3. Save and reboot
4. Verify with the `SENSORS` console command

## Audio Routing with Garmin Audio Panel

If you have a Garmin audio panel (GMA 345, GTR 200), see [Audio Wiring](../installation/audio.md) for specific integration options with Garmin audio equipment.
