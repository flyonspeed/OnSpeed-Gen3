// util/Crc.h — CRC helpers shared by OnSpeed protocols.
//
// The OnSpeed display-serial `#1` frame uses a simple additive 8-bit checksum:
//
//   checksum = (sum of all payload bytes) & 0xFF
//
// Both the Gen3 firmware builder (DisplaySerial.cpp) and the M5 display
// parser (SerialRead.cpp) implement this. This header is the single source
// of truth for the algorithm so the two sides cannot drift.

#ifndef ONSPEED_CORE_UTIL_CRC_H
#define ONSPEED_CORE_UTIL_CRC_H

#include <cstddef>
#include <cstdint>

namespace onspeed::util {

/// Sum the bytes in `data[0..len)` and return the low 8 bits.
/// This matches the algorithm used by both DisplaySerial.cpp (Gen3) and
/// SerialRead.cpp (M5) as of master: sum each byte then mask with 0xFF.
inline uint8_t Checksum8(const uint8_t* data, size_t len)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < len; ++i)
        sum += data[i];
    return static_cast<uint8_t>(sum & 0xFFu);
}

}   // namespace onspeed::util

#endif  // ONSPEED_CORE_UTIL_CRC_H
