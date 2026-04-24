// DynonSkyview.cpp
//
// Ported verbatim from EfisSerial.cpp (EnDynonSkyview branch). No algorithmic
// changes — characterisation first, correctness improvements belong in a
// follow-on PR once tests pin current behaviour.

#include <efis/DynonSkyview.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <types/EfisFrame.h>

namespace onspeed::efis {

// ---------------------------------------------------------------------------
// Internal field-parsing helpers (mirrors EfisSerial.cpp static helpers)
// ---------------------------------------------------------------------------

// Parse len chars starting at pos as float.
// Returns fallback when the field equals the sentinel string.
static float parseFieldFloat(const char* buf, int pos, int len,
                             const char* sentinel, float fallback, float scale)
{
    char tmp[16];
    memcpy(tmp, buf + pos, static_cast<size_t>(len));
    tmp[len] = '\0';
    if (sentinel && memcmp(tmp, sentinel, static_cast<size_t>(len)) == 0)
        return fallback;
    return strtof(tmp, nullptr) / scale;
}

static int parseFieldInt(const char* buf, int pos, int len,
                         const char* sentinel, int fallback, int scale)
{
    char tmp[16];
    memcpy(tmp, buf + pos, static_cast<size_t>(len));
    tmp[len] = '\0';
    if (sentinel && memcmp(tmp, sentinel, static_cast<size_t>(len)) == 0)
        return fallback;
    return static_cast<int>(strtol(tmp, nullptr, 10)) * scale;
}

// Parse a 2-char hex CRC field at the given position
static int parseHexCRC(const char* buf, int pos)
{
    char szCRC[3] = { buf[pos], buf[pos + 1], '\0' };
    return static_cast<int>(strtol(szCRC, nullptr, 16));
}

// Parse Dynon !1 System Time bytes buf[3..8] ("HHMMSS" — the FF fraction
// at buf[9..10] is ignored). Writes an 8-char "HH:MM:SS" NUL-terminated
// string into `out[9]`. Writes an empty string if any dash is present in
// the 6 HHMMSS bytes or if H/M/S are out of range.
static void parseSystemTime(const char* buf, char out[9])
{
    out[0] = '\0';

    // Any dash in the HHMMSS portion = GPS has never locked. Treat as absent.
    for (int i = 3; i <= 8; i++)
        if (buf[i] == '-')
            return;

    // Parse two-digit fields. Returns -1 if either char isn't 0-9.
    auto twoDigit = [](const char* p) -> int {
        if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
        return (p[0] - '0') * 10 + (p[1] - '0');
    };
    int hh = twoDigit(buf + 3);
    int mm = twoDigit(buf + 5);
    int ss = twoDigit(buf + 7);
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59)
        return;

    snprintf(out, 9, "%02d:%02d:%02d", hh, mm, ss);
}

// ---------------------------------------------------------------------------
// DynonSkyviewParser
// ---------------------------------------------------------------------------

void DynonSkyviewParser::FeedByte(uint8_t b)
{
    const char c = static_cast<char>(b);

    // When bufLen_ == 0 we wait for the frame-start magic byte '!'.
    if (bufLen_ == 0)
    {
        if (c != '!')
            return;
        // Start collecting — guard overflow.
        if (bufLen_ < static_cast<int>(kBufSize) - 1)
        {
            buf_[bufLen_++] = c;
        }
        return;
    }

    // Collecting — guard against overflow (matches original firmware: reset on
    // iBufferLen > 230).
    if (bufLen_ > 230)
    {
        bufLen_ = 0;
        return;
    }

    buf_[bufLen_++] = c;

    // End-of-line: attempt decode then reset.
    if (c == '\n')
    {
        buf_[bufLen_] = '\0';   // null-terminate for strtol/strtof safety
        Decode();
        bufLen_ = 0;
    }
}

std::optional<EfisFrame> DynonSkyviewParser::TakeFrame()
{
    if (!pending_)
        return std::nullopt;
    EfisFrame out = *pending_;
    pending_.reset();
    return out;
}

