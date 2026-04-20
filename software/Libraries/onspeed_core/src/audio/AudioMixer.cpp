// AudioMixer.cpp — stereo mixer with pan, gain, and pulse gating.

#include "AudioMixer.h"

#include <cstdint>

namespace onspeed::audio {

namespace {

inline std::int16_t ClampI32ToI16(std::int32_t v) {
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return static_cast<std::int16_t>(v);
}

// Matches Audio.cpp's ScaleAndClampI16() — float multiply then clamp.
inline std::int16_t ScaleAndClampI16(std::int16_t sample, float scale) {
    const std::int32_t scaled = static_cast<std::int32_t>(sample * scale);
    return ClampI32ToI16(scaled);
}

}   // namespace

std::uint32_t PackStereoI16(std::int16_t left, std::int16_t right) {
    return static_cast<std::uint16_t>(left) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(right)) << 16);
}

void Mix(const MixerInputs& inputs,
         std::int16_t* outStereo,
         std::size_t frameCount,
         MixerState& state) {
    if (inputs.in == nullptr || outStereo == nullptr || frameCount == 0)
        return;

    const bool pulsing = (inputs.pulseSpec.halfPeriodSamples > 0.0f);

    for (std::size_t i = 0; i < frameCount; ++i) {
        // Per-sample gate factor
        float gate = 1.0f;
        if (pulsing)
            gate = state.pulse.isOnPhase ? 1.0f : inputs.pulseSpec.offScale;

        const float effL = inputs.leftScale  * gate;
        const float effR = inputs.rightScale * gate;

        const std::int16_t sample = inputs.in[i];
        outStereo[2 * i + 0] = ScaleAndClampI16(sample, effL);
        outStereo[2 * i + 1] = ScaleAndClampI16(sample, effR);

        if (pulsing) {
            if (state.pulse.counter >= inputs.pulseSpec.halfPeriodSamples) {
                state.pulse.counter  -= inputs.pulseSpec.halfPeriodSamples;
                state.pulse.isOnPhase = !state.pulse.isOnPhase;
            }
            else {
                state.pulse.counter += 1.0f;
            }
        }
    }
}

}   // namespace onspeed::audio
