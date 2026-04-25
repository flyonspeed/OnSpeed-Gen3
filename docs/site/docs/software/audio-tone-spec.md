# Audio Tone Spec

!!! warning "Work in progress — aspirational spec, not yet verified"
    This document captures what the OnSpeed aural tones **should sound like**, derived from the Gen2 (Teensy) reference implementation that Lenny tuned.

    The current Gen3 firmware on `master` **does not implement this spec**. PR [#102](https://github.com/flyonspeed/OnSpeed-Gen3/pull/102) is the in-flight attempt to close the gap. Even with that PR, on-aircraft scope traces and side-by-side listening tests against Gen2 hardware are still pending — Lenny is recording waveforms.

    Treat this as a target, not a contract. If you find Gen2 source code or hardware behavior that contradicts something here, the source code wins; please file an issue and update this page.

## Purpose

This is a developer-facing reference for anyone porting, auditing, or modifying the OnSpeed audio tone subsystem. It defines the audible output precisely enough that two independent implementations should produce indistinguishable audio at the speaker.

It is **not** a pilot-facing document. For "what do the tones mean in flight", see [What the Tones Mean](../flying/tone-map.md).

## Source of truth

The spec is derived from [`OnSpeed-Gen2/Software/OnSpeedTeensy_AHRS/Tones.ino`](https://github.com/flyonspeed/OnSpeed-Gen2/blob/master/Software/OnSpeedTeensy_AHRS/Tones.ino) and supporting files in the Gen2 repository. Every numeric constant in this document cites a Gen2 source line.

Gen2 is the reference because it sounds the way Lenny tuned the system on the original F-4-inspired Teensy prototype. Gen3's job is to reproduce that audible behavior on different hardware (ESP32-S3 + I2S DAC vs. Teensy + onboard DAC).

## 0. Signal chain

```
sinewave(carrier_hz, carrier_amp) → envelope(gate ∈ [0,1])
   → mixer(channel_0_gain = master_volume)
   → ampLeft(left_pan_gain), ampRight(right_pan_gain)
   → DAC L/R
```

For each output sample, the audible level is:

$$
\text{sample}_L = \sin(2\pi \cdot \text{carrier\_hz} \cdot t) \cdot \text{carrier\_amp} \cdot \text{gate}(t) \cdot \text{master\_volume} \cdot \text{left\_pan\_gain}
$$

$$
\text{sample}_R = \sin(2\pi \cdot \text{carrier\_hz} \cdot t) \cdot \text{carrier\_amp} \cdot \text{gate}(t) \cdot \text{master\_volume} \cdot \text{right\_pan\_gain}
$$

| Term | Domain | Meaning |
|---|---|---|
| `carrier_hz` | {400, 1600} Hz | Low / high tone (Gen2 lines 401, 404) |
| `carrier_amp` | {0, 0.25, 1.0} | Silenced / cruise / stall (Gen2 lines 857-860) |
| `gate(t)` | [0, 1] | DAHDR envelope output |
| `master_volume` | [0, 1] | Pot-derived (Gen2 `Volume.ino:21-28`) |
| `left_pan_gain`, `right_pan_gain` | [0, 1+] | 3D-audio centripetal pan (Gen2 `3DAudio.ino:11-13`) |

## 1. Tone-decision regions

`updateTones()` runs continuously and maps the current $(\text{IAS}, \text{AOA}, \text{flap-position-derived setpoints})$ to a tone mode. Decision precedence is top-down — first match wins.

| Region | Condition | Mode | Tone | PPS | Carrier amp |
|---|---|---|---|---|---|
| **MUTE** | `IAS ≤ muteAudioUnderIAS` | `TONE_OFF` | none | 20 (held for fast pickup) | 0 |
| **STALL** | `AOA ≥ stallWarningAOA` | `PULSE_TONE` | high (1600 Hz) | **20.0** (`HIGH_TONE_STALL_PPS`) | **1.0** |
| **APPROACH-STALL** | `onSpeedAOAslow < AOA < stallWarningAOA` | `PULSE_TONE` | high (1600 Hz) | linear **1.5 → 6.2** as AOA goes onSpeedSlow → stallWarn | linear **0.25 → 1.0** as AOA goes onSpeedSlow → stallWarn |
| **ON-SPEED** | `onSpeedAOAfast ≤ AOA ≤ onSpeedAOAslow` | `SOLID_TONE` | low (400 Hz) | n/a (held PPS=8.2 for next-state pickup) | **0.25** |
| **PULSED-LOW** | `LDmaxAOA ≤ AOA < onSpeedAOAfast` AND `LDmaxAOA < onSpeedAOAfast` | `PULSE_TONE` | low (400 Hz) | linear **1.5 → 8.2** as AOA goes LDmax → onSpeedFast | **0.25** |
| **BELOW-LDMAX** | `AOA < LDmaxAOA` (or `LDmaxAOA ≥ onSpeedAOAfast`) | `TONE_OFF` | none | 20 (held for next-state pickup) | 0 |

!!! note "Full-flaps PULSED-LOW skip"
    When `LDmaxAOA ≥ onSpeedAOAfast` (typical full-flaps case), the PULSED-LOW region is skipped entirely. AOA below onSpeedFast goes straight to `TONE_OFF`. (`Tones.ino:194-195`)

!!! note "Uncalibrated configs (Gen3 only)"
    Gen3 adds a defensive guard: if any of `fONSPEEDFASTAOA`, `fONSPEEDSLOWAOA`, `fSTALLWARNAOA` is `≤ 0`, return `None`. Gen2 doesn't have this check, but the situation can't arise on Gen2 because Gen2 ships with hardcoded sample setpoints. Adding the guard doesn't change behavior on a properly-calibrated unit.

## 2. Per-pulse envelope (DAHDR shape)

For `PULSE_TONE` modes, every pulse is shaped by Delay → Attack → Hold → Decay, then a silent inter-pulse Gap, then auto-loop to the next pulse. All times in milliseconds.

```
pulse_period = 1000 / pps                        # full cycle, Gen2 IntervalTimer rate
tone_length  = pulse_period - 3                  # envelope-active window  (Tones.ino:5)
ramp_time    = 5  if pps == 20.0 (stall)         # STALL_RAMP_TIME (Tones.ino:108-113)
             = 15 otherwise                      # TONE_RAMP_TIME

silent_delay = tone_length / 2                   # silent first half  (Tones.ino:105)
attack       = ramp_time
hold         = tone_length / 2 - 2 * ramp_time   # clamped ≥ 0  (Tones.ino:118)
decay        = ramp_time                         # (Tones.ino:119)
gap          = pulse_period - (silent_delay + attack + hold + decay)
             = 3 ms                              # inter-pulse silence

release      = ramp_time                         # only on NoteOff (mode change)
```

### Worked numeric values

| PPS | `pulse_period` | `silent_delay` | `attack` | `hold` | `decay` | `gap` | `release` |
|---|---|---|---|---|---|---|---|
| 1.5 (min) | 666.67 ms | 331.83 | 15 | 301.83 | 15 | 3 | 15 |
| 6.2 (high max) | 161.29 ms | 79.15 | 15 | 49.15 | 15 | 3 | 15 |
| 8.2 (low max) | 121.95 ms | 59.48 | 15 | 29.48 | 15 | 3 | 15 |
| 20.0 (stall) | 50.00 ms | 23.50 | **5** | 13.50 | **5** | 3 | **5** |

### Gate trace per pulse

```
1.0 ─                    ┌──────────┐
                         │  hold    │
                        ╱│          │╲
gate                   ╱ │          │ ╲
                      ╱  │          │  ╲
0.0 ─────────────────╱   │          │   ╲────────────
     │← silent_delay→│attack│       │decay│←  gap   →│
     0              SD   SD+A    SD+A+H  SD+A+H+D   period
```

### Solid tone shape

For `SOLID_TONE` (on-speed region):

```
silent_delay = 60.97 ms       # = 1000 / LOW_TONE_PPS_MAX / 2  (Tones.ino:63)
attack       = 15 ms          # TONE_RAMP_TIME
sustain      = ∞              # until tone changes
release      = 15 ms          # on NoteOff
```

The 60.97 ms entry delay is **unconditional** — applied every time `SOLID_TONE` is entered, regardless of what was playing before. (`Tones.ino:51-74`; the `if (tonePlaying!=SOLID_TONE)` guard at line 54 means "don't re-arm if already in solid", not "skip the delay sometimes".)

## 3. Cadence requirement

The **measured pulse-to-pulse period at the speaker** must equal `1000 / pps` ms within ± 1 audio-sample of jitter, for any pps in the supported range $[1.5, 20.0]$.

If you record the audio and detect attack-onsets, the time from one attack-onset to the next must equal `1000/pps` ms within one audio-sample tolerance. **Not** `tone_length` ms (which would be 3 ms early per cycle).

On Gen2, the `IntervalTimer` (Teensy hardware timer) provides the cadence by construction — it fires every `1000/pps` ms regardless of how long the envelope phases consume. Gen3 has no separate timer; the envelope's auto-loop must produce the same cadence.

## 4. Transition policies

| From → To | Behavior |
|---|---|
| any → MUTE | Release current tone (15 ms, or 5 ms if stall). After release, gate stays at 0. |
| MUTE → STALL / APPROACH-STALL / ON-SPEED / PULSED-LOW | Apply the new mode's entry envelope. For `PULSE_TONE`, the first pulse uses the standard `silent_delay = tone_length/2`. For `SOLID_TONE`, apply the 60.97 ms entry delay. |
| ON-SPEED → STALL / APPROACH-STALL | The first high-tone pulse uses a shortened silent delay of `60.97 ms` instead of `tone_length/2`. (`Tones.ino:101`) Gen2 also restarts its `IntervalTimer` at `pulse_period/2 + 60.97 ms` so the first pulse arrives within ~half a perceptual period. (`Tones.ino:11`) |
| ON-SPEED → PULSED-LOW | Same shortened-first-pulse treatment. |
| STALL / APPROACH-STALL / PULSED-LOW → ON-SPEED | Release the current pulsed envelope. Begin `SOLID_TONE` with its 60.97 ms entry delay. The release tail (15 or 5 ms) overlaps with the silent delay, so the perceived gap before the solid tone is roughly the 60.97 ms delay minus the release overlap. |
| STALL ↔ APPROACH-STALL (PPS change, same carrier) | The currently-running pulse finishes naturally. The next pulse uses the new PPS's envelope shape. No release between same-tone PPS changes. |
| PULSED-LOW PPS change | Same as stall ↔ approach-stall. |

## 5. Per-PPS volume ramp (carrier amplitude)

The carrier amplitude is determined by the AOA region (Gen2 sets it via `sinewave1.amplitude(volume)`):

| Region | `carrier_amp` | Source |
|---|---|---|
| MUTE | 0 (no audio) | `Tones.ino:46` |
| STALL | 1.0 (`HIGH_TONE_VOLUME_MAX`) | `Tones.ino:88` |
| APPROACH-STALL | linear from 0.25 → 1.0 as PPS goes 1.5 → 6.2 | `Tones.ino:88` |
| ON-SPEED | 0.25 (`SOLID_TONE_VOLUME`) | `Tones.ino:53` |
| PULSED-LOW | 0.25 (`LOW_TONE_VOLUME`) | `Tones.ino:83` |

The interpolation formula for APPROACH-STALL is:

```
carrier_amp = mapfloat(pps, HIGH_TONE_PPS_MIN, HIGH_TONE_PPS_MAX,
                            HIGH_TONE_VOLUME_MIN, HIGH_TONE_VOLUME_MAX)
            = mapfloat(pps, 1.5, 6.2, 0.25, 1.0)
```

The ramp is on the **amplitude**, not the gate, so each pulse keeps the same shape but gets louder as the aircraft approaches stall.

## 6. Master volume

A single multiplier $\text{master\_volume} \in [0, 1]$ is applied to all tone output downstream of the envelope. Gen2 reads it from a volume pot at `analogRead(VOLUME_PIN)`, smoothed with α = 0.5 EMA, mapped from `[volumeLowAnalog, volumeHighAnalog] → [0, 100]%`, then divided by 100 and applied to the tone-mixer channel gain. (`Volume.ino:1-32`)

`master_volume` updates can occur at any time; they should take effect within ≈40 ms (one Gen2 main-loop iteration ≈ pot smoothing window).

## 7. 3D audio (centripetal lateral pan)

A per-channel multiplier is applied at the amp stage:

```
left_pan_gain  = abs(-1 + channelGain)
right_pan_gain = abs( 1 + channelGain)
```

Where `channelGain = α · curve(|aLatCorr|) · sign(aLatCorr) + (1-α) · prevChannelGain`, with `α = 0.1`, `aLatCorr` the smoothed installation-corrected lateral G, and `curve` an `AUDIO_3D_CURVE` polynomial clamped to `[0, 1]`. (`3DAudio.ino:1-17`)

| Aircraft state | `aLatCorr` | `channelGain` | left | right |
|---|---|---|---|---|
| Trim | 0 | 0 | 1 | 1 |
| Right turn (coordinated) | > 0 | > 0 | < 1 | > 1 |
| Left turn (coordinated) | < 0 | < 0 | > 1 | < 1 |

3D-pan updates happen at a slower rate (~10 Hz from the Gen2 main loop). The α=0.1 EMA limits the per-update step to 10% of the swing, which is generally inaudible but not strictly click-free if the multiplier changes during attack/hold/decay/sustain. **This is a known imperfection in Gen2 that the spec inherits.**

## 8. Click-free invariant

The audible signal must not contain step discontinuities. All amplitude changes ≥ 1 LSB on the 16-bit DAC must occur while $\text{gate}(t) = 0$ OR during a smooth ramp. This is the load-bearing reason the envelope exists.

In particular:

- `carrier_amp` step changes at AOA-region transitions are masked by either Release (mode change) or by the silent_delay of the new mode.
- `master_volume` step changes are masked by the EMA pot smoothing AND by the silent_delay between pulses (≥ 30 ms at all PPS).
- Pan multiplier updates are masked by the EMA on `channelGain` AND by silent_delay/release periods.

## 9. Voice (PCM) playback

Distinct from the tone path. PCM audio (e.g. stall warning voice clip) plays through `AudioPlayMemory voice1` into mixer channel 2 with a separate gain (10× the tone master). Voice and tones can mix simultaneously. Voice playback does NOT involve the envelope. (`OnSpeedTeensy_AHRS.ino:581-588`, `Volume.ino:27`)

## 10. Audio switch

A physical switch (`switchState` in Gen2) can mute all audio. When the switch is off:

- `setFrequencytone()` sets `carrier_amp = 0` immediately, sets `toneMode = TONE_OFF`, returns. (`Tones.ino:34-40`)
- `tonePlayHandler()` returns immediately on the next `IntervalTimer` fire. (`Tones.ino:131-134`)

When the switch flips back on, the next `updateTones()` call re-arms the appropriate mode for the current AOA. **Stall warning is the one exception** — even when audio is muted by the user, the stall warning still fires.

## 11. Latency targets

| Stimulus | Time to audible response |
|---|---|
| MUTE → STALL transition | ≤ `silent_delay(20 PPS) + attack(5 ms) = 28.5 ms` from the AOA decision |
| ON-SPEED → STALL transition | ≤ `60.97 ms (shortened delay) + 5 ms (attack) ≈ 66 ms` |
| AOA chatter at any threshold | Must NOT produce audible artifacts. The current pulse finishes; transitions only happen at pulse boundaries. |
| `master_volume` change via pot | ≤ 40 ms perceived response |
| 3D-pan change | ≤ 100 ms perceived response (slow, intentional) |

## Where the implementations stand

This section is a snapshot — expect it to drift as Gen3 evolves. Last updated for `master` at the time of writing.

### Gen2 (reference)

Meets the spec by construction, since the spec is derived from Gen2's source. Two caveats worth stating explicitly:

- **§3 Cadence**: Gen2's hardware `IntervalTimer` fires at exactly `1000/pps` µs (line 992). Jitter is well below one audio sample.
- **§7 3D audio**: Gen2's `channelGain` step *can* land during attack/hold/decay/sustain because `Check3DAudio` runs from the main loop without coordination with the envelope. The α=0.1 EMA holds the per-update step to a small fraction of the swing. The spec inherits this behavior.

### Gen3 on `master` (current shipping behavior)

Gen3 master ships a different audio path — there is no DAHDR envelope, no per-PPS volume ramp, no inter-pulse Gap, no 60.97 ms solid entry delay. Tones are gated by hard amplitude switches at pulse edges, which produces audible clicks and a pulse cadence that runs faster than Gen2 (especially at stall warning, where measured PPS would land ~6% above the configured 20 PPS if the same envelope construction were used).

In practical terms: a pilot listening to Gen3 on `master` and Gen2 in the same airframe will hear that they are different. The spec above documents what Gen3 *should* do; PR [#102](https://github.com/flyonspeed/OnSpeed-Gen3/pull/102) is the in-flight attempt to make that true.

### Gen3 with PR #102

Closes the structural gap. Sections 0, 1, 2, 3, 4, 5, 8, 9, 10, 11 are intended to match the spec. Sections 6 and 7 inherit Gen3's existing behavior (volume pot read path, 3D pan computation) — neither path is touched by #102, so any divergence there is pre-existing and out of scope.

The PR carries 41 envelope unit tests (covering every phase transition, NoteOff termination, output continuity, and 6 cadence/Gap-specific tests added in the latest commit). A host-side cadence probe driving the production `MakePulseSpec` formulas confirms the measured PPS lands within 0.05% of the spec across the operating range.

**On-aircraft verification is still pending.** Lenny is recording scope traces from both Gen2 and Gen3 hardware to compare side-by-side. When that work completes, this section should be updated with the diff between observed waveform and the spec, not just the construction of the math.

## Reporting discrepancies

If you find:

- **Gen2 source code that contradicts this spec**: the source code wins. File an issue with the line reference and we'll correct the doc.
- **Gen3 hardware that doesn't match the spec**: file an issue with the listening-test or scope-trace evidence; that's a real bug.
- **Spec ambiguity**: file an issue. The goal is "two independent implementations produce indistinguishable audio at the speaker"; if it's not precise enough to support that, it's not done yet.
