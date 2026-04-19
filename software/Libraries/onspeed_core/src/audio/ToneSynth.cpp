// ToneSynth.cpp — PCM tone generator implementation.

#include "ToneSynth.h"

#include <cmath>
#include <cstdint>

namespace onspeed::audio {

namespace {

inline int16_t ClampToI16(float f) {
    if (f > 32767.0f)  return 32767;
    if (f < -32768.0f) return -32768;
    return static_cast<int16_t>(f);
}

}   // namespace

float Synthesize(float frequencyHz,
                 int16_t amplitude,
                 int sampleRateHz,
                 int16_t* out,
                 std::size_t sampleCount,
                 float phaseRadIn) {
    if (out == nullptr || sampleCount == 0)
        return phaseRadIn;

    if (frequencyHz == 0.0f || amplitude == 0 || sampleRateHz <= 0) {
        for (std::size_t i = 0; i < sampleCount; ++i)
            out[i] = 0;
        return phaseRadIn;
    }

    const float kTwoPi   = 2.0f * 3.14159265358979323846f;
    const float phaseInc = kTwoPi * frequencyHz / static_cast<float>(sampleRateHz);
    const float amp      = static_cast<float>(amplitude);

    float phase = phaseRadIn;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        // Wrap per-sample to keep float precision bounded for long runs.
        phase = std::remainder(phase, kTwoPi);
        out[i] = ClampToI16(amp * std::cos(phase));
        phase += phaseInc;
    }

    return std::remainder(phase, kTwoPi);
}

void SynthesizeLegacyCosine(float frequencyHz,
                            int sampleRateHz,
                            int16_t* out,
                            std::size_t sampleCount) {
    // Exactly matches Audio.cpp's init loop:
    //   fAngle = remainderf(2*pi*i*freq/SR, 2*pi);
    //   out[i] = uint16_t(25000.0f * cosf(fAngle));
    // Note: The cast to uint16_t of a negative float value is
    // implementation-defined in C++, but on ESP32 GCC it truncates to the
    // low 16 bits of the integer representation; that truncation is
    // equivalent to a two's-complement int16 wrap, which is what the
    // legacy firmware relied on.  We mirror that via int32 -> int16 cast.

    if (out == nullptr || sampleCount == 0 || sampleRateHz <= 0)
        return;

    const float kTwoPi = 2.0f * 3.14159265358979323846f;
    const float amp    = 25000.0f;   // kLegacyToneAmplitude as float

    for (std::size_t i = 0; i < sampleCount; ++i) {
        const float fAngle = std::remainder(
            kTwoPi * static_cast<float>(i) * frequencyHz / static_cast<float>(sampleRateHz),
            kTwoPi);
        // The original code was `uint16_t(25000.0f * cosf(...))`.  The
        // low 16 bits of a float->int conversion match a direct int16
        // cast for values in [-32768, 32767], and 25000 keeps us inside
        // that range.  Use a clamped int16 conversion here so we have
        // defined behaviour on the host test build too.
        const float sample = amp * std::cos(fAngle);
        out[i] = ClampToI16(sample);
    }
}

void ApplyPulseGate(const int16_t* in,
                    int16_t* out,
                    std::size_t sampleCount,
                    float fBaseScale,
                    const PulseGateSpec& spec,
                    PulseGateState& state) {
    if (in == nullptr || out == nullptr || sampleCount == 0)
        return;

    const bool pulsing = (spec.halfPeriodSamples > 0.0f);

    for (std::size_t i = 0; i < sampleCount; ++i) {
        float scale = fBaseScale;
        if (pulsing) {
            const float gate = state.isOnPhase ? 1.0f : spec.offScale;
            scale *= gate;
        }

        const int32_t scaled = static_cast<int32_t>(static_cast<float>(in[i]) * scale);
        int16_t clamped;
        if (scaled > 32767)       clamped = 32767;
        else if (scaled < -32768) clamped = -32768;
        else                      clamped = static_cast<int16_t>(scaled);
        out[i] = clamped;

        if (pulsing) {
            if (state.counter >= spec.halfPeriodSamples) {
                state.counter  -= spec.halfPeriodSamples;
                state.isOnPhase = !state.isOnPhase;
            }
            else {
                state.counter += 1.0f;
            }
        }
    }
}

}   // namespace onspeed::audio
