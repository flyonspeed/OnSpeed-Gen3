// AudioMixer.h — stereo PCM mixer with per-channel gain, pan, optional
// pulse-gate modulation, and optional DAHDR envelope shaping.
//
// Takes a mono int16 PCM input (either a synthesized tone buffer from
// ToneSynth or a voice-prompt PCM asset from WavDecode) and produces
// stereo interleaved int16 output after applying:
//
//   gate = envelope ? envelope.Tick()
//                   : pulseGate(i)               // legacy fallback
//   left_sample  = clamp(in_sample * leftScale  * gate)
//   right_sample = clamp(in_sample * rightScale * gate)
//
// Pan is expressed via independent left/right gain factors — the caller
// composes these from (masterVolume * channelGain * stallVolMult * pan)
// and caps each at 1.0 to prevent waveform clipping.
//
// **Envelope path (preferred for tones)**: pass a non-null `envelope`
// pointer in `MixerInputs`.  The envelope's per-sample Tick() value
// becomes the gate.  This is the Gen2-faithful path: the silent phases
// of the DAHDR envelope mask any inter-buffer step changes in
// leftScale/rightScale (volume, 3D panning), giving click-free
// transitions for every parameter change.
//
// **Pulse-gate fallback**: when `envelope == nullptr`, the legacy
// per-sample on/off toggle is used (gate = 1.0 on-phase, offScale on
// off-phase, switching at halfPeriodSamples).  Retained for voice
// playback and for callers that don't need full envelope shaping.
//
// No dynamic allocation. Output buffer must be sized for 2*frameCount
// int16 values (interleaved L,R,L,R,...).  Input and output buffers
// must not overlap.

#ifndef ONSPEED_CORE_AUDIO_AUDIO_MIXER_H
#define ONSPEED_CORE_AUDIO_AUDIO_MIXER_H

#include <cstddef>
#include <cstdint>

#include "Envelope.h"
#include "ToneSynth.h"   // PulseGateSpec / PulseGateState

namespace onspeed::audio {

// Per-call inputs to AudioMixer::Mix().
struct MixerInputs {
    // Mono PCM source samples.  Must be non-null if frameCount > 0.
    const int16_t* in = nullptr;

    // Per-channel scale factors (0.0 = silent, 1.0 = unity).  Caller
    // composes from (masterVolume * stallVolMult * channelGain * panFactor)
    // and is responsible for clipping protection (cap each at 1.0).
    float leftScale  = 1.0f;
    float rightScale = 1.0f;

    // Preferred per-sample gate: a DAHDR envelope, advanced one Tick()
    // per output sample.  When non-null this overrides pulseSpec entirely
    // — the caller drives pulse rate and shape via envelope NoteOn/Off.
    Envelope* envelope = nullptr;

    // Legacy pulse-gate.  Used only when envelope == nullptr.  Set
    // halfPeriodSamples = 0 to pass through (gate = 1.0 every sample).
    PulseGateSpec  pulseSpec;
};

// Caller-owned state for AudioMixer.  Tracks pulse-gate phase for the
// legacy path; the envelope owns its own state (passed by pointer in
// MixerInputs).
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
// bits = right).
std::uint32_t PackStereoI16(std::int16_t left, std::int16_t right);

}   // namespace onspeed::audio

#endif   // ONSPEED_CORE_AUDIO_AUDIO_MIXER_H
