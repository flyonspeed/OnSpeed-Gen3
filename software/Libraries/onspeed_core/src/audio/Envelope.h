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

    // Request a new envelope shape.  Owns all state-transition policy so
    // callers (e.g. UpdateTones at 208 Hz) can call freely without
    // worrying about disturbing the running envelope.  Behaviour:
    //
    //   Active + same spec   → no-op (debounced; current pulse cycle
    //                          continues unstomped).
    //   Idle or Release      → arm immediately; next Tick() starts the
    //                          new Delay (or Attack) phase from level 0
    //                          (Idle) or from current decaying level
    //                          (Release).  Calling here after a NoteOff
    //                          re-arms cleanly even if the release
    //                          hasn't reached zero yet.
    //   Sustain (solid tone) → queue new spec and start releasing the
    //                          held level.  Queued spec fires when the
    //                          release tail reaches zero.  This is
    //                          Gen2's solid→pulsed transition: the
    //                          release tail (15 ms) hides behind the
    //                          new spec's silent Delay (~61 ms).
    //   Mid-pulse (Delay/    → queue new spec and let the current pulse
    //   Attack/Hold/Decay)     finish naturally.  Decay picks up the
    //                          queued spec at the pulse boundary.
    //                          This is what makes 208 Hz parameter
    //                          churn (e.g. AOA chatter at the stall
    //                          threshold) sound clean — only the
    //                          most-recent pending spec survives.
    void NoteOn(const EnvelopeSpec& spec);

    // Begin releasing toward zero from the current level.  No-op if
    // already Idle or Release.  Also clears any queued spec — an
    // explicit stop request always wins over a pending re-trigger.
    void NoteOff();

    // Advance one audio sample, returning the gate level [0, 1].
    float Tick();

    // Diagnostic / test helpers.
    EnvPhase Phase() const   { return phase_; }
    float    Level() const   { return level_; }
    bool     IsIdle() const  { return phase_ == EnvPhase::Idle; }
    bool     HasPending() const { return notePending_; }

    // True iff the envelope is in an actively-playing phase (Delay,
    // Attack, Hold, Decay, Sustain).  Returns false during Release
    // (winding down) and Idle (stopped).
    bool IsActive() const
    {
        return phase_ != EnvPhase::Idle && phase_ != EnvPhase::Release;
    }

    // Returns true if the currently-active spec is a solid (sustain)
    // tone.  Used by the sketch to decide whether the *next* tone
    // change is a solid→pulsed transition (so it can apply Gen2's
    // 61 ms silent-delay trick).  Only meaningful when IsActive() —
    // returns false during Release/Idle.
    bool IsCurrentSolid() const
    {
        return IsActive() && spec_.isSolid;
    }

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
