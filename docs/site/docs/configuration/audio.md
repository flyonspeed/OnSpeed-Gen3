# Audio Settings

Configure how OnSpeed generates and delivers audio tones.

## Volume Control

### Hardware Volume Pot

If you installed a volume potentiometer (V4P: MCP3202 ADC Channel 1):

- **Volume Enabled**: `true`
- **Low Analog**: ADC value at minimum volume (default: 170)
- **High Analog**: ADC value at maximum volume (default: 339)

The pot reading is linearly mapped between these values to produce 0–100% volume.

### Default Volume

If you don't have a hardware volume pot:

- **Volume Enabled**: `false`
- **Default Volume**: Set to your preferred level (0–100%)

## Mute Under IAS

The **Mute Under IAS** setting (default: **25 knots**) silences all tones when indicated airspeed is below this threshold. This prevents nuisance tones during:

- Taxi
- Engine run-up
- Sitting on the ground in wind

!!! note "Stall warning still overrides in some conditions"
    When the audio is muted by the pilot (button press), the stall warning can still sound if AOA exceeds the stall threshold AND IAS is above the mute-under-IAS value. The IAS threshold prevents false stall warnings during ground operations.

## 3D Audio (Lateral G Panning)

When **3D Audio** is enabled (`true`):

- The audio tone pans **left and right** based on lateral G-force
- In a right turn, the tone shifts toward the left ear (and vice versa)
- In coordinated flight (ball centered), the tone stays centered
- In a slip or skid, the tone shifts to one side

This provides **subconscious coordination awareness** — you'll notice the tone shift before you notice the slip/skid ball.

**Requirements**: Stereo audio wiring (both left and right channels connected). If you have mono wiring, disable 3D audio.

## Vno Overspeed Chime

Plays a chime when IAS exceeds your aircraft's Vno (max structural cruising speed):

- **Vno Speed**: Your aircraft's Vno in knots
- **Chime Enabled**: `true` to enable
- **Chime Interval**: Seconds between chimes (default: 3)

The chime repeats at the configured interval as long as you're above Vno.

## G-Limit Warning

When **Over-G Warning** is enabled (`true`), a warning tone plays when the load factor exceeds the configured limits:

- **Positive G-Limit**: Maximum positive G (default: 4.0)
- **Negative G-Limit**: Maximum negative G (default: -2.0)

## Audio Mute Button

The physical button on the controller (GPIO 12) provides:

- **Single press**: Toggle audio mute on/off
- When muted: Only the stall warning sounds (if conditions are met)
- The status LED reflects the current audio state

## Testing Audio

### AUDIOTEST Console Command

Via USB serial (921600 baud), type `AUDIOTEST` to run a 20-second audio test:

- Tests left and right channels independently
- Plays various tone types and pulse rates
- Runs as a background task (doesn't block the console)
- Reports "busy" if a test is already running

If you hear no audio during the test, check:

1. Audio wiring (see [Audio Wiring](../installation/audio.md))
2. Audio panel is set to the correct input
3. Audio panel volume is up
4. Headset is plugged in
