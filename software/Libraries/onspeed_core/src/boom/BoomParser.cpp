// BoomParser.cpp
//
// Single-pass decode: validate CRC at the tail, scan for commas from
// the fixed anchor at byte 21, accumulate digits into 4 integer fields.
// No strtok, no atoi, no strtol — all of those are locale-aware and
// substantially slower than what we need for fixed-format ASCII counts.

#include <boom/BoomParser.h>
#include <efis/FastParse.h>

#include <cstring>

namespace onspeed::boom {

using onspeed::efis::fastparse::parseHex2;

// Find the index of byte `c` in [start, end). Returns -1 if not found.
static int findByte(const char* buf, int start, int end, char c)
{
    for (int i = start; i < end; i++)
        if (buf[i] == c) return i;
    return -1;
}

// Parse a signed decimal integer from buf[start..end). Tolerates leading
// whitespace and an optional + / - sign. Stops at the first non-digit
// byte and returns whatever was accumulated. Locale-independent.
static int parseSignedRange(const char* buf, int start, int end)
{
    int i = start;
    while (i < end && (buf[i] == ' ' || buf[i] == '\t')) i++;
    bool neg = false;
    if (i < end)
    {
        if (buf[i] == '-') { neg = true; i++; }
        else if (buf[i] == '+') { i++; }
    }
    int acc = 0;
    while (i < end)
    {
        const unsigned char ch = static_cast<unsigned char>(buf[i]);
        if (ch < '0' || ch > '9') break;
        acc = acc * 10 + (ch - '0');
        i++;
    }
    return neg ? -acc : acc;
}

BoomFrame Decode(const char* buf, int len, bool checkCrc)
{
    BoomFrame out;

    // Mirror the legacy parser's minimum-length floor (21). Below that
    // there isn't enough room for the prefix + at least 1-char field +
    // CRC, so the decode would be meaningless anyway.
    if (len < 21) return out;
    if (buf[0] != '$') return out;

    // CRC bytes are the last 2; the byte at [len-3] is the separator
    // (',' for real $AIRDAQ, '*' for $BOOM synth — we don't care which).
    // Sum is over [0..len-3) inclusive of indices 0..len-4. That matches
    // the legacy BoomSerial.cpp behavior.
    if (checkCrc)
    {
        uint32_t crc = 0;
        for (int i = 0; i < len - 3; i++)
            crc += static_cast<unsigned char>(buf[i]);
        crc &= 0xFFu;
        const int wireCrc = parseHex2(buf, len - 2);
        if (wireCrc < 0 || static_cast<int>(crc) != wireCrc) return out;
    }

    // Field walk: 4 comma-separated integers starting at position 21,
    // ending at the separator before the CRC (byte [len-3]). For real
    // $AIRDAQ that's a ',', for $BOOM synth it's '*'. Either way, we
    // scan commas inside [21..len-3) and grab the run between each.
    const int fieldsEnd = len - 3;   // exclusive end of last integer field
    if (fieldsEnd <= 21) return out;

    int fieldStart = 21;
    int values[4]  = {0, 0, 0, 0};
    int field      = 0;
    while (field < 4 && fieldStart < fieldsEnd)
    {
        int fieldEnd = findByte(buf, fieldStart, fieldsEnd, ',');
        if (fieldEnd < 0) fieldEnd = fieldsEnd;
        if (fieldEnd <= fieldStart) return out;   // empty field is malformed
        values[field] = parseSignedRange(buf, fieldStart, fieldEnd);
        fieldStart = fieldEnd + 1;
        field++;
    }
    if (field < 4) return out;   // not enough fields

    out.staticCounts  = values[0];
    out.dynamicCounts = values[1];
    out.alphaCounts   = values[2];
    out.betaCounts    = values[3];
    out.valid         = true;
    return out;
}

}   // namespace onspeed::boom
