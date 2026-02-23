# Calibration Wizard

The calibration wizard guides you through an in-flight deceleration sweep and fits the AOA curves for your aircraft. You'll fly this for each flap position.

## Prerequisites

- [First-time setup](../configuration/first-time-setup.md) is complete
- [Sensor calibration](../configuration/sensor-calibration.md) has been done
- Calm air — avoid turbulence, thermals, or gusty conditions
- Safe altitude — at least 3,000 feet AGL recommended
- Clear of traffic
- Familiar with slow flight in your aircraft

## Before the Flight

1. Power on the OnSpeed controller and verify it's running (heartbeat LED blinking)
2. If using EFIS, verify EFIS data is flowing (`SENSORS` console command)
3. Note the current weight — enter it in the wizard when prompted
4. Plan your calibration altitude and area

## Step-by-Step Wizard Walkthrough

### Step 1: Access the Wizard

You can access the wizard in two ways:

- **Pre-flight**: Connect to OnSpeed WiFi on the ground, navigate to `http://192.168.0.1/calwiz`, enter your aircraft parameters, then fly with the page open
- **In-flight**: If you have a tablet/phone mounted in the cockpit, connect to OnSpeed WiFi and open the wizard page

### Step 2: Enter Aircraft Parameters

The wizard asks for:

| Parameter | What to Enter |
|-----------|---------------|
| **Gross Weight** | Current aircraft weight in pounds |
| **Stall Speed (Vs)** | Published stall speed at gross weight for the current flap setting |
| **Vfe** | Max flaps-extended speed (if calibrating with flaps) |
| **G-Limit** | Aircraft structural G-limit |
| **Best Glide IAS** | Best glide speed at current weight |

### Step 3: Select Flap Position

Set your flaps to the position you want to calibrate and select it in the wizard. The wizard calibrates **one flap position at a time**.

Start with **flaps up (clean)** — this is the most important configuration.

### Step 4: Select Calibration Source

- **ONSPEED** — use OnSpeed's own IAS and sensor data
- **EFIS** — use EFIS-provided IAS (recommended if EFIS is connected)

### Step 5: Fly the Deceleration Sweep

This is the critical part. You need a smooth, steady deceleration from cruise to near-stall:

1. **Establish level flight** at a safe altitude
2. **Trim for cruise** at a moderate power setting
3. **Start the recording** in the wizard
4. **Smoothly reduce power** to idle (or near idle)
5. **Maintain altitude** — hold wings level, coordinated flight
6. **Decelerate steadily** from cruise through approach speed and into slow flight
7. **Continue until you feel the stall buffet** or reach the stall warning speed
8. **Stop the recording** in the wizard
9. **Add power and recover** to normal flight

!!! danger "Safety first"
    This is slow flight near stall. Maintain safe altitude, clear of traffic, and be prepared to recover immediately. Do not stall the aircraft — stop the recording at the first indication of stall and recover.

### What Good Data Looks Like

A good deceleration sweep shows:

- **Smooth, continuous deceleration** — no sudden power changes or pitch inputs
- **Wings level** — minimal bank angle throughout
- **Coordinated flight** — ball centered
- **Steady altitude** — not climbing or descending significantly
- **Full speed range** — from well above approach speed down to near-stall
- **R² above 0.95** for both the polynomial and hyperbolic fits

The wizard's deceleration gauge shows your current decel rate. Aim for a steady 1–3 knot/second deceleration.

### What Bad Data Looks Like

| Problem | Cause | Fix |
|---------|-------|-----|
| Low R² (< 0.90) | Turbulence, uncoordinated flight, or incomplete speed range | Re-fly in calmer air, stay coordinated |
| Noisy curve | Gusts or thermal activity | Wait for calmer conditions |
| Flat or missing slow-speed data | Didn't decelerate far enough | Continue the sweep closer to stall |
| Discontinuities | Sudden pitch or power changes during sweep | Smoother control inputs |
| Poor stall detection | Buffet onset too gradual or too abrupt | Multiple sweeps may help |

### Step 6: Review Results

After the sweep, the wizard displays:

- **Cp → AOA polynomial** — the fitted curve with R²
- **IAS → DerivedAOA fit** — the $K/\text{IAS}^2 + \alpha_0$ curve with R²
- **Extracted parameters**: $\alpha_0$, $\alpha_\text{stall}$, $K$
- **Computed setpoints**: L/Dmax, OnSpeed-Fast, OnSpeed-Slow, Stall Warning AOA values
- **Equivalent speeds**: what IAS each setpoint corresponds to at your current weight

### Step 7: Save

If the results look good (R² > 0.95, setpoints make sense for your aircraft):

1. Click **Save Calibration**
2. The AOA curve and setpoints are written to the configuration for the current flap position
3. The firmware re-initializes the AHRS to apply the new parameters

### Step 8: Repeat for Other Flap Positions

If your aircraft has multiple flap settings (e.g., 0°, 20°, 40°):

1. Set flaps to the next position
2. Select the new flap position in the wizard
3. Fly another deceleration sweep
4. Save the results

Each flap position gets its own independent calibration.

## Tips for Better Calibration

- **Fly in the morning** — calmer air, fewer thermals
- **Use smooth, small inputs** — the system is sensitive to pitch rate and bank angle
- **Decelerate slowly** — 1–2 knots/second is ideal
- **Cover the full speed range** — from at least 1.5× Vs down to 1.1× Vs
- **Multiple runs** — if the first sweep doesn't give good R², try again
- **Trim well** — a well-trimmed aircraft produces smoother deceleration data
