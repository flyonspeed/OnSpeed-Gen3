// EfisFrame.h
//
// Normalized EFIS data, shared across all supported EFIS vendors. Produced
// by the appropriate parser in PR 2.2; consumed by the sensor-merge logic
// in SensorIO that blends EFIS and on-board sources.

#ifndef ONSPEED_CORE_TYPES_EFIS_FRAME_H
#define ONSPEED_CORE_TYPES_EFIS_FRAME_H

#include <cstdint>

namespace onspeed {

// Identifies which EFIS vendor produced this frame. Allows consumers to
// apply vendor-specific corrections or log the source without storing it
// separately.
//
// Note: Dynon covers both SkyView and D10; Garmin covers both G5 and G3X.
// The vendor-model distinction matters in the parser-dispatch layer
// (EFIS driver picks which byte-level parser to use), not in this
// post-parse normalized frame — consumers do not branch on model.
enum class EfisSource {
    None,
    Dynon,
    Garmin,
    Mgl,
    Vn300
};

struct EfisFrame {
    // Attitude (degrees).
    float pitchDeg   = 0.0f;
    float rollDeg    = 0.0f;
    float headingDeg = 0.0f;

    // Airspeed and altitude.
    float iasKt  = 0.0f;   // indicated airspeed (knots)
    float tasKt  = 0.0f;   // true airspeed (knots)
    float paltFt = 0.0f;   // pressure altitude (feet)
    float vsiFpm = 0.0f;   // vertical speed (feet/minute)

    // Air data.
    float oatCelsius = 0.0f;   // outside air temperature (Celsius)

    // AOA / lift. -1 if not supported by this EFIS type.
    float aoaPercent = -1.0f;   // 0..100 percent of stall AOA

    // G-loads.
    float lateralG  = 0.0f;   // lateral (side) G
    float verticalG = 0.0f;   // vertical (normal) G

    // Source identifier.
    EfisSource source = EfisSource::None;

    // Microsecond-resolution timestamp of the most recent parse.
    // Wraps ~71 minutes; consumers must diff with uint32_t arithmetic.
    uint32_t timestampUs = 0;
};

}   // namespace onspeed

#endif
