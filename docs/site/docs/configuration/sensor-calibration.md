# Sensor Calibration (Biases)

Sensor calibration establishes the **zero-point** (bias) for each pressure sensor and the IMU. This must be done on the ground, with the aircraft level and in zero-wind conditions.

## When to Calibrate

- **Initial installation** — always calibrate during first-time setup
- **After moving the controller** — if you change the mounting location or orientation
- **After a firmware update** — if the update changed sensor processing
- **If readings seem off** — drifting altitude on the ground, non-zero pitch/roll when level

## Prerequisites

- Aircraft on the ground, **level** (check with a level on the longerons or a known-level surface)
- **Zero wind** — cover the pitot tube and AOA probe if possible, or wait for calm conditions
- **Engine off** — prop wash will contaminate pressure readings
- **Aircraft stationary** — no movement during calibration

## Calibration Procedure

### Via Web Interface

1. Connect to OnSpeed WiFi (`OnSpeed` / `angleofattack`)
2. Navigate to the **Sensor Config** page: `http://192.168.0.1/sensorconfig`
3. Verify the aircraft is level and still
4. Click the **Calibrate** button
5. Wait for the calibration to complete (takes a few seconds — collects 1000 samples)
6. The page will display the new bias values

### Via Console Command

1. Connect to the OnSpeed USB serial port (921600 baud)
2. Type: `BIAS`
3. The system collects 1000 samples each of Pfwd (pitot) and P45 (AOA) pressure
4. It also zeros the accelerometer and gyro biases
5. New bias values are displayed and saved

## What Gets Calibrated

| Bias | What It Is | Stored As |
|------|-----------|-----------|
| **Pfwd** | Pitot pressure sensor zero reading | 14-bit ADC count (nominal ~8192) |
| **P45** | AOA pressure sensor zero reading | 14-bit ADC count (nominal ~8192) |
| **PStatic** | Static pressure offset | PSI offset |
| **GX** | Forward acceleration bias | G offset |
| **GY** | Lateral acceleration bias | G offset |
| **GZ** | Vertical acceleration bias | G offset |
| **Pitch** | Pitch angle bias | Degrees offset |
| **Roll** | Roll angle bias | Degrees offset |

## Verifying Calibration

After calibration, check the `SENSORS` console command output:

- **Pfwd pressure**: Should read approximately 0 PSI with no airflow
- **P45 pressure**: Should read approximately 0 PSI with no airflow
- **IAS**: Should read 0 or very close to 0 knots
- **Pitch**: Should read approximately 0° (or your aircraft's known ground pitch angle)
- **Roll**: Should read approximately 0°
- **VerticalG**: Should read approximately 1.0G
- **LateralG**: Should read approximately 0.0G
- **ForwardG**: Should read approximately 0.0G

If any of these are significantly off, verify the aircraft is truly level and there's no wind, then recalibrate.

!!! note "AHRS re-initialization"
    As of v4.15, the firmware re-initializes the AHRS algorithm after calibration to ensure the new bias values take effect immediately. You don't need to reboot after calibration.
