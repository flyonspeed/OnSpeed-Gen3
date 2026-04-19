// EfisFrame.h
//
// Normalized EFIS data, shared across all supported EFIS vendors. Produced
// by the appropriate parser in PR 2.2; consumed by the sensor-merge logic
// in SensorIO that blends EFIS and on-board sources.

#ifndef ONSPEED_CORE_TYPES_EFIS_FRAME_H
#define ONSPEED_CORE_TYPES_EFIS_FRAME_H

#include <cstdint>
#include <limits>

namespace onspeed {

// "Field absent in this frame" sentinel. Parsers leave a field at this
// value when the underlying protocol either does not carry it, or carries
// a vendor-specific "temporarily unavailable" marker (Dynon D10 status bit,
// Garmin `____` placeholder, MGL missing message, etc.) that means
// "hold the previous value".
//
// Consumers test std::isfinite() at the boundary: finite -> apply,
// non-finite -> skip (preserve the prior suEfis value). This lets every
// parser be write-only for the fields it actually decoded this frame and
// preserves the hold-last-value semantics the vendors expect.
inline constexpr float kEfisFieldAbsent = std::numeric_limits<float>::quiet_NaN();

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
    // Attitude (degrees). Default = absent; set by parser only if the frame
    // carries a valid value for that field.
    float pitchDeg   = kEfisFieldAbsent;
    float rollDeg    = kEfisFieldAbsent;
    float headingDeg = kEfisFieldAbsent;

    // Airspeed and altitude.
    float iasKt  = kEfisFieldAbsent;   // indicated airspeed (knots)
    float tasKt  = kEfisFieldAbsent;   // true airspeed (knots)
    float paltFt = kEfisFieldAbsent;   // pressure altitude (feet)
    float vsiFpm = kEfisFieldAbsent;   // vertical speed (feet/minute)

    // Air data.
    float oatCelsius = kEfisFieldAbsent;   // outside air temperature (Celsius)

    // AOA / lift, 0..100 percent of stall AOA.
    float aoaPercent = kEfisFieldAbsent;

    // G-loads.
    float lateralG  = kEfisFieldAbsent;   // lateral (side) G
    float verticalG = kEfisFieldAbsent;   // vertical (normal) G

    // Source identifier.
    EfisSource source = EfisSource::None;

    // Microsecond-resolution timestamp of the most recent parse.
    // Wraps ~71 minutes; consumers must diff with uint32_t arithmetic.
    uint32_t timestampUs = 0;
};

}   // namespace onspeed

#endif
