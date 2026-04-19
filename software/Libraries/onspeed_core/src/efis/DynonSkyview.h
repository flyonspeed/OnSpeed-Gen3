// DynonSkyview.h
//
// Dynon SkyView serial protocol parser.
//
// `!1` frames carry ADAHRS data (74 bytes including CR+LF):
//   pitch, roll, heading, IAS, TAS, Palt, VSI, OAT, LateralG, VerticalG,
//   PercentLift, and time.
//
// `!3` frames carry EMS data (225 bytes including CR+LF):
//   FuelRemaining, FuelFlow, MAP, RPM, PercentPower.
//
// Both frame types use the same checksum: sum of all bytes from index 0 to
// (length - 4) inclusive, modulo 256, compared against a two-hex-digit ASCII
// field at (length - 4).
//
// See: docs/site/docs/efis-integration/dynon.md for field descriptions.
// See: EfisSerial.cpp (original implementation) for field offsets.

#ifndef ONSPEED_CORE_EFIS_DYNON_SKYVIEW_H
#define ONSPEED_CORE_EFIS_DYNON_SKYVIEW_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <types/EfisFrame.h>

namespace onspeed::efis {

// DynonSkyviewParser — state machine for Dynon SkyView serial output.
//
// Feed one byte at a time with FeedByte(). After each call, check TakeFrame()
// to see if a complete, CRC-valid frame is available. If so, TakeFrame()
// returns the frame and clears the pending slot; subsequent calls return
// std::nullopt until the next complete frame arrives.
//
// The parser handles interleaved !1 and !3 frames and silently discards bytes
// that don't match the expected framing (wrong magic, wrong length, bad CRC).
//
// State machine:
//   WAIT_MAGIC — discards bytes until '!' is seen; transition to COLLECT.
//   COLLECT    — accumulates bytes; on '\n' (0x0A), attempts to decode;
//                resets to WAIT_MAGIC regardless of decode success.
//
// Both !1 and !3 frames are line-terminated (0x0D 0x0A). The parser treats
// '\n' (0x0A) as the frame-complete signal, matching the original firmware.
// The accumulated buffer includes everything up to and including the '\n'.
class DynonSkyviewParser {
public:
    DynonSkyviewParser() = default;

    void FeedByte(uint8_t b);
    std::optional<EfisFrame> TakeFrame();
    void Reset();

private:
    // State is implicit in bufLen_: 0 means we are waiting for '!'.
    // When bufLen_ > 0 we are collecting.
    static constexpr size_t kBufSize = 256;

    char   buf_[kBufSize] = {};
    int    bufLen_         = 0;
    std::optional<EfisFrame> pending_;

    void   Decode();
    bool   DecodeAdahrs(EfisFrame& out);   // !1 frame
    bool   DecodeEms(EfisFrame& out);      // !3 frame
};

}   // namespace onspeed::efis

#endif
