# AOA Indexer LEDs (Planned)

A visual AOA indexer — a strip of colored LEDs that changes color based on AOA region — has been discussed as a future feature. **The current firmware does not support it.**

Tracking work on this is captured in [issue #247](https://github.com/flyonspeed/OnSpeed-Gen3/issues/247). If you're interested in helping implement it, that's the place to start.

## Why a Visual Indexer

- **Training**: Helps new OnSpeed users correlate tones with AOA during initial flights.
- **Passengers**: Gives non-pilots a visual indication of flight state.
- **Backup**: Visual indication if audio is muted or you're wearing noise-canceling headphones.
- **Peripheral vision**: A row of colored LEDs at the top of the glare shield is easy to see without looking away from the outside view.

## Planned Color Scheme

When the feature ships, the indexer LEDs will follow the standard FAA green/yellow/red progression, matching the M5 secondary display and the web liveview indexer:

| AOA Region | Color | Meaning |
|-----------|-------|---------|
| Fast | Green | Above approach speed but safe |
| On Speed | Green | Target approach speed |
| Slow | Yellow | Below on-speed, approaching stall warning |
| Stall Warning | Red | Stall warning AOA reached |
| Stall | Flashing Red | Beyond stall warning — stall imminent |

Green covers the whole safe band (fast but safe, and on-speed); yellow and red escalate as AOA rises toward stall.

## Status

Not implemented in firmware. No pin is wired up for LED output today, and the `Adafruit_NeoPixel` library is not vendored. Adding the feature requires:

1. Re-adding the `Adafruit_NeoPixel` submodule under `software/Libraries/`.
2. Picking a free GPIO on the V4P hardware and wiring it in `Globals.h`.
3. A new `Indexer.cpp/h` module that maps AOA region to LED color and drives the strip.
4. Config UI for LED count, brightness, and color scheme.
