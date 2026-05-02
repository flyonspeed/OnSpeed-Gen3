// GarminG5.cpp
//
// Ported verbatim from EfisSerial.cpp (EnGarminG5 branch). No algorithmic
// changes — characterisation first.
//
// Note on "keep" semantics: the original firmware used parseFieldFloatKeep /
// parseFieldIntKeep which left the destination unchanged when the sentinel
// was matched. This pure parser has no cross-frame state, so it leaves
// sentinel fields at kEfisFieldAbsent (NaN). The caller's applyFrame()
// tests std::isfinite() and holds the prior suEfis value on NaN, which
// reproduces the vendor-specified hold-last-value semantics.

#include <efis/GarminG5.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <types/EfisFrame.h>

namespace onspeed::efis {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Returns fallback when field equals sentinel, otherwise returns parsed / scaled value.
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

static int parseHexCRC(const char* buf, int pos)
{
    char szCRC[3] = { buf[pos], buf[pos + 1], '\0' };
    return static_cast<int>(strtol(szCRC, nullptr, 16));
}

// ---------------------------------------------------------------------------
// GarminG5Parser
// ---------------------------------------------------------------------------

void GarminG5Parser::FeedByte(uint8_t b)
{
    const char c = static_cast<char>(b);

    if (bufLen_ == 0)
    {
        // Only start collecting on '='.
        if (c != '=')
            return;
    }

    if (bufLen_ > 230)
    {
        bufLen_ = 0;
        return;
    }

    buf_[bufLen_++] = c;

    if (c == '\n')
    {
        buf_[bufLen_] = '\0';
        Decode();
        bufLen_ = 0;
    }
}

std::optional<EfisFrame> GarminG5Parser::TakeFrame()
{
    if (!pending_)
        return std::nullopt;
    EfisFrame out = *pending_;
    pending_.reset();
    return out;
}

void GarminG5Parser::Reset()
{
    bufLen_ = 0;
    pending_.reset();
}

void GarminG5Parser::Decode()
{
    // Must be exactly kFrameLen bytes, starting with "=11".
    if (bufLen_ != kFrameLen)
        return;
    if (buf_[0] != '=' || buf_[1] != '1' || buf_[2] != '1')
        return;

    // Checksum: sum bytes 0..54 mod 256.
    int calcCRC = 0;
    for (int i = 0; i <= 54; i++)
        calcCRC += static_cast<unsigned char>(buf_[i]);
    calcCRC &= 0xFF;

    if (calcCRC != parseHexCRC(buf_, 55))
        return;

    EfisFrame out;

    // Sentinel for G5 is '_' (underscore). On sentinel match, parseFieldX
    // returns the fallback (NaN), which leaves the field marked absent and
    // applyFrame() will hold the previous suEfis value.
    const float kNaN = kEfisFieldAbsent;
    out.iasKt      = parseFieldFloat(buf_, 23, 4, "____",   kNaN,  10.0f);
    out.pitchDeg   = parseFieldFloat(buf_, 11, 4, "____",   kNaN,  10.0f);
    out.rollDeg    = parseFieldFloat(buf_, 15, 5, "_____",  kNaN,  10.0f);
    {
        const int raw = parseFieldInt(buf_, 20, 3, "___", INT32_MIN, 1);
        out.headingDeg = (raw == INT32_MIN) ? kNaN : static_cast<float>(raw);
    }
    out.lateralG   = parseFieldFloat(buf_, 37, 3, "___",    kNaN, 100.0f);
    out.verticalG  = parseFieldFloat(buf_, 40, 3, "___",    kNaN,  10.0f);
    {
        const int raw = parseFieldInt(buf_, 27, 6, "______", INT32_MIN, 1);
        out.paltFt = (raw == INT32_MIN) ? kNaN : static_cast<float>(raw);
    }
    {
        const int raw = parseFieldInt(buf_, 45, 4, "____", INT32_MIN, 10);
        out.vsiFpm = (raw == INT32_MIN) ? kNaN : static_cast<float>(raw);
    }
    // G5 does not output AOA%; aoaPercent stays at kEfisFieldAbsent.
    out.source     = EfisSource::Garmin;

    // Time-of-day: bytes 3..10 carry "HHMMSSFF" (FF = centiseconds).
    // Leave timeOfDayHms empty if any of those 8 bytes isn't an ASCII
    // digit so an out-of-spec frame doesn't poison the sidecar metadata.
    bool timeDigitsOk = true;
    for (int i = 3; i <= 10; ++i)
        if (buf_[i] < '0' || buf_[i] > '9') { timeDigitsOk = false; break; }
    if (timeDigitsOk)
        snprintf(out.timeOfDayHms, sizeof(out.timeOfDayHms),
                 "%c%c:%c%c:%c%c.%c%c",
                 buf_[3], buf_[4], buf_[5], buf_[6],
                 buf_[7], buf_[8], buf_[9], buf_[10]);

    pending_ = out;
}

}   // namespace onspeed::efis
