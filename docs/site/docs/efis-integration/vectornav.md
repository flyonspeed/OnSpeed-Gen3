# VectorNav VN-300

The VectorNav VN-300 is a high-precision INS/GNSS unit used primarily in research and reference installations. It provides the most comprehensive data set of any supported EFIS type.

## When You'd Use This

The VN-300 is typically used for:

- **Flight test** — reference-grade attitude and position data
- **Algorithm development** — validating OnSpeed's internal AHRS against a known-good reference
- **Research installations** — academic or engineering flight data collection

Most normal OnSpeed installations use a Dynon, Garmin, or MGL instead.

## Serial Setup

- **Baud rate**: 921600
- **Protocol**: Binary (138-byte packets with CRC-16)
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
| **Time** | Per-sample TimeStartup (ns since VN-300 boot) and TimeGps (ns since GPS epoch 1980), plus a TimeStatus byte indicating which are valid |
| **Wind (derived)** | Horizontal speed/direction and vertical component, solved on-board from GPS ground velocity, VN-300 attitude, and OnSpeed TAS |

All fields are logged to the SD card with `vn` prefix (e.g., `vnPitch`, `vnRoll`, `vnGnssLat`).

The per-sample timestamps land in `vnTimeStartupNs` (always advancing) and `vnTimeGpsNs` (only valid once `vnTimeStatus & 0x02` is set — GPS week resolved). At the 400 Hz output rate each successive row carries a distinct timestamp ~2,500,000 ns (2.5 ms) apart.

## Wind columns

`vnWindSpd` (kt), `vnWindDir` (degrees), and `vnWindVertical` (kt, positive = updraft) are derived columns: the firmware solves the wind triangle each time a VN-300 frame arrives, using the GPS ground velocity (`vnGnssVelNed*`), the VN-300's yaw/pitch, and OnSpeed's own TAS. Cells are empty when there's no valid solution (below ~30 KIAS, no GPS fix, or any NaN input).

The wind "from" direction is in the **same frame as `vnYaw`**. The analysis workbook treats `vnWindDir` as **true** north; for that to hold, the VN-300 must be configured with WMM declination via its `ReferenceVectorConfiguration` register. OnSpeed does not apply declination correction itself — a misconfigured VN-300 will deliver a wind direction off by local declination (10–15° in the western US).

TAS accuracy depends on a valid OAT source. With no OAT, TAS falls back to IAS and the wind solution will be slightly hot at altitude.

## Configuration

OnSpeed expects the VN-300 to send a **specific binary output packet** at 921600 baud. The parser validates the 10-byte header and rejects packets that don't match — so the unit must be configured to emit exactly the groups and fields below before OnSpeed will see any data.

### Required output packet

| Property | Value |
|---|---|
| Output port | Serial 1 |
| Baud rate | 921600 |
| Async rate | 400 Hz (IMU rate 400 Hz ÷ divisor 1) |
| Packet size | 138 bytes (10 byte header + 126 byte payload + 2 byte CRC) |
| Groups enabled | **Common + Time + GNSS1 + AHRS** |
| Common fields | TimeStartup, TimeGps, AngularRate, Position (lat/lon/alt), Velocity (NED), Accel — bitmask `0x01E3` |
| Time fields | TimeStatus — bitmask `0x0200` |
| GNSS1 fields | Fix, VelNed — bitmask `0x0090` |
| AHRS fields | YawPitchRoll, LinearAccelBody, YprU — bitmask `0x0142` |

If the running config doesn't match exactly, OnSpeed silently drops every packet — `vn*` log columns stay empty.

### Configure via VectorNav Control Center (recommended)

The Control Center GUI ships with the VN-300. Connect over USB and:

