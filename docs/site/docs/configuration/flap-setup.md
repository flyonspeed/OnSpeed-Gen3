# Flap Position Setup

OnSpeed needs to know your current flap position to select the correct AOA calibration curve and setpoints. Each flap setting has its own AOA-to-tone mapping because extending flaps changes the wing's lift characteristics.

## How Flap Detection Works

The system reads an analog value from a sensor (potentiometer or resistor ladder) and matches it to configured flap positions. The algorithm:

1. Reads the ADC value
2. Calculates midpoints between adjacent configured positions
3. Assigns the current flap position based on which range the reading falls into

For example, with three positions at ADC values 129, 158, and 206:

- Midpoint between 0° and 20° = (129 + 158) / 2 = **143**
- Midpoint between 20° and 40° = (158 + 206) / 2 = **182**
- ADC < 143 → **Flaps 0°**
- 143 ≤ ADC < 182 → **Flaps 20°**
- ADC ≥ 182 → **Flaps 40°**

## Potentiometer Setup (V4P Hardware)

The V4P board reads flap position from an external MCP3202 ADC (Channel 0, 12-bit resolution: 0–4095).

### Mechanical Linkage

Mount a potentiometer so that its shaft rotates with your flap handle:

- Use a rotary potentiometer linked to the flap handle via a pushrod, bellcrank, or cable
- The pot must produce a **different voltage at each flap position**
- The values can be ascending or descending — the algorithm handles both directions

### Recording Pot Values

For each flap position:

1. Set the flaps to the desired position (e.g., 0°)
2. Read the ADC value using the `FLAPS` console command
3. Note the displayed raw value
4. Enter this value as the **Pot Value** for that flap position in the configuration
5. Repeat for each flap position

### Configuration

In the web interface, for each flap position entry:

- **Degrees**: The flap deflection angle (e.g., 0, 20, 40)
- **Pot Value**: The ADC reading you recorded for this position

!!! tip "Leave margin between positions"
    Make sure adjacent pot values are far enough apart that vibration or pot jitter won't cause false transitions. A separation of at least 20 ADC counts between adjacent positions is recommended.

## Discrete Jumpers (V4B Hardware)

The V4B board uses a resistor ladder circuit to detect discrete flap positions via jumper wires.

### How It Works

Different combinations of jumper connections produce different analog voltages on the FLAP_PIN (GPIO 2). Each combination maps to a flap position.

### Setup

1. Wire the jumper resistor ladder according to the V4B schematic
2. For each flap position, connect the appropriate jumper combination
3. Read the analog value with the `FLAPS` command
4. Enter values in the configuration

## Verifying Flap Detection

Use the `FLAPS` console command at any time to check:

```
FLAPS
> Flap position: 0 degrees (raw value: 131, index: 0)
```

Move the flaps to each position and verify the correct degrees and index are displayed.

## Adding or Removing Flap Positions

If your aircraft has more or fewer flap settings than the default configuration:

1. Open the web configuration page
2. Add or remove flap position entries as needed
3. For each position, enter the degrees and pot value
4. **Each flap position also needs its own AOA calibration** — you'll fly the calibration wizard separately for each flap setting

!!! warning "Each flap position needs calibration"
    Adding a new flap position creates an uncalibrated entry with default AOA setpoints. You must fly the calibration wizard with that flap setting to establish proper tone thresholds.