void DynonSkyviewParser::Reset()
{
    bufLen_ = 0;
    pending_.reset();
}

void DynonSkyviewParser::Decode()
{
    if (bufLen_ == 74 && buf_[0] == '!' && buf_[1] == '1')
    {
        EfisFrame frame;
        if (DecodeAdahrs(frame))
            pending_ = frame;
        return;
    }

    if (bufLen_ == 225 && buf_[0] == '!' && buf_[1] == '3')
    {
        EfisFrame frame;
        if (DecodeEms(frame))
            pending_ = frame;
        return;
    }

    // Unknown length or wrong magic — silently discard.
}

bool DynonSkyviewParser::DecodeAdahrs(EfisFrame& out)
{
    // Checksum: sum bytes 0..69 mod 256, compare to hex digits at 70..71.
    int calcCRC = 0;
    for (int i = 0; i <= 69; i++)
        calcCRC += static_cast<unsigned char>(buf_[i]);
    calcCRC &= 0xFF;

    if (calcCRC != parseHexCRC(buf_, 70))
        return false;

    // Sentinel = 'XXXX'. Fallback = NaN marks the field absent so applyFrame()
    // will hold the prior suEfis value rather than overwrite with junk.
    const float kNaN = kEfisFieldAbsent;
    out.iasKt      = parseFieldFloat(buf_, 23, 4, "XXXX",   kNaN, 10.0f);
    out.pitchDeg   = parseFieldFloat(buf_, 11, 4, "XXXX",   kNaN, 10.0f);
    out.rollDeg    = parseFieldFloat(buf_, 15, 5, "XXXXX",  kNaN, 10.0f);
    {
        const int raw = parseFieldInt(buf_, 20, 3, "XXX", INT32_MIN, 1);
        out.headingDeg = (raw == INT32_MIN) ? kNaN : static_cast<float>(raw);
    }
    out.lateralG   = parseFieldFloat(buf_, 37, 3, "XXX",    kNaN, 100.0f);
    out.verticalG  = parseFieldFloat(buf_, 40, 3, "XXX",    kNaN, 10.0f);
    {
        const int raw = parseFieldInt(buf_, 43, 2, "XX", INT32_MIN, 1);
        out.aoaPercent = (raw == INT32_MIN) ? kNaN : static_cast<float>(raw);
    }
    {
        const int raw = parseFieldInt(buf_, 27, 6, "XXXXXX", INT32_MIN, 1);
        out.paltFt = (raw == INT32_MIN) ? kNaN : static_cast<float>(raw);
    }
    {
        const int raw = parseFieldInt(buf_, 45, 4, "XXXX", INT32_MIN, 10);
        out.vsiFpm = (raw == INT32_MIN) ? kNaN : static_cast<float>(raw);
    }
    out.tasKt      = parseFieldFloat(buf_, 52, 4, "XXXX",   kNaN, 10.0f);
    out.oatCelsius = parseFieldFloat(buf_, 49, 3, "XXX",    kNaN, 1.0f);
    out.source     = EfisSource::Dynon;
    parseSystemTime(buf_, out.timeOfDayHms);
    return true;
}

bool DynonSkyviewParser::DecodeEms(EfisFrame& out)
{
    // Checksum: sum bytes 0..220 mod 256, compare to hex digits at 221..222.
    int calcCRC = 0;
    for (int i = 0; i <= 220; i++)
        calcCRC += static_cast<unsigned char>(buf_[i]);
    calcCRC &= 0xFF;

    if (calcCRC != parseHexCRC(buf_, 221))
        return false;

    // EMS carries engine data only (RPM, MAP, fuel flow, PercentPower).
    // EfisFrame has no engine fields, so leave every numeric field at
    // kEfisFieldAbsent — applyFrame() will hold all prior suEfis values.
    // Source is still set so consumers can log that EMS arrived this frame.
    out.source = EfisSource::Dynon;
    return true;
}

}   // namespace onspeed::efis
