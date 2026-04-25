# Configuration Parameters Reference

Complete reference of all OnSpeed configuration parameters. Configuration is stored as XML (`config.cfg`). The **Default** column shows the values set by `FOSConfig::LoadDefaultConfiguration()` in `Config.cpp` — what a fresh install or "Load Defaults" button press produces.

## General Settings

| XML Tag | Type | Default | Description |
|---------|------|---------|-------------|
| `AOA_SMOOTHING` | int | 20 | EMA filter window for AOA (samples) |
| `PRESSURE_SMOOTHING` | int | 15 | EMA filter window for pressure sensors (samples) |
| `DATASOURCE` | string | `SENSORS` | Data source: `SENSORS`, `TESTPOT`, `RANGESWEEP`, `REPLAYLOGFILE` |
| `REPLAYLOGFILENAME` | string | (empty) | Log file to replay when DATASOURCE=REPLAYLOGFILE |
| `SDLOGGING` | bool | false | Enable SD card data logging |
| `LOGRATE` | int | 50 | Logging rate in Hz: 50 (pressure rate) or 208 (IMU rate) |
| `CALWIZ_SOURCE` | string | `ONSPEED` | Calibration wizard IAS source: `ONSPEED` or `EFIS` |
| `AHRS_ALGORITHM` | int | 0 | AHRS algorithm: 0=Madgwick, 1=EKF6. See [Advanced Settings](../configuration/advanced.md) before changing. |

## EFIS Settings

| XML Tag | Type | Default | Description |
|---------|------|---------|-------------|
| `EFISTYPE` | string | `VN-300` | EFIS type: `VN-300`, `ADVANCED`, `DYNOND10`, `GARMING5`, `GARMING3X`, `MGL` |
| `SERIALEFISDATA` | bool | false | Enable reading EFIS serial data |
| `SERIALOUTFORMAT` | string | `ONSPEED` | Display serial output: `ONSPEED` or `G3X` |

## Orientation

| XML Path | Type | Default | Options |
|----------|------|---------|---------|
| `ORIENTATION > PORTS` | string | `FORWARD` | `FORWARD`, `AFT`, `UP`, `DOWN`, `LEFT`, `RIGHT` |
| `ORIENTATION > BOX_TOP` | string | `UP` | `UP`, `DOWN`, `LEFT`, `RIGHT` |

## Volume & Audio

| XML Path | Type | Default | Description |
|----------|------|---------|-------------|
| `VOLUME > ENABLED` | bool | false | Use hardware volume potentiometer |
| `VOLUME > HIGH_ANALOG` | int | 4095 | ADC value at max volume |
| `VOLUME > LOW_ANALOG` | int | 0 | ADC value at min volume |
| `VOLUME > DEFAULT` | int | 100 | Default volume (0–100%) when pot disabled |
| `VOLUME > ENABLE_3DAUDIO` | bool | false | Enable stereo lateral-G panning |
| `VOLUME > MUTE_UNDER_IAS` | int | 30 | Mute audio below this IAS (knots); 0 = always on |
| `OVERGWARNING` | bool | false | Enable G-limit warning tone |

## Vno Chime

| XML Path | Type | Default | Description |
|----------|------|---------|-------------|
| `VNO > SPEED` | int | 150 | Vno speed threshold (knots) |
| `VNO > CHIME_INTERVAL` | int | 3 | Chime repeat interval (seconds) |
| `VNO > CHIME_ENABLED` | bool | false | Enable Vno overspeed chime |

## Load Limits

Over-G warning thresholds (fire when `OVERGWARNING` is enabled). Distinct from the airframe structural G-limit in `AIRCRAFT > G_LIMIT`, which drives the maneuvering-speed computation.

| XML Path | Type | Default | Description |
|----------|------|---------|-------------|
| `LOAD_LIMIT > POSITIVE` | float | 4.0 | Positive-G warning threshold |
| `LOAD_LIMIT > NEGATIVE` | float | -2.0 | Negative-G warning threshold |
| `LOAD_LIMIT > ASYMMETRIC_GYRO_LIMIT` | float | 15.0 | Roll/yaw rate (deg/s) above which reduced G-limits apply |
| `LOAD_LIMIT > ASYMMETRIC_REDUCTION` | float | 0.667 | G-limit multiplier during asymmetric flight (e.g., 0.667 = 2/3) |

