// MglBinary.h
//
// MGL Avionics binary serial protocol parser.
//
// Each message begins with a 2-byte sync sequence (0x05, 0x02) followed by
// a header and then a CRC32-validated body. Two message types are used:
//
//   Type 1 (primary flight data, 44 bytes total):
//     Palt, IAS, TAS, AOA, VSI, OAT, time.
//
//   Type 3 (attitude data, 40 bytes total):
//     Heading, Pitch, Roll, VerticalG, LateralG.
//
// The message structure (from EfisSerial.cpp):
//
//   Offset  Byte   Field
//     0     0x05   DLE (sync byte 1)
//     1     0x02   STX (sync byte 2)
//     2            MessageLength (N)
//     3            MessageLengthXOR (must XOR with MessageLength to give 0xFF)
//     4            MessageType (1 or 3)
//     5            MessageRate
//     6            MessageCount
//     7            MessageVersion
//     8..N+19      Message body (varies by type)
//     N+16..N+19   CRC32
//
// Total message length = MessageLength + 20.
// MessageLength of 0x00 is treated as 256 (giving total = 276).
//
// CRC32 verification is NOT performed in this parser (the original firmware
// does not implement CRC32 for MGL either; it only validates MessageLength ^
// MessageLengthXOR == 0xFF). Ported verbatim.
//
// See: EfisSerial.cpp (EnMglBinary branch) for original implementation.

#ifndef ONSPEED_CORE_EFIS_MGL_BINARY_H
#define ONSPEED_CORE_EFIS_MGL_BINARY_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <types/EfisFrame.h>

namespace onspeed::efis {

// MglBinaryParser — state machine for MGL Avionics binary serial output.
//
// Feed one byte at a time with FeedByte(). After each call, check TakeFrame()
// to see if a complete frame is available.
//
// State machine:
//   bufLen_ == 0: waiting for first sync byte (0x05).
//   bufLen_ == 1: waiting for second sync byte (0x02).
//   bufLen_ == 2,3: accumulating length bytes.
//   bufLen_ >= 4: accumulating body bytes up to msgLen_.
//
// On message completion (bufLen_ >= msgLen_) the parser attempts to decode
// and then resets.
class MglBinaryParser {
public:
    MglBinaryParser() = default;

    void FeedByte(uint8_t b);
    bool TryTakeFrame(EfisFrame& out);
    std::optional<EfisFrame> TakeFrame();
    void Reset();

private:
    static constexpr size_t kBufSize = 512;

    uint8_t    buf_[kBufSize] = {};
    int        bufLen_         = 0;
    int        msgLen_         = 0;   // computed target length (MessageLength + 20)
    EfisFrame  pending_;
    bool       pendingReady_   = false;

    void Decode();
};

}   // namespace onspeed::efis

#endif
