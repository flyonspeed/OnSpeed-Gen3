// FastParse.h
//
// Shared single-pass field parsers for the ASCII EFIS protocols
// (Dynon SkyView, Dynon D10, Garmin G5, Garmin G3X).
//
// Replaces the per-field memcpy + memcmp + strtof/strtol dance with a
// single-pass byte scan. Numerically identical to the prior code: the
// final divide-by-scale uses the same float constant, so `234 / 10.0f`
// produces the same IEEE-754 bits whether the 234 came from strtof or
// from accumulating digits ourselves. The hand-rolled parser also
// returns the same fallback when the sentinel matches.
//
// Sentinel handling: each ASCII protocol uses a single repeating
// sentinel byte ('X' for Dynon SkyView, '_' for Garmin G5/G3X, '-' for
// Dynon D10 time fields). First-byte check is exact for fixed-width
// fields, since the sentinel ALWAYS fills the entire field (never a
// partial mix of digits and sentinel chars).

#ifndef ONSPEED_CORE_EFIS_FAST_PARSE_H
#define ONSPEED_CORE_EFIS_FAST_PARSE_H

#include <cstdint>

namespace onspeed::efis::fastparse {

// ===========================================================================
// Inline attribute. Forces inlining through static-function boundaries
// even when LTO is off (PIO native test env builds without LTO).
// ===========================================================================
#if defined(__GNUC__) || defined(__clang__)
#define ONSPEED_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define ONSPEED_ALWAYS_INLINE inline
#endif

// ===========================================================================
// parseFixedUnsigned — parse `len` ASCII digits at buf[pos..pos+len) into
// an unsigned integer. Leading spaces are skipped (matches strtol on
// space-padded fields like Dynon's IAS "  62"). No sign handling.
//
// Returns 0 on a malformed field (any non-digit, non-space byte).
// Callers gate on sentinel detection before invoking this.
// ===========================================================================
ONSPEED_ALWAYS_INLINE uint32_t parseFixedUnsigned(const char* buf, int pos, int len)
{
    uint32_t acc = 0;
    // Skip leading spaces (Dynon and Garmin pad short numbers with leading
    // spaces; strtol does the same). Bytes after the first non-space must
    // be digits — anything else aborts and returns whatever we'd accumulated.
    int i = 0;
    while (i < len && buf[pos + i] == ' ') i++;
    while (i < len)
    {
        const unsigned char c = static_cast<unsigned char>(buf[pos + i]);
        if (c < '0' || c > '9') return acc;
        acc = acc * 10u + (c - '0');
        i++;
    }
    return acc;
}

// ===========================================================================
// parseFixedSigned — parse `len` ASCII chars at buf[pos..pos+len) into
// a signed integer. The first non-space byte may be '+', '-', or a
// digit. Matches strtol on Dynon's signed fields (e.g. "+023", "-0050").
//
// Returns 0 on a malformed field. Callers gate on sentinel detection.
// ===========================================================================
ONSPEED_ALWAYS_INLINE int32_t parseFixedSigned(const char* buf, int pos, int len)
{
    int i = 0;
    while (i < len && buf[pos + i] == ' ') i++;
    bool negative = false;
    if (i < len)
    {
        const char c = buf[pos + i];
        if (c == '+') { i++; }
        else if (c == '-') { negative = true; i++; }
    }
    int32_t acc = 0;
    while (i < len)
    {
        const unsigned char c = static_cast<unsigned char>(buf[pos + i]);
        if (c < '0' || c > '9') return negative ? -acc : acc;
        acc = acc * 10 + (c - '0');
        i++;
    }
    return negative ? -acc : acc;
}

// ===========================================================================
// parseHex2 — parse the two ASCII hex characters at buf[pos] and
// buf[pos+1] into a byte (0..255). Returns -1 if either character is
// not a hex digit. Used for the trailing checksum byte in all four
// ASCII protocols.
// ===========================================================================
ONSPEED_ALWAYS_INLINE int parseHex2(const char* buf, int pos)
{
    auto hexNibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    const int hi = hexNibble(buf[pos]);
    const int lo = hexNibble(buf[pos + 1]);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

// ===========================================================================
// parseHex1 — parse one ASCII hex character at buf[pos] into a nibble.
// Returns -1 on non-hex input. Used for the Dynon D10 status nibble.
// ===========================================================================
ONSPEED_ALWAYS_INLINE int parseHex1(const char* buf, int pos)
{
    const char c = buf[pos];
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// ===========================================================================
// isSentinel — first-byte sentinel check. ASCII EFIS protocols fill the
// ENTIRE field with the sentinel byte when a reading is unavailable
// ("XXXX" for Dynon, "____" for Garmin). First-byte equality is
// sufficient and exact: a partial mix never occurs.
// ===========================================================================
ONSPEED_ALWAYS_INLINE bool isSentinel(const char* buf, int pos, char sentinelByte)
{
    return buf[pos] == sentinelByte;
}

// ===========================================================================
// Sum-checksum — sums `len` bytes starting at buf[pos], masks to 8 bits.
// Identical to the per-protocol checksum loops; centralized for clarity.
// ===========================================================================
ONSPEED_ALWAYS_INLINE uint8_t sumChecksum(const char* buf, int pos, int len)
{
    uint32_t acc = 0;
    for (int i = 0; i < len; i++)
        acc += static_cast<unsigned char>(buf[pos + i]);
    return static_cast<uint8_t>(acc & 0xFFu);
}

}   // namespace onspeed::efis::fastparse

#endif
