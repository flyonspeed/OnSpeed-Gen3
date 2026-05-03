# X-Plane Simulator

The OnSpeed X-Plane plugin runs the same audio engine and indexer
renderer as the panel-mounted Gen3 inside X-Plane 12. Aircraft-state
inputs come from X-Plane datarefs (`sim/flightmodel/position/alpha`
for AOA, `sim/flightmodel/position/indicated_airspeed` for IAS,
`sim/flightmodel/forces/g_side` for lateral G); output is OpenAL
audio on the sim PC, an embedded floating window with the M5 indexer
graphics, and an optional USB-serial stream that drives a physically
connected M5Stack.

## What it does

- **Tone generation** — same `Envelope`, `AudioMixer`, `AudioOrchestrator`,
  and `Panning` code path as the firmware, via `onspeed_core::audio`.
  Tones, pulse rates, transition timing, and 3D pan from lateral G
  match the panel box sample-for-sample.
- **Embedded indexer** — a floating X-Plane window hosts the OnSpeed M5
  indexer renderer. Five display modes (the same five the M5 firmware
  ships with): AOA + numbers, attitude, narrow AOA, decel gauge, and
  G-history. Click the indexer body to cycle modes, or use the menu.
- **Physical M5 passthrough** — when configured, every display-serial
  frame the plugin builds is also pushed to a USB-serial port at
  115200 8N1. A Core2 plugged into the sim PC behaves the same way it
  does plugged into a real Gen3.
- **Per-aircraft persistence** — settings live at
  `Output/preferences/AOA-Tone-FlyOnSpeed-<acf>.prf`, so the RV-10's
  AOA setpoints don't bleed into the Cessna 172.

## Why use it

- Practice OnSpeed-driven approaches at home before flying them.
- Audition setpoint changes against a sim model before re-flying a
  calibration sortie.
- Drive a physical M5 from the sim — useful for development of the
  M5 firmware itself, and for pilots who want to rehearse with the
  same display they have in the panel.
- Fly the same audio cues from the same `onspeed_core` code that
  ships in the firmware. If a tone behavior is wrong in the sim it's
  wrong in the airplane, and vice versa.

## What's not supported

- **AOA convention.** The plugin reads X-Plane's `alpha` dataref, which
  is **wing AOA** in degrees. The OnSpeed firmware works in **body
  angle** (see [How OnSpeed Measures AOA](../calibration/how-aoa-works.md)).
  The two are linearly related per airframe but not equal. Plugin
  setpoints sit in wing-AOA units; firmware setpoints sit in
  body-angle units. Don't paste numbers between them without
  applying the wing-incidence offset.
- **Hardcoded setpoint defaults.** The plugin ships with generic
  RV-class defaults (LDmax 6.0°, OnSpeedFast 7.3°, OnSpeedSlow 9.6°,
  StallWarn 12.5°). Auto-derivation from per-aircraft datarefs is
  tracked in [#392](https://github.com/flyonspeed/OnSpeed-Gen3/issues/392).
  Edit the four AOA fields in the audio control window to match your
  airframe.
- **No per-flap calibration.** The firmware stores six setpoints per
  flap detent and switches sets when the lever moves. The plugin has
  one set of four setpoints, period. Per-flap support is tracked in
  [#393](https://github.com/flyonspeed/OnSpeed-Gen3/issues/393).
- **Boom probe / Madgwick / EKF6** — none of the firmware's sensor
  fusion runs. The plugin reads X-Plane's already-fused alpha and
  pipes it through a small median+mean smoother before handing it
  to `ToneCalc`.

## Pages

- **[Install](install.md)** — download the `.xpl`, drop it in the
  right per-arch directory under `Resources/plugins/`.
- **[Using the indexer](indexer.md)** — show/hide, mode cycling, what
  each of the five modes draws.
- **[Tethering a physical M5](m5-tethered.md)** — flash the M5
  firmware, USB-C cable to the sim PC, pick the port, fly the sim
  with the same M5 you'd hold in the airplane.
- **[Per-aircraft settings](settings.md)** — `.prf` location, fields,
  what each one does.
- **[Troubleshooting](troubleshooting.md)** — no sound, gray indexer,
  M5 not detected, settings not persisting.
