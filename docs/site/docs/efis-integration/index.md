# EFIS Integration

Connecting OnSpeed to your EFIS (Electronic Flight Instrument System) significantly improves system performance. The EFIS provides calibrated IAS, OAT, attitude data, and more — data that OnSpeed uses for better AOA computation and density corrections.

## Why Connect Your EFIS?

| With EFIS | Without EFIS |
|-----------|-------------|
| Calibrated IAS from pitot system | IAS from OnSpeed's own pitot sensor (still works, but less precise) |
| OAT from EFIS probe | Requires DS18B20 sensor, or no temperature correction |
| Attitude from EFIS AHRS | Uses OnSpeed's internal IMU only |
| Altitude from EFIS | Computed from static pressure sensor |
| Fuel, RPM, MAP data logged | Not available |

OnSpeed works without an EFIS — but the calibration and accuracy are better with one.

## Supported EFIS Types

| EFIS | Protocol | Status |
|------|----------|--------|
| **[Dynon SkyView / HDX](dynon-skyview.md)** | Text serial (`!1` ADAHRS + `!3` EMS) | Fully supported — most common |
| **[Dynon D10/D100](dynon-d10.md)** | Text serial | Supported |
| **[Garmin G5](garmin-g5.md)** | Text serial | Supported |
| **[Garmin G3X / G3X Touch](garmin-g3x.md)** | Text serial | Supported |
| **[MGL iEFIS / Odyssey](mgl.md)** | Binary (iLink protocol) | Supported |
| **[VectorNav VN-300](vectornav.md)** | Binary (127-byte packets) | Supported — research/reference use |
| **[No EFIS (Standalone)](standalone.md)** | — | Works, with limitations |

## Common Serial Settings

All EFIS types use the same serial settings on the OnSpeed side:

- **Baud rate**: 115200
- **Data bits**: 8
- **Parity**: None
- **Stop bits**: 1
- **Flow control**: None

The EFIS must be configured to output serial data at **115200 baud**. This is not always the default — check your EFIS manual and the specific integration page.

## Wiring

Connect your EFIS serial **TX** (transmit) output to the OnSpeed **RX** (receive) input:

- **OnSpeed RX pin**: GPIO 11 (through ADM3202 RS-232 level shifter on the PCB)
- **OnSpeed TX pin**: GPIO 46 (not connected — OnSpeed only receives from the EFIS)

!!! warning "TX to RX — not TX to TX"
    Serial connections are crossed. Connect the EFIS **transmit** pin to the OnSpeed **receive** pin. This is the most common wiring mistake.

## Configuration

After wiring, set the EFIS type in the OnSpeed web interface:

1. Connect to OnSpeed WiFi (`OnSpeed` / `angleofattack`)
2. Navigate to the configuration page
3. Set **EFIS Type** to match your hardware
4. Save and reboot

!!! danger "EFIS type must match your hardware"
    Setting the wrong EFIS type means OnSpeed will try to parse serial data in the wrong format. You'll get no EFIS data or — worse — garbled data. Always verify the EFIS type matches what you've wired.

## Verifying EFIS Data

After configuration, verify data is flowing:

1. Use the `SENSORS` console command (via USB serial) — EFIS fields should show non-zero values
2. Check the web interface live view — EFIS IAS, OAT, and attitude should display
3. In the log files, check the `efisAge` column — this should show small values (< 1000 ms). Large values mean data isn't arriving.
