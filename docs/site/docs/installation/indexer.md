# Optional: AOA Indexer LEDs

The AOA indexer is a **NeoPixel LED strip** that provides a visual AOA indication — colored lights that change based on your angle of attack. It supplements the audio tones with a visual reference.

## When You'd Want One

- **Training**: Helps new OnSpeed users correlate tones with AOA during initial flights
- **Passengers**: Gives non-pilots a visual indication of flight state
- **Backup**: Visual indication if audio is muted or you're wearing noise-canceling headphones
- **Glare shield mount**: A row of colored LEDs at the top of the panel is easy to see in peripheral vision

## Hardware

- **NeoPixel LED strip or bar** — WS2812B or compatible addressable RGB LEDs
- **Data pin**: Connected to the NeoPixel data input on the controller
- **Power**: 5V from the controller (for small strips; larger strips may need separate 5V supply)
- **Ground**: Shared ground with the controller

## Wiring

1. Connect the NeoPixel **data in** to the controller's NeoPixel output pin
2. Connect **5V** and **GND** from the controller to the LED strip
3. For strips longer than a few LEDs, add a 300–500 ohm resistor on the data line near the first LED
4. Keep the data wire short — long data lines can cause signal issues with NeoPixels

## Mounting

Common mounting locations:

- **Glare shield top edge** — visible in peripheral vision during approach
- **Panel edge** — near the top of the instrument panel
- **Glareshield underside** — less visible but less distracting

Use the LED strip's adhesive backing or small screws/brackets to secure it.

## Color Scheme

The indexer LEDs display colors corresponding to AOA regions:

| AOA Region | Color | Meaning |
|-----------|-------|---------|
| Fast | Green | Well above approach speed |
| Approaching | Yellow | Approaching on-speed band |
| On Speed | Blue/White | Target approach speed |
| Slow | Red | Below target, getting slow |
| Stall | Flashing Red | Stall warning |
