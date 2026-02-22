# What the Tones Mean

This is the reference for all OnSpeed audio cues. Print this page and keep it in the cockpit until you've internalized the tone progression.

## Tone Regions

As you slow down (AOA increases), you progress through these regions:

```
 FAST ──────── L/Dmax ────── ON SPEED ────── SLOW ────── STALL
 (silence)    (low pulse)   (solid low)    (high pulse)  (high buzz)
              1.5-8.2 pps   no pulse       1.5-6.2 pps   20 pps
```

### Complete Reference

| # | Region | Tone | Pulse Rate | What You Hear | What It Means |
|---|--------|------|------------|---------------|---------------|
| 1 | **Fast** | None | — | Silence | Above best-glide speed. Normal for cruise, downwind, departure. |
| 2 | **Approaching** | Low (400 Hz) | 1.5 → 8.2 pps | Low-pitched pulse, speeding up | Between best-glide and approach speed. Getting into the approach range. |
| 3 | **On Speed** | Low (400 Hz) | Solid (0 pps) | Steady low-pitched tone | Target approach speed zone. **Fly to this tone.** |
| 4 | **Getting Slow** | High (1600 Hz) | 1.5 → 6.2 pps | High-pitched pulse, speeding up | Below approach speed. Add power or lower nose. |
| 5 | **Stall Warning** | High (1600 Hz) | 20 pps | Rapid high-pitched buzz | Stall imminent. **Reduce AOA immediately.** |

### Pulse Rate Details

Within the pulsing regions, the pulse rate changes linearly:

- **Region 2** (L/Dmax → On-Speed-Fast): Starts at 1.5 pulses/sec at L/Dmax, increases to 8.2 pulses/sec as you approach the on-speed band
- **Region 4** (On-Speed-Slow → Stall Warning): Starts at 1.5 pulses/sec just below on-speed, increases to 6.2 pulses/sec near the stall warning threshold

The **stall warning** (Region 5) is a fixed 20 pulses/sec — essentially a continuous buzz that's unmistakably urgent.

## Tone Transitions in Practice

Here's what you hear during a typical approach in an RV-4 (flaps 0°, ~2300 lbs):

| Speed (KIAS) | Tone | Phase of Flight |
|------|------|------|
| 120+ | Silence | Downwind |
| 100 | Silence | Base turn |
| ~87 | First low pulse | Approaching best glide (L/Dmax onset) |
| 80–75 | Low pulse, speeding up | Slowing toward approach speed |
| ~72 | **Solid low tone** | On speed — hold this |
| 68 | High pulse starts | Below on speed — add power |
| 65 | Faster high pulse | Getting slow — power now |
| ~62 | **Rapid buzz** | Stall warning — immediate recovery |

!!! note "Your speeds will be different"
    These speeds are examples for a specific aircraft and weight. After calibration, the tones will match **your** aircraft's characteristics. The beauty of AOA-based tones is that the same AOA thresholds apply regardless of your current weight or G-loading.

## Key Principles

1. **If you hear nothing, you're fine** — silence means you're fast. No action needed.

2. **The solid tone is your target** — on approach, fly to hear the steady low tone. That's your ideal approach speed.

3. **Pulsing means you're between zones** — the pulse rate tells you how close you are to the next zone. Faster pulsing = closer.

4. **High pitch = too slow** — if the tone jumps from low to high, you've gone below approach speed.

5. **Never ignore the buzz** — the 20 pps stall warning means stall is imminent. Always respond with immediate AOA reduction.

## Muted Mode

If you press the audio mute button:

- All tones go silent
- **The stall warning still sounds** (safety override)
- To unmute, press the button again

The stall warning in muted mode only fires if both conditions are met:

- AOA is above the stall warning threshold
- IAS is above the mute-under-IAS setting (default: 25 knots)
