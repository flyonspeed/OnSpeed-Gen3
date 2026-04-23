// Envelope.h — DAHDR (Delay → Attack → Hold → Decay → [Sustain] → Release)
// envelope generator, ported verbatim from Gen2 OnSpeed's
// `AudioEffectEnvelope` usage in `Tones.ino`.
//
// Gen2 ran a continuous Teensy `AudioSynthWaveformSine` through an
// `AudioEffectEnvelope` and re-triggered the envelope on each pulse via an
// `IntervalTimer`.  Each pulse was shaped:
//
//     |── delay ──|── attack ──|── hold ──|── decay ──|
//      silent       0 → 1        1.0        1 → 0
//
// On a "solid" tone the envelope was configured with sustain = 1, so after
// the attack/hold the level held at 1.0 indefinitely, and a `noteOff()`
// triggered a release ramp 1 → 0.
//
// Gen2's two perceptual tricks are preserved:
//
//  1. **Silent delay before attack** — every pulse spends its first half
//     in the delay phase, which "hides" the previous pulse's release tail.
//     The audible portion is the second half of the pulse interval.
//
//  2. **NoteOff-on-change** — every parameter change (frequency, PPS, mode)
//     was preceded by `envelope1.noteOff()`, so any in-flight envelope
//     released cleanly to zero before the new attack started.  We model
//     this by accepting `NoteOn(spec)` while in any phase: if not idle, we
//     transition to Release with the new spec queued to fire when the
//     current envelope reaches zero.
//
// All amplitude steps in upstream layers (per-PPS volume ramp, master
// volume from the pot, 3D-audio panning) happen *between* pulses while
// the envelope is at zero, so they are inaudible — this is what made
// Gen2 sound click-free.

#ifndef ONSPEED_CORE_AUDIO_ENVELOPE_H
#define ONSPEED_CORE_AUDIO_ENVELOPE_H

#include <cstdint>

namespace onspeed::audio {

// Per-note envelope spec.  All times in audio samples (caller converts
// from milliseconds using the audio sample rate).
struct EnvelopeSpec {
    float delaySamples   = 0.0f;
    float attackSamples  = 0.0f;
    float holdSamples    = 0.0f;
    float decaySamples   = 0.0f;
    float releaseSamples = 0.0f;

    // true: after attack/hold, latch at level 1.0 indefinitely (Gen2
    //       SOLID_TONE behaviour with sustain=1).  Caller invokes
    //       NoteOff() to start the release ramp.
    // false: after attack/hold, run the decay ramp to 0 then *automatically
    //        re-trigger* — Delay → Attack → Hold → Decay → Delay → ...
    //        This produces continuous pulses at the rate implied by the
    //        DAHD timing, mirroring Gen2's IntervalTimer-driven re-trigger
    //        of `envelope1.noteOn()` once per pulse period.  NoteOff()
    //        still terminates immediately via the Release ramp.
    bool isSolid = false;
};

enum class EnvPhase : std::uint8_t {
    Idle,
    Delay,
    Attack,
    Hold,
    Decay,
    Sustain,
    Release,
};

class Envelope {
public:
    Envelope() = default;

    // Trigger a new envelope.  Behaviour mirrors Gen2's
    // `noteOff(); ... noteOn();` pattern:
    //   - If currently Idle or Release, the new spec arms immediately
    //     and the next Tick() begins the Delay (or Attack) phase.
    //   - Otherwise the new spec is queued; the envelope transitions to
    //     Release and the queued NoteOn fires automatically when the
    //     release ramp reaches zero.
    void NoteOn(const EnvelopeSpec& spec);

    // Begin releasing toward zero from the current level.  No-op if
    // already Idle or Release.
    void NoteOff();

    // Advance one audio sample, returning the gate level [0, 1].
    float Tick();

    // Diagnostic / test helpers.
    EnvPhase Phase() const   { return phase_; }
    float    Level() const   { return level_; }
    bool     IsIdle() const  { return phase_ == EnvPhase::Idle; }
    bool     HasPending() const { return notePending_; }

    // Reset to a clean idle state.  Used by the sketch when the audio
    // hardware is (re)initialised; not normally needed at runtime.
    void Reset();

private:
    EnvPhase     phase_ = EnvPhase::Idle;
    float        level_ = 0.0f;
    float        samplesInPhase_ = 0.0f;
    EnvelopeSpec spec_{};
    EnvelopeSpec pendingSpec_{};
    bool         notePending_ = false;

    void EnterPhase(EnvPhase next);
};

}  // namespace onspeed::audio

#endif  // ONSPEED_CORE_AUDIO_ENVELOPE_H
