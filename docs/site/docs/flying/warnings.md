# Warnings and Alerts

OnSpeed provides three types of warnings in addition to the normal AOA tone progression.

## Stall Warning

**What you hear**: Rapid high-pitched pulsing at 20 pulses per second — a continuous buzz.

**When it sounds**: AOA exceeds the stall warning threshold for the current flap setting.

**What to do**: Reduce AOA immediately — lower the nose, add power, reduce bank angle. This is an imminent stall indication.

!!! danger "Never ignore the stall warning"
    The stall warning is the most critical safety feature of the system. It sounds even when audio is muted (if IAS is above the mute threshold). Always respond immediately.

### Stall Warning in Muted Mode

If you've pressed the mute button to silence normal tones, the stall warning **still sounds** — but only if both conditions are met:

1. AOA is above the stall warning threshold
2. IAS is above the mute-under-IAS setting (default: 25 knots)

The IAS check prevents false stall warnings on the ground or during taxi in windy conditions.

## Vno Overspeed Chime

**What you hear**: A distinct chime, repeating at a configured interval (default: every 3 seconds).

**When it sounds**: IAS exceeds your aircraft's Vno (max structural cruising speed).

**What to do**: Reduce power and/or adjust pitch to bring airspeed below Vno. Operating above Vno in turbulence risks structural damage.

### Configuration

- **Vno Speed**: Set to your aircraft's published Vno (knots)
- **Chime Interval**: How often the chime repeats (default: 3 seconds)
- **Chime Enabled**: Can be disabled if not desired

## G-Limit Warning

**What you hear**: A G-limit warning tone.

**When it sounds**: The measured G-loading exceeds the configured positive or negative G-limit.

**What to do**: Relax back pressure (for positive G) or push forward (for negative G) to return within the G-limit envelope.

### Configuration

- **Positive G-Limit**: Set to your aircraft's maximum positive load factor (e.g., +3.8G for normal category, +6.0G for aerobatic)
- **Negative G-Limit**: Set to your aircraft's maximum negative load factor (e.g., -1.52G for normal category)
- **Over-G Warning Enabled**: Can be disabled if not desired

## Audio Test Tones

During the `AUDIOTEST` sequence, you'll hear various tones including:

- **Startup chime** — played on power-up
- **Calibration mode tone** — indicates calibration wizard is active
- **Calibration saved tone** — confirms calibration data was saved
- **Data mark tone** — brief tone when a data mark is placed in the log

These are not warnings — they're system status indicators.

## Unexpected Audio Behavior

If you hear tones when you shouldn't (e.g., in cruise) or don't hear them when you should (e.g., on approach):

1. **Check calibration** — poor calibration can put setpoints at the wrong AOA values
2. **Check flap detection** — if the wrong flap position is selected, the wrong setpoints are active
3. **Check sensor biases** — drift in pressure biases can shift the AOA reading
4. **Check for moisture** — water in pneumatic lines causes erratic pressure readings

See [Troubleshooting](../troubleshooting/index.md) for detailed diagnosis steps.
