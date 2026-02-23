# AOA Tone Simulator

Hear exactly what OnSpeed sounds like. Click **Sound On**, then move the AOA slider to sweep through the full range of audio cues — from silence through L/D~MAX~, ONSPEED, slow, and stall warning.

!!! tip "Turn on sound"
    Click **Sound On** and move the slider. Use headphones for the best experience. The tones match the actual firmware output: 400 Hz low tone, 1600 Hz high tone, with pulse rates interpolated exactly as in flight.

<div id="tone-simulator"></div>

## What You're Hearing

| Region | Tone | What It Sounds Like | Pilot Action |
|--------|------|-------|--------------|
| **Fast** (below L/D~MAX~) | Silence | Nothing | No action needed |
| **L/D~MAX~** | Low pulse (400 Hz, 1.5-8.2 pps) | Slow ticking that speeds up | Normal deceleration |
| **ONSPEED** | Solid low tone (400 Hz) | Steady hum | **Hold this** |
| **Slow** | High pulse (1600 Hz, 1.5-6.2 pps) | High-pitched ticking, speeding up | **Push** (add power or lower nose) |
| **Stall Warning** | High buzz (1600 Hz, 20 pps) | Urgent rapid buzz | **Unload immediately** |

The pulse rate increases as you approach the next boundary — listen for the tempo change as you sweep the slider through each region. The stall warning at 20 pulses per second is unmistakable.

## How This Maps to Flight

In the air, moving the slider left (lower AOA) is like speeding up, and moving it right (higher AOA) is like slowing down. The colored bands on the left show the tone regions. As you increase AOA, the indicator line sweeps upward through the bands — just like slowing down in the airplane. The aircraft pitches to match.

The tones are the same regardless of weight, G-loading, or altitude — OnSpeed uses angle of attack, not airspeed, so the cues are always correct.

For the complete tone reference, see [What the Tones Mean](tone-map.md). For how the tone regions map across the full flight envelope, see the [V-n Diagram](vn-diagram.md).
