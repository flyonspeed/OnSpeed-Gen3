# Audio Tone Spec

This is the developer-facing reference for the OnSpeed aural-tone subsystem. It defines the audible output precisely enough that two independent implementations should produce indistinguishable audio at the speaker.

It is **not** a pilot-facing document. For "what do the tones mean in flight", see [What the Tones Mean](../flying/tone-map.md).

The canonical implementation is the Gen3 firmware on `master`. Where Gen2 (the Teensy-based reference Lenny tuned) differs, the spec calls out the deviation under a "Gen2 deviation" admonition — useful context for porters, but not a contract.

!!! warning "Bench verification still pending"
    The audible behaviour on real V4P hardware has not yet been scope-traced and compared side-by-side with a Gen2 unit. Until that's done, treat the spec as the design intent; if hardware diverges from the spec, file an issue with the trace.

## 0. Signal chain

```
sinewave(carrier_hz, carrier_amp) → envelope(gate ∈ [0,1])
   → tone_mixer(master_volume)
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
| `carrier_hz` | {400, 1600} Hz | Low / high tone (Audio.cpp `LOW_TONE_HZ` / `HIGH_TONE_HZ`) |
| `carrier_amp` | [0, 1] | Per-PPS amplitude (`ToneResult::fVolumeMult`); see §5 |
| `gate(t)` | [0, 1] | DAHDR envelope output |
| `master_volume` | [0, 1] | Pot-derived (`AudioPlay::SetVolume`) |
| `left_pan_gain`, `right_pan_gain` | [0, 2] | 3D-audio centripetal pan; can exceed 1.0 in a hard turn (see §8 for clipping behaviour) |

## 1. Tone-decision regions

`UpdateTones()` runs at 50 Hz and maps the current $(\text{IAS}, \text{AOA}, \text{flap-position-derived setpoints})$ to a tone mode. Decision precedence is top-down — first match wins.

| Region | Condition | Mode | Tone | PPS | Carrier amp |
|---|---|---|---|---|---|
| **MUTE** | `IAS` below the unmute threshold (see below) | `TONE_OFF` | none | n/a | 0 |
| **STALL** | `AOA ≥ stallWarningAOA` | `PULSE_TONE` | high (1600 Hz) | **20.0** (`HIGH_TONE_STALL_PPS`) | **1.0** |
| **APPROACH-STALL** | `onSpeedAOAslow < AOA < stallWarningAOA` | `PULSE_TONE` | high (1600 Hz) | linear **1.5 → 6.2** as AOA goes onSpeedSlow → stallWarn | linear **0.25 → 1.0** over the same range |
| **ON-SPEED** | `onSpeedAOAfast ≤ AOA ≤ onSpeedAOAslow` | `SOLID_TONE` | low (400 Hz) | n/a (sustained) | **0.25** |
| **PULSED-LOW** | `LDmaxAOA ≤ AOA < onSpeedAOAfast` AND `LDmaxAOA < onSpeedAOAfast` | `PULSE_TONE` | low (400 Hz) | linear **1.5 → 8.2** as AOA goes LDmax → onSpeedFast | **0.25** |
| **BELOW-LDMAX** | `AOA < LDmaxAOA` (or `LDmaxAOA ≥ onSpeedAOAfast`) | `TONE_OFF` | none | n/a | 0 |

!!! note "Full-flaps PULSED-LOW skip"
    When `LDmaxAOA ≥ onSpeedAOAfast` (typical full-flaps case), the PULSED-LOW region is skipped entirely. AOA below onSpeedFast goes straight to `TONE_OFF`.

!!! note "Uncalibrated configuration gate"
    If any of `fONSPEEDFASTAOA`, `fONSPEEDSLOWAOA`, `fSTALLWARNAOA` is `≤ 0`, `calculateTone` returns `None`. Defense-in-depth against an uninitialised or partially-saved configuration reaching the audio path.

### MUTE region details

The MUTE row above describes the AOA-decision-side gate. Two refinements:

- **Hysteresis**. Audio unmutes when `IAS ≥ iMuteAudioUnderIAS + 5` (kt) and re-mutes when `IAS < iMuteAudioUnderIAS`. The +5 kt unmute band prevents touchdown chatter as IAS oscillates a few knots around the threshold.
- **`iMuteAudioUnderIAS == 0` always-on sentinel**. A configured value of 0 means "never mute — audio is live from boot regardless of IAS." Useful for bench testing or non-airspeed-equipped installations.

??? note "Gen2 deviation"
    Gen2 uses a bare `IAS ≤ muteAudioUnderIAS` check with no hysteresis, producing audible chatter at touchdown as IAS oscillates around the threshold. Gen2 has no always-on sentinel; configuring 0 mutes below 0 kt (always-on by accident).

## 2. Per-pulse envelope (DAHDR shape)

For `PULSE_TONE` modes, every pulse is shaped by Delay → Attack → Hold → Decay, then a silent inter-pulse Gap, then auto-loop to the next pulse. All times in milliseconds.

```
pulse_period = 1000 / pps                        # full cycle
tone_length  = pulse_period - 3                  # envelope-active window
ramp_time    = 5  if pps == 20.0 (stall)         # STALL_RAMP_TIME
             = 15 otherwise                      # TONE_RAMP_TIME

