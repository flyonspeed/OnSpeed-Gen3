// Envelope.cpp — DAHDR envelope generator implementation.
// See Envelope.h for the Gen2 lineage and design notes.

#include "Envelope.h"

#include <algorithm>

namespace onspeed::audio {

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
    if (phase_ == EnvPhase::Idle || phase_ == EnvPhase::Release)
    {
        // Idle: arm immediately.
        // Release: also arm immediately and let the new spec take over from
        // the current (decaying) level.  This matches Gen2's
        // `noteOff(); noteOn();` pattern when the previous note had already
        // been released — the new attack starts cleanly from wherever the
        // level currently sits.
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
        // Solid tone → new spec (pulsed or different solid): Sustain never
        // auto-exits, so we must release from the sustained level.  The
        // release tail (15 ms) hides behind the new spec's ~61 ms silent
        // Delay phase — Gen2's solid→pulsed transition trick
        // (Tones.ino:11, :63).
        pendingSpec_ = spec;
        notePending_ = true;
        NoteOff();
        return;
    }

    // Mid-pulse (Delay / Attack / Hold / Decay): queue the new spec and
    // let the current pulse finish naturally.  Decay picks up
    // pendingSpec_ at the pulse boundary (see Tick() below).
    //
    // This debounces rapid parameter churn without silencing the audio:
    // Gen3's UpdateTones() runs at 208 Hz (vs Gen2's ~20 Hz), so when
    // AOA chatters near the stall-warn threshold the PPS jumps
    // discontinuously between 6.2 and 20 PPS on many sensor ticks.
    // If every such call triggered a Release, the envelope would spend
    // its life in Release/Attack transitions and produce rattly,
    // indistinct audio.  With pulse-boundary queueing the currently
    // playing pulse always finishes cleanly; only the *next* pulse
    // reflects the latest spec, and only the most-recent pending
    // spec wins (replacement, not chaining).
    pendingSpec_ = spec;
    notePending_ = true;
}

void Envelope::NoteOff()
{
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