1. **Binary Async Output 1** → enable.
2. **Async Mode**: `Serial 1`. **Rate Divisor**: `1`.
3. Tick **Common** group, then under Common enable: TimeStartup, TimeGps, AngularRate, Position, Velocity, Accel.
4. Tick **Time** group, then enable: TimeStatus.
5. Tick **GNSS1** group, then enable: Fix, VelNed.
6. Tick **AHRS** group, then enable: YawPitchRoll, LinearAccelBody, YprU.
7. Hit **Apply**, then **Write Settings to Non-Volatile Memory** so the config survives power cycles.
8. Set **Serial 1 Baud Rate** to `921600`. The 138-byte frame at 400 Hz needs 55.2 kB/s on the wire; 921600 baud runs at ~60% utilization.
9. Under **Reference Vector Configuration**, ensure **WMM declination is enabled** and the magnetic reference is current. Without this, `vnYaw` is magnetic and the wind-direction columns will be off by local declination (10–15° in the western US — see [Wind columns](#wind-columns) above).

### Configure by sending raw register commands

The same config can be sent over the VN-300's serial port (or USB) as ASCII commands. Each is a single line ending with `\r\n`. Send these in order, waiting for the ACK between each.

In VectorNav Control Center's terminal, the `*XX` placeholder works (VNCC computes the checksum for you):

```
$VNWRG,75,1,1,1B,01E3,0200,0090,0142*XX
$VNWRG,5,921600,1*XX
$VNWNV*57
```

In a plain serial terminal (minicom, screen, PuTTY) the checksum must be exact. The precomputed values are:

```
$VNWRG,75,1,1,1B,01E3,0200,0090,0142*50
$VNWRG,5,921600,1*7E
$VNWNV*57
```

What each line does:

- **`$VNWRG,75,1,1,1B,01E3,0200,0090,0142`** — writes register 75 (Binary Output 1):
    - `1` = AsyncMode (output on serial port 1)
    - `1` = RateDivisor (400 Hz IMU clock / 1 = 400 Hz output rate)
    - `1B` = OutputGroup bitmask (`0x1B` = Common + Time + GNSS1 + AHRS)
    - `01E3` = Common-group field bitmask (LE 16-bit hex)
    - `0200` = Time-group field bitmask
    - `0090` = GNSS1-group field bitmask
    - `0142` = AHRS-group field bitmask
- **`$VNWRG,5,921600,1`** — writes register 5 (Serial Baud Rate) to 921600 on port 1. **VNCC will lose the connection immediately after this command — reconnect at 921600 to continue.**
- **`$VNWNV`** — persists all current settings to non-volatile memory. Without this, every power cycle reverts to factory defaults.

The trailing `*XX` is the NMEA-style XOR checksum of every character between `$` and `*`. If you modify any command field, recompute the checksum (e.g. in Python: `c=0; [c:=c^b for b in payload.encode()]; print(f'{c:02X}')`). VectorNav silently ignores commands with a bad checksum.

### Coupling between firmware and VN-300

The VN-300 reconfiguration and the OnSpeed firmware version are tightly coupled:

- The **current firmware** (this release) expects the 138-byte / 400 Hz / 921600 format.
- **Older firmware** expected a 127-byte / 50 Hz / 115200 format with different field masks (no TimeStartup, no TimeGps, no TimeStatus — and `GNSS1.UTC` populated in place of those).

If you flash new firmware without reconfiguring the VN-300 (or vice versa), the OnSpeed VN-300 columns will silently stay empty. The two changes must happen in the same maintenance window. Order: VN-300 first, verify config survives a power cycle, then flash OnSpeed and reconnect.

### Then in OnSpeed

1. Set **EFIS Type** to `VN-300` in the OnSpeed web interface.
2. Save and reboot.
3. Verify the LiveView page shows non-zero `vnPitch`, `vnRoll`, `vnYaw`, and `vnGnssLat`/`vnGnssLon` once GPS fixes. If those stay zero, the binary output groups aren't matching — re-check the Control Center field selection against the table above.
4. Pull a short SD log and confirm `vnTimeStartupNs` is monotonically increasing by ~2.5 ms per row. That's the proof that per-sample timestamps are working.
