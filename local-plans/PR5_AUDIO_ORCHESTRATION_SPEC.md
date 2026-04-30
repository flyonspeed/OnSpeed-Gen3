# PR 5 spec — Extract audio orchestration into `onspeed_core::audio`

**Tracks:** [#368](https://github.com/flyonspeed/OnSpeed-Gen3/issues/368)
**Status:** SPEC ONLY — review before agent dispatch.
**Estimated diff:** ~120 lines added to `onspeed_core/audio/`, ~75 lines deleted from `Audio.cpp`, ~75 lines deleted from `tools/synth-record/audio_harness.cpp` (after PR 7).

## What the bug is

The audio path's "decide what envelope spec to arm" logic lives inside the firmware sketch wrapper (`software/sketch_common/src/audio_io/Audio.cpp`) as static helpers and inlined `SetTone` decision tree. The platform-free portions are buried in there — they don't depend on FreeRTOS, I2S DMA, mutexes, or `g_Config`, but they live in a file that does.

**Specifically:**

| Symbol | Location | Purpose |
|---|---|---|
| `MakePulseSpec(pps, isStall, fromSolid)` | `Audio.cpp:160` | builds DAHDR `EnvelopeSpec` for a pulsed tone |
| `MakeSolidSpec()` | `Audio.cpp:198` | builds DAHDR `EnvelopeSpec` for a sustained tone |
| `TONE_RAMP_TIME = 15ms` | `Audio.cpp:110` | normal envelope ramp time |
| `STALL_RAMP_TIME = 5ms` | `Audio.cpp:111` | stall-buzz envelope ramp time (faster, since pulses are 50ms apart) |
| `SOLID_TRANSITION_DELAY_MS = 1000/8.2/2 ≈ 60.97ms` | `Audio.cpp:118` | first-pulse silent delay when transitioning solid → pulsed |
| `AudioPlay::SetTone` decision tree | `Audio.cpp:400` | maps `(EnToneType, fTonePulseMaxSamples, fromSolid)` → `Envelope::NoteOn(spec)` or `NoteOff()` |

These are platform-free. They produce a value (the `EnvelopeSpec`) given inputs (`ToneResult` from `calculateTone`, plus the running envelope's "is currently solid" flag). No I/O, no globals, no mutexes.

## What the consequences are

A second consumer of "what envelope spec should this AOA fire?" has to either:

1. **Reimplement** — the synth-record audio harness did this initially. Drift risk on every constant change. The `pulse_period - 3ms` cadence math, the `SOLID_TRANSITION_DELAY_MS` derivation from `LOW_TONE_PPS_MAX`, the `isStall` threshold of `pps >= 19.5` — every single thing has to be transcribed and kept in sync.

2. **Copy-paste** — what the synth-record harness does now. ~75 lines of duplication. Two sources of truth that diverge silently the next time someone tunes `TONE_RAMP_TIME`.

3. **Bypass** — what the X-Plane plugin does. Calls `ToneCalc::calculateTone` and then drives `ToneSynth` directly without the envelope path. Works for the plugin's narrow needs but loses the Gen2-faithful click-free behavior the envelope provides.

`docs/ARCHITECTURE_DECOUPLING.md` §5 calls out exactly this gap: *"The audio path reads `g_Config` directly... a wrapper around it pulls from globals... a second audio path tomorrow would need to either read the same globals or duplicate the wrapper."*

## Proposed extraction

New module `software/Libraries/onspeed_core/src/audio/AudioOrchestrator.{h,cpp}`.

### Header API

```cpp
// AudioOrchestrator.h — decide what EnvelopeSpec to arm given a
// ToneResult and the running envelope's state.
//
// Pure function of inputs.  No platform deps, no globals.

#ifndef ONSPEED_CORE_AUDIO_AUDIO_ORCHESTRATOR_H
#define ONSPEED_CORE_AUDIO_AUDIO_ORCHESTRATOR_H

#include <audio/Envelope.h>
#include <audio/ToneCalc.h>

namespace onspeed::audio {

// Audio-path constants pulled out of Audio.cpp.  Sample rate is the
// only knob that varies (the V4P firmware uses 16 kHz; demo tools and
// future variants pass their own).  Ramp times and solid-transition
// delay are Gen2-derived and constant across all OnSpeed audio paths.
struct OrchestratorConfig {
    int   sampleRateHz             = 16000;
    float toneRampMs               = 15.0f;       // TONE_RAMP_TIME
    float stallRampMs              =  5.0f;       // STALL_RAMP_TIME
    float solidTransitionDelayMs   = 60.9756f;    // 1000 / LOW_TONE_PPS_MAX / 2
    float pulseToneLengthSubtractMs =  3.0f;      // tone_length = pulse_period - 3ms
    float stallPpsThreshold        = 19.5f;       // pps >= this → stall ramp
};

// Build the EnvelopeSpec for a pulsed tone at the given PPS.
// `fromSolid` true → use SOLID_TRANSITION_DELAY for the first pulse's
// silent_delay (Gen2's "shortened first pulse after solid→pulsed")
// instead of toneLength/2.
EnvelopeSpec MakePulseSpec(float pps, bool isStall, bool fromSolid,
                           const OrchestratorConfig& cfg);

// Build the EnvelopeSpec for a solid (sustained) tone.
// Always carries the 60.97ms entry delay (Gen2's unconditional
// envelope1.delay() in the SOLID_TONE branch).
EnvelopeSpec MakeSolidSpec(const OrchestratorConfig& cfg);

// One-call decide-and-act helper for callers that want it.
//
// `toneResult` comes from `calculateTone(aoa, thresholds)`.
// `currentEnvelopeIsSolid` is `envelope.IsCurrentSolid()` from the
//   running envelope state (used to detect solid → pulsed transitions
//   so MakePulseSpec applies the shortened first-pulse delay).
//
// Mutates `envelope`:
//   - If toneResult.enTone == None: envelope.NoteOff()
//   - Else: envelope.NoteOn(spec) with spec built from toneResult
//
// Returns the spec used (for tests or telemetry); callers can ignore.
EnvelopeSpec DecideAndArm(const ToneResult& toneResult,
                          Envelope& envelope,
                          const OrchestratorConfig& cfg);

}   // namespace onspeed::audio

#endif
```

### Source

The `MakePulseSpec` / `MakeSolidSpec` bodies are lifted verbatim from `Audio.cpp:160` and `Audio.cpp:198`, with the constants taken from `cfg` instead of `#define`s. `DecideAndArm` is the body of `AudioPlay::SetTone` (`Audio.cpp:400`) rewritten to take `Envelope&` directly instead of going through the `s_ToneEnvelope` static.

### Sketch wrapper changes (`Audio.cpp`)

`AudioPlay::SetTone` becomes:

```cpp
void AudioPlay::SetTone(EnAudioTone enAudioTone)
{
    onspeed::ToneResult tr;
    tr.enTone = static_cast<onspeed::EnToneType>(enAudioTone);
    if (fTonePulseMaxSamples > 0.0f) {
        tr.fPulseFreq = SAMPLE_RATE / (fTonePulseMaxSamples * 2.0f);
    } else {
        tr.fPulseFreq = 0.0f;  // signals solid
    }
    tr.fVolumeMult = fStallVolumeMult;  // unused here, but kept for parity

    static const onspeed::audio::OrchestratorConfig kCfg = {
        .sampleRateHz = SAMPLE_RATE,
        // others use their defaults, which match the Gen2 #defines
    };
    onspeed::audio::DecideAndArm(tr, s_ToneEnvelope, kCfg);

    s_LastEnvTone = enAudioTone;
    this->enTone  = enAudioTone;
    NotifyAudioTask();
}
```

Drop the static `MakePulseSpec` / `MakeSolidSpec` and the `#define`s for ramp times. The `SOLID_TRANSITION_DELAY_MS` static constexpr can also go.

### Tests

`test/test_audio_orchestrator/` — new directory.

Pin the platform-free invariants:

1. **`MakePulseSpec` numeric pins** — at PPS = 1.5, 6.2, 8.2, 20.0 (the four corners of the tone map), assert delay/attack/hold/decay/gap/release in samples for sampleRateHz=16000. The exact values are computable by hand from the formulas; this prevents silent drift.

2. **`MakeSolidSpec` numeric pins** — assert solid spec has delaySamples = 60.97 ms × 16000 / 1000 = 975.6 samples (rounded), attackSamples = 240, holdSamples = 0, decaySamples = 0, gapSamples = 0, releaseSamples = 240, isSolid = true.

3. **`fromSolid` shortens first pulse** — call MakePulseSpec(pps=8.2, isStall=false, fromSolid=true), assert delaySamples = 975 (the shortened delay), not 484 (toneLength/2). With fromSolid=false at the same PPS, assert 484.

4. **`isStall` switches ramp time** — call MakePulseSpec(pps=20, isStall=true), assert attackSamples = 80 (5ms × 16000/1000), not 240. With isStall=false, assert 240.

5. **`DecideAndArm` orchestration**:
   - toneResult.enTone == None → envelope.IsIdle() after the call (well, releasing if it wasn't idle before).
   - toneResult.enTone == Low + fPulseFreq == 0 → envelope.IsCurrentSolid() returns true after the call.
   - toneResult.enTone == High + fPulseFreq == 20 → envelope active with a pulse spec, isSolid = false.
   - Solid → pulsed transition: NoteOn was called with a spec whose delaySamples == 975 (shortened).

6. **Round-trip behavior with the firmware's existing path** — record the EnvelopeSpec emitted by `Audio.cpp::SetTone` for a fixed sequence of (AOA, prior state) pairs both before and after the extraction. Bytes must match. **This is the load-bearing regression test.** Implementing it: keep a snapshot of `Audio.cpp::SetTone`'s pre-extraction body in the test as a reference function, then assert the new orchestrator produces the same EnvelopeSpec field-by-field.

### Cross-tool drop-in

After this PR lands, `tools/synth-record/audio_harness.cpp` deletes its local `MakePulseSpec` / `MakeSolidSpec` / SetTone-mimic block (~75 lines) and calls `DecideAndArm` directly. That second drop-in lands in PR 7 (`tools/synth-record/` proper). The harness's tone-path correctness then automatically tracks any future changes to the orchestrator.

### Acceptance criteria (for the agent who lands this)

- [ ] `pio test -e native` — all existing tests pass + new test_audio_orchestrator tests pass
- [ ] `pio run -e esp32s3-v4p` — clean firmware build
- [ ] All M5 envs build clean (the M5 doesn't directly use the orchestrator but won't regress)
- [ ] `scripts/check_core_purity.sh` passes (the new module is platform-free)
- [ ] **Audio output regression**: build the firmware audio test harness and compare the WAV emitted by a known sequence against a pre-extraction snapshot. Bytes must match. (Setup: use `tools/audio-sweep/` if accessible, or commit a small synthetic-tone harness.)
- [ ] PR description shows: the API surface, the deletion summary from `Audio.cpp`, the new test count, and a "behavior unchanged on the device" assertion with verification command output.

## Risks

1. **Static initialization order**: `Audio.cpp`'s static `OrchestratorConfig` initialization must happen before any `SetTone` call. Mitigated by making the config a function-local static (per the sketch style).

2. **Field name drift**: `EnvelopeSpec` field names (`delaySamples`, `attackSamples`, etc.) must already exist in `onspeed_core/audio/Envelope.h` — which they do, since both the sketch and the synth-record harness use them. No new fields needed.

3. **The `notePending_` re-entry behavior** — `Envelope::NoteOn` handles being called while active by queuing the new spec. This is unchanged; just being called from a different file.

4. **Naming bikeshed**: `DecideAndArm` could be `Step` or `Tick` — defer to whatever the agent prefers, just keep it descriptive.

## What to ask before dispatching

1. Are you OK with the `OrchestratorConfig` struct shape (defaults match Gen2 constants exactly)?
2. Is `DecideAndArm` the right granularity — single-call wrapper around the three primitives — or should the public API expose only `MakePulseSpec` / `MakeSolidSpec` and let callers do the decision?
3. Should the `OrchestratorConfig` constants live as `kDefault*` `constexpr` in the header instead of being a struct? (Header-as-spec vs. config-as-data.)
4. Should we ALSO move the `fStallVolumeMult` selection logic out of `PlayTone` into the orchestrator, or keep that in the wrapper for now?

## Estimated agent dispatch effort

- Read `Audio.cpp` orchestration end-to-end (~30 min if context is empty).
- Implement `AudioOrchestrator.{h,cpp}` (~30 min).
- Wire into Audio.cpp (~15 min).
- Write 6 tests (~45 min).
- Verify firmware build + audio-bytes regression (~30 min).
- Write the PR (~15 min).
- **Total: ~3 hours of agent work for a clean review.**
