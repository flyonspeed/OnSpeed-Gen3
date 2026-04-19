// AudioFrame.h
//
// Non-owning handle to a buffer of stereo PCM audio samples. Used by
// core::audio::AudioMixer (PR 3.3) to pass synthesized audio to the sketch's
// I2S consumer. The buffer lifetime is the caller's responsibility — typically
// a statically-allocated ring buffer in the audio task.

#ifndef ONSPEED_CORE_TYPES_AUDIO_FRAME_H
#define ONSPEED_CORE_TYPES_AUDIO_FRAME_H

#include <cstddef>
#include <cstdint>

namespace onspeed {

struct AudioFrame {
    // Non-owning pointer to stereo int16 PCM samples, interleaved L,R,L,R,...
    // Null when no audio is pending.
    int16_t* samples = nullptr;

    // Number of stereo frames (NOT individual samples).
    // One frame = one L sample + one R sample = 2 int16 values.
    size_t sampleCount = 0;

    // Sample rate of the PCM data (Hz). Typically 16000 for OnSpeed audio.
    int sampleRateHz = 16000;
};

}   // namespace onspeed

#endif
