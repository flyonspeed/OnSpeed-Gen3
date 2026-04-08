# Dynon D10/D100

The Dynon D10 and D100 are legacy Dynon EFIS units. OnSpeed supports their serial output format.

## Serial Setup

- **Baud rate**: 115200 (must be configured on the Dynon)
- **Protocol**: Similar text format to SkyView
- **EFIS Type setting**: `DYNOND10`

## Wiring

Same as [Dynon SkyView](dynon-skyview.md) — connect the Dynon serial TX to OnSpeed RX (GPIO 11).

## Data Available

The D10/D100 provides similar data to the SkyView: IAS, pitch, roll, altitude, and heading. Some fields available on the SkyView (like Percent Lift or engine data) may not be present depending on your specific D10/D100 configuration.

## Configuration

1. Set **EFIS Type** to `DYNOND10` in the OnSpeed web interface
2. Configure the D10/D100 serial output for 115200 baud
3. Verify data flow with the `SENSORS` console command

!!! note "Check your Dynon manual"
    Serial output configuration varies between D10 and D100 models. Consult your specific unit's installation manual for serial port setup instructions.
