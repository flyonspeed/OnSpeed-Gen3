# What You Need

Before starting your OnSpeed installation, make sure you have the following.

## Required Components

- **OnSpeed Gen3 controller board** — assembled PCB with all sensors and ESP32-S3
- **Pneumatic tubing** — to connect pitot, AOA, and static ports (1/4" or 3/16" depending on your fittings)
- **AOA pressure probe** — mounted in the wing or fuselage to sense angle of attack differential pressure
- **Power wiring** — 12–28V from your aircraft bus, fused or through a circuit breaker (the system draws minimal current)
- **Audio wiring** — to connect to your audio panel or headset (see [Audio Wiring](../installation/audio.md) for options)
- **microSD card** — any standard microSD card for data logging (FAT32 formatted)

## Recommended

- **EFIS with serial output** — Dynon SkyView/HDX, Garmin G5/G3X, or MGL iEFIS. Provides IAS, OAT, and attitude data for better performance. See [EFIS Integration](../efis-integration/index.md).
- **Flap position sensor** — potentiometer mechanically linked to your flap handle, or jumper wires for discrete flap positions. Required for multi-flap calibration.
- **Laptop or phone with WiFi** — for configuration, calibration, and log download via the web interface.

## Optional

- **DS18B20 OAT sensor** — if you don't have an EFIS providing outside air temperature. Needed for density altitude corrections in standalone mode.
- **NeoPixel LED strip** — for a visual AOA indexer (colored LED bar on the glare shield)
- **M5Stack display** — for an external cockpit display
- **Boom probe** — a reference AOA probe for testing or calibration validation

## Tools for Installation

- Wire strippers and crimpers (for Molex or D-sub connectors)
- Soldering iron (for audio connections and any custom wiring)
- Heat shrink tubing
- Zip ties and adel clamps for securing tubing and wires
- Multimeter (for verifying connections)
- Level (for sensor calibration — the aircraft must be level on the ground)

## Skills

You should be comfortable with:

- Basic aircraft electrical wiring (12V DC, circuit breakers, grounds)
- Connecting pneumatic tubing (pitot/static plumbing)
- Using a web browser for configuration
- Flying slow-flight calibration sweeps (for the calibration wizard)

No programming or special software knowledge is needed for installation and operation. Firmware flashing is done via a web browser over WiFi, or with a simple USB tool.
