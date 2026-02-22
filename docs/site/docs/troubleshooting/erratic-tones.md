# Troubleshooting: Erratic or Incorrect Tones

Tones are playing but at the wrong speeds, or the tone behavior is erratic and inconsistent.

## Tones at Wrong Speeds

### Check Calibration Quality

If the calibration R² was low (< 0.90), the AOA curve is a poor fit to your aircraft. This means tone transitions will occur at incorrect AOA values.

**Fix**: Re-fly the calibration wizard in calm air with a smooth, complete deceleration sweep. Aim for R² > 0.95.

### Check Flap Detection

If the wrong flap position is detected, the system uses the wrong set of AOA thresholds.

Use the `FLAPS` console command to verify the correct flap position is showing:

```
FLAPS
> Flap position: 20 degrees (raw value: 158, index: 1)
```

If the displayed position doesn't match your actual flaps, check:

- Potentiometer wiring and connection
- Pot values in configuration (see [Flap Position Setup](../configuration/flap-setup.md))
- Pot mechanical linkage (is it moving with the flap handle?)

### Check Sensor Biases

If pressure sensor biases have drifted, the AOA reading will be offset from reality.

**Fix**: Recalibrate sensor biases on the ground in zero-wind conditions. See [Sensor Calibration](../configuration/sensor-calibration.md).

## Erratic Tone Behavior

### Fluctuating Tones in Cruise

If you hear intermittent tones during cruise (where you should hear silence):

1. **Wind gust on the ground** — if testing on the ground, wind can produce airspeed and AOA readings
2. **Incorrect bias calibration** — the zero-point is off, making the system think there's airflow when there isn't
3. **Moisture in lines** — water in the pneumatic tubing causes pressure spikes

### Rapid Tone Changes in Flight

If tones rapidly switch between regions in flight:

1. **Turbulence** — in rough air, AOA fluctuates naturally. Increase AOA smoothing if this is bothersome (but be aware that more smoothing adds lag)
2. **AOA smoothing too low** — try increasing the AOA smoothing value from the default of 20
3. **Pressure line issue** — a loose connection or partially blocked line can cause pressure oscillations
4. **Mounting vibration** — if the controller is vibrating, IMU readings will be noisy. Improve the mounting rigidity.

### Check Box Orientation

If the PORTS_ORIENTATION or BOX_TOP_ORIENTATION settings don't match your physical mounting, the IMU axes are misaligned. This means pitch and roll readings are wrong, which corrupts the Derived AOA calculation.

Use the `SENSORS` command to verify:

- With aircraft level: pitch ≈ 0°, roll ≈ 0°, VerticalG ≈ 1.0G
- If these are obviously wrong, your orientation settings need to be corrected

### Check for Moisture

Water in the pneumatic lines is a common cause of erratic readings, especially in humid or cold conditions:

1. Disconnect tubing at the controller
2. Blow through each line with dry air
3. Reconnect and verify readings stabilize
