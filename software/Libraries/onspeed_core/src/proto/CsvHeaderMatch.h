// proto/CsvHeaderMatch.h — comma-boundary token matching for CSV headers.
//
// LogReplay reads the first line of an OnSpeed CSV log via fgets and probes
// it for column names to validate that the producer firmware wrote a layout
// that the consumer firmware knows how to parse. A plain strstr would let
// `"Pitch"` match the unrelated `"PitchRate"` column, silently misaligning
// ParseRow's column-by-column reads if a column is added or renamed; the
// helper here matches a name only when it appears as a complete CSV token.
//
// A token is delimited on the left by string start or `,` and on the right
// by `,`, `\0`, `\r`, or `\n`. The line-ending boundaries cover the trailing
// column on lines read by fgets (terminated with `\n`, or `\r\n` on
// Windows-transcoded logs); fgets stops at and includes `\n`, so a mid-line
// `\r` is not possible in this input.
//
// Header-only by design: both LogReplay.cpp (firmware) and the native unit
// test link against this single inline definition.
//
// Defensive note: passing an empty `name` is undefined for callers but the
// function returns false. The implementation guards against the
// `strlen(name) == 0` case so it does not infinite-loop.

#ifndef ONSPEED_CORE_PROTO_CSV_HEADER_MATCH_H
#define ONSPEED_CORE_PROTO_CSV_HEADER_MATCH_H

#include <cstring>

namespace onspeed::proto {

/// Return true if `name` appears in the CSV header line `hdr` as a complete
/// comma-delimited token. Both pointers must be non-null, NUL-terminated
/// strings. An empty `name` returns false.
inline bool HasColumn(const char* hdr, const char* name)
{
    const size_t n = strlen(name);
    if (n == 0)
        return false;
    const char* p = hdr;
    while ((p = strstr(p, name)) != nullptr)
        {
        const bool leftOk  = (p == hdr) || (p[-1] == ',');
        const char rightCh = p[n];
        const bool rightOk = (rightCh == '\0') || (rightCh == ',') ||
                             (rightCh == '\r') || (rightCh == '\n');
        if (leftOk && rightOk)
            return true;
        p += 1;   // advance past this candidate to seek the next
        }
    return false;
}

}   // namespace onspeed::proto

#endif  // ONSPEED_CORE_PROTO_CSV_HEADER_MATCH_H
