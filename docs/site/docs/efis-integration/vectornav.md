# VectorNav VN-300

The VectorNav VN-300 is a high-precision INS/GNSS unit used primarily in research and reference installations. It provides the most comprehensive data set of any supported EFIS type.

## When You'd Use This

The VN-300 is typically used for:

- **Flight test** — reference-grade attitude and position data
- **Algorithm development** — validating OnSpeed's internal AHRS against a known-good reference
- **Research installations** — academic or engineering flight data collection

Most normal OnSpeed installations use a Dynon, Garmin, or MGL instead.

## Serial Setup

- **Baud rate**: 115200
- **Protocol**: Binary (127-byte packets with CRC-16)
- **EFIS Type setting**: `VN-300`

## Data Available

The VN-300 provides the richest data set:

| Category | Fields |
|----------|--------|
| **Attitude** | Yaw, pitch, roll with sigma (uncertainty) estimates |
| **Angular rates** | Roll, pitch, yaw rates (rad/s) |
| **Velocities** | NED frame velocities (m/s) |
| **Accelerations** | Body frame and linear accelerations (m/s²) |
| **GNSS** | Latitude, longitude, GPS fix quality, GPS velocities |
| **Time** | UTC time from GPS |

All fields are logged to the SD card with `vn` prefix (e.g., `vnPitch`, `vnRoll`, `vnGnssLat`).

## Configuration

1. Configure the VN-300 binary output at 115200 baud (consult VectorNav documentation)
2. Set **EFIS Type** to `VN-300` in the OnSpeed web interface
3. Save and reboot
