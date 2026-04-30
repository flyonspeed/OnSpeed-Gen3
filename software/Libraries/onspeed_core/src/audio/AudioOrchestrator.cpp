// AudioOrchestrator.cpp — see AudioOrchestrator.h for the rationale.
//
// The bodies here are the platform-free portions of Audio.cpp's
// MakePulseSpec / MakeSolidSpec / SetTone decision tree, reparameterised
// over OrchestratorConfig instead of Audio.cpp's #defines.

#include "AudioOrchestrator.h"

namespace onspeed::audio {

namespace {

// Helper: ms → samples at the configured sample rate.  Kept local so
// callers don't need to think about the sample-rate units.
inline float MsToSamples(float ms, int sampleRateHz)
{
    return ms * (static_cast<float>(sampleRateHz) / 1000.0f);
}

}  // namespace

EnvelopeSpec MakePulseSpec(float pps,
                           bool isStall,
                           bool fromSolid,
                           const OrchestratorConfig& cfg)
{
    const float pulseDelayMs = 1000.0f / pps;          // full period
    const float toneLengthMs = pulseDelayMs - cfg.pulseToneLengthSubtractMs;
    const float gapMs        = pulseDelayMs - toneLengthMs;   // = subtract value
    const float rampMs       = isStall ? cfg.stallRampMs : cfg.toneRampMs;
    const float delayMs      = fromSolid
                                 ? cfg.solidTransitionDelayMs
                                 : (toneLengthMs * 0.5f);
    float       holdMs       = (toneLengthMs * 0.5f) - 2.0f * rampMs;
    if (holdMs < 0.0f) holdMs = 0.0f;

    EnvelopeSpec s;
    s.delaySamples   = MsToSamples(delayMs,   cfg.sampleRateHz);
    s.attackSamples  = MsToSamples(rampMs,    cfg.sampleRateHz);
    s.holdSamples    = MsToSamples(holdMs,    cfg.sampleRateHz);
    s.decaySamples   = MsToSamples(rampMs,    cfg.sampleRateHz);
    s.gapSamples     = MsToSamples(gapMs,     cfg.sampleRateHz);
    s.releaseSamples = MsToSamples(rampMs,    cfg.sampleRateHz);
    s.isSolid        = false;
    return s;
}

EnvelopeSpec MakeSolidSpec(const OrchestratorConfig& cfg)
{
    EnvelopeSpec s;
    s.delaySamples   = MsToSamples(cfg.solidTransitionDelayMs, cfg.sampleRateHz);
    s.attackSamples  = MsToSamples(cfg.toneRampMs,             cfg.sampleRateHz);
    s.holdSamples    = 0.0f;
    s.decaySamples   = 0.0f;
    s.gapSamples     = 0.0f;     // solid latches in Sustain, never auto-loops
    s.releaseSamples = MsToSamples(cfg.toneRampMs,             cfg.sampleRateHz);
    s.isSolid        = true;
    return s;
}

EnvelopeSpec DecideAndArm(const ToneResult& toneResult,
                          Envelope& envelope,
                          const OrchestratorConfig& cfg)
{
    if (toneResult.enTone == EnToneType::None)
    {
        envelope.NoteOff();
        return EnvelopeSpec{};
    }

    EnvelopeSpec spec;
    if (toneResult.fPulseFreq <= 0.0f)
    {
        // Solid tone (currently used only for low cruise).
        spec = MakeSolidSpec(cfg);
    }
    else
    {
        const bool isStall   = (toneResult.fPulseFreq >= cfg.stallPpsThreshold);
        const bool fromSolid = envelope.IsCurrentSolid();
        spec = MakePulseSpec(toneResult.fPulseFreq, isStall, fromSolid, cfg);
    }

    envelope.NoteOn(spec);
    return spec;
}

}  // namespace onspeed::audio
