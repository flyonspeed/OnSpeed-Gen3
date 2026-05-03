// AudioOrchestrator.h — decide what EnvelopeSpec to arm given a
// ToneResult and the running envelope's state.
//
// Pure functions of inputs.  No platform deps, no globals.  The
// per-pulse DAHD shape and the solid-tone entry delay implement the
// Gen2 OnSpeed audio model — see Envelope.h for the per-phase
// semantics, and docs/site/docs/software/audio-tone-spec.md for the
// pilot-facing description of each region's tone.  Constants are
// carried on OrchestratorConfig so callers at non-firmware sample
// rates (offline harnesses, host-side simulators) can re-use the
// same math.
//
// Three primitives:
//   - MakePulseSpec  — DAHDR shape for a pulsed tone at the given PPS.
//   - MakeSolidSpec  — DAHDR shape for a sustained tone.
//   - DecideAndArm   — one-call wrapper: maps ToneResult → spec → NoteOn,
//                      observing the running envelope's solid/pulsed
//                      state to apply the shortened first-pulse delay
//                      on solid → pulsed transitions.

#ifndef ONSPEED_CORE_AUDIO_AUDIO_ORCHESTRATOR_H
#define ONSPEED_CORE_AUDIO_AUDIO_ORCHESTRATOR_H

#include <audio/Envelope.h>
#include <audio/ToneCalc.h>

namespace onspeed::audio {

// Audio-path constants.  Sample rate is the only knob that varies in
// practice (the V4P firmware uses 16 kHz; offline tools pass their
// own).  Ramp times and the solid-transition delay are pinned by the
// audio-tone spec and shouldn't be changed without rev'ing it.
struct OrchestratorConfig
{
    int   sampleRateHz              = 16000;
    float toneRampMs                = 15.0f;       // normal attack/decay
    float stallRampMs               =  5.0f;       // attack/decay at stall PPS
    // 1000 / LOW_TONE_PPS_MAX / 2 = 1000 / 8.2 / 2 ≈ 60.9756 ms.
    // Tracks LOW_TONE_PPS_MAX so changing that constant moves this in
    // lockstep — rationale below at MakePulseSpec.
    float solidTransitionDelayMs    =
        1000.0f / onspeed::LOW_TONE_PPS_MAX / 2.0f;
    float pulseToneLengthSubtractMs =  3.0f;       // tone_length = pulse_period - 3ms
    // 0.5 PPS hysteresis below the stall PPS — at exactly 19.5 PPS the
    // snappier stall ramp engages.  Single threshold consumed by both
    // MakePulseSpec and DecideAndArm so the cutover is consistent.
    float stallPpsThreshold         =
        onspeed::HIGH_TONE_STALL_PPS - 0.5f;
};

// Build the EnvelopeSpec for a pulsed tone at the given PPS.
//   isStall    — true at stall warning PPS; uses the snappier ramp
//                (cfg.stallRampMs) instead of cfg.toneRampMs.
//   fromSolid  — true when transitioning out of a solid tone.  Uses a
//                shortened ~61 ms first-pulse delay (one half-period
//                at LOW_TONE_PPS_MAX) so the new pulsed tone arrives
//                within one perceptual half-period instead of the full
//                pulse_delay/2 — keeps the perceived transition snappy
//                while still leaving room for the prior solid's
//                release tail to drain under silence.
//
// Per-pulse shape:
//   pulse_delay = 1000 / pps                   (full cycle period)
//   tone_length = pulse_delay - 3 ms           (envelope-active window)
//   delay       = tone_length / 2              (silent first half)
//   attack      = ramp_time
//   hold        = tone_length/2 - 2*ramp_time
//   decay       = ramp_time
//   gap         = pulse_delay - tone_length    (= 3 ms inter-pulse silence)
//   release     = ramp_time                    (used only on NoteOff)
//
// The 3 ms gap is load-bearing for cadence: without it the auto-loop
// would fire pulses at 1/tone_length instead of 1/pulse_delay,
// producing ~3 ms cadence error per cycle (audible at 20 PPS stall).
EnvelopeSpec MakePulseSpec(float pps,
                           bool isStall,
                           bool fromSolid,
                           const OrchestratorConfig& cfg);

// Build the EnvelopeSpec for a solid (sustained) tone.  Always carries
// the ~61 ms entry delay regardless of prior state — the silent delay
// gives any prior tone's release tail room to drain under silence
// before the solid's attack starts, producing a click-free entry.
// Solid tones latch in Sustain so gap/decay are zero (no auto-loop).
EnvelopeSpec MakeSolidSpec(const OrchestratorConfig& cfg);

// One-call decide-and-act helper.
//
// `toneResult` comes from `calculateTone(aoa, thresholds)`.  PPS
// distinguishes pulse vs solid:
//   toneResult.fPulseFreq <= 0 → solid tone (uses MakeSolidSpec).
//   toneResult.fPulseFreq >  0 → pulsed tone at fPulseFreq PPS.
//
// Mutates `envelope`:
//   - toneResult.enTone == None → envelope.NoteOff()
//   - else → envelope.NoteOn(spec).  Spec built from the rules above,
//     observing envelope.IsCurrentSolid() to apply the shortened
//     first-pulse delay on a solid → pulsed transition.
//
// Returns the spec used (zero-filled on NoteOff) for tests and
// telemetry; production callers can ignore the return.
EnvelopeSpec DecideAndArm(const ToneResult& toneResult,
                          Envelope& envelope,
                          const OrchestratorConfig& cfg);

}  // namespace onspeed::audio

#endif  // ONSPEED_CORE_AUDIO_AUDIO_ORCHESTRATOR_H
