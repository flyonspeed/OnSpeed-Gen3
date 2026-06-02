# Understanding OnSpeed Logs

OnSpeed records flight data to CSV files on the microSD card at **50 Hz** by default (configurable to 208 Hz via the web interface).

## File Format

- **Format**: CSV with headers (comma-separated values)
- **Rate**: 50 Hz default (one row every 20 ms); optionally 208 Hz (IMU rate)
- **Naming**: `log_NNN.csv` (sequential numbering). When a VN-300 EFIS provides a UTC timestamp, the file is renamed to `YYYY-MM-DD_NNN.csv` at close so the date travels with the file. Dynon, Garmin, and no-EFIS logs keep `log_NNN.csv`.
- **Size**: A 1-hour flight produces approximately 50–100 MB of data

### Log rotation on config change

A log file is internally consistent: the columns advertised in its header match every row in the file, and the row cadence (50 vs 208 Hz) stays constant from first row to last. To preserve that invariant, the firmware **closes the active log and opens a new one** if any of these settings change while logging is on:

| Setting changed | Why rotation happens |
|---|---|
| `Log Rate` (50 ↔ 208 Hz) | Row cadence changes; mixed cadence in one file confuses replay tools |
| `Read Boom` toggle | Boom columns appear/disappear in the row |
| `Read EFIS Data` toggle | EFIS columns appear/disappear in the row |
| EFIS type (e.g. Dynon ↔ VN-300) | EFIS column set differs (VN-300 has its own column block) |

When rotation fires, the success page shows: "The active log file was rotated because the change affects column set or sample cadence; subsequent rows go into a fresh `log_NNN.csv`." The closed file gets its sidecar `.meta` written and (if a VN-300 UTC date is available) is renamed to `YYYY-MM-DD_NNN.csv`. The new file gets the next sequence number and a fresh header.

