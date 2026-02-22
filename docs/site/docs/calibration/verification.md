# Verifying Calibration

After calibrating, verify that the tones correspond to the correct speeds for your aircraft.

## Ground Check

Before flying, verify basic sensor operation:

1. Power on the controller
2. With the aircraft stationary on the ground:
    - IAS should read 0 (or very close)
    - No tones should play (below mute-under-IAS threshold)
    - Pitch and roll should read approximately level
3. Use the `SENSORS` console command to confirm all readings look reasonable

## In-Flight Verification

### Speed Check

The simplest verification: fly at known speeds and confirm the tones match.

1. **Cruise speed** — you should hear **silence** (well above L/Dmax AOA)
2. **Best glide speed** (Vbg) — you should hear the **transition from pulsing to solid** tone (L/Dmax region). For example, if your Vbg is 87 knots, the first tone onset should start around that speed.
3. **Published approach speed** — you should hear the **solid "on speed" tone**
4. **Below approach speed** — you should hear **high-pitched pulsing** that speeds up as you slow further
5. **Near stall** — you should hear the **stall warning** (rapid high pulse)

If the tones are consistently at the wrong speeds (e.g., stall warning at approach speed, or silence at approach speed), the calibration needs to be re-done.

### Flap Check

Repeat the speed check with different flap settings to verify each flap position's calibration:

- The on-speed tone should correspond to approximately 1.3× Vs for each flap setting
- The stall warning should occur at approximately the same AOA margin from stall in each configuration

## Using Log Data

After a verification flight, download the log file and check:

1. **DerivedAOA values** — should be reasonable angles (typically 0° to 20° for normal flight)
2. **Tone transitions** — correlate tone changes with IAS to verify they match expected speeds
3. **Flap detection** — verify the `flapsPos` column shows the correct flap setting for each flight phase

## When to Recalibrate

Recalibrate if:

- Tones are consistently at wrong speeds
- You changed the mounting position or orientation of the controller
- You changed the AHRS algorithm (Madgwick ↔ EKF6)
- You modified the aircraft's aerodynamics (new prop, fairings, seals, etc.)
- You changed the weight distribution significantly (new equipment, moved battery, etc.)
- The R² of the original calibration was marginal (< 0.95)

## Cross-Checking with Dynon Percent Lift

If you have a Dynon SkyView, you can compare OnSpeed's AOA against the Dynon's **Percent Lift** reading (logged as `efisPercentLift`). While the two systems measure differently, they should show consistent trends — both should increase together as you slow down, and both should peak near the stall.
