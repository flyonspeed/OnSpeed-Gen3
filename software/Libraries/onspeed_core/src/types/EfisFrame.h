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

    // Engine data (Dynon SkyView EMS and Garmin G3X EMS; absent on
    // protocols/frames that don't carry it — G3X has no PercentPower).
    // Same hold-last-value contract as the airdata fields above:
    // `applyFrame()` only updates the corresponding SuEfisData member
    // when the value is finite. Vendor "absent" sentinels (Dynon's
    // `XXX`/`XXXX`, G3X's `___`/`____`) decode to kEfisFieldAbsent at
    // the parser boundary.
    float rpm              = kEfisFieldAbsent;   // engine rpm
    float mapInchHg        = kEfisFieldAbsent;   // manifold absolute pressure (inHg)
    float fuelFlowGph      = kEfisFieldAbsent;   // current fuel flow (gallons/hour)
    float fuelRemainingGal = kEfisFieldAbsent;   // fuel quantity remaining (gallons)
    float percentPower     = kEfisFieldAbsent;   // 0..100, EFIS-computed engine power

    // Source identifier.
    EfisSource source = EfisSource::None;

    // Microsecond-resolution timestamp of the most recent parse.
    // Wraps ~71 minutes; consumers must diff with uint32_t arithmetic.
    uint32_t timestampUs = 0;

    // Wall-clock time-of-day from the EFIS, as "HH:MM:SS" or
    // "HH:MM:SS.FF" NUL-terminated; the .FF tail is fractional seconds
    // (centiseconds) when the underlying protocol carries them
    // (Dynon D10, Garmin G5/G3X). SkyView writes only HH:MM:SS.
    // Empty string ("") means this frame did not carry a valid time.
    // Parsers populate this only when the underlying protocol provides
    // a real time and the frame's validity sentinel (Dynon's all-dashes,
    // VN-300's GPSFix==0, etc.) doesn't mark it as absent.
    char timeOfDayHms[12] = {};
};

}   // namespace onspeed

#endif
