// AirDataValid.h — per-channel validity flags for the air-data pipeline.
//
// One bit per channel.  Producers set when the value is trustworthy;
// consumers check before use.  Default-constructed == all clear (safe
// boot state).
//
// Low 16 bits ride the v4.24 wire format `validFlags` field.  Upper 16
// bits are firmware-internal only (planned: cross-channel sanity layer,
// EFIS-source provenance flags).

#ifndef ONSPEED_CORE_TYPES_AIR_DATA_VALID_H
#define ONSPEED_CORE_TYPES_AIR_DATA_VALID_H

#include <cstdint>

namespace onspeed::types {

struct AirDataValid {
    uint32_t bits = 0;

    enum Bit : uint32_t {
        kOatRaw              = 1u <<  0,  // raw TAT, passed FilterOat
        kOatSat              = 1u <<  1,  // ram-rise-corrected SAT
        kIas                 = 1u <<  2,  // post iasAlive
        kPalt                = 1u <<  3,
        kTas                 = 1u <<  4,  // requires kOatSat + kIas + kPalt
        kDensityAlt          = 1u <<  5,  // requires kOatSat + kPalt
        kDerivedAoa          = 1u <<  6,  // requires kTas + kVsi
        kVsi                 = 1u <<  7,
        kPitch               = 1u <<  8,
        kRoll                = 1u <<  9,
        kPercentLift         = 1u << 10,
        kFlapsPos            = 1u << 11,
        // bits 12..14 reserved for future channels.
        kFrameSelfConsistent = 1u << 15,
        // bits 16..31 are firmware-internal only.
    };

    constexpr bool has(Bit b) const { return (bits & b) != 0; }
    void set(Bit b)   { bits |= b; }
    void clear(Bit b) { bits &= ~static_cast<uint32_t>(b); }
};

}   // namespace onspeed::types

#endif  // ONSPEED_CORE_TYPES_AIR_DATA_VALID_H
