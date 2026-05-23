# CSV Log Columns Reference

Complete reference for all columns in OnSpeed SD card log files. Data is logged at 50 Hz by default (configurable to 208 Hz via the web interface).

Columns are written in four groups: (1) base core columns (`timeStamp` through `Roll`), (2) optional Boom and/or EFIS columns when those features are enabled, (3) derived core columns (`EarthVerticalG` through `CoeffP`), (4) tail-optional `flapsRawADC`. Parse by column name — direct index positions shift when optional blocks are enabled.

## Core Columns — Base

| Column | Units | Description |
|--------|-------|-------------|
| `timeStamp` | ms | Milliseconds since power-on |
| `Pfwd` | counts | Pitot pressure, bias subtracted (14-bit ADC) |
| `PfwdSmoothed` | counts | Pitot pressure after median + running-average smoothing |
| `P45` | counts | AOA pressure, bias subtracted (14-bit ADC) |
| `P45Smoothed` | counts | AOA pressure after median + running-average smoothing |
| `PStatic` | mbar | Static (barometric) pressure |
| `Palt` | ft | Pressure altitude (ISA 1013.25 hPa reference; no Kollsman/QNH correction). Computed each row from `PStatic`. |
| `IAS` | knots | Indicated airspeed |
| `AngleofAttack` | degrees | Computed AOA from pressure polynomial |
| `flapsPos` | degrees | Detected flap position (snapped to the nearest configured detent) |
| `DataMark` | 0–99 | Pilot data annotation (wraps at 100) |
| `OAT` | °C | Outside air temperature |
| `TAS` | knots | True airspeed |
| `imuTemp` | °C | IMU330 sensor temperature |
| `VerticalG` | G | Vertical acceleration (body Z-axis) |
| `LateralG` | G | Lateral acceleration (body Y-axis) |
| `ForwardG` | G | Forward acceleration (body X-axis) |
| `RollRate` | deg/s | Roll angular rate (gyro) |
| `PitchRate` | deg/s | Pitch angular rate (gyro) |
| `YawRate` | deg/s | Yaw angular rate (gyro) |
| `Pitch` | degrees | Pitch angle (AHRS output, smoothed) |
| `Roll` | degrees | Roll angle (AHRS output, smoothed) |

## Core Columns — Derived

Appended after any optional Boom/EFIS columns.

| Column | Units | Description |
|--------|-------|-------------|
| `EarthVerticalG` | G | Vertical acceleration in Earth frame |
| `FlightPath` | degrees | Flight path angle = arcsin(VSI/TAS) |
| `VSI` | fpm | Vertical speed (Kalman filtered) |
| `Altitude` | ft | Pressure altitude, Kalman-filtered. Same quantity as `Palt`, smoothed with vertical accel. Not MSL — ISA reference, no Kollsman/QNH correction. |
| `DerivedAOA` | degrees | SmoothedPitch − FlightPath |
| `CoeffP` | — | Pressure coefficient (P45/Pfwd) |

## Boom Probe Columns (When Boom Enabled)

| Column | Units | Description |
|--------|-------|-------------|
| `boomStatic` | counts¹ | Static pressure from boom |
| `boomDynamic` | counts¹ | Dynamic (pitot) pressure from boom |
| `boomAlpha` | counts¹ | Angle of attack from boom |
| `boomBeta` | counts¹ | Sideslip angle from boom |
| `boomIAS` | knots | Indicated airspeed from boom |
| `boomAge` | ms | Time since last boom data packet |

¹ Raw ADC counts by default. When `BOOMCONVERTDATA=true` the firmware applies the polynomial calibration curves in `onspeed_core/BoomConvert.h` and writes physical units (PSI for `boomStatic` / `boomDynamic`, degrees for `boomAlpha` / `boomBeta`). `BOOMCONVERTDATA` defaults to `false`.

## EFIS Columns — Dynon/Garmin/MGL

When EFIS type is `ADVANCED`, `DYNOND10`, `GARMING5`, `GARMING3X`, or `MGL`:

