# What is OnSpeed?

OnSpeed is an **open-source Angle of Attack (AOA) indicator** that tells you how close you are to stalling — through audio tones in your headset, not a gauge on the panel.

## The Problem

Loss of control is the #1 cause of fatal accidents in general aviation. Most stall/spin accidents happen in the traffic pattern — low altitude, distracted pilots, turning base-to-final. The airspeed indicator isn't much help because stall speed changes with:

- **Weight** — lighter aircraft stall at a lower airspeed
- **Bank angle** — load factor in turns increases stall speed
- **G-loading** — pulling back adds G's and raises stall speed
- **Flap setting** — flaps change the wing's lift characteristics
- **Altitude** — indicated stall speed stays roughly constant, but TAS changes

## The Solution: Angle of Attack

The wing doesn't care about airspeed. It cares about the **angle between the wing chord and the oncoming air** — the angle of attack (AOA). A wing always stalls at roughly the same AOA, regardless of weight, bank angle, or altitude.

If you know your AOA, you know how close you are to stalling. Always. In every configuration.

## Why Audio?

Military carrier-landing systems have used aural AOA cues since the 1960s. The reason: **you don't have to look at anything**. In the traffic pattern, your eyes should be outside — on the runway, on traffic, on the terrain. An audio tone that changes pitch and pulse rate tells you everything you need to know about your energy state without taking your eyes off the world.

OnSpeed provides:

- **Silence** when you're fast (cruise, downwind) — no distraction
- **Progressive tones** as you slow down — subconscious awareness
- **An "on speed" solid tone** at your target approach speed — fly to this
- **Urgent pulsing** when you're too slow — add power now
- **A continuous stall warning** — take immediate action

## How It Works

The OnSpeed Gen3 controller measures AOA using a combination of:

1. **Differential pressure** from a probe mounted in your wing or fuselage that senses the air angle
2. **Pitot pressure** for indicated airspeed
3. **An IMU (inertial measurement unit)** that measures pitch attitude and accelerations
4. **An AHRS algorithm** that fuses IMU data into stable pitch/roll estimates

From these inputs, it computes a **Derived AOA** — the angle between where the fuselage is pointed and where it's actually going. This gets calibrated against known speeds to produce accurate AOA-based audio cues for your specific aircraft.

The system connects to your headset through your audio panel (or directly) and plays tones that map to AOA regions. Each flap setting gets its own calibration, so the tones are correct whether you're clean, in the approach config, or full flaps.

## Who Is This For?

OnSpeed is designed for **experimental/amateur-built aircraft**. It is not certified for use in type-certificated aircraft. It's a supplemental awareness tool — not a primary flight instrument.

Ideal for:

- RV-series homebuilts
- Other experimental aircraft with pitot/static systems
- Pilots who want stall awareness without a panel-mounted AOA gauge
- Aircraft with Dynon, Garmin, or MGL glass panels (for best integration)
- Standalone installations on aircraft with only steam gauges
