# Troubleshooting: No Audio

You're not hearing any tones through your headset. Work through these checks in order.

## Check 1: Is IAS Above the Mute Threshold?

OnSpeed mutes all audio when indicated airspeed is below the **Mute Under IAS** setting (default: 25 knots).

- On the ground with no wind, you'll hear nothing — this is normal
- If testing on the ground, you can temporarily lower the mute threshold, or use the `AUDIOTEST` command which bypasses the IAS check

## Check 2: Is Audio Muted by Button?

The controller button (GPIO 12) toggles audio mute on/off.

- Press the button once to toggle
- The status LED indicates the current state
- Even when muted, `AUDIOTEST` should still produce audio
- The stall warning also overrides mute

## Check 3: Run AUDIOTEST

Connect to the USB serial console (921600 baud) and type:

```
AUDIOTEST
```

This runs a 20-second audio test sequence that plays tones on left and right channels. If you hear the test tones, the audio hardware is working and the problem is in your wiring path, volume, or the AOA conditions aren't triggering tones.

If you hear nothing from AUDIOTEST, the issue is either the audio hardware or the wiring from the controller to your headset.

## Check 4: Volume Setting

- If using a hardware volume pot: is it turned up?
- If using default volume: check the `DEFAULT` volume setting (should be > 0, typically 100)
- If the volume pot is enabled but you don't have one installed, the system may read a zero or random value. Set **Volume Enabled** to `false` and use the default volume instead.

## Check 5: Audio Wiring

Verify the physical audio connection:

1. Check that the audio output is connected to the correct input on your audio panel
2. Verify the audio panel has the correct input channel selected and enabled
3. Verify the audio panel volume for that channel is turned up
4. Check that the audio panel isn't muting the input (some panels mute music inputs during transmit)
5. Verify the ground connection between OnSpeed and the audio panel

Use a multimeter to check continuity from the I2S audio output pins to the audio panel input.

## Check 6: Are Pressure Sensors Working?

If the pressure sensors aren't reading correctly, the system may think IAS is below the mute threshold even when flying.

Use the `SENSORS` console command and check:

- **Pfwd pressure** — should show approximately 0 PSI on the ground, positive in flight
- **IAS** — should show reasonable airspeed in flight

If pressure readings are stuck at 0 or max, check:

- Pneumatic line connections
- Sensor SPI wiring
- Sensor bias calibration

## Still No Audio?

If all of the above check out:

1. Try reflashing the firmware via OTA or USB
2. Check for cold solder joints on the I2S DAC output
3. Try connecting headphones directly to the audio output (bypassing the audio panel) to isolate whether the issue is in the controller or the aircraft wiring
