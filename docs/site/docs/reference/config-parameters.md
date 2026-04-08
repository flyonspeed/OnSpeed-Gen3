# Configuration Parameters Reference

Complete reference of all OnSpeed configuration parameters. Configuration is stored as XML (`config.cfg`).

## General Settings

| XML Tag | Type | Default | Description |
|---------|------|---------|-------------|
| `AOA_SMOOTHING` | int | 20 | EMA filter window for AOA (samples) |
| `PRESSURE_SMOOTHING` | int | 15 | EMA filter window for pressure sensors (samples) |
| `DATASOURCE` | string | `SENSORS` | Data source: `SENSORS`, `TESTPOT`, `RANGESWEEP`, `REPLAYLOGFILE` |
| `REPLAYLOGFILENAME` | string | `log.csv` | Log file to replay when DATASOURCE=REPLAYLOGFILE |
| `SDLOGGING` | bool | true | Enable SD card data logging |
| `CALWIZ_SOURCE` | string | `ONSPEED` | Calibration wizard IAS source: `ONSPEED` or `EFIS` |
| `AHRS_ALGORITHM` | int | 0 | AHRS algorithm: 0=Madgwick, 1=EKF6 |

## EFIS Settings

| XML Tag | Type | Default | Description |
|---------|------|---------|-------------|
| `EFISTYPE` | string | `VN-300` | EFIS type: `VN-300`, `ADVANCED`, `DYNOND10`, `GARMING5`, `GARMING3X`, `MGL` |
| `SERIALEFISDATA` | bool | true | Enable reading EFIS serial data |
| `SERIALOUTFORMAT` | string | `ONSPEED` | Display serial output: `ONSPEED` or `G3X` |

## Orientation

| XML Path | Type | Default | Options |
|----------|------|---------|---------|
| `ORIENTATION > PORTS` | string | `FORWARD` | `FORWARD`, `AFT`, `UP`, `DOWN`, `LEFT`, `RIGHT` |
| `ORIENTATION > BOX_TOP` | string | `UP` | `UP`, `DOWN`, `LEFT`, `RIGHT` |

## Volume & Audio

| XML Path | Type | Default | Description |
|----------|------|---------|-------------|
| `VOLUME > ENABLED` | bool | true | Use hardware volume potentiometer |
| `VOLUME > HIGH_ANALOG` | int | 339 | ADC value at max volume |
| `VOLUME > LOW_ANALOG` | int | 170 | ADC value at min volume |
| `VOLUME > DEFAULT` | int | 100 | Default volume (0–100%) when pot disabled |
| `VOLUME > ENABLE_3DAUDIO` | bool | true | Enable stereo lateral-G panning |
| `VOLUME > MUTE_UNDER_IAS` | int | 25 | Mute audio below this IAS (knots) |
| `OVERGWARNING` | bool | true | Enable G-limit warning tone |

## Vno Chime

| XML Path | Type | Default | Description |
|----------|------|---------|-------------|
| `VNO > SPEED` | int | 157 | Vno speed threshold (knots) |
| `VNO > CHIME_INTERVAL` | int | 3 | Chime repeat interval (seconds) |
| `VNO > CHIME_ENABLED` | bool | true | Enable Vno overspeed chime |

## Load Limits

| XML Path | Type | Default | Description |
|----------|------|---------|-------------|
| `LOAD_LIMIT > POSITIVE` | float | 4.0 | Maximum positive G-load |
| `LOAD_LIMIT > NEGATIVE` | float | -2.0 | Maximum negative G-load |

## Sensor Biases

| XML Path | Type | Default | Description |
|----------|------|---------|-------------|
| `BIAS > PFWD` | int | 8192 | Pitot pressure 14-bit count zero bias |
| `BIAS > P45` | int | 8192 | AOA pressure 14-bit count zero bias |
| `BIAS > PSTATIC` | float | 0.0 | Static pressure offset (PSI) |
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
| `BOOM` | bool | true | Enable boom probe serial input |
| `BOOMCHECKSUM` | bool | true | Validate boom data checksums |
| `OATSENSOR` | bool | false | Enable DS18B20 OAT sensor |

## Aircraft Parameters

| XML Path | Type | Default | Description |
|----------|------|---------|-------------|
| `AIRCRAFT > GROSS_WEIGHT` | float | 2282 | Aircraft weight (lbs) |
| `AIRCRAFT > BEST_GLIDE_IAS` | float | 87.5 | Best glide speed (knots) |
| `AIRCRAFT > VFE` | float | 150 | Max flaps-extended speed (knots) |
| `AIRCRAFT > G_LIMIT` | float | 3.5 | Structural G-limit |

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

### Per-Flap AOA Curve

| XML Path | Type | Description |
|----------|------|-------------|
| `AOA_CURVE > TYPE` | int | 1=polynomial, 2=logarithmic, 3=exponential |
| `AOA_CURVE > X3` | float | Cubic coefficient |
| `AOA_CURVE > X2` | float | Quadratic coefficient |
| `AOA_CURVE > X1` | float | Linear coefficient |
| `AOA_CURVE > X0` | float | Constant coefficient |
