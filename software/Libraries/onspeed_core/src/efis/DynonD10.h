// DynonD10.h
//
// Dynon D10/D100/EFIS-D10A serial protocol parser.
//
// The D10 outputs a single 53-byte line (52 bytes for logged data). The frame
// starts at the beginning of the line (no explicit magic byte — framing
// relies on line-length matching). Fields are ASCII decimal.
//
// Checksum: sum bytes 0..48 mod 256, compared against 2-hex-digit ASCII at
// positions 49..50. CRC bytes are followed by CR+LF.
//
// Speed is output in m/s (converted to knots in parse). Altitude is in metres
// (converted to feet). VSI is in feet/sec (converted to feet/min).
//
// The status byte at position 46 carries a bitmask; bit 0 = 1 means Palt and
// VSI fields in the current frame are valid. When bit 0 = 0 the parser leaves
// Palt and VSI at 0 (not carrying the previous value — no keep semantics in
// the pure parser layer; callers may implement that on top).
//
// See: EfisSerial.cpp (EnDynonD10 branch) for original implementation.

#ifndef ONSPEED_CORE_EFIS_DYNON_D10_H
#define ONSPEED_CORE_EFIS_DYNON_D10_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <types/EfisFrame.h>

namespace onspeed::efis {

// DynonD10Parser — state machine for Dynon D10/D100/EFIS-D10A serial output.
//
// Feed one byte at a time with FeedByte(). After each call, check TakeFrame()
// to see if a complete, CRC-valid frame is available.
//
// State machine mirrors DynonSkyviewParser: bytes are accumulated into a line
// buffer; on '\n' the decode attempt is made and the buffer is cleared.
// Unlike SkyView there is no magic leading byte, so the parser waits for the
// previous line to end (indicated by seeing '\n' with bufLen_ == 0, or simply
// starting collection on any byte after a reset).
//
// Original firmware quirk preserved: the D10 CRC is checked against positions
// 0..48 (49 bytes), and the frame must be exactly DYNON_SERIAL_LEN (53) bytes
// long (including CR+LF). Frames of other lengths are silently discarded.
class DynonD10Parser {
public:
    DynonD10Parser() = default;

    void FeedByte(uint8_t b);
    bool TryTakeFrame(EfisFrame& out);
    std::optional<EfisFrame> TakeFrame();
    void Reset();

private:
    static constexpr size_t kBufSize   = 256;
    static constexpr int    kFrameLen  = 53;   // DYNON_SERIAL_LEN

    char       buf_[kBufSize] = {};
    int        bufLen_         = 0;
    bool       lineStart_      = false;   // true once we've seen the first '\n'
    EfisFrame  pending_;
    bool       pendingReady_   = false;

    void Decode();
};

}   // namespace onspeed::efis

#endif
