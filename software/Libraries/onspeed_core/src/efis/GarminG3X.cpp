// GarminG3X.cpp
//
// Ported verbatim from EfisSerial.cpp (EnGarminG3X branch). No algorithmic
// changes — characterisation first.
//
// See GarminG5.cpp for notes on "keep" semantics.

#include <efis/GarminG3X.h>

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
// GarminG3XParser
// ---------------------------------------------------------------------------

void GarminG3XParser::FeedByte(uint8_t b)
{
    const char c = static_cast<char>(b);

    if (bufLen_ == 0)
    {
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

std::optional<EfisFrame> GarminG3XParser::TakeFrame()
{
    if (!pending_)
        return std::nullopt;
    EfisFrame out = *pending_;
    pending_.reset();
    return out;
}

void GarminG3XParser::Reset()
{
    bufLen_ = 0;
    pending_.reset();
}

void GarminG3XParser::Decode()
{
    if (bufLen_ == kAttFrameLen &&
        buf_[0] == '=' && buf_[1] == '1' && buf_[2] == '1')
    {
        DecodeAttitude();
        return;
    }

    if (bufLen_ == kEmsFrameLen &&
        buf_[0] == '=' && buf_[1] == '3' && buf_[2] == '1')
    {
        DecodeEms();
        return;
    }
}

void GarminG3XParser::DecodeAttitude()
{
    // Checksum: sum bytes 0..54 mod 256.
    int calcCRC = 0;
    for (int i = 0; i <= 54; i++)
        calcCRC += static_cast<unsigned char>(buf_[i]);
    calcCRC &= 0xFF;

    if (calcCRC != parseHexCRC(buf_, 55))
        return;

    EfisFrame out;

    // Sentinel for G3X is '_'. Fallback = NaN marks the field absent, so
    // applyFrame() holds the previous suEfis value on sentinel match.
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
        const int raw = parseFieldInt(buf_, 43, 2, "__", INT32_MIN, 1);
        out.aoaPercent = (raw == INT32_MIN) ? kNaN : static_cast<float>(raw);
    }
    {
        const int raw = parseFieldInt(buf_, 27, 6, "______", INT32_MIN, 1);
        out.paltFt = (raw == INT32_MIN) ? kNaN : static_cast<float>(raw);
    }
    out.oatCelsius = parseFieldFloat(buf_, 49, 3, "___",    kNaN,  1.0f);
    {
        const int raw = parseFieldInt(buf_, 45, 4, "____", INT32_MIN, 10);
        out.vsiFpm = (raw == INT32_MIN) ? kNaN : static_cast<float>(raw);
    }
    out.source     = EfisSource::Garmin;

    // Time-of-day: bytes 3..10 carry "HHMMSSFF" (FF = centiseconds).
    // Validate ASCII digits and HH<=23, MM<=59, SS<=59, FF<=99 — an
    // out-of-spec or never-locked GPS frame leaves timeOfDayHms empty
    // rather than poisoning the sidecar metadata stamp.
    auto twoDigit = [](char a, char b) -> int {
        if (a < '0' || a > '9' || b < '0' || b > '9') return -1;
        return (a - '0') * 10 + (b - '0');
    };
    const int hh = twoDigit(buf_[3], buf_[4]);
    const int mm = twoDigit(buf_[5], buf_[6]);
    const int ss = twoDigit(buf_[7], buf_[8]);
    const int ff = twoDigit(buf_[9], buf_[10]);
    if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59 &&
        ss >= 0 && ss <= 59 && ff >= 0 && ff <= 99)
    {
        snprintf(out.timeOfDayHms, sizeof(out.timeOfDayHms),
                 "%02d:%02d:%02d.%02d", hh, mm, ss, ff);
    }

    pending_ = out;
}

void GarminG3XParser::DecodeEms()
{
    // Checksum: sum bytes 0..216 mod 256.
    int calcCRC = 0;
    for (int i = 0; i <= 216; i++)
        calcCRC += static_cast<unsigned char>(buf_[i]);
    calcCRC &= 0xFF;

    if (calcCRC != parseHexCRC(buf_, 217))
        return;

    // Field offsets and scales: Garmin G3X serial spec, EMS frame.
    // Sentinel "___"/"____" decodes to kEfisFieldAbsent so applyFrame()
    // holds the prior value rather than overwriting with junk. G3X EMS
    // carries no PercentPower (Dynon SkyView's wire does); leave it
    // absent here.
    EfisFrame out;
    const float kNaN = kEfisFieldAbsent;
    out.rpm              = parseFieldFloat(buf_, 18, 4, "____", kNaN, 1.0f);
    out.mapInchHg        = parseFieldFloat(buf_, 26, 3, "___",  kNaN, 10.0f);
    out.fuelFlowGph      = parseFieldFloat(buf_, 29, 3, "___",  kNaN, 10.0f);
    out.fuelRemainingGal = parseFieldFloat(buf_, 44, 3, "___",  kNaN, 10.0f);

    out.source = EfisSource::Garmin;
    pending_   = out;
}

}   // namespace onspeed::efis
