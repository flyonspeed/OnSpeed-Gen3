// GarminG3X.h
//
// Garmin G3X serial protocol parser.
//
// The G3X outputs two frame types:
//
//   `=11` attitude/air-data (59 bytes, 10 Hz):
//     Same byte layout as the G5 `=11` frame but also includes AOA% and OAT.
//
//   `=31` EMS data (221 bytes, 5 Hz):
//     Engine data: FuelRemaining, FuelFlow, MAP, RPM.
//
// Checksum for both: sum bytes 0..(N-4) mod 256, compared against 2-hex-digit
// ASCII at (N-4). Bytes N-3 and N-2 are CR+LF.
//
// Like the G5 the G3X uses '_' as its sentinel. Keep semantics are handled
// by the caller; the parser returns -1.0f/-1 for sentinel fields.
//
// See: EfisSerial.cpp (EnGarminG3X branch) for original implementation.

#ifndef ONSPEED_CORE_EFIS_GARMIN_G3X_H
#define ONSPEED_CORE_EFIS_GARMIN_G3X_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <types/EfisFrame.h>

namespace onspeed::efis {

// GarminG3XParser — state machine for Garmin G3X serial output.
//
// Feed one byte at a time with FeedByte(). After each call, check TakeFrame()
// to see if a complete, CRC-valid frame is available.
//
// Frames start with '=' and are line-terminated with CR+LF.
class GarminG3XParser {
public:
    GarminG3XParser() = default;

    void FeedByte(uint8_t b);
    std::optional<EfisFrame> TakeFrame();
    void Reset();

private:
    static constexpr size_t kBufSize     = 256;
    static constexpr int    kAttFrameLen = 59;
    static constexpr int    kEmsFrameLen = 221;

    char   buf_[kBufSize] = {};
    int    bufLen_         = 0;
    std::optional<EfisFrame> pending_;

    void Decode();
    void DecodeAttitude();
    void DecodeEms();
};

}   // namespace onspeed::efis

#endif
