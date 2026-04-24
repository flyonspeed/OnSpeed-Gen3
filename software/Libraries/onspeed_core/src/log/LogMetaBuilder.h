// LogMetaBuilder.h
//
// Accumulator for per-session log metadata. Fed one LogRow at a time
// during the session; produces a populated LogMeta at close. Pure,
// platform-independent — no I/O.

#ifndef ONSPEED_CORE_LOG_LOG_META_BUILDER_H
#define ONSPEED_CORE_LOG_LOG_META_BUILDER_H

#include <cstdint>

#include <log/LogMeta.h>
#include <types/LogRow.h>

namespace onspeed::log {

class LogMetaBuilder {
public:
    // Call once at session start. Safe against oversized strings
    // (truncated to fit LogMeta's char arrays).
    void Begin(const char* firmware,
               const char* firmwareSha,
               int         logFormatVersion,
               EfisType    efisType);

    // Call once per row written to the CSV. `hmsOrNull` is an 8-char
    // "HH:MM:SS" NUL-terminated string when the EFIS carries a valid
    // time-of-day this frame, otherwise nullptr or empty string (both
    // treated identically). `utcOrNull` is ISO-8601 UTC (e.g.
    // "2026-04-18T14:32:07Z") with the same convention.
    //
    // Only the FIRST non-empty time string encountered is captured.
    // Subsequent rows update duration and running maxima only.
    void OnRow(const onspeed::LogRow& row,
               const char* hmsOrNull,
               const char* utcOrNull);

    // Produce a populated LogMeta. Safe to call at any time.
    // Duration is last-seen minus first-seen timeStampMs; 0 if no rows
    // have arrived yet.
    LogMeta Finalize() const;

    // Return to a just-constructed state. Mainly for tests.
    void Reset();

private:
    LogMeta  m_meta{};
    uint32_t m_firstTimeMs  = 0;
    uint32_t m_lastTimeMs   = 0;
    bool     m_haveFirstRow = false;
};

} // namespace onspeed::log

#endif