## Sensor Biases

| XML Path | Type | Default | Description |
|----------|------|---------|-------------|
| `BIAS > PFWD` | int | 8192 | Pitot pressure 14-bit count zero bias |
| `BIAS > P45` | int | 8192 | AOA pressure 14-bit count zero bias |
| `BIAS > PSTATIC` | float | 0.0 | Static pressure offset (millibars; subtracted from measured mbar) |
| `BIAS > GX` | float | 0.0 | Forward acceleration bias (G) |
| `BIAS > GY` | float | 0.0 | Lateral acceleration bias (G) |
| `BIAS > GZ` | float | 0.0 | Vertical acceleration bias (G) |
| `BIAS > PITCH` | float | 0.0 | Pitch angle bias (degrees) |
| `BIAS > ROLL` | float | 0.0 | Roll angle bias (degrees) |

## CAS Curve (Airspeed Calibration)

| XML Path | Type | Default | Description |
|----------|------|---------|-------------|
| `CAS_CURVE > TYPE` | int | 1 | Curve type: 1=polynomial |
| `CAS_CURVE > X3` | float | 0.0 | Cubic coefficient |
| `CAS_CURVE > X2` | float | 0.0 | Quadratic coefficient |
| `CAS_CURVE > X1` | float | 1.0 | Linear coefficient (1.0 = pass-through) |
| `CAS_CURVE > X0` | float | 0.0 | Constant coefficient |
| `CAS_CURVE > ENABLED` | bool | false | Apply CAS correction |

## Optional Features

| XML Tag | Type | Default | Description |
|---------|------|---------|-------------|
| `BOOM` | bool | false | Enable boom probe serial input |
| `BOOMCHECKSUM` | bool | true | Validate boom data checksums |
| `BOOMCONVERTDATA` | bool | false | Apply polynomial calibration curves to boom data (false = log raw counts) |
| `OATSENSOR` | bool | false | Enable DS18B20 OAT sensor |

## Aircraft Parameters

| XML Path | Type | Default | Description |
|----------|------|---------|-------------|
| `AIRCRAFT > GROSS_WEIGHT` | int | 0 | Max gross weight (lbs) |
| `AIRCRAFT > BEST_GLIDE_IAS` | float | 0.0 | Best glide speed at max gross weight (knots) |
| `AIRCRAFT > VFE` | float | 0.0 | Max flaps-extended speed (knots) |
| `AIRCRAFT > G_LIMIT` | float | 0.0 | Airframe structural load factor limit. Used for maneuvering-speed computation. The config page offers FAR 23 category radio buttons (Normal +3.8G, Utility +4.4G, Aerobatic +6.0G) plus a Custom field. |

## Per-Flap Position Settings

Each `FLAP_POSITION` element contains:

| XML Path | Type | Description |
|----------|------|-------------|
| `DEGREES` | int | Flap deflection angle |
| `POT_VALUE` | int | ADC reading for this position |
| `LDMAXAOA` | float | Best L/D angle of attack (degrees) |
| `ONSPEEDFASTAOA` | float | Fast approach AOA (degrees) |
| `ONSPEEDSLOWAOA` | float | Slow approach AOA (degrees) |
| `STALLWARNAOA` | float | Stall warning threshold AOA (degrees) |
| `STALLAOA` | float | Actual stall AOA from calibration (degrees) |
| `MANAOA` | float | Maneuvering speed AOA (degrees) |
| `ALPHA0` | float | Zero-lift angle from physics fit (degrees) |
| `ALPHASTALL` | float | Stall angle from physics fit (degrees) |
| `KFIT` | float | Lift sensitivity from IAS-to-AOA fit (deg·kt²) |

### Per-Flap AOA Curve

| XML Path | Type | Description |
|----------|------|-------------|
| `AOA_CURVE > TYPE` | int | 1=polynomial, 2=logarithmic, 3=exponential |
| `AOA_CURVE > X3` | float | Cubic coefficient |
| `AOA_CURVE > X2` | float | Quadratic coefficient |
| `AOA_CURVE > X1` | float | Linear coefficient |
| `AOA_CURVE > X0` | float | Constant coefficient |
