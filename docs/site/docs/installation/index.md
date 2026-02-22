# Hardware Installation

This section covers physically installing the OnSpeed Gen3 controller in your aircraft.

## Installation Steps

Follow these in order:

1. **[Mounting the Controller](mounting.md)** — Choose a location and secure the box
2. **[Pneumatic Plumbing](pneumatics.md)** — Connect pitot, AOA, and static lines
3. **[Electrical Wiring](wiring.md)** — Power, sensors, flap position
4. **[Audio Wiring](audio.md)** — Connect to your audio panel or headset

## Optional Accessories

- **[AOA Indexer LEDs](indexer.md)** — NeoPixel LED bar for visual AOA indication
- **[OAT Sensor](oat-sensor.md)** — DS18B20 temperature sensor for standalone installations
- **[External Display](external-display.md)** — M5Stack cockpit display
- **[Boom Probe](boom-probe.md)** — Reference AOA probe for testing

## Before You Start

!!! note "Plan your installation before drilling holes"
    Read through all the installation pages first. The location of the controller box affects pneumatic line routing, wire lengths, and audio cable runs. Think about maintenance access — you'll need to reach the SD card slot and occasionally connect a USB cable.

### Orientation Matters

The controller box contains an IMU that measures pitch and roll. You can mount the box in various orientations, but you **must configure the orientation in software** to match your physical mounting. See [First-Time Setup](../configuration/first-time-setup.md) for orientation configuration.

When done with installation, proceed to [First-Time Setup](../configuration/first-time-setup.md) to configure the system.
