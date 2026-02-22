# Advanced Settings

These settings are for fine-tuning. The defaults work well for most installations.

## AHRS Algorithm

OnSpeed supports two attitude estimation algorithms:

| Algorithm | Setting | Characteristics |
|-----------|---------|-----------------|
| **Madgwick** | `0` (default) | Complementary filter, quaternion-based. Well-proven, lower CPU cost. Good for most installations. |
| **EKF6** | `1` | 6-state Extended Kalman Filter. Estimates pitch, roll, AOA, and 3 gyro biases. More sophisticated, better long-term stability. |

!!! danger "Changing algorithm requires recalibration"
    The calibration wizard fits curves against Derived AOA, which is computed differently by each algorithm. If you switch between Madgwick and EKF6, you must re-fly the calibration wizard for all flap positions.

## AOA Smoothing

Controls the Exponential Moving Average (EMA) filter applied to the AOA signal.

- **Default**: 20 (samples)
- **Lower values**: More responsive but noisier — the tone will fluctuate more in turbulence
- **Higher values**: Smoother but slower response — the tone may lag behind rapid AOA changes

For most aircraft, the default of 20 provides a good balance.

## Pressure Smoothing

Controls EMA filtering on the pitot, AOA, and static pressure readings.

- **Default**: 15 (samples)
- **Lower values**: Faster response, more noise
- **Higher values**: Smoother, more lag

## CAS Curve (Calibrated Airspeed)

If your aircraft has a known airspeed calibration curve (correcting IAS to CAS), you can enter polynomial coefficients here:

- **Type**: `1` (polynomial)
- **Coefficients**: X3, X2, X1, X0 (cubic polynomial)
- **Enabled**: `false` by default

Most aircraft don't need this — the default is a pass-through (X1=1.0, all others=0.0).

Only enable this if you have a measured position error correction curve for your pitot/static system. An incorrect CAS curve is worse than no curve at all.

## Serial Output Format

Controls the format of data sent on the display serial output (GPIO 10):

- **ONSPEED** — OnSpeed native format
- **G3X** — Garmin G3X-compatible format

Use `G3X` if you're feeding data to a Garmin display. Otherwise, leave as `ONSPEED`.

## Data Source

Controls where the AOA data comes from:

| Source | Use |
|--------|-----|
| **SENSORS** | Normal operation — live sensor data |
| **TESTPOT** | Test mode — AOA from a test potentiometer |
| **RANGESWEEP** | Test mode — automatic AOA sweep 0–20° |
| **REPLAYLOGFILE** | Replay a recorded log file through the system |

For normal flight operations, this should always be set to **SENSORS**.

## Calibration Source

Controls which IAS source the calibration wizard uses:

- **ONSPEED** — use OnSpeed's own pitot/IAS
- **EFIS** — use the EFIS-provided IAS

If you have an EFIS connected with accurate IAS, using `EFIS` may give better calibration results.
