// WavDecode.h — PCM/WAV asset decoder.
//
// Two entry points:
//
//   FromRawPcm()  — wraps a header-less byte array of signed-16-bit
//                   little-endian PCM samples. This is the format of the
//                   compiled-in Audio/PCM_*.h byte arrays in the OnSpeed
//                   firmware (xxd-converted from .pcm files produced by
//                   `ffmpeg -f s16le -ar 16000 ...`).
//
//   DecodeWav()   — parses a RIFF/WAVE byte stream, finds the `data`
//                   chunk, and returns a view over the PCM samples. Used
//                   when the assets ever ship as real .wav files.
//
// Returns a non-owning PcmAsset view (pointer + count + sample rate +
// channel count). On malformed input DecodeWav() returns an empty view
// (samples == nullptr, sampleCount == 0).
//
// No dynamic allocation. No platform dependencies.

#ifndef ONSPEED_CORE_AUDIO_WAV_DECODE_H
#define ONSPEED_CORE_AUDIO_WAV_DECODE_H

#include <cstddef>
#include <cstdint>

namespace onspeed::audio {

// Non-owning view over a block of int16 PCM samples.  The lifetime of
// `samples` is the caller's responsibility — typically the byte array is
// a `const` global compiled into ROM.
struct PcmAsset {
    const int16_t* samples      = nullptr;
    std::size_t    sampleCount  = 0;     // mono samples per channel x channels
    int            sampleRateHz = 0;
    int            channels     = 0;     // 1 = mono, 2 = stereo

    bool empty() const { return samples == nullptr || sampleCount == 0; }
};

// Wrap a raw PCM byte array (no header) as a PcmAsset.  The number of
// samples is derived from `byteLen / 2` (each int16 sample is two bytes).
//
// `bytes` is reinterpret_cast to (const int16_t*); this assumes:
//   - host is little-endian (matches both ESP32 and x86_64 dev hosts)
//   - the byte array is correctly aligned for int16 access
// Both assumptions hold for the OnSpeed firmware's xxd-generated arrays.
//
// If `bytes == nullptr` or `byteLen < 2` returns an empty asset.
PcmAsset FromRawPcm(const unsigned char* bytes,
                    std::size_t byteLen,
                    int sampleRateHz,
                    int channels);

// Parse a RIFF/WAVE byte stream and return a view over the PCM samples
// inside the `data` chunk.  Supports only PCM (format code 1) at 16-bit
// sample width.  Returns an empty PcmAsset on any of:
//   - bytes == nullptr or byteLen < 44 (minimum WAV header)
//   - "RIFF" / "WAVE" magic missing
//   - no `fmt ` chunk found
//   - no `data` chunk found
//   - audioFormat != 1 (PCM)
//   - bitsPerSample != 16
//   - declared data length exceeds remaining bytes
//
// On success the returned view points into `bytes`; the caller must keep
// `bytes` alive for as long as the view is used.
PcmAsset DecodeWav(const unsigned char* bytes,
                   std::size_t byteLen);

}   // namespace onspeed::audio

#endif   // ONSPEED_CORE_AUDIO_WAV_DECODE_H
