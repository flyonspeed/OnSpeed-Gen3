# What is OnSpeed?

## A Note on the Term "ONSPEED"

The term *on speed* originated in military aviation to describe a flight condition in which the airplane is maintaining the angle of attack associated with the desired approach and landing condition. In this documentation, the convention **ONSPEED** is used: one word, capitalized, to denote a specific **angle-of-attack condition**, not an airspeed. ONSPEED represents a narrow band of angle of attack corresponding to the desired aerodynamic state for approach, landing, and certain maneuvering conditions. The airspeed associated with this angle of attack is not fixed — it varies with airplane weight, configuration, and load factor.

Pilots describe deviations from ONSPEED using airspeed-based language: "fast" or "slow" relative to ONSPEED. Although this mixes airspeed and angle-of-attack terminology, the convention is practical — airspeed is simply a surrogate for AOA.

## The Problem

Unintentional stalls remain a leading cause of fatal accidents in general aviation, resulting in fatality rates nearly 50 percent greater than those associated with non-stall mishaps. Most GA accidents occur during daylight VMC and light winds. A disproportionate number occur during the departure and maneuvering phases of flight — takeoff, initial climb, go-around, and base-to-final turns.

The two fundamental skills required to fly an airplane are controlling **angle of attack** to generate lift and managing **power** to generate speed and altitude. Yet angle of attack cannot be seen directly, and there is no cockpit instrument that explicitly displays power required. Pilots receive no direct feedback for the two most critical tasks they must perform.

The airspeed indicator is part of the problem:

- **Stall speed changes with weight** — a lighter airplane stalls at a lower airspeed
- **Stall speed changes with load factor** — a 45° banked turn increases stall speed by ~18%
- **Stall speed changes with configuration** — flaps change the wing's lift characteristics
- **Airspeed markings apply only at max gross weight and 1G** — the rest of the time, the pilot must do mental math

Pilots cannot realistically compute stall speed corrections for changing weight and G-loading during high-workload maneuvering flight.

## The Solution: Angle of Attack

The wing doesn't care about airspeed. It cares about the **angle between the wing chord and the oncoming air** — the angle of attack (AOA). A wing always stalls at the same **critical angle of attack** for a given configuration, regardless of weight, bank angle, or altitude. An airplane can stall at any airspeed and in any attitude, but critical AOA remains the same.

If you know your AOA, you know how hard the wing is working and how close you are to stalling. Always. In every configuration. Angle of attack solves the stall-speed math problem directly — no mental corrections required.

This concept is not new. Orville Wright patented an angle-of-incidence indicator in 1913. Military aviation adopted AOA systems and energy-management training decades ago, substantially reducing loss-of-control mishaps. OnSpeed brings that proven technology to experimental general aviation as open-source hardware and software.

## Why Audio? Directive vs. Descriptive Information

Conventional flight instruments present **descriptive** information. The pilot must first perceive the indication visually, then interpret it, then act on it. This "see—interpret—react" loop typically consumes on the order of one second. When pilots become task-saturated or distracted, this loop can break down entirely.

An AOA tone provides **directive** information. The pilot does not need to look at an instrument or infer meaning from a changing indication — the cue directly calls for action. Pilots can respond roughly twice as fast as with visual interpretation. There is no requirement to notice subtle airspeed decay, buffet onset, or changing attitude.

In the traffic pattern, your eyes should be outside — on the runway, on traffic, on terrain. An audio tone that changes pitch and pulse rate tells you everything you need to know about your energy state without diverting visual attention.

The tone logic is derived from systems originally developed for military carrier operations, and later adopted more broadly. Two aviation communities with extensive experience flying with continuous aural AOA cueing — military fighter aviation and glider operations — have demonstrated the effectiveness of direct auditory feedback for aircraft control.

## What the Tones Tell You

OnSpeed maps angle of attack to a continuous spectrum of audio cues. As you slow down (AOA increases), the tones progress:

| What You Hear | AOA Condition | What It Means | What to Do |
|---|---|---|---|
| **Silence** | Below L/D~MAX~ | You're fast — well above best-glide speed | No action needed |
| **Slow pulsing, low tone** | L/D~MAX~ to ONSPEED | Approaching the ONSPEED band | Decelerating normally |
| **Steady tone** | ONSPEED | Balanced effective power at any condition | Hold this — you're on speed |
| **Fast pulsing, high tone** | Below ONSPEED | Effective power is negative — unsustainable | Push: add power and/or reduce AOA |
| **Rapid buzz** | Near stall | Stall imminent — no aerodynamic margin | Immediate AOA reduction |

The logic is simple:

- **Slow tone → Push** something (throttle forward, nose down)
- **Fast tone → Pull** something (throttle back, ease back pressure to slow descent rate)
- **Steady tone → Hold** this condition

Because AOA is independent of weight and G-loading, the tones work correctly in turns, at any weight, and at any altitude.

## What ONSPEED Represents

ONSPEED is not just "approach speed." It corresponds to approximately **60% of the wing's maximum lift capability** (fractional lift) and is associated with:

- **Balanced effective power** at any flight condition
- **Maximum sustained turn rate**
- **Best angle of climb (V~X~)**
- **Maximum endurance**
- **V~REF~ (approach reference speed)**
- **Optimum low-altitude maneuvering**

The ONSPEED band is approximately ±1° of AOA — corresponding to an airspeed band of roughly ±2–3 knots at 1G. This is why the system works as both an approach reference and a maneuvering reference.

## How It Works

The OnSpeed Gen3 controller measures AOA using:

1. **Differential pressure** from a probe mounted in your wing or fuselage that senses the air angle
2. **Pitot pressure** for indicated airspeed
3. **An IMU (inertial measurement unit)** that measures pitch attitude and accelerations
4. **An AHRS algorithm** that fuses IMU data into stable pitch/roll estimates

From these inputs, it computes a **Derived AOA** — the angle between where the fuselage is pointed and where it's actually going. This is calibrated against known speeds to produce accurate AOA-based audio cues for your specific aircraft. Each flap setting gets its own calibration.

## Who Is This For?

OnSpeed is designed for **experimental/amateur-built aircraft**. It is not certified for use in type-certificated aircraft. It is a supplemental awareness tool — not a primary flight instrument.

Ideal for:

- RV-series homebuilts
- Other experimental aircraft with pitot/static systems
- Pilots who want stall awareness without a panel-mounted AOA gauge
- Aircraft with Dynon, Garmin, or MGL glass panels (for best integration)
- Standalone installations on aircraft with only steam gauges