**For post-flight analysis**: one flight that included a mid-flight config change produces multiple `log_NNN.csv` files. Each is a coherent segment with its own header. Stitch them together by timestamp (the `timeStamp` column is `millis()` since power-on, so it's continuous across files until reboot).

Changes that do NOT trigger rotation (the column set and cadence stay the same):

- Flap calibration adjustments (alpha_0, AOA setpoints, polynomial coefficients)
- AHRS algorithm choice (Madgwick ↔ EKF6)
- Audio settings, volume curves
- Aircraft Vno / Vfe / G-limit values

Column values (e.g. `DerivedAOA`) may shift subtly mid-file in those cases — downstream analysis tooling typically handles that gracefully.

### Metadata sidecar

Each `log_NNN.csv` is written alongside a `log_NNN.meta` plain-text sidecar. The firmware refreshes the sidecar every 30 seconds while the log is open and rewrites it once at close, so a flight that ends with a power yank still leaves a usable sidecar (worst case: the last 30 s of metadata is missing). The first 30 seconds of any flight are an exception — a power yank in that window leaves no sidecar at all, and the `/logs` page renders em-dashes for that flight rather than a misleading zero-valued line. One `key=value` per line:

```
duration_ms=1842113
row_count=92106
max_ias_kt=148
max_alt_ft=8420
firmware=4.20+1825dff
efis_type=dynon_skyview
time_of_day_start=14:32:08
gps_fix_seen=1
```

`time_of_day_start` is captured from any EFIS that publishes a clock (Dynon SkyView, VN-300, Garmin G3X, MGL). `utc_start` is VN-300-only. The `/logs` page reads the sidecar to populate the start-time, duration, max IAS, and max pressure altitude columns. The `max_alt_ft` value comes from the `Palt` column — every altitude OnSpeed records is pressure altitude (ISA 1013.25 hPa reference); there is no Kollsman/QNH correction.

## Key Columns

### Core Flight Data

| Column | Units | Description |
|--------|-------|-------------|
| `timeStamp` | ms | Milliseconds since power-on |
| `IAS` | knots | Indicated airspeed |
| `Palt` | ft | Pressure altitude |
| `AngleofAttack` | degrees | Computed AOA (from pressure polynomial) |
| `DerivedAOA` | degrees | SmoothedPitch - FlightPath (AHRS-derived) |
| `flapsPos` | degrees | Detected flap position (snapped to the nearest configured detent) |
| `OAT` | °C | Outside air temperature |
| `TAS` | knots | True airspeed |

### Pressure Sensors

| Column | Units | Description |
|--------|-------|-------------|
| `Pfwd` | counts | Raw pitot pressure (14-bit ADC: 0–16383) |
| `PfwdSmoothed` | PSI | Pitot pressure (EMA smoothed) |
| `P45` | counts | Raw AOA pressure (14-bit ADC) |
| `P45Smoothed` | PSI | AOA pressure (EMA smoothed) |
| `PStatic` | PSI | Static pressure |
| `CoeffP` | — | Pressure coefficient (P45 / Pfwd) |

### IMU Data

| Column | Units | Description |
|--------|-------|-------------|
| `VerticalG` | G | Vertical acceleration (body frame) |
| `LateralG` | G | Lateral acceleration (body frame) |
| `ForwardG` | G | Forward acceleration (body frame) |
| `RollRate` | deg/s | Roll angular rate |
| `PitchRate` | deg/s | Pitch angular rate |
| `YawRate` | deg/s | Yaw angular rate |
| `Pitch` | degrees | Pitch angle (AHRS output) |
| `Roll` | degrees | Roll angle (AHRS output) |
| `imuTemp` | °C | IMU sensor temperature |

### Computed Values

| Column | Units | Description |
|--------|-------|-------------|
| `EarthVerticalG` | G | Vertical G in earth frame |
| `FlightPath` | degrees | Flight path angle = arcsin(VSI/TAS) |
| `VSI` | fpm | Vertical speed, smoothed by the active AHRS algorithm's vertical channel |
| `Altitude` | ft | Pressure altitude smoothed by the active AHRS algorithm's vertical channel. Same quantity as `Palt` (column 7), smoothed with vertical accel. |

### EFIS Data (when connected)

For Dynon, Garmin, or MGL EFIS:

| Column | Units | Description |
|--------|-------|-------------|
| `efisIAS` | knots | IAS from EFIS |
| `efisPitch` | degrees | Pitch from EFIS |
| `efisRoll` | degrees | Roll from EFIS |
| `efisLateralG` | G | Lateral G from EFIS |
| `efisVerticalG` | G | Vertical G from EFIS |
| `efisPercentLift` | % | Percent lift / AOA from EFIS (0–99%) |
| `efisPalt` | ft | Pressure altitude from EFIS |
| `efisVSI` | fpm | Vertical speed from EFIS |
| `efisTAS` | knots | True airspeed from EFIS |
| `efisOAT` | °C | OAT from EFIS |
| `efisFuelRemaining` | gal | Fuel remaining |
| `efisFuelFlow` | gph | Fuel flow |
| `efisMAP` | inHg | Manifold pressure |
| `efisRPM` | rpm | Engine RPM |
| `efisPercentPower` | % | Percent power |
| `efisMagHeading` | degrees | Magnetic heading |
| `efisAge` | ms | Time since last EFIS packet |

For VectorNav VN-300, the columns use `vn` prefix instead (e.g., `vnPitch`, `vnRoll`, `vnGnssLat`, etc.) — see [CSV Log Columns Reference](../reference/log-columns.md) for the complete list.

### Boom Data (when connected)

| Column | Units | Description |
|--------|-------|-------------|
| `boomAlpha` | degrees | Reference AOA from boom |
| `boomBeta` | degrees | Sideslip from boom |
| `boomIAS` | knots | IAS from boom |
| `boomAge` | ms | Time since last boom packet |

### Other

| Column | Units | Description |
|--------|-------|-------------|
| `DataMark` | 0–99 | Pilot annotation mark (wraps at 100) |

### Tail-Optional (Format Version 2)

Appended at the very end of every row, after `CoeffP`. Older firmware
(format version 1) omits these columns entirely.

| Column | Units | Description |
|--------|-------|-------------|
| `flapsRawADC` | counts | Raw flap-lever pot ADC reading. Replay tools interpolate the L/Dmax pip across detent transitions using this value and the configured `<FLAPPOTPOSITIONS>` boundaries. |

## Loading Logs in Python

```python
import pandas as pd

df = pd.read_csv("log_001.csv")

# Plot IAS vs DerivedAOA
import matplotlib.pyplot as plt
plt.scatter(df["IAS"], df["DerivedAOA"], s=1, alpha=0.3)
plt.xlabel("IAS (knots)")
plt.ylabel("DerivedAOA (degrees)")
plt.show()
```

## SD Card Management

- **Capacity**: depends on your card size. A 32GB card holds hundreds of hours of flight data.
- **Format**: FAT32. Use the `FORMAT` console command or format on a computer.
- **When full**: the system stops logging. Download and delete old logs periodically.
