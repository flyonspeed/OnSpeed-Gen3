# How AOA Tones Work

OnSpeed maps your angle of attack to a **continuous spectrum of audio cues**. The tone pattern is derived from systems originally developed for military carrier operations. It provides **directive** feedback — telling you what to do, not just what's happening.

## The Tone Map

As AOA increases (you slow down or load up), you progress through five regions:

```
 FAST ─────── L/Dmax ────── ONSPEED ────── SLOW ─────── STALL
 (silence)   (low pulse)   (solid tone)   (high pulse)  (high buzz)
              ~50% lift      ~60% lift     65-90% lift    >90% lift
```

### Region by Region

#### 1. Silence — Below L/D~MAX~ (~50% Lift)

You're above best-glide speed. No tones play. The wing is working at less than half its capability. This is normal for cruise, downwind, and most maneuvering above approach speeds.

#### 2. Low Pulse — L/D~MAX~ to ONSPEED (~50–60% Lift)

A **low-pitched tone (400 Hz)** begins pulsing slowly (~1.5 pulses/sec), speeding up as you approach the ONSPEED band (up to ~8.2 pulses/sec). This region spans from best-glide speed (maximum range, maximum L/D) down to the ONSPEED condition.

This is the "slightly fast" region. Effective power is positive — the airplane can sustain this condition and has margin available. In gusty conditions, flying in this region provides extra stall margin.

**Pilot action**: Decelerating normally — no correction needed unless you want to slow further.

#### 3. Solid Low Tone — ONSPEED (~60% Lift)

The pulse stops and you hear a **steady, solid low-pitched tone (400 Hz)**. This is the ONSPEED condition — balanced effective power, where thrust and drag are matched for the current effective weight.

ONSPEED corresponds to:

- **V~REF~** (approach reference speed at 1G)
- **Maximum sustained turn rate**
- **Best angle of climb**
- **Maximum endurance**
- **Optimum low-altitude maneuvering**

**Pilot action**: Hold this. Fly to keep this tone steady.

#### 4. High Pulse — Below ONSPEED (~65–90% Lift)

The tone jumps to a **higher pitch (1600 Hz)** and starts pulsing — slowly at first (~1.5 pulses/sec), speeding up as AOA continues to increase (up to ~6.2 pulses/sec). Volume also increases.

This is the critical region. **Effective power is negative** — the current combination of power and angle of attack is unsustainable. The airplane must either descend, decelerate, or the pilot must correct. The faster the pulsing, the less margin remains.

**Pilot action**: **Push** — add power (push throttle forward) and/or reduce AOA (push forward on the stick/yoke). Both actions reduce the energy deficit.

#### 5. Stall Warning — Above ~90% Lift

A **rapid high-pitched pulsing (1600 Hz)** at 20 pulses per second — essentially a continuous buzz. The wing is at or near its maximum lift capability. Stall is imminent.

**Pilot action**: **Unload for control** — reduce AOA immediately. Ease forward pressure, add power, reduce bank angle. The wing cannot produce more lift; the only recovery is reducing the demand on it.

!!! danger "Never ignore the stall warning"
    The stall warning tone overrides audio muting. Even if you've pressed the mute button, the stall warning will still sound if AOA is above the stall threshold and IAS is above the mute-under-IAS setting.

## Push/Pull: The Decision Framework

The tone pattern provides a simple decision cue:

| Tone | Decision | Action |
|------|----------|--------|
| **High-pitched pulsing** (slow) | **Push** | Add power, reduce AOA, or both |
| **Solid tone** (ONSPEED) | **Hold** | Maintain current pitch and power |
| **Low-pitched pulsing** (fast) | **Pull** | Reduce power, increase AOA, or both |

When flying an approach at ONSPEED, the pilot need only respond to the cueing:

- A slow tone requires **pushing** something — throttle, control pressure, or both
- A fast tone requires **pulling** something — throttle back, ease pitch
- The logic works in any flight condition, not just straight-and-level approaches

## Tone Characteristics

| Region | Tone Pitch | Pulse Rate | Approx. Fractional Lift |
|--------|-----------|------------|-------------------------|
| Fast (silence) | — | — | Below ~50% |
| Approaching (L/D~MAX~ → ONSPEED) | Low (400 Hz) | 1.5 – 8.2 pps | ~50–60% |
| ONSPEED | Low (400 Hz) | Solid (no pulse) | ~60% |
| Slow (below ONSPEED) | High (1600 Hz) | 1.5 – 6.2 pps | ~65–90% |
| Stall Warning | High (1600 Hz) | 20 pps | >90% |

Within the pulsing regions, the pulse rate is **interpolated linearly** — you get a smooth, continuous indication of where you are. Faster pulses = closer to the next boundary.

## Why Continuous Cueing Works

The ONSPEED condition is indicated by a **steady tone rather than silence**. The continuous presence of the tone serves as confirmation of system operation. Silence means fast; the absence of tone is itself information.

This design means you always know the system is working when you need it most — during the approach and landing phase. If you're on speed and the tone disappears, something is wrong with the system, not with your flying.

## 3D Audio (Optional)

When enabled, OnSpeed uses the IMU's lateral acceleration to **pan the tones left and right** in your headset. If you're in uncoordinated flight (slipping or skidding), the tone shifts to one ear. Pilots can coordinate rudder inputs intuitively by "stepping on the tone" — applying rudder toward the ear where the tone is louder.

This provides sideslip awareness without requiring a visual scan of the slip/skid ball. 3D audio requires stereo wiring to your headset (see [Audio Wiring](../installation/audio.md)).

## Muted Mode

If you press the audio mute button:

- All normal tones are silenced
- The **stall warning still sounds** (safety override)
- Stall warning in muted mode requires both AOA above the stall threshold AND IAS above the mute-under-IAS setting

## Why This Works

The AOA tone eliminates the "see—interpret—react" loop of visual instruments. You don't have to:

- Compute stall speed for your current weight and G-loading
- Mentally adjust for bank angle in a turn
- Divert your eyes from the outside world to check a gauge
- Notice subtle airspeed decay before it becomes critical

The tones are calibrated per flap setting, so the same audio cue works whether you're clean, in approach config, or full flaps. Your ears learn the tone progression after a few flights. After that, you have subconscious energy awareness — you'll know you're getting slow before you consciously think about it.
