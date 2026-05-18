// Crc8.h — CRC-8 with poly 0x07, init 0x00, no XOR-out, no reflection.
//
// Algorithm matches the SMBus / I²C-block-write CRC.  The 256-entry
// table is computed once at compile time; the runtime path is one xor
// plus one table lookup per byte.  Catches all single-bit flips, all
// 2-bit errors within 256 bits, and most burst errors that an
// additive sum-mod-256 silently masks.

#ifndef ONSPEED_CORE_PROTO_CRC8_H
#define ONSPEED_CORE_PROTO_CRC8_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace onspeed::proto {

namespace detail {

constexpr std::array<uint8_t, 256> MakeCrc8Table()
{
    std::array<uint8_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        uint8_t c = static_cast<uint8_t>(i);
        for (int j = 0; j < 8; ++j) {
            c = (c & 0x80) ? static_cast<uint8_t>((c << 1) ^ 0x07)
                           : static_cast<uint8_t>(c << 1);
        }
        table[i] = c;
    }
    return table;
}

inline constexpr std::array<uint8_t, 256> kCrc8Table = MakeCrc8Table();

}   // namespace detail

// CRC-8 of `len` bytes starting at `data`.  `data` may be nullptr
// iff `len == 0` (an empty input returns 0x00).
inline uint8_t Crc8(const uint8_t* data, std::size_t len)
{
    uint8_t c = 0;
    for (std::size_t i = 0; i < len; ++i) {
        c = detail::kCrc8Table[c ^ data[i]];
    }
    return c;
}

}   // namespace onspeed::proto

#endif  // ONSPEED_CORE_PROTO_CRC8_H
