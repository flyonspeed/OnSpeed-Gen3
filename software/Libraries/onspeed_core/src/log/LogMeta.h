// LogMeta.h
//
// Metadata record written alongside each SD-card log file as `log_NNN.meta`.
// Accumulated during the session by LogMetaBuilder; serialized at close by
// LogMetaFile::WriteMetaFile; parsed on the /logs web page by
// LogMetaFile::ParseMetaFile.

#ifndef ONSPEED_CORE_LOG_LOG_META_H
#define ONSPEED_CORE_LOG_LOG_META_H

#include <cstddef>
#include <cstdint>

namespace onspeed::log {

// Small local EfisType enum. Kept separate from the sketch's EfisSerialPort
// so onspeed_core stays platform-independent (no Arduino.h pulled in).
enum class EfisType : uint8_t {
    None   = 0,
    Dynon  = 1,
    Garmin = 2,
    Mgl    = 3,
    Vn300  = 4,
};

// "YYYY-MM-DDTHH:MM:SSZ" is 20 chars; 24 gives slack for trailing NUL + noise.
inline constexpr size_t kLogMetaUtcLen = 24;

// "HH:MM:SS" is 8 chars + NUL.
inline constexpr size_t kLogMetaHmsLen = 9;

// Firmware version strings; BuildInfo::version is "X.Y.Z" plus optional "-dirty".
inline constexpr size_t kLogMetaFwLen  = 24;
inline constexpr size_t kLogMetaShaLen = 16;

struct LogMeta {
    uint8_t  metaVersion        = 1;
    int      logFormatVersion   = 0;
    char     firmware[kLogMetaFwLen]  = {};
    char     firmwareSha[kLogMetaShaLen] = {};
    uint32_t durationMs         = 0;
    uint32_t rowCount           = 0;
    float    maxIasKt           = 0.0f;
    float    maxPaltFt          = 0.0f;
    EfisType efisType           = EfisType::None;
    bool     gpsFixSeen         = false;
    char     utcStart[kLogMetaUtcLen]  = {};   // "" = absent
    char     timeOfDayStart[kLogMetaHmsLen] = {};   // "" = absent
};

// Convert enum to wire string. Always returns a valid pointer; defaults
// to "none" for anything unrecognised.
const char* EfisTypeToString(EfisType t);

// Inverse of the above. Returns EfisType::None for unknown input.
EfisType EfisTypeFromString(const char* s);

} // namespace onspeed::log

#endif
