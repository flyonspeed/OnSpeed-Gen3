# No EFIS (Standalone Operation)

OnSpeed works without an EFIS connection. In standalone mode, the system relies entirely on its own sensors for flight data.

## What Works Without EFIS

- **AOA measurement** — the pressure sensors and IMU provide AOA data independently
- **IAS computation** — from the onboard pitot pressure sensor
- **Altitude** — from the static pressure sensor (Kalman filtered)
- **Audio tones** — fully functional
- **SD card logging** — all OnSpeed sensor data is logged
- **WiFi configuration** — fully functional
- **Calibration wizard** — works using OnSpeed's own sensors

## What You Lose

| Feature | Impact |
|---------|--------|
| EFIS IAS | OnSpeed uses its own pitot sensor. Still accurate, but not cross-checked against your primary ASI. |
| EFIS OAT | No temperature data unless you install a DS18B20 sensor. Affects TAS and density altitude corrections. |
| EFIS attitude | OnSpeed uses its own IMU + AHRS. Works well, but not cross-checked. |
| Engine data | No RPM, MAP, fuel flow, or percent power in logs |
| Percent Lift | No Dynon/Garmin AOA percentage for cross-calibration |

## When to Add a DS18B20 OAT Sensor

If you're running standalone, you should install a [DS18B20 OAT sensor](../installation/oat-sensor.md). Without OAT data, OnSpeed can't compute accurate TAS or density altitude, which affects the accuracy of flight path angle calculations.

The DS18B20 is inexpensive and easy to install — it's strongly recommended for standalone installations.

## Configuration for Standalone

1. Connect to OnSpeed WiFi
2. Set **EFIS Type** to `None`
3. Set **OAT Sensor** to `Enabled` (if you installed a DS18B20)
4. Set **Serial EFIS Data** to `Disabled`
5. Save and reboot

## Calibration in Standalone Mode

The calibration wizard works in standalone mode using OnSpeed's own IAS and attitude data. Set the **Calibration Source** to `ONSPEED` (not `EFIS`) in the calibration wizard.

The calibration quality may be slightly lower without EFIS cross-reference data, but it is fully functional for normal operations.
