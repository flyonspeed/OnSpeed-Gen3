// Envelope.cpp — DAHDR envelope generator implementation.
// See Envelope.h for the Gen2 lineage and design notes.

#include "Envelope.h"

#include <algorithm>
#include <cmath>

namespace onspeed::audio {

namespace {

// True if two specs describe the same envelope shape.  Used by NoteOn()
// to debounce identical re-trigger requests so a 208 Hz UpdateTones()
// loop doesn't stomp on the natural pulse cycle.  Tolerance of one
// audio sample on the time fields swallows fp noise from the
// SAMPLE_RATE / pps conversions performed by the spec builder.
inline bool SameSpec(const EnvelopeSpec& a, const EnvelopeSpec& b)
{
    constexpr float kTol = 1.0f;  // 1 sample @ 16 kHz ≈ 62 µs
    return a.isSolid == b.isSolid
        && std::fabs(a.delaySamples   - b.delaySamples)   <= kTol
        && std::fabs(a.attackSamples  - b.attackSamples)  <= kTol
        && std::fabs(a.holdSamples    - b.holdSamples)    <= kTol
        && std::fabs(a.decaySamples   - b.decaySamples)   <= kTol
        && std::fabs(a.releaseSamples - b.releaseSamples) <= kTol;
}

}  // namespace

void Envelope::Reset()
{
    phase_          = EnvPhase::Idle;
    level_          = 0.0f;
    samplesInPhase_ = 0.0f;
    spec_           = EnvelopeSpec{};
    pendingSpec_    = EnvelopeSpec{};
    notePending_    = false;
}

void Envelope::EnterPhase(EnvPhase next)
{
    phase_          = next;
    samplesInPhase_ = 0.0f;
}

void Envelope::NoteOn(const EnvelopeSpec& spec)
{
    // Debounce: identical re-trigger while the envelope is actively
    // playing is a no-op.  Critical for tolerating UpdateTones()'s
    // 208 Hz call rate without disturbing the running pulse cycle.
    if (IsActive() && SameSpec(spec, spec_))
        return;

    if (phase_ == EnvPhase::Idle || phase_ == EnvPhase::Release)
    {
        // Idle:    arm immediately from level 0.
        // Release: arm immediately and let the new spec take over from
        //          the current (still-decaying) level.  Matches Gen2's
        //          `noteOff(); noteOn();` pattern when the previous note
        //          had already started releasing — the new attack starts
        //          cleanly from wherever the level currently sits.
        spec_ = spec;
        if (phase_ == EnvPhase::Idle)
            level_ = 0.0f;
        EnterPhase(spec_.delaySamples > 0.0f ? EnvPhase::Delay
                                             : EnvPhase::Attack);
        notePending_ = false;
        return;
    }

    if (phase_ == EnvPhase::Sustain)
    {
        // Solid tone → new spec: Sustain never auto-exits, so we must
        // release from the sustained level.  The release tail (15 ms)
        // hides behind the new spec's ~61 ms silent Delay phase —
        // Gen2's solid→pulsed transition trick (Tones.ino:11, :63).
        pendingSpec_ = spec;
        notePending_ = true;
        EnterPhase(EnvPhase::Release);
        return;
    }

    // Mid-pulse (Delay / Attack / Hold / Decay): queue the new spec and
    // let the current pulse finish naturally.  Decay picks up
    // pendingSpec_ at the pulse boundary.  Only the most-recent
    // pending spec survives (replacement, not chaining), so rapid
    // churn settles on the latest request once the current pulse ends.
    pendingSpec_ = spec;
    notePending_ = true;
}

void Envelope::NoteOff()
{
    // An explicit stop request always wins over any pending re-trigger.
    notePending_ = false;

    if (phase_ == EnvPhase::Idle || phase_ == EnvPhase::Release)
        return;

    EnterPhase(EnvPhase::Release);
}

float Envelope::Tick()
{
    switch (phase_)
    {
        case EnvPhase::Idle:
            return 0.0f;

        case EnvPhase::Delay:
            samplesInPhase_ += 1.0f;
            if (samplesInPhase_ >= spec_.delaySamples)
                EnterPhase(EnvPhase::Attack);
            return 0.0f;

        case EnvPhase::Attack:
            samplesInPhase_ += 1.0f;
            if (spec_.attackSamples > 0.0f)
                level_ = std::min(1.0f, samplesInPhase_ / spec_.attackSamples);
            else
                level_ = 1.0f;
            if (samplesInPhase_ >= spec_.attackSamples)
            {
                level_ = 1.0f;
                if (spec_.holdSamples > 0.0f)
                    EnterPhase(EnvPhase::Hold);
                else if (spec_.isSolid)
                    EnterPhase(EnvPhase::Sustain);
                else
                    EnterPhase(EnvPhase::Decay);
            }
            return level_;

        case EnvPhase::Hold:
            samplesInPhase_ += 1.0f;
            if (samplesInPhase_ >= spec_.holdSamples)
            {
                if (spec_.isSolid)
                    EnterPhase(EnvPhase::Sustain);
                else
                    EnterPhase(EnvPhase::Decay);
            }
            return 1.0f;

        case EnvPhase::Decay:
            samplesInPhase_ += 1.0f;
            if (spec_.decaySamples > 0.0f)
                level_ = std::max(0.0f, 1.0f - samplesInPhase_ / spec_.decaySamples);
            else
                level_ = 0.0f;
            if (samplesInPhase_ >= spec_.decaySamples)
            {
                level_ = 0.0f;
                if (notePending_)
                {
                    // A new spec is queued — fire it instead of looping.
                    notePending_ = false;
                    EnvelopeSpec next = pendingSpec_;
                    EnterPhase(EnvPhase::Idle);   // mark done before re-arm
                    NoteOn(next);
                }
                else
                {
                    // Auto-loop: pulsed notes re-trigger from Delay so the
                    // gate continues producing pulses at the rate implied
                    // by the DAHD timing.  This mirrors Gen2's
                    // IntervalTimer-driven `noteOn()` per pulse.
                    EnterPhase(spec_.delaySamples > 0.0f ? EnvPhase::Delay
                                                         : EnvPhase::Attack);
                }
            }
            return level_;

        case EnvPhase::Sustain:
            return 1.0f;

        case EnvPhase::Release:
        {
            // Linear ramp from current level toward 0 over releaseSamples.
            // releaseSamples is the time it would take to ramp from 1.0 to
            // 0 — when releasing from a partial level the time is shorter
            // (matches Gen2 envelope behaviour).
            const float step = (spec_.releaseSamples > 0.0f)
                                 ? (1.0f / spec_.releaseSamples)
                                 : 1.0f;
            level_ = std::max(0.0f, level_ - step);
            samplesInPhase_ += 1.0f;
            if (level_ <= 0.0f)
            {
                level_ = 0.0f;
                EnterPhase(EnvPhase::Idle);
                if (notePending_)
                {
                    notePending_ = false;
                    EnvelopeSpec next = pendingSpec_;
                    NoteOn(next);
                }
            }
            return level_;
        }
    }
    return 0.0f;
}

}  // namespace onspeed::audio
