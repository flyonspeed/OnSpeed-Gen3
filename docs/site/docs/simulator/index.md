# Simulator

Train with OnSpeed at home before you ever fly with it. The simulator
plugin produces the same audio cues your panel box would, driven by
your sim's flight model — so the muscle memory you build at the desk
translates directly to the cockpit.

- **[X-Plane Plugin](xplane-plugin.md)** — install the plugin into
  X-Plane 12, get OnSpeed tones in any aircraft.

## Why simulator practice matters

OnSpeed is a *learned* skill. The audio cues — pulse rate climbing as
you slow, the transition from low pulse to solid tone at OnSpeed, the
high-pulse warning above OnSpeed — only become useful when your hands
respond to them automatically. That takes repetition.

Sim training builds that loop without burning fuel, without weather
constraints, and without the consequences of a real stall. Fly slow
flight at altitude, fly approaches to minimums, fly accelerated stalls
in the pattern — over and over, until the pulse-to-stick relationship
is reflex.

## What the simulator gives you (and what it doesn't)

| ✅ The simulator is great for | ⚠️ The simulator can't replicate |
|---|---|
| Building muscle memory for tone-driven slow flight | Real airframe-specific stall behavior |
| Practicing precision approaches at OnSpeed | Real noise environment of your cockpit |
| Testing your audio panel routing before installation | Ground effect during landing flare |
| Showing the system to friends without firing up the plane | Calibration accuracy (sim uses generic AOA) |

The plugin uses X-Plane's published AOA (`sim/flightmodel/position/alpha`)
directly. That value is X-Plane's flight model output — it is *not*
your aircraft's calibrated AOA. So the *tones* will sound right relative
to where the sim thinks the airframe is in the AOA range, but the
specific *thresholds* (LDmax, OnSpeed, stall) are plugin defaults, not
your aircraft's calibrated setpoints. For most training use cases this
is fine; if you need aircraft-accurate setpoints in the sim, edit the
plugin's threshold UI to match your tuned values.

## Roadmap

The plugin currently plays tones only. Planned follow-ups:

- **Visual indexer overlay** — same chevron + donut + index bar visuals
  as the M5 display, rendered inside X-Plane. Tracked in
  [issue #221](https://github.com/flyonspeed/OnSpeed-Gen3/issues/221).
- **Drive a real M5 display from the sim** — plug your hardware M5 into
  the sim PC over USB, plugin streams `#1` frames at 20 Hz so the M5
  shows sim data exactly the way it'd show flight data. Tracked in
  [issue #228](https://github.com/flyonspeed/OnSpeed-Gen3/issues/228).
- **Microsoft Flight Simulator** — equivalent gauge for MSFS 2024.
  Tracked in [issue #225](https://github.com/flyonspeed/OnSpeed-Gen3/issues/225).
