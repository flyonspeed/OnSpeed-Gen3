# Electrical Wiring

The OnSpeed controller requires power, and optionally connects to flap position sensors, EFIS serial output, and other accessories.

## Power Supply

The controller accepts **12–28V DC** input (standard aircraft electrical system voltage). An on-board L7805ABD2T-TR voltage regulator provides the 5V needed by the electronics.

- **Wire gauge**: 22 AWG or larger is sufficient (the system draws minimal current)
- **Circuit protection**: Install a 1A circuit breaker or fuse on the power feed
- **Ground**: Connect to the aircraft's ground bus

!!! warning "Use a circuit breaker"
    Like any avionics installation, the OnSpeed power feed should have its own circuit breaker or fuse. This protects against shorts in the wiring and allows you to disable the system in flight if needed.

## Flap Position Sensor

OnSpeed needs to know your flap position to select the correct AOA calibration curve. There are two methods depending on your hardware version.

### Potentiometer (V4P Hardware)

The V4P board includes an external MCP3202 12-bit ADC. A potentiometer mechanically linked to your flap handle provides a variable voltage that maps to flap position.

- **ADC Channel**: CH0 on the MCP3202 (CS pin: GPIO 5)
- **Wiring**: Connect the potentiometer wiper to the ADC input, with power and ground to the pot's end terminals
- **Calibration**: Record the ADC reading at each flap position and enter them in the configuration

See [Flap Position Setup](../configuration/flap-setup.md) for calibration.

### Discrete Jumpers (V4B Hardware)

The V4B board uses a **resistor ladder** with jumper wires to indicate discrete flap positions. Different combinations of jumpers produce different analog voltages, each corresponding to a flap setting.

- **Input pin**: FLAP_PIN (GPIO 2)
- **Wiring**: Connect jumper wires from the flap handle mechanism to the controller's flap input through the appropriate resistors
- **Positions**: Typically 2–3 discrete positions (e.g., 0°, 20°, 40°)

## EFIS Serial Connection

If you have a glass panel EFIS, connecting it to OnSpeed provides IAS, OAT, attitude, and other data that improves system performance.

- **OnSpeed RX pin**: GPIO 11 (through ADM3202 RS-232 level shifter)
- **Baud rate**: 115200, 8N1
- **Wiring**: Connect your EFIS's serial **TX** output to the OnSpeed **RX** input

!!! note "TX to RX"
    Serial connections are crossed: the EFIS transmit pin connects to the OnSpeed receive pin. If you wire TX-to-TX, no data will flow.

See [EFIS Integration](../efis-integration/index.md) for EFIS-specific setup.

## Volume Control

### Potentiometer (V4P)

The volume control uses MCP3202 ADC Channel 1. Wire a potentiometer to provide variable voltage for volume control.

### Default Volume

If you don't install a volume potentiometer, set the `VOLUME ENABLED` option to `false` in the configuration and use the `DEFAULT` volume setting (0–100%).

## Button / Switch

A momentary push button connected to GPIO 12 provides:

- **Single press**: Toggle audio mute on/off
- **Hold 5 seconds**: Reboot the system

The button uses an internal pull-up, so wire between GPIO 12 and ground.

## Status LED

An LED on GPIO 13 provides a heartbeat indicator showing the system is running. This is optional but useful for confirming the controller is powered and operational.

## Wiring Summary

| Connection | Pin | Direction | Notes |
|-----------|-----|-----------|-------|
| Power (12–28V) | VIN | Input | Through circuit breaker |
| Ground | GND | — | Aircraft ground bus |
| EFIS Serial | GPIO 11 (RX) | Input | RS-232 level, 115200 baud |
| Flap Pot (V4P) | MCP3202 CH0 | Input | Through external ADC |
| Volume Pot (V4P) | MCP3202 CH1 | Input | Through external ADC |
| Audio Left | I2S DOUT | Output | See [Audio Wiring](audio.md) |
| Audio Right | I2S DOUT | Output | Stereo via I2S |
| Button | GPIO 12 | Input | Momentary, pull-up, to GND |
| Status LED | GPIO 13 | Output | Optional heartbeat |
| OAT Sensor | GPIO 14 | Input | Optional DS18B20 OneWire |
| Display TX | GPIO 10 | Output | Optional M5Stack serial |
| Boom RX | GPIO 3 | Input | Optional boom probe |
