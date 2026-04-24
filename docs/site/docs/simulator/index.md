# Simulator

Drive OnSpeed tones from a desktop flight simulator. The plugin runs the
same `onspeed_core` tone decision the panel box runs — same thresholds,
same pulse rates, same stall warning.

- **[X-Plane Plugin](xplane-plugin.md)** — install the plugin into X-Plane 12, get OnSpeed tones in any aircraft.

## Scope

The plugin reads X-Plane's AOA dataref (`sim/flightmodel/position/alpha`)
and pipes it through `onspeed_core` to the same `calculateTone()` call
the firmware uses. Output is OpenAL audio on the sim PC.

Because the dataref is the sim's flight-model AOA, the *tones* sound
right relative to where the sim thinks the airframe is, but the
*thresholds* (LDmax, OnSpeed, stall) are plugin defaults, not your
airplane's calibrated setpoints. The plugin's UI exposes all four
thresholds as editable fields — override them with your tuned values
if you want aircraft-specific behavior.

## Roadmap

Audio-only today. Planned:

- **Visual indexer overlay inside X-Plane** — same chevron/donut/bar widget as the M5 display. [Issue #221](https://github.com/flyonspeed/OnSpeed-Gen3/issues/221).
- **Drive a physical M5 display from the sim** — plugin streams `#1` frames at 20 Hz over USB. [Issue #228](https://github.com/flyonspeed/OnSpeed-Gen3/issues/228).
- **MSFS 2024 gauge** — equivalent for Microsoft Flight Simulator. [Issue #225](https://github.com/flyonspeed/OnSpeed-Gen3/issues/225).
