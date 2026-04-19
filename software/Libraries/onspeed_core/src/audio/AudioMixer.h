// AudioMixer.h — stereo PCM mixer with per-channel gain, pan, and optional
// pulse-gate modulation.
//
// Takes a mono int16 PCM input (either a synthesized tone buffer from
// ToneSynth or a voice-prompt PCM asset from WavDecode) and produces
// stereo interleaved int16 output after applying:
//
//   left_sample  = clamp(in_sample * leftScale  * pulseGate(i))
//   right_sample = clamp(in_sample * rightScale * pulseGate(i))
//
// Pan is expressed via independent left/right gain factors — the caller
// is expected to precompute these from (masterVolume * channelGain) and
// any 3D-audio panning, capping each at 1.0 to prevent waveform clipping
// when panning combines with full gain.  This mirrors the existing
// Audio.cpp behavior (see PlayVoice / PlayTone: `if (fL > 1.0f) fL = 1.0f`).
//
// No dynamic allocation. Output buffer must be sized for 2*frameCount
// int16 values (interleaved L,R,L,R,...).  Input and output buffers
// must not overlap.

#ifndef ONSPEED_CORE_AUDIO_AUDIO_MIXER_H
#define ONSPEED_CORE_AUDIO_AUDIO_MIXER_H

#include <cstddef>
#include <cstdint>

#include "ToneSynth.h"   // PulseGateSpec / PulseGateState

namespace onspeed::audio {

// Per-call inputs to AudioMixer::Mix().
struct MixerInputs {
    // Mono PCM source samples.  Must be non-null if frameCount > 0.
    const int16_t* in = nullptr;

    // Per-channel scale factors (0.0 = silent, 1.0 = unity).  The caller
    // composes these from (masterVolume * channelGain) and is responsible
    // for any 3D-panning ramp.  Values above 1.0 clip; values below 0.0
    // invert the phase (pass through, at the caller's risk).
    float leftScale  = 1.0f;
    float rightScale = 1.0f;

    // Optional pulse-gate modulation.  If spec.halfPeriodSamples > 0 the
    // amplitude toggles between (1.0 * scale) and (spec.offScale * scale)
    // each half-period.  Set halfPeriodSamples = 0 to disable gating.
    PulseGateSpec  pulseSpec;
};

// Caller-owned state for AudioMixer — tracks pulse-gate phase across
// successive calls so pulse continuity is preserved when Mix() is
// invoked repeatedly with small frame batches.
struct MixerState {
    PulseGateState pulse;
};

// Mix `frameCount` mono input samples into `outStereo` (2 * frameCount
// int16 values, interleaved L,R).  Updates `state` for pulse continuity.
//
// If `in == nullptr` or `frameCount == 0`, the output is left untouched
// and state is not advanced.
void Mix(const MixerInputs& inputs,
         int16_t* outStereo,
         std::size_t frameCount,
         MixerState& state);

// Convenience: pack left/right int16 samples into a single uint32 matching
// the legacy firmware's I2S DMA word layout (low 16 bits = left, high 16
// bits = right).  Exposed so the sketch's I2sSink can keep using its
// pre-existing frame buffer layout after extraction.
std::uint32_t PackStereoI16(std::int16_t left, std::int16_t right);

}   // namespace onspeed::audio

#endif   // ONSPEED_CORE_AUDIO_AUDIO_MIXER_H
