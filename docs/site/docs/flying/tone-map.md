# What the Tones Mean

This is the complete reference for all OnSpeed audio cues. Print this page and keep it in the cockpit until you've internalized the tone progression.

## Tone Regions

As you slow down (AOA increases), you progress through five regions:

```
 FAST ─────── L/Dmax ────── ONSPEED ────── SLOW ─────── STALL
 (silence)   (low-pitch    (ONSPEED       (high-pitch   (stall warning
              pulsing)      solid tone)     pulsing)      buzz)
  <50% lift   ~50% lift      ~60% lift    65-90% lift    >90% lift
```

### Complete Reference

| # | Region | Tone | Pulse Rate | Fractional Lift | What It Means | Pilot Action |
|---|--------|------|------------|-----------------|---------------|--------------|
| 1 | **Fast** | None | — | Below ~50% | Above best-glide speed. Positive energy margin. | No action needed. |
| 2 | **Approaching** | Low-pitch pulsing (400 Hz) | 1.5 → 8.2 pps | ~50–60% | Between L/D~MAX~ and ONSPEED. Decelerating into the approach range. | Normal deceleration. |
| 3 | **ONSPEED** | ONSPEED solid tone (400 Hz) | Solid (0 pps) | ~60% | Balanced effective power. Thrust and drag matched for current weight. | **Hold this.** |
| 4 | **Slow** | High-pitch pulsing (1600 Hz) | 1.5 → 6.2 pps | ~65–90% | Effective power is negative. Unsustainable condition. | **Push**: add power, reduce AOA, or both. |
| 5 | **Stall Warning** | Stall warning buzz (1600 Hz) | 20 pps | >90% | At the aerodynamic limit. Stall imminent. | **Unload**: reduce AOA immediately. |

### The Push/Pull Decision

The tone pattern tells you whether to push or pull:

- **Hear high-pitch pulsing?** → **Push** something: throttle forward, nose down, or both
- **Hear the ONSPEED solid tone?** → **Hold**: you're balanced
- **Hear low-pitch pulsing?** → **Pull** something: throttle back, allow pitch to increase
- **Hear the stall warning buzz?** → **Unload for control**: reduce AOA immediately

This logic works in any flight condition — straight and level, in a turn, climbing, descending.

### Pulse Rate Details

Within the pulsing regions, the pulse rate changes linearly:

- **Region 2 — Low-pitch pulsing** (L/D~MAX~ → ONSPEED): Starts at 1.5 pulses/sec at L/D~MAX~ (low-pitch slow tone), increases to 8.2 pulses/sec approaching the ONSPEED band (low-pitch fast tone)
- **Region 4 — High-pitch pulsing** (below ONSPEED → Stall Warning): Starts at 1.5 pulses/sec just below ONSPEED (high-pitch slow tone), increases to 6.2 pulses/sec near the stall warning threshold (high-pitch fast tone)

The **stall warning buzz** (Region 5) is a fixed 20 pulses/sec — an unmistakably urgent buzz. Volume also increases in the slow region, and the stall warning buzz overrides any audio muting.

## Key Performance Conditions

Each tone region corresponds to well-defined aerodynamic performance conditions:

### Stall Warning (>90% Lift)

- Aerodynamic limit — maximum instantaneous turn capability just prior to stall
- No sustainable maneuvering margin
- The wing cannot produce more lift

### ONSPEED (~60% Lift)

- Balanced effective power at any flight condition (thrust and drag balanced as a function of velocity)
- Maximum sustained turn rate
- V~REF~ (approach reference speed)
- Best angle of climb (V~X~)
- Maximum endurance
- Optimum low-altitude maneuvering
- Best blend of turn and glide performance, with appropriate energy for landing transition and safe stall margin

The ONSPEED band is approximately ±1° of AOA, resulting in an airspeed band of approximately ±2–3 knots at 1G.

### "Slightly Fast" (~50–55% Lift)

- Increased stall margin for gusty or turbulent conditions
- Fly here when conditions warrant extra margin

### L/D~MAX~ (~50% Lift)

- Maximum range (no-wind)
- Maximum range glide (best glide)
- Approximate best rate of climb (V~Y~)

### Maneuvering Speed (varies with aircraft G-limit)

- The fractional lift associated with V~A~
- Determined by dividing 100% by the airplane's positive G-limit
- For a normal-category airplane (3.8G limit): ~26% lift
- Below this value, the airplane will stall before reaching the structural limit

## Tone Transitions in Practice

Here's what you hear during a typical approach in an RV-4 (flaps 0°, ~2300 lbs):

| Speed (KIAS) | Tone | What's Happening |
|------|------|------|
| 120+ | Silence | Downwind — wing at low lift fraction |
| 100 | Silence | Base turn — still above L/D~MAX~ |
| ~87 | Low-pitch slow tone | At L/D~MAX~ — best glide speed |
| 80–75 | Low-pitch pulsing, speeding up | Decelerating through the approach range |
| ~72 | **ONSPEED solid tone** | ONSPEED — balanced effective power |
| 68 | High-pitch slow tone | Below ONSPEED — energy deficit |
| 65 | High-pitch fast tone | Getting slow — correct now |
| ~62 | **Stall warning buzz** | Stall warning — unload immediately |

!!! note "Your speeds will be different"
    These speeds are examples for a specific aircraft and weight. After calibration, the tones match **your** aircraft's characteristics. The beauty of AOA-based tones is that the same AOA thresholds apply regardless of your current weight or G-loading — the tones automatically account for bank angle, weight, and any other factor that changes stall speed.

## How Angle of Attack Relates to Speed

As airspeed decreases, angle of attack increases — but not linearly. AOA increases more rapidly as you slow down. Approximately half of the wing's lift-producing capability is used in the lower third of the airplane's speed range. This is why maneuvering near approach speeds can quickly become hazardous: angle of attack rises rapidly with even small increases in G.

The tone regions are spaced to reflect this reality. The "slow" region (high-pitch pulsing) covers a smaller speed range but a rapidly changing AOA range, giving the pilot progressively more urgent cueing as margin decreases.

## Muted Mode

If you press the audio mute button:

- All tones go silent
- **The stall warning buzz still sounds** (safety override)
- To unmute, press the button again

The stall warning in muted mode only fires if both conditions are met:

- AOA is above the stall warning threshold
- IAS is above the mute-under-IAS setting (default: 25 knots)

!!! info "Gen3 feature"
    The stall warning override in muted mode is a Gen3 feature. Gen2 units do not override the mute for stall warnings.
