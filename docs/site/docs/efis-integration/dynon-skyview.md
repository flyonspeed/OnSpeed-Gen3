# Dynon SkyView / SkyView HDX

The Dynon SkyView is the most common EFIS used with OnSpeed. It provides comprehensive flight data including IAS, pitch, roll, G-loads, OAT, altitude, fuel, and even its own Percent Lift (AOA) reading.

## What Dynon Provides

When connected, OnSpeed receives from the SkyView:

| Data | Field | Units |
|------|-------|-------|
| Indicated Airspeed | `efisIAS` | knots |
| Pitch Angle | `efisPitch` | degrees |
| Roll Angle | `efisRoll` | degrees |
| Lateral G | `efisLateralG` | G |
| Vertical G | `efisVerticalG` | G |
| Percent Lift | `efisPercentLift` | 0–99% |
| Pressure Altitude | `efisPalt` | feet |
| Vertical Speed | `efisVSI` | fpm |
| True Airspeed | `efisTAS` | knots |
| Outside Air Temp | `efisOAT` | °C |
| Fuel Remaining | `efisFuelRemaining` | gallons |
| Fuel Flow | `efisFuelFlow` | gph |
| Manifold Pressure | `efisMAP` | inHg |
| Engine RPM | `efisRPM` | rpm |
| Percent Power | `efisPercentPower` | % |
| Magnetic Heading | `efisMagHeading` | degrees |

All of this data is logged to the SD card for post-flight analysis.

## Dynon Serial Protocol

The SkyView outputs two message types:

- **`!1` message** — ADAHRS data (74 bytes): attitude, airspeed, altitude, G-loads, heading, time
- **`!3` message** — EMS data (225 bytes): engine parameters, fuel, OAT, percent lift

Both messages must be enabled for full functionality.

## Dynon Configuration

### Step 1: Identify the Serial Port

The SkyView has multiple serial ports on the rear connector. Choose one that isn't already assigned to autopilot, transponder, or other devices.

### Step 2: Configure Serial Output

On the SkyView:

1. Go to **SETUP** menu
2. Navigate to **SERIAL PORT SETUP**
3. Select the serial port you'll use for OnSpeed
4. Set the following:
    - **Protocol**: ADAHRS + EMS output
    - **Baud Rate**: **115200**
    - **Output**: Enabled

!!! danger "Baud rate must be 115200"
    The Dynon defaults to **9600 baud**. OnSpeed requires **115200 baud**. If you leave the default, OnSpeed will receive garbled data. This is the **#1 most common configuration mistake**.

### Step 3: Wire the Connection

Connect the Dynon serial **TX** pin to the OnSpeed **RX** pin (GPIO 11):

- Find the TX pin for your chosen serial port on the SkyView rear connector
- Run a wire from Dynon TX to OnSpeed RX
- Connect grounds between the two devices
- The Dynon uses RS-232 levels; the OnSpeed board has an ADM3202 RS-232 level shifter

### Step 4: Configure OnSpeed

1. Connect to OnSpeed WiFi (`OnSpeed` / `angleofattack`)
2. Open the configuration page at `192.168.0.1`
3. Set **EFIS Type** to **ADVANCED** (this is the SkyView/HDX setting)
4. Save and reboot

### Step 5: Verify

1. Power on both the Dynon and OnSpeed
2. Use the `SENSORS` console command — you should see EFIS data fields populated
3. Check `efisAge` — should be a small number (< 500 ms), meaning data is arriving regularly

## Common Mistakes

| Problem | Cause | Fix |
|---------|-------|-----|
| No EFIS data at all | Wrong baud rate | Set Dynon to 115200 baud |
| No EFIS data at all | TX/RX swapped | Swap the serial wire connection |
| No EFIS data at all | Wrong serial port | Verify which Dynon serial port you wired to |
| Partial data (IAS but no OAT) | EMS output not enabled | Enable both ADAHRS + EMS output on the Dynon |
| Wrong EFIS type | EFIS Type not set to ADVANCED | Change EFIS Type in OnSpeed config |
| Garbled data | Baud mismatch | Both must be 115200 |

## Dynon Percent Lift

The Dynon SkyView has its own built-in AOA system that outputs a **Percent Lift** value (0–99%). OnSpeed logs this as `efisPercentLift`. The Dynon uses a **linear model**: `AOA% = gain × pressure_ratio + offset`, configured per flap setting.

This is useful for cross-calibration — you can compare OnSpeed's AOA computation against the Dynon's to validate both systems.

!!! note "Dynon V-speeds are in meters/second"
    If you ever look at V-speed values in the Dynon configuration file, they're stored in **meters per second** (multiply by 1.94384 to convert to knots). This doesn't affect the OnSpeed serial interface — IAS comes across in knots.

## Dynon SkyView Configuration File

The Dynon stores its configuration in a `.dfg` file that can be saved to and loaded from USB. This file contains serial port settings, V-speeds, AOA model parameters, and all other system configuration. If you need to verify your serial port settings, you can inspect this file on a computer.
