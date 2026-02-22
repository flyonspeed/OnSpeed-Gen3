# Optional: Boom Probe

A boom probe is a reference AOA measurement device used for testing and calibration validation. It provides independent alpha (AOA) and beta (sideslip) measurements.

## When You'd Use One

- **Calibration validation** — comparing OnSpeed's computed AOA against a reference measurement
- **Research installations** — flight test data collection
- **Development** — firmware development and algorithm testing

Most normal installations do **not** need a boom probe.

## Wiring

- **OnSpeed RX**: GPIO 3 (BOOM_SER_RX)
- **OnSpeed TX**: GPIO 8 (BOOM_SER_TX, normally not used)
- **Baud rate**: 115200, 8N1

## Data Provided

When connected, the boom probe provides:

| Field | Units | Description |
|-------|-------|-------------|
| Static pressure | PSI | Reference static pressure |
| Dynamic pressure | PSI | Reference pitot pressure |
| Alpha | degrees | Angle of attack |
| Beta | degrees | Sideslip angle |
| IAS | knots | Indicated airspeed |

This data is logged alongside OnSpeed's own measurements for post-flight comparison.

## Configuration

1. Set **Boom** to `Enabled` in the web interface configuration
2. Optionally enable **Boom Checksum** validation (`bBoomChecksum`) to reject corrupted packets

Both settings are runtime configuration toggles — no firmware reflash needed.

!!! note "Serial port sharing"
    The boom probe shares Serial1 with the display serial interface. Both can be active, but simultaneous transmission from both could cause conflicts.