silent_delay = tone_length / 2                   # silent first half
attack       = ramp_time
hold         = tone_length / 2 - 2 * ramp_time   # clamped ≥ 0
decay        = ramp_time
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
silent_delay = 60.97 ms       # = 1000 / LOW_TONE_PPS_MAX / 2
attack       = 15 ms          # TONE_RAMP_TIME
sustain      = ∞              # until tone changes
release      = 15 ms          # on NoteOff
```

The 60.97 ms entry delay is **unconditional** — applied every time `SOLID_TONE` is entered, regardless of what was playing before. The constant value (rather than a state-dependent value) keeps the spec stable across `UpdateTones()` cycles, so the running envelope's same-spec debounce correctly leaves the running Sustain alone.

## 3. Cadence requirement

The **measured pulse-to-pulse period at the speaker** must equal `1000 / pps` ms within ± 1 audio-sample of jitter, for any pps in the supported range $[1.5, 20.0]$.

If you record the audio and detect attack-onsets, the time from one attack-onset to the next must equal `1000/pps` ms within one audio-sample tolerance.

??? note "Implementation note"
    The implementation runs a sample-by-sample envelope state machine that auto-loops Decay → Gap → Delay. The Gap phase exists specifically to make the cycle period add up to `pulse_period` exactly; without it the cycle would be `tone_length = pulse_period - 3 ms` and pulses would run ~3 ms early per cycle (≈ 6% over at stall PPS).

??? note "Gen2 deviation"
    Gen2's hardware `IntervalTimer` schedules each pulse fire at exactly `1000/pps` µs. Equivalent cadence, different mechanism.

## 4. Transition policies

| From → To | Behaviour |
|---|---|
| any → MUTE | NoteOff: release current tone (15 ms, or 5 ms if stall). Gate at 0 after release completes. |
| MUTE → STALL / APPROACH-STALL / ON-SPEED / PULSED-LOW | Apply the new mode's entry envelope. For `PULSE_TONE`, the first pulse uses the standard `silent_delay = tone_length/2`. For `SOLID_TONE`, apply the 60.97 ms entry delay. |
| ON-SPEED → STALL / APPROACH-STALL | Sustain → Release; Release tail (15 ms or 5 ms) overlaps with the new pulse's first silent_delay. The new pulse spec uses the shortened `silent_delay = 60.97 ms` (instead of `tone_length/2`) so the first new pulse arrives within one perceptual half-period rather than waiting a full pulse_period. |
| ON-SPEED → PULSED-LOW | Same shortened-first-pulse treatment as ON-SPEED → STALL. |
| STALL / APPROACH-STALL / PULSED-LOW → ON-SPEED | NoteOn-during-active enters Release; the Release tail (15 ms) overlaps with the SOLID_TONE's 60.97 ms silent entry delay. |
| STALL ↔ APPROACH-STALL (PPS change, same carrier) | The currently-running pulse finishes naturally. The next pulse uses the new PPS's envelope shape. No release between same-tone PPS changes. |
| PULSED-LOW PPS change | Same as STALL ↔ APPROACH-STALL. |

??? note "Gen2 deviation: solid → pulsed snap"
    Gen2's Teensy envelope is configured with `releaseNoteOn(0)`, which causes a `noteOn` arriving during the previous note's release to **snap mult to 0 and start the new envelope from zero** rather than performing a graceful release ramp. At the SOLID → PULSED transition this produces an audible click — the sustained tone cuts off in roughly one sample. Gen3 performs a smooth Release ramp (`SameSpec` debounce + Sustain → Release path) and avoids the click.

## 5. Per-PPS volume ramp (carrier amplitude)

The carrier amplitude is determined by the AOA region:

| Region | `carrier_amp` | Formula |
|---|---|---|
| MUTE | 0 (no audio) | — |
| STALL | 1.0 (`STALL_VOL_MAX`) | constant |
| APPROACH-STALL | linear from 0.25 → 1.0 | `mapfloat(AOA, onSpeedSlow, stallWarn, STALL_VOL_MIN, STALL_VOL_MAX)` |
| ON-SPEED | 0.25 (`STALL_VOL_MIN`) | constant |
| PULSED-LOW | 0.25 (`STALL_VOL_MIN`) | constant |

The ramp is on the **amplitude**, not the gate, so each pulse keeps the same shape but gets louder as the aircraft approaches stall.

??? note "Implementation note"
    `calculateTone` returns `fVolumeMult` alongside `enTone` and `fPulseFreq`. The audio task multiplies it into the per-sample composition (§0), so the value takes effect at the next pump.

??? note "Gen2 deviation: input variable"
    Gen2's `mapfloat` takes `pps` as input rather than `AOA`. Mathematically equivalent on the linear segment because PPS and AOA are linearly related there. The numeric output is the same.

## 6. Master volume

A single multiplier $\text{master\_volume} \in [0, 1]$ is applied to all tone output. Read from a volume pot via `analogRead`, smoothed with α = 0.5 EMA, mapped from the configured analog range to `[0, 100]%`, then divided by 100 and applied at the tone-mixer stage. Updates take effect within ≈40 ms (one volume-poll iteration).

## 7. 3D audio (centripetal lateral pan)

A per-channel multiplier is applied at the amp stage:

```
left_pan_gain  = abs(-1 + channelGain)
right_pan_gain = abs( 1 + channelGain)
```

Where `channelGain = α · curve(|aLatCorr|) · sign(aLatCorr) + (1-α) · prevChannelGain`, with `α = 0.1`, `aLatCorr` the smoothed installation-corrected lateral G, and `curve` the `AUDIO_3D_CURVE` polynomial $-92.822 \cdot x^2 + 20.025 \cdot x$ clamped to $[0, 1]$.

| Aircraft state | `aLatCorr` | `channelGain` | left | right |
|---|---|---|---|---|
| Trim | 0 | 0 | 1 | 1 |
| Right turn (coordinated) | > 0 | > 0 | < 1 | > 1 |
| Left turn (coordinated) | < 0 | < 0 | > 1 | < 1 |

3D-pan updates fire from the housekeeping task at ~10 Hz. The α=0.1 EMA limits the per-update step to a small fraction of the swing — generally inaudible, but not strictly click-free if the multiplier changes during attack/hold/decay/sustain. See §8.

## 8. Click-free invariant

The audible signal must not contain large step discontinuities. The envelope's silent_delay and release phases provide a window during which other multipliers (`carrier_amp`, `master_volume`, pan gains) can change without producing audible clicks, because the gate is at or near zero.

In particular:

- `carrier_amp` step changes at AOA-region transitions land during the next pulse's silent_delay or during the previous mode's release tail.
- `master_volume` step changes are damped by the EMA pot smoothing AND fall during silent_delay/release windows for most pulses (silent_delay ≥ 23.5 ms at all PPS).
- Pan multiplier updates are damped by the α=0.1 EMA on `channelGain`.

!!! warning "Not strictly click-free at the tail"
    A small audible step is possible when `carrier_amp` or `master_volume` changes during the release ramp of an outgoing pulse — the gate is at a small but nonzero level, so the step bleeds through. The step is small in absolute terms (release-tail is at low gate level) and rarely audible. A future revision might require deferring `carrier_amp` writes until the gate has fully reached zero.

!!! note "Output clipping (V4P hardware-protection)"
    The per-sample composition `fL = fVolume × fStallVolumeMult × fLeftGain` is hard-clamped to ≤ 1.0 in the audio task to prevent 16-bit PCM clipping when the 3D pan gain (which can reach 2.0 in a hard turn) combines with master volume above ~50%. Input over-range produces an amplitude ceiling rather than a wrap-around glitch.

## 9. Voice (PCM) playback

Distinct from the tone path. PCM audio (e.g. stall-warning voice clip) plays through a separate audio source into the mixer with a `VOICE_BOOST = 3.0` factor above the tone gain. Voice and tones can mix simultaneously. Voice playback does NOT involve the DAHDR envelope.

??? note "Gen2 deviation"
    Gen2 boosts voice 10× the tone master (`mixer1.gain(2, 10*volumePercent/100.0)`). The 3× value on Gen3 is tuned for the V4P I2S DAC + amp circuit, where 10× would clip given the higher native amplitude of the source PCM and the upstream `fStallVolumeMult × fVolume × fGain` chain. Both numbers are correct for their respective hardware.

## 10. Audio switch

A physical switch (`g_bAudioEnable`) can mute all audio. When the switch is off, audio routes through `calculateToneMuted`, which lets ONLY the stall warning fire (high-tone, 20 PPS, full amplitude). All other regions are silenced.

When the switch flips back on, the next `UpdateTones()` call re-arms the appropriate mode for the current AOA.

??? note "Gen2 deviation: stall warning silenced when user-muted"
    Gen2's switch-off path silences everything, including the stall warning (`Tones.ino:34-40` sets the carrier amplitude to 0 unconditionally). Gen3 deliberately keeps the stall warning live regardless of the user-mute switch — silencing the stall warning is unsafe.

## 11. Latency targets

Two sources of delay between the AOA decision in `UpdateTones()` and the first audible attack of the new tone:

1. **Scheduling latency**: time until the implementation's "next tone update" hook fires. Gen3 calls SetTone synchronously from `UpdateTones()` — scheduling latency is 0.
2. **Envelope latency**: time from the new spec arming to the first non-zero gate sample = `silent_delay + attack`.

| Stimulus | Total latency |
|---|---|
| MUTE → STALL transition | `silent_delay(20 PPS) + attack(5 ms) = 28.5 ms` |
| ON-SPEED → STALL transition | `60.97 ms (shortened delay) + 5 ms (attack) ≈ 66 ms` |
| AOA chatter at any threshold | Must NOT produce audible artifacts. The current pulse finishes; transitions only happen at pulse boundaries. |
| `master_volume` change via pot | ≤ 40 ms perceived response |
| 3D-pan change | ≤ 100 ms perceived response (slow, intentional) |

??? note "Gen2 deviation"
    Gen2's `IntervalTimer` runs the pulse cadence; mode changes have to wait for the next timer fire to take effect, adding up to one `pulse_period` of the outgoing mode's PPS. Worst-case MUTE → STALL is therefore `1000/20 + 28.5 ≈ 78.5 ms` on Gen2.

## Reporting discrepancies

If you find:

- **Spec ambiguity**: file an issue. The goal is "two independent implementations produce indistinguishable audio at the speaker"; if it's not precise enough to support that, it's not done yet.
- **Hardware that doesn't match the spec**: file an issue with the listening-test or scope-trace evidence; that's a real bug.
- **Gen2 source code that disagrees with this spec on a non-Gen2-deviation point**: the Gen3 implementation is the canonical reference now, so this only matters if you're porting Gen3 behaviour back to Gen2 or if you spot an undocumented deviation.
