# Optional: OAT Temperature Sensor

An outside air temperature (OAT) sensor allows OnSpeed to compute density altitude and true airspeed corrections without relying on EFIS data.

## When You Need One

- **No EFIS connected** — if you're running OnSpeed standalone (no Dynon, Garmin, or MGL), the DS18B20 OAT sensor provides the temperature data needed for TAS corrections
- **EFIS doesn't provide OAT** — some older EFIS units may not output OAT on the serial interface

If your EFIS provides OAT data via serial (Dynon SkyView, Garmin G5/G3X, MGL all do), you probably **don't need** a separate OAT sensor. The firmware will use EFIS OAT when available, and falls back to the DS18B20 if EFIS data goes stale (v4.15+).

## Hardware

- **DS18B20** digital temperature sensor (OneWire protocol)
- Waterproof probe versions are available and recommended for aircraft use
- Accuracy: ±0.5°C from -10°C to +85°C

## Wiring

The DS18B20 connects to the controller via the OneWire protocol on **GPIO 14**:

1. **VCC** → 3.3V or 5V from the controller
2. **GND** → Ground
3. **Data** → GPIO 14 (OAT_PIN)
4. Add a **4.7kΩ pull-up resistor** between Data and VCC (required for OneWire)

## Mounting

Mount the sensor probe where it will measure **outside air temperature**:

- **Outside the cabin** — in the airstream, not inside the cockpit
- **Shielded from direct sun** — solar radiation heats the sensor and gives false readings
- **Away from engine heat** — don't mount near exhaust or engine cowling
- **In moving air** — a location with good airflow during flight (e.g., under a wing root fairing)

## Configuration

1. Connect to the OnSpeed web interface
2. Set **OAT Sensor** to `Enabled` in the configuration
3. Save and reboot

Verify the sensor is working by checking the `SENSORS` console command — the OAT reading should show a reasonable ambient temperature.

!!! note "Runtime configuration"
    The OAT sensor is a runtime config toggle (`bOatSensor`), not a compile-time option. You can enable/disable it from the web interface without reflashing firmware.
