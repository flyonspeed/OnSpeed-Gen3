# First-Time Setup

This is the step-by-step checklist for configuring a newly installed OnSpeed controller. Complete these steps before your first calibration flight.

## Prerequisites

- Hardware installation complete (see [Installation Checklist](../installation/checklist.md))
- SD card inserted
- A device (phone, tablet, laptop) with WiFi
- The aircraft should be **on the ground, level, in zero-wind conditions** for sensor calibration

---

## Step 1: Connect to OnSpeed WiFi

1. Power on the OnSpeed controller
2. Wait ~10 seconds for the WiFi AP to start (the status LED should be blinking)
3. On your device, connect to:
    - **SSID**: `OnSpeed`
    - **Password**: `angleofattack`
4. Open a browser and go to **`http://192.168.0.1`**

## Step 2: Set EFIS Type

On the configuration page (`/aoaconfig`):

1. Find the **EFIS Type** setting
2. Select the EFIS you have connected:
    - **ADVANCED** — Dynon SkyView / HDX
    - **DYNOND10** — Dynon D10 / D100
    - **GARMING5** — Garmin G5
    - **GARMING3X** — Garmin G3X / G3X Touch
    - **MGL** — MGL iEFIS / Odyssey
    - **VN-300** — VectorNav VN-300
    - **None** — No EFIS (standalone mode)

!!! danger "This must match your wiring"
    Wrong EFIS type = no data or garbled data. Double-check.

## Step 3: Set Box Orientation

The controller needs to know how it's physically mounted. Set two values:

1. **PORTS_ORIENTATION** — Which direction do the pressure port fittings face?
    - `FORWARD` — ports face the nose of the aircraft
    - `AFT` — ports face the tail
    - `LEFT` — ports face the left wing
    - `RIGHT` — ports face the right wing

2. **BOX_TOP_ORIENTATION** — Which direction does the top of the PCB/enclosure point?
    - `UP` — top faces the sky (normal upright)
    - `DOWN` — top faces the floor (inverted)
    - `LEFT` — top faces left wing
    - `RIGHT` — top faces right wing

These two settings tell the firmware how to rotate the IMU axes into the aircraft frame.

## Step 4: Set Flap Positions

Configure your aircraft's flap positions. For each flap setting you use:

1. Set the **Degrees** value (e.g., 0°, 20°, 40°)
2. Set the **Pot Value** — the ADC reading for that flap position

To find pot values, physically set each flap position and read the value from the `FLAPS` console command or the sensor config page.

See [Flap Position Setup](flap-setup.md) for details.

## Step 5: Set Aircraft Limits

Configure these aircraft-specific values:

| Setting | What to Enter | Example (RV-4) |
|---------|---------------|-----------------|
| **Vno** | Your aircraft's max structural cruising speed (knots) | 157 |
| **Positive G-limit** | Maximum positive load factor | 4.0 |
| **Negative G-limit** | Maximum negative load factor (enter as negative) | -2.0 |
| **Best Glide IAS** | Best glide speed at gross weight (knots) | 87.5 |
| **Gross Weight** | Aircraft gross weight (lbs) | 2282 |
| **Vfe** | Max flaps-extended speed (knots) | 150 |

## Step 6: Configure Audio

| Setting | Recommendation |
|---------|----------------|
| **Volume Default** | 100% (adjust after first flight) |
| **Volume Pot Enabled** | `true` if you wired a volume pot, `false` otherwise |
| **3D Audio** | `true` if you have stereo wiring, `false` for mono |
| **Mute Under IAS** | 25 knots (silences tones on the ground and during taxi) |
| **Vno Chime Enabled** | `true` |
| **Vno Chime Interval** | 3 seconds |
| **Over-G Warning** | `true` |

## Step 7: Enable/Disable Optional Features

| Setting | Set to |
|---------|--------|
| **OAT Sensor** | `Enabled` if you wired a DS18B20, `Disabled` otherwise |
| **Boom** | `Enabled` only if you have a boom probe connected |
| **Boom Checksum** | `true` (validates boom data integrity) |
| **SD Logging** | `Enabled` (you want flight data logged) |
| **Data Source** | `SENSORS` (normal operation) |

## Step 8: Save Configuration

Click **Save** on the configuration page. The settings are written to the SD card and flash memory.

## Step 9: Run Sensor Calibration

With the aircraft **level on the ground** in **zero-wind conditions** (covers on pitot/static if possible):

1. Navigate to the **Sensor Config** page (`/sensorconfig`)
2. Click the **Calibrate** button (or use the `BIAS` console command)
3. The system takes 1000 samples of each pressure sensor to establish zero-bias readings
4. It also zeros the IMU accelerometer and gyro biases for the current orientation

See [Sensor Calibration](sensor-calibration.md) for details.

!!! warning "Level and still"
    The aircraft must be level (use a level on the longerons) and stationary during sensor calibration. Any wind on the pitot or AOA ports will give incorrect bias values.

## Step 10: Test Audio

Use the `AUDIOTEST` console command (via USB serial) or trigger it from the web interface. You should hear:

- Left channel test tone
- Right channel test tone
- Various tone types

If you hear nothing, see [Troubleshooting — No Audio](../troubleshooting/no-audio.md).

## Step 11: Verify EFIS Data (if connected)

1. Power on your EFIS
2. Run the `SENSORS` console command
3. Look for non-zero EFIS data values (IAS, pitch, roll, OAT)
4. If all EFIS fields are zero, see [Troubleshooting — No EFIS Data](../troubleshooting/no-efis.md)

## Step 12: Ready for Calibration Flight

Configuration is complete. The next step is flying the **[Calibration Wizard](../calibration/wizard.md)** to establish AOA curves for your aircraft.

!!! tip "Save a backup"
    After completing first-time setup, download a backup of your configuration from the web interface. See [Backup & Restore](backup-restore.md).
