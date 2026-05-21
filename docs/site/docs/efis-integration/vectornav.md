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
| **Time** | UTC time from GPS (millisecond resolution) |
| **Wind (derived)** | Horizontal speed/direction and vertical component, solved on-board from GPS ground velocity, VN-300 attitude, and OnSpeed TAS |

All fields are logged to the SD card with `vn` prefix (e.g., `vnPitch`, `vnRoll`, `vnGnssLat`).

## Wind columns

`vnWindSpd` (kt), `vnWindDir` (degrees), and `vnWindVertical` (kt, positive = updraft) are derived columns: the firmware solves the wind triangle each time a VN-300 frame arrives, using the GPS ground velocity (`vnGnssVelNed*`), the VN-300's yaw/pitch, and OnSpeed's own TAS. Cells are empty when there's no valid solution (below ~30 KIAS, no GPS fix, or any NaN input).

The wind "from" direction is in the **same frame as `vnYaw`**. The analysis workbook treats `vnWindDir` as **true** north; for that to hold, the VN-300 must be configured with WMM declination via its `ReferenceVectorConfiguration` register. OnSpeed does not apply declination correction itself — a misconfigured VN-300 will deliver a wind direction off by local declination (10–15° in the western US).

TAS accuracy depends on a valid OAT source. With no OAT, TAS falls back to IAS and the wind solution will be slightly hot at altitude.

## Configuration

OnSpeed expects the VN-300 to send a **specific binary output packet** at 115200 baud. The parser validates the 8-byte header and rejects packets that don't match — so the unit must be configured to emit exactly the groups and fields below before OnSpeed will see any data.

### Required output packet

| Property | Value |
|---|---|
| Output port | Serial 1 |
| Baud rate | 115200 |
| Async rate | 50 Hz (IMU rate 800 Hz ÷ divisor 16) |
| Packet size | 127 bytes (header + 117 byte payload + 2 byte CRC) |
| Groups enabled | **Common + GPS + Attitude** |
| Common fields | AngularRate, Position (lat/lon/alt), Velocity (NED), Accel — bitmask `0x01E0` |
| GPS fields | UTC, Fix, VelNed — bitmask `0x0091` |
| Attitude fields | YawPitchRoll, LinearAccelBody, YprU — bitmask `0x0142` |

If the running config doesn't match exactly, OnSpeed silently drops every packet — `vn*` log columns stay empty.

### Configure via VectorNav Control Center (recommended)

The Control Center GUI ships with the VN-300. Connect over USB and:

1. **Binary Async Output 1** → enable.
2. **Async Mode**: `Serial 1`. **Rate Divisor**: `16`.
3. Tick **Common** group, then under Common enable: AngularRate, Position, Velocity, Accel.
4. Tick **GPS** group, then enable: UTC, Fix, VelNed.
5. Tick **Attitude** group, then enable: YawPitchRoll, LinearAccelBody, YprU.
6. Hit **Apply**, then **Write Settings to Non-Volatile Memory** so the config survives power cycles.
7. Set **Serial 1 Baud Rate** to `115200` (this is the factory default — leave it unless you've changed it).
8. Under **Reference Vector Configuration**, ensure **WMM declination is enabled** and the magnetic reference is current. Without this, `vnYaw` is magnetic and the wind-direction columns will be off by local declination (10-15° in the western US — see [Wind columns](#wind-columns) above).

### Configure by sending raw register commands

The same config can be sent over the VN-300's serial port (or USB) as ASCII commands. Each is a single line ending with `\r\n`. Send these in order, waiting for the `$VNRRG,…` ACK between each:

```
$VNWRG,5,115200*68
$VNWRG,75,1,16,19,01E0,0091,0142*31
$VNWNV*57
```

What each line does:

- **`$VNWRG,5,115200`** — writes register 5 (Serial Baud Rate, port 1) to 115200 baud. Skip if your unit is already at 115200; sending it at the wrong current baud just times out without harm.
- **`$VNWRG,75,1,16,19,01E0,0091,0142`** — writes register 75 (Binary Output 1):
    - `1` = AsyncMode (output on serial port 1)
    - `16` = RateDivisor (800 Hz IMU clock / 16 = 50 Hz output rate)
    - `19` = OutputGroup bitmask (hex: `0x19` = Common + GPS + Attitude)
    - `01E0` = Common-group field bitmask (LE 16-bit hex)
    - `0091` = GPS-group field bitmask
    - `0142` = Attitude-group field bitmask
- **`$VNWNV`** — persists all current settings to non-volatile memory. Without this, every power cycle reverts to factory defaults.

The trailing `*XX` is the NMEA-style XOR checksum of every character between `$` and `*`. The values above are precomputed; if you modify any command parameter you have to recompute the checksum or VectorNav will reject the line.

### Higher output rates (advanced)

The VN-300 IMU runs at 800 Hz internally. RateDivisor=16 gives 50 Hz, which is what OnSpeed expects today. Going faster (e.g., divisor=2 → 400 Hz) requires bumping the serial baud well above 115200 — at 115200 the wire saturates near 75 Hz for our 127-byte packet. 921600 baud gives comfortable headroom for 400 Hz output, but **the OnSpeed firmware currently opens the EFIS port at 115200** and would need a matching change before higher rates work. There's no user-facing config knob for this yet.

### Then in OnSpeed

1. Set **EFIS Type** to `VN-300` in the OnSpeed web interface.
2. Save and reboot.
3. Verify the LiveView page shows non-zero `vnPitch`, `vnRoll`, `vnYaw`, and `vnGnssLat`/`vnGnssLon` once GPS fixes. If those stay zero, the binary output groups aren't matching — re-check the Control Center field selection against the table above.
