// WavDecode.cpp — PCM/WAV asset decoder implementation.

#include "WavDecode.h"

#include <cstdint>
#include <cstring>

namespace onspeed::audio {

namespace {

// Read a little-endian uint16 from a byte buffer at offset `off`.
inline std::uint16_t ReadU16LE(const unsigned char* p, std::size_t off) {
    return static_cast<std::uint16_t>(p[off]) |
           (static_cast<std::uint16_t>(p[off + 1]) << 8);
}

// Read a little-endian uint32 from a byte buffer at offset `off`.
inline std::uint32_t ReadU32LE(const unsigned char* p, std::size_t off) {
    return static_cast<std::uint32_t>(p[off]) |
           (static_cast<std::uint32_t>(p[off + 1]) << 8) |
           (static_cast<std::uint32_t>(p[off + 2]) << 16) |
           (static_cast<std::uint32_t>(p[off + 3]) << 24);
}

inline bool MatchTag(const unsigned char* p, std::size_t off, const char* tag) {
    return p[off]     == static_cast<unsigned char>(tag[0]) &&
           p[off + 1] == static_cast<unsigned char>(tag[1]) &&
           p[off + 2] == static_cast<unsigned char>(tag[2]) &&
           p[off + 3] == static_cast<unsigned char>(tag[3]);
}

}   // namespace

PcmAsset FromRawPcm(const unsigned char* bytes,
                    std::size_t byteLen,
                    int sampleRateHz,
                    int channels) {
    PcmAsset out;
    if (bytes == nullptr || byteLen < sizeof(int16_t))
        return out;

    out.samples      = reinterpret_cast<const int16_t*>(bytes);
    out.sampleCount  = byteLen / sizeof(int16_t);
    out.sampleRateHz = sampleRateHz;
    out.channels     = channels;
    return out;
}

PcmAsset DecodeWav(const unsigned char* bytes, std::size_t byteLen) {
    PcmAsset out;

    // Minimum: 12-byte RIFF header + 8-byte fmt chunk header + 16-byte fmt
    // payload + 8-byte data chunk header = 44 bytes.
    if (bytes == nullptr || byteLen < 44)
        return out;

    if (!MatchTag(bytes, 0, "RIFF") || !MatchTag(bytes, 8, "WAVE"))
        return out;

    // Walk subchunks starting at offset 12 looking for 'fmt ' and 'data'.
    std::size_t pos = 12;
    bool haveFmt = false;
    int      sampleRateHz   = 0;
    int      channels       = 0;
    std::uint16_t audioFmt  = 0;
    std::uint16_t bitsPerSample = 0;

    while (pos + 8 <= byteLen) {
        const std::uint32_t chunkSize = ReadU32LE(bytes, pos + 4);
        const std::size_t   payload   = pos + 8;
        if (payload > byteLen)
            return out;

        if (MatchTag(bytes, pos, "fmt ")) {
            if (chunkSize < 16 || payload + 16 > byteLen)
                return out;
            audioFmt       = ReadU16LE(bytes, payload + 0);
            channels       = static_cast<int>(ReadU16LE(bytes, payload + 2));
            sampleRateHz   = static_cast<int>(ReadU32LE(bytes, payload + 4));
            bitsPerSample  = ReadU16LE(bytes, payload + 14);
            haveFmt = true;
        }
        else if (MatchTag(bytes, pos, "data")) {
            if (!haveFmt)
                return out;
            if (audioFmt != 1)            // PCM only
                return out;
            if (bitsPerSample != 16)
                return out;
            if (channels < 1 || channels > 2)
                return out;
            if (sampleRateHz <= 0)
                return out;
            if (chunkSize < sizeof(int16_t))
                return out;
            if (payload + chunkSize > byteLen)
                return out;

            out.samples      = reinterpret_cast<const int16_t*>(bytes + payload);
            out.sampleCount  = chunkSize / sizeof(int16_t);
            out.sampleRateHz = sampleRateHz;
            out.channels     = channels;
            return out;
        }

        // Advance to next chunk.  Chunks are padded to even byte size.
        std::size_t step = static_cast<std::size_t>(chunkSize) + 8;
        if (step & 1u) ++step;
        if (step < 8 || pos + step < pos)   // overflow / underflow
            return out;
        pos += step;
    }

    return out;   // no data chunk found
}

}   // namespace onspeed::audio
