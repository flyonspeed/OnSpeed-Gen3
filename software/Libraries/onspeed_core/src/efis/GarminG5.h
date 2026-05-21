// GarminG5.h
//
// Garmin G5 serial protocol parser.
//
// The G5 outputs a single frame type: `=11` attitude/air-data (59 bytes
// including CR+LF). Fields are ASCII decimal.
//
// Checksum: sum bytes 0..54 mod 256, compared against 2-hex-digit ASCII at
// positions 55..56. CRC bytes followed by CR+LF.
//
// The G5 uses '_' (underscore) as its sentinel for "field not available",
// unlike Dynon which uses 'X'. Fields with sentinel are kept at their
// previous value (hence parseFieldFloatKeep/parseFieldIntKeep in the
// original firmware). In this pure parser, we use -1.0f / -1 as the
// returned sentinel value for unavailable fields; the caller / adapter may
// implement keep-previous semantics on top.
//
// Note: the G5 does NOT output an AOA percent field.
//
// See: EfisSerial.cpp (EnGarminG5 branch) for original implementation.

#ifndef ONSPEED_CORE_EFIS_GARMIN_G5_H
#define ONSPEED_CORE_EFIS_GARMIN_G5_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <types/EfisFrame.h>

namespace onspeed::efis {

// GarminG5Parser — state machine for Garmin G5 serial output.
//
// Feed one byte at a time with FeedByte(). After each call, check TakeFrame()
// to see if a complete, CRC-valid frame is available.
//
// Frames start with '=' and are line-terminated with CR+LF. Any line that
// does not start with '=', does not have the `=11` type prefix, or does not
// match the expected 59-byte length is silently discarded.
class GarminG5Parser {
public:
    GarminG5Parser() = default;

    void FeedByte(uint8_t b);
    bool TryTakeFrame(EfisFrame& out);
    std::optional<EfisFrame> TakeFrame();
    void Reset();

private:
    static constexpr size_t kBufSize  = 256;
    static constexpr int    kFrameLen = 59;

    char       buf_[kBufSize] = {};
    int        bufLen_         = 0;
    EfisFrame  pending_;
    bool       pendingReady_   = false;

    void Decode();
};

}   // namespace onspeed::efis

#endif
