// FlapState.h
//
// Current flap-lever position: raw ADC, normalized fraction, and discrete
// detected index. Produced by FlapsDetector in PR 1.2; consumed by AHRS
// (flap-specific AOA setpoints), display serial, and log CSV.

#ifndef ONSPEED_CORE_TYPES_FLAP_STATE_H
#define ONSPEED_CORE_TYPES_FLAP_STATE_H

#include <cstdint>

namespace onspeed {

struct FlapState {
    // Raw ADC counts from the flap position input.
    uint16_t rawAdc = 0;

    // Normalized position: 0.0 = flaps fully up, 1.0 = full flaps.
    float normalized = 0.0f;

    // Which configured flap position the reading is nearest.
    // In range [0, FOSConfig::kMaxFlapPositions). 0 = flaps up.
    int detectedIndex = 0;

    // False if config has zero flap positions or reading is pinned to a
    // sentinel (sensor absent, ADC error, etc.).
    bool valid = false;
};

}   // namespace onspeed

#endif