| Column | Units | Description |
|--------|-------|-------------|
| `efisIAS` | knots | IAS from EFIS |
| `efisPitch` | degrees | Pitch angle from EFIS |
| `efisRoll` | degrees | Roll angle from EFIS |
| `efisLateralG` | G | Lateral acceleration from EFIS |
| `efisVerticalG` | G | Vertical acceleration from EFIS |
| `efisPercentLift` | % | Percent lift / AOA (0–99%) |
| `efisPalt` | ft | Pressure altitude from EFIS |
| `efisVSI` | fpm | Vertical speed from EFIS |
| `efisTAS` | knots | True airspeed from EFIS |
| `efisOAT` | °C | Outside air temperature from EFIS |
| `efisFuelRemaining` | gal | Fuel remaining |
| `efisFuelFlow` | gph | Fuel flow rate |
| `efisMAP` | inHg | Manifold absolute pressure |
| `efisRPM` | rpm | Engine RPM |
| `efisPercentPower` | % | Computed percent power |
| `efisMagHeading` | degrees | Magnetic heading |
| `efisAge` | ms | Time since last valid EFIS packet |
| `efisTime` | ms | Timestamp from EFIS |

## EFIS Columns — VectorNav VN-300

When EFIS type is `VN-300`:

| Column | Units | Description |
|--------|-------|-------------|
| `vnAngularRateRoll` | rad/s | Roll rate |
| `vnAngularRatePitch` | rad/s | Pitch rate |
| `vnAngularRateYaw` | rad/s | Yaw rate |
| `vnVelNedNorth` | m/s | North velocity (NED frame) |
| `vnVelNedEast` | m/s | East velocity (NED frame) |
| `vnVelNedDown` | m/s | Down velocity (NED frame) |
| `vnAccelFwd` | m/s² | Forward acceleration |
| `vnAccelLat` | m/s² | Lateral acceleration |
| `vnAccelVert` | m/s² | Vertical acceleration |
| `vnYaw` | degrees | Yaw angle |
| `vnPitch` | degrees | Pitch angle |
| `vnRoll` | degrees | Roll angle |
| `vnLinAccFwd` | m/s² | Linear forward acceleration |
| `vnLinAccLat` | m/s² | Linear lateral acceleration |
| `vnLinAccVert` | m/s² | Linear vertical acceleration |
| `vnYawSigma` | rad | Yaw uncertainty |
| `vnRollSigma` | rad | Roll uncertainty |
| `vnPitchSigma` | rad | Pitch uncertainty |
| `vnGnssVelNedNorth` | m/s | GPS north velocity |
| `vnGnssVelNedEast` | m/s | GPS east velocity |
| `vnGnssVelNedDown` | m/s | GPS down velocity |
| `vnWindSpd` | kt | Horizontal wind speed (derived; empty when below 30 KIAS or no GPS fix) |
| `vnWindDir` | degrees | Wind "from" direction in [0, 360), same frame as `vnYaw` (true if VN-300 has WMM declination configured, magnetic otherwise) |
| `vnWindVertical` | kt | Vertical wind component, positive = updraft |
| `vnGnssLat` | degrees | GPS latitude |
| `vnGnssLon` | degrees | GPS longitude |
| `vnEstAltFt` | feet | INS-estimated altitude from Common.Position (sensor-fused GPS+IMU) — filter on `vnGPSFix` |
| `vnGPSFix` | enum | GPS fix quality |
| `vnDataAge` | ms | Time since last VN-300 packet |
| `vnTimeStartupNs` | ns | Per-sample timestamp from the VN-300 Common group: nanoseconds since VN-300 boot (TXCO ±20 ppm). Always advancing, no GPS dependency. At 400 Hz output, consecutive rows differ by ~2,500,000 ns. |
| `vnTimeGpsNs` | ns | Per-sample timestamp: nanoseconds since GPS epoch (1980-01-06 UTC, minus accumulated GPS leap seconds — currently 18 s ahead of UTC). Valid only when `vnTimeStatus & 0x02` (dateOk) is set. |
| `vnTimeStatus` | bit flags | VN-300 TimeStatus byte (UM005 §5.5.10). `bit0` = timeOk (GpsTow valid), `bit1` = dateOk (`vnTimeGpsNs` valid — GPS week resolved), `bit2` = utcTimeValid. |

## Tail-Optional Columns (Format Version 2)

Appended at the end of every row, after `CoeffP`. Older firmware (format
version 1) omits these columns; parsers must look up by name from the
header line rather than by fixed position.

| Column | Units | Description |
|--------|-------|-------------|
| `flapsRawADC` | counts | Raw flap-lever pot ADC reading (uint16, typically 0–4095 on the 12-bit ADC). Lets replay and analysis tools reproduce the L/Dmax pip slide across detent transitions; consumers interpolate the pip lever-end-to-end using this value against the configured `<FLAPPOTPOSITIONS>` boundaries. |
