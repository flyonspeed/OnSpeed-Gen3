# Optional: External Display

OnSpeed can drive an external display via serial output, providing a cockpit-mounted visual readout of AOA, airspeed, and other flight data.

## M5Stack Display

The M5Stack is a small, self-contained display unit that receives data from OnSpeed over a serial connection.

### Wiring

- **OnSpeed TX**: GPIO 10 (DISPLAY_SER_TX)
- **Display RX**: Connect to the M5Stack's RX input
- **Ground**: Shared ground
- **Baud rate**: RS-232 level

### Display Data

The display serial output includes:

- Current AOA
- IAS
- Tone region indicator
- Flap position
- OAT

### Serial Output Format

The output format is configurable via `SERIALOUTFORMAT` in the configuration:

- **ONSPEED** — OnSpeed native format
- **G3X** — Garmin G3X-compatible format (for use with Garmin displays)

## Mounting

Mount the display where it's visible during approach without blocking your view of primary instruments. Common locations:

- Panel cutout
- Glareshield mount
- Side console

!!! note "Display is supplemental"
    The external display provides additional visual information. The primary OnSpeed interface remains the audio tones.
