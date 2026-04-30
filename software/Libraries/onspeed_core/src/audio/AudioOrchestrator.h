// AudioOrchestrator.h — decide what EnvelopeSpec to arm given a
// ToneResult and the running envelope's state.
//
// Pure functions of inputs.  No platform deps, no globals.  The
// per-pulse DAHD shape and the solid-tone entry delay are ported
// verbatim from Gen2 OnSpeed's `Tones.ino`; see Envelope.h for the
// per-phase semantics.  The constants ride on `OrchestratorConfig`
// rather than `#define`s so a second consumer (synth-record harness,
// X-Plane plugin) can re-use the same math without dragging in a
// SAMPLE_RATE macro from the sketch.
//
// Three primitives:
//   - MakePulseSpec  — DAHDR shape for a pulsed tone at the given PPS.
//   - MakeSolidSpec  — DAHDR shape for a sustained tone.
//   - DecideAndArm   — one-call wrapper: maps ToneResult → spec → NoteOn,
//                      observing the running envelope's solid/pulsed
//                      state for Gen2's shortened-first-pulse trick.
//
// The X-Plane plugin and the synth-record harness can call these
// directly; the firmware sketch's AudioPlay::SetTone is a five-line
// wrapper around DecideAndArm.

#ifndef ONSPEED_CORE_AUDIO_AUDIO_ORCHESTRATOR_H
#define ONSPEED_CORE_AUDIO_AUDIO_ORCHESTRATOR_H

#include <audio/Envelope.h>
#include <audio/ToneCalc.h>

namespace onspeed::audio {

// Audio-path constants pulled out of Audio.cpp.  Sample rate is the
// only knob that varies (the V4P firmware uses 16 kHz; demo tools and
// future variants pass their own).  Ramp times and the solid-transition
// delay are Gen2-derived and constant across all OnSpeed audio paths,
// so the defaults match Audio.cpp's previous `#define`s exactly.
struct OrchestratorConfig
{
    int   sampleRateHz              = 16000;
    float toneRampMs                = 15.0f;       // TONE_RAMP_TIME
    float stallRampMs               =  5.0f;       // STALL_RAMP_TIME
    // 1000 / LOW_TONE_PPS_MAX / 2 = 1000 / 8.2 / 2 ≈ 60.9756 ms.
    // Computed at compile time from onspeed::LOW_TONE_PPS_MAX so the
    // value tracks any future change to that constant in one place.
    float solidTransitionDelayMs    =
        1000.0f / onspeed::LOW_TONE_PPS_MAX / 2.0f;
    float pulseToneLengthSubtractMs =  3.0f;       // tone_length = pulse_period - 3ms
    // Stall PPS hits the snappier 5 ms ramp.  Threshold matches the
    // existing Audio.cpp check `(fPps >= HIGH_TONE_STALL_PPS - 0.5f)`.
    float stallPpsThreshold         =
        onspeed::HIGH_TONE_STALL_PPS - 0.5f;
};

// Build the EnvelopeSpec for a pulsed tone at the given PPS.
//   isStall    — true at stall warning PPS; uses the snappier ramp
//                (cfg.stallRampMs) instead of cfg.toneRampMs.
//   fromSolid  — true when the previous note was solid; uses Gen2's
//                shortened ~61 ms first-pulse delay so the new pulsed
//                tone arrives within one perceptual half-period.
//
// Gen2 per-pulse shape (Tones.ino:104–120) plus the IntervalTimer
// cadence (Tones.ino:14):
//   pulse_delay = 1000 / pps                  (full cycle period)
//   tone_length = pulse_delay - 3 ms          (envelope-active window)
//   delay       = tone_length / 2             (silent first half)
//   attack      = ramp_time
//   hold        = tone_length/2 - 2*ramp_time
//   decay       = ramp_time
//   gap         = pulse_delay - tone_length    (= 3 ms inter-pulse silence)
//   release     = ramp_time                    (only used on NoteOff)
EnvelopeSpec MakePulseSpec(float pps,
                           bool isStall,
                           bool fromSolid,
                           const OrchestratorConfig& cfg);

// Build the EnvelopeSpec for a solid (sustained) tone.  Always carries
// the ~61 ms entry delay (Gen2's unconditional `envelope1.delay()` in
// the SOLID_TONE branch).  Solid tones latch in Sustain so the gap and
// decay phases stay zero.
EnvelopeSpec MakeSolidSpec(const OrchestratorConfig& cfg);

// One-call decide-and-act helper for callers that want it.
//
// `toneResult` comes from `calculateTone(aoa, thresholds)`.  The
// caller is responsible for translating PPS into pulse vs solid:
//   toneResult.fPulseFreq == 0 → solid tone (uses MakeSolidSpec).
//   toneResult.fPulseFreq >  0 → pulsed tone at fPulseFreq PPS.
//
// Mutates `envelope`:
//   - toneResult.enTone == None → envelope.NoteOff()
//   - else → envelope.NoteOn(spec) where spec is built per the rules
//     above, observing envelope.IsCurrentSolid() to apply Gen2's
//     shortened first-pulse delay on a solid → pulsed transition.
//
// Returns the spec used (zero-filled when the call resolves to NoteOff)
// for tests and telemetry; production callers can ignore the return.
EnvelopeSpec DecideAndArm(const ToneResult& toneResult,
                          Envelope& envelope,
                          const OrchestratorConfig& cfg);

}  // namespace onspeed::audio

#endif  // ONSPEED_CORE_AUDIO_AUDIO_ORCHESTRATOR_H
