# Understanding OnSpeed Logs

OnSpeed records flight data to CSV files on the microSD card at **50 Hz** (50 samples per second).

## File Format

- **Format**: CSV with headers (comma-separated values)
- **Rate**: 50 Hz (one row every 20 ms)
- **Naming**: `log_NNN.csv` (sequential numbering)
- **Size**: A 1-hour flight produces approximately 50–100 MB of data

## Key Columns

### Core Flight Data

| Column | Units | Description |
|--------|-------|-------------|
| `timeStamp` | ms | Milliseconds since power-on |
| `IAS` | knots | Indicated airspeed |
| `Palt` | ft | Pressure altitude |
| `AngleofAttack` | degrees | Computed AOA (from pressure polynomial) |
| `DerivedAOA` | degrees | SmoothedPitch - FlightPath (AHRS-derived) |
| `flapsPos` | degrees | Detected flap position |
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
| `VSI` | fpm | Vertical speed (Kalman filtered) |
| `Altitude` | ft | Altitude (Kalman filtered) |

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
