# Audio Tone Spec

!!! warning "Work in progress — aspirational spec, not yet bench-verified"
    This document captures what the OnSpeed aural tones **should sound like**, derived from the Gen2 (Teensy) reference implementation that Lenny tuned, with deliberate adjustments where Gen2's behaviour was inferior on safety grounds (stall warning silenced by the audio switch, no IAS-mute hysteresis, etc. — flagged inline).

    The current Gen3 firmware on `master` **does not implement most of this spec**. PR [#102](https://github.com/flyonspeed/OnSpeed-Gen3/pull/102) is the in-flight attempt to close the gap; once merged, Gen3 should match the spec to the degree the spec itself does. Either way, on-aircraft scope traces and side-by-side listening tests against Gen2 hardware are still pending — Lenny is recording waveforms.

    Treat this as a target, not a contract. If you find Gen2 source code that contradicts something here, the source code wins UNLESS the spec explicitly flags the difference as a deliberate Gen3 improvement; please file an issue and we'll update the page.

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
| `left_pan_gain`, `right_pan_gain` | [0, 2] | 3D-audio centripetal pan (Gen2 `3DAudio.ino:11-13`); can exceed 1.0 in a hard turn (see §8 for clipping behaviour) |

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

### MUTE-region details

The MUTE row above describes the AOA-decision-side gate. Two implementation refinements live alongside it on Gen3 (and are part of the spec's desired behavior because they fix real Gen2 limitations):

- **Hysteresis (Gen3 PR #113)**. Gen2 uses a bare `IAS ≤ muteAudioUnderIAS` check, which produces audible chatter at touchdown when IAS oscillates a few knots around the threshold. Gen3 unmutes at `iMuteAudioUnderIAS + 5 kt` and re-mutes at `iMuteAudioUnderIAS`. The +5 kt unmute band is normative — without it, every Gen3 landing rollout would chatter as IAS bounced through the threshold during the deceleration. Gen2 does not have this; pilots familiar with Gen2 hear the chatter and know to ignore it.
- **`iMuteAudioUnderIAS == 0` always-on sentinel (Gen3)**. A configured value of 0 means "never mute — audio is live from boot regardless of IAS." Useful for bench testing or non-airspeed-equipped installations. Gen2 has no equivalent (a 0 value would mute below 0 kt, which is always-on by accident; Gen3 makes the intent explicit).

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

!!! note "Release time on Gen2 is implicit"
    Gen2's pulsed-tone setup block (`Tones.ino:95-126`) doesn't call `envelope1.release(...)` per pulse — the release time is whatever the previous solid-tone setup left in the Teensy `AudioEffectEnvelope` (typically `TONE_RAMP_TIME` from `Tones.ino:68`, or the boot default before the first solid tone). The spec specifies `release = ramp_time` per pulse as the desired deterministic behaviour; Gen3 PR #102 sets it explicitly. Practical impact on Gen2 is small because release only matters at mode-change boundaries, where it overlaps with the next mode's silent delay.

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

!!! note "Input variable: PPS vs AOA"
    Gen2's mapfloat takes `pps` as input (`Tones.ino:88`), so the ramp is mathematically a function of PPS. Since PPS itself is a linear function of AOA in the APPROACH-STALL region (same start/end points), `mapfloat(AOA, onSpeedSlow, stallWarn, MIN, MAX)` produces identical numeric results. Gen3 PR #102 maps from AOA directly. Either is correct on the linear segment; the spec doesn't differentiate.

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

The audible signal must not contain large step discontinuities. The envelope's silent_delay and release phases provide a window during which other multipliers (`carrier_amp`, `master_volume`, pan gains) can change without producing audible clicks, because the gate is at or near zero.

In particular:

- `carrier_amp` step changes at AOA-region transitions land during the next pulse's silent_delay or during the previous mode's release tail.
- `master_volume` step changes are damped by the EMA pot smoothing AND fall during silent_delay/release windows for most pulses (silent_delay ≥ 23.5 ms at all PPS).
- Pan multiplier updates are damped by the α=0.1 EMA on `channelGain`.

!!! warning "Not strictly click-free at the tail"
    Both Gen2 and Gen3 can produce a small audible step when `carrier_amp` or `master_volume` changes during the release ramp of an outgoing pulse — the gate is at a small but nonzero level, so the step bleeds through. In Gen2 this occurs because every pulse's `setFrequencytone` call writes `sinewave1.amplitude(volume)` (`Tones.ino:96`) before the new noteOn fires, and the previous pulse's release may still be ramping down. The step is small in absolute terms (release-tail is at low gate level) and rarely audible, but the spec doesn't claim absolute click-freedom — only that the envelope structure minimises perceptual discontinuity. A future revision might require deferring `carrier_amp` writes until the gate has fully reached zero.

!!! note "Output clipping (Gen3 V4P hardware-protection)"
    Gen3 PR #102 hard-clamps the per-sample composition `fL = fVolume × fStallVolumeMult × fLeftGain` to ≤ 1.0 (`Audio.cpp:663-664`) to prevent 16-bit PCM clipping when the 3D pan gain (which can reach 2.0 in a hard turn) combines with master volume above ~50%. Gen2's Teensy mixer clamps differently (or saturates downstream in the analog amp); the audible behaviour may diverge in steep banks at high master volume. Both implementations hit the limit of the 16-bit signed PCM range; neither is strictly correct vs. the multiplicative chain in §0.

## 9. Voice (PCM) playback

Distinct from the tone path. PCM audio (e.g. stall-warning voice clip) plays through a separate audio source into the mixer with a per-implementation boost above the tone gain. Voice and tones can mix simultaneously. Voice playback does NOT involve the DAHDR envelope.

| Implementation | Voice gain | Source |
|---|---|---|
| Gen2 | **10×** the tone master | `Volume.ino:27` (`mixer1.gain(2, 10*volumePercent/100.0)` vs ch.0 at `volumePercent/100.0`) |
| Gen3 PR #102 | **3×** the tone master | `Audio.cpp:589` (`VOICE_BOOST = 3.0f`) |

The 3× value on Gen3 was tuned for the I2S DAC + amp circuit on the V4P board, where 10× would clip given the higher native amplitude of the source PCM and the upstream `fStallVolumeMult × fVolume × fGain` chain. Both numbers are correct for their respective hardware; the spec doesn't pick a winner because the right value depends on the downstream audio path.

## 10. Audio switch

A physical switch (`switchState` in Gen2, `g_bAudioEnable` in Gen3) can mute all audio. When the switch is off:

- `setFrequencytone()` sets `carrier_amp = 0` immediately, sets `toneMode = TONE_OFF`, returns. (`Tones.ino:34-40`)
- `tonePlayHandler()` returns immediately on the next `IntervalTimer` fire. (`Tones.ino:131-134`)

When the switch flips back on, the next `updateTones()` call re-arms the appropriate mode for the current AOA.

!!! note "Stall warning when user-muted — Gen3 deviation"
    Gen2's switch-off path silences everything, including the stall warning. Gen3 routes the user-muted path through `calculateToneMuted` so a stall-warning-grade AOA still fires the high-tone pulse train even when the pilot has muted the audio. This is a deliberate Gen3 safety improvement; pre-existing OnSpeed Gen2 hardware does not have this behaviour. Spec leans Gen3-side: silencing the stall warning is unsafe regardless of source-of-truth precedent.

## 11. Latency targets

Two sources of delay between the AOA decision in `updateTones()` and the first audible attack of the new tone:

1. **Scheduling latency**: time until the implementation's "next tone update" hook fires.
   - Gen2: bounded by the `IntervalTimer` period at the *outgoing* mode's PPS (e.g., MUTE holds PPS=20 → ≤ 50 ms; ON-SPEED holds PPS=8.2 → ≤ 122 ms; with the line-11 timer-restart trick on solid → high, the first stall pulse is scheduled `pulse_delay/2 + 60.97 ms` ahead).
   - Gen3: SetTone runs synchronously from `updateTones()` — scheduling latency is 0; the next concern is the envelope phase that follows.

2. **Envelope latency**: time from the new spec arming to the first non-zero gate sample = `silent_delay + attack`.

| Stimulus | Envelope latency | Worst-case total (Gen2) |
|---|---|---|
| MUTE → STALL transition | `silent_delay(20 PPS) + attack(5 ms) = 28.5 ms` | up to `1000/20 + 28.5 ≈ 78.5 ms` (timer wait + envelope) |
| ON-SPEED → STALL transition | `60.97 ms (shortened delay) + 5 ms (attack) ≈ 66 ms` | up to `pulse_delay/2 + 60.97 + 66 ≈ 188 ms` (timer-restart wait + envelope) |
| AOA chatter at any threshold | Must NOT produce audible artifacts. The current pulse finishes; transitions only happen at pulse boundaries. | — |
| `master_volume` change via pot | ≤ 40 ms perceived response | — |
| 3D-pan change | ≤ 100 ms perceived response (slow, intentional) | — |

!!! note "Why the worst-case totals matter"
    Gen2's hardware `IntervalTimer` runs the pulse cadence; mode changes have to wait for the next timer fire to take effect. Gen3 has no separate timer — it calls SetTone synchronously, so the envelope latency *is* the total latency. This means **Gen3 produces the first stall pulse sooner than Gen2 would**, which is desirable (faster stall warning at the AOA crossing). The spec leans toward the Gen3 envelope-only column as the target; the Gen2 worst-case is documented for reference.

## Where the implementations stand

This section is a snapshot — expect it to drift as Gen3 evolves. Last updated for `master` at the time of writing.

### Gen2 (reference) — what the source actually does

Mostly meets the spec, since the spec was derived from Gen2's source. A line-by-line audit found a handful of places where Gen2 doesn't strictly meet what the spec describes:

- **§1 MUTE — no hysteresis**. Gen2 uses a bare `IAS ≤ muteAudioUnderIAS` comparison (`Tones.ino:165`). At touchdown, IAS oscillating around the threshold causes audible chatter. The spec's MUTE row describes Gen3's hysteretic behaviour as the desired behaviour because chatter at landing is a real defect; Gen2 inherits the defect.
- **§2 Release time — implicit**. Gen2's pulsed setup block doesn't call `envelope1.release(...)` per pulse; release is whatever the previous solid setup or boot default left. Practical impact is small (release only matters at mode-change boundaries, where it overlaps with the next mode's silent delay), but the spec describes the deterministic per-pulse release that Gen3 PR #102 implements.
- **§3 Cadence**: Gen2's hardware `IntervalTimer` fires at exactly `1000/pps` µs (line 992). Jitter is well below one audio sample. ✅
- **§7 3D audio**: Gen2's `channelGain` step can land during attack/hold/decay/sustain because `Check3DAudio` runs from the main loop without coordination with the envelope. The α=0.1 EMA limits the per-update step to a small fraction of the swing.
- **§8 Click-free**: see qualifier in §8 above. Gen2 writes `sinewave1.amplitude(volume)` mid-release; small audible bleed-through is possible.
- **§10 Audio switch**: Gen2's switch-off path silences everything, including the stall warning (`Tones.ino:34-40`). The spec leans Gen3-side: silencing the stall warning is unsafe.
- **§11 Latency**: Gen2's worst-case MUTE→STALL latency is `pulse_delay(MUTE PPS=20) + envelope ≈ 78.5 ms` because the IntervalTimer schedules pulse fires. Gen3, calling SetTone synchronously, does the envelope-only `28.5 ms`. The spec leans toward the Gen3 envelope-only target as the desired behaviour.

These are catalogued (not pejoratively — Gen2 is the reference for sound character, not for being a bug-free spec implementation) so a reader can tell when "Gen3 differs from Gen2" is a regression versus a deliberate improvement.

### Gen3 on `master` (current shipping behaviour)

Gen3 master ships a different audio path — there is no DAHDR envelope, no per-PPS volume ramp, no inter-pulse Gap, no 60.97 ms solid entry delay. Tones are gated by hard amplitude switches at pulse edges, which produces audible clicks and a pulse cadence that runs faster than Gen2 (especially at stall warning, where measured PPS would land ~6% above the configured 20 PPS if the same envelope construction were used).

In practical terms: a pilot listening to Gen3 on `master` and Gen2 in the same airframe will hear that they are different. The spec above documents what Gen3 *should* do; PR [#102](https://github.com/flyonspeed/OnSpeed-Gen3/pull/102) is the in-flight attempt to make that true.

### Gen3 with PR #102

Closes the structural gap on the audio path. Section-by-section status:

- §0 (signal chain), §1 (regions), §2 (envelope shape), §3 (cadence within 0.05% of spec), §4 (transition policies), §5 (per-PPS volume), §11 (envelope latency) — implemented and unit-tested.
- §1 MUTE hysteresis + always-on sentinel, §10 stall-on-during-user-mute — implemented; spec has been updated to describe these as desired behaviour rather than Gen3-only deviations.
- §8 click-free invariant — partially achieved (envelope-driven masking works; release-tail amplitude bleed-through and Gen3 hardware-protection clamps are documented as known limitations shared with Gen2).
- §6 master volume + §7 3D audio — Gen3's existing pot-read and 3D-pan paths are unchanged by #102. Behaviour matches the spec to the same extent Gen2 does.
- §9 voice gain — Gen3 uses 3× (vs Gen2's 10×) tuned for the V4P amp circuit; both are spec-compliant for their respective hardware.

The PR carries 41 envelope unit tests (covering every phase transition, NoteOff termination, output continuity, and 6 cadence/Gap-specific tests added in the latest commit). A host-side cadence probe driving the production `MakePulseSpec` formulas confirms the measured PPS lands within 0.05% of the spec across the operating range.

**On-aircraft verification is still pending.** Lenny is recording scope traces from both Gen2 and Gen3 hardware to compare side-by-side. When that work completes, this section should be updated with the diff between observed waveform and the spec, not just the construction of the math.

## Reporting discrepancies

If you find:

- **Gen2 source code that contradicts this spec**: the source code wins. File an issue with the line reference and we'll correct the doc.
- **Gen3 hardware that doesn't match the spec**: file an issue with the listening-test or scope-trace evidence; that's a real bug.
- **Spec ambiguity**: file an issue. The goal is "two independent implementations produce indistinguishable audio at the speaker"; if it's not precise enough to support that, it's not done yet.
