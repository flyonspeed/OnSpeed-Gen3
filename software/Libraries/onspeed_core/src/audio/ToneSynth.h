// ToneSynth.h — PCM tone generator
//
// Produces int16_t PCM samples at the OnSpeed audio sample rate
// (16 kHz nominal). Two entry points:
//
//   Synthesize()     — continuous-phase cosine at arbitrary frequency
//                      suitable for precomputing a fixed-frequency buffer
//                      or for streaming generation.
//
//   ApplyPulseGate() — modulate an existing tone buffer with a square-wave
//                      pulse at a given pulse rate and duty cycle. This is
//                      the OnSpeed "pulse" behavior: the tone amplitude is
//                      scaled to either 1.0 (on) or a small "off" level,
//                      toggling each half-period.
//
// The precomputed-cosine approach matches the legacy Audio.cpp, which
// filled aTone_400Hz[] and aTone_1600Hz[] at startup with
//    aTone[i] = uint16_t(25000.0f * cosf(2*pi*i*freq/SAMPLE_RATE))
// where the uint16 cast is a bit-pattern reinterpret of the float. We
// match that behavior exactly so that existing tone timbre is preserved.
//
// No dynamic allocation. No platform dependencies.

#ifndef ONSPEED_CORE_AUDIO_TONE_SYNTH_H
#define ONSPEED_CORE_AUDIO_TONE_SYNTH_H

#include <cstddef>
#include <cstdint>

namespace onspeed::audio {

// Nominal OnSpeed audio sample rate.  Provided as a convenience; callers
// may specify any sample rate they need via the Synthesize() parameters.
inline constexpr int kSampleRateHz = 16000;

// Legacy amplitude used by the sketch for its 400/1600 Hz precomputed
// tones.  Callers synthesizing at runtime may pass any amplitude.
inline constexpr int16_t kLegacyToneAmplitude = 25000;

// Synthesize `sampleCount` samples of a cosine wave at `frequencyHz` into
// the caller-provided buffer `out`.
//
//   amplitude       — peak amplitude in PCM counts (0..32767).
//   phaseRadIn      — starting phase in radians (for chaining calls).
//   sampleRateHz    — output sample rate (Hz).
//
// Returns the phase (in radians, wrapped to [-pi, pi] via remainderf)
// one sample past the end, so chained calls are phase-continuous.
//
// If frequencyHz == 0 or amplitude == 0 the buffer is filled with zeros
// and the returned phase is unchanged.
float Synthesize(float frequencyHz,
                 int16_t amplitude,
                 int sampleRateHz,
                 int16_t* out,
                 std::size_t sampleCount,
                 float phaseRadIn = 0.0f);

// Legacy-compatible cosine fill — matches the behavior of Audio.cpp's
// startup loop that initialized aTone_400Hz / aTone_1600Hz.  Equivalent
// to Synthesize(freq, 25000, 16000, out, len, 0.0f) except that the
// per-sample result is bit-cast through uint16_t to preserve the exact
// sample values of the legacy firmware (negative cosine values get
// their two's-complement encoding via the uint16 round-trip).
void SynthesizeLegacyCosine(float frequencyHz,
                            int sampleRateHz,
                            int16_t* out,
                            std::size_t sampleCount);

// Pulse-gate state advanced per call to ApplyPulseGate().
struct PulseGateState {
    float  counter    = 0.0f;   // samples since last edge
    bool   isOnPhase  = true;   // true = on (1.0 scale), false = off (offScale)
};

// Parameters for ApplyPulseGate().
struct PulseGateSpec {
    // Half-period in samples.  0 means "pulsing disabled" — output is a
    // straight copy of input with no gating.  Compute this from the desired
    // pulses-per-second via (sampleRateHz / (pps * 2.0f)).  Negative values
    // are treated the same as 0.
    float halfPeriodSamples = 0.0f;

    // Amplitude scale applied during the "off" half.  Typically 0.0 for a
    // clean gate, or ~0.2 for the legacy "ducked" pulse sound.
    float offScale = 0.2f;
};

// Scale `in[i] * fBaseScale * gate(i)` into `out[i]`, clamped to int16.
// `in` and `out` may alias; they are read-then-write per sample.
//
// On each sample the state counter is incremented; when it crosses
// halfPeriodSamples the phase toggles between on (scale = fBaseScale) and
// off (scale = fBaseScale * spec.offScale).
//
// `sampleCount` is the mono-sample count; this routine does not interleave.
//
// If spec.halfPeriodSamples <= 0 the input is passed through scaled only
// by fBaseScale.  No state is advanced.
void ApplyPulseGate(const int16_t* in,
                    int16_t* out,
                    std::size_t sampleCount,
                    float fBaseScale,
                    const PulseGateSpec& spec,
                    PulseGateState& state);

}   // namespace onspeed::audio

#endif   // ONSPEED_CORE_AUDIO_TONE_SYNTH_H
