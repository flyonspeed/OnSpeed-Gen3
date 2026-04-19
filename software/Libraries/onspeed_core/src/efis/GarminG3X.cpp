// GarminG3X.cpp
//
// Ported verbatim from EfisSerial.cpp (EnGarminG3X branch). No algorithmic
// changes — characterisation first.
//
// See GarminG5.cpp for notes on "keep" semantics.

#include <efis/GarminG3X.h>

#include <cstdlib>
#include <cstring>

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

    out.iasKt      = parseFieldFloat(buf_, 23, 4, "____",   -1.0f,  10.0f);
    out.pitchDeg   = parseFieldFloat(buf_, 11, 4, "____",   -1.0f,  10.0f);
    out.rollDeg    = parseFieldFloat(buf_, 15, 5, "_____",  -1.0f,  10.0f);
    out.headingDeg = static_cast<float>(parseFieldInt(buf_, 20, 3, "___", -1, 1));
    out.lateralG   = parseFieldFloat(buf_, 37, 3, "___",    -1.0f, 100.0f);
    out.verticalG  = parseFieldFloat(buf_, 40, 3, "___",    -1.0f,  10.0f);
    out.aoaPercent = static_cast<float>(parseFieldInt(buf_, 43, 2, "__", -1, 1));
    out.paltFt     = static_cast<float>(parseFieldInt(buf_, 27, 6, "______", -1, 1));
    out.oatCelsius = parseFieldFloat(buf_, 49, 3, "___",    -1.0f,  1.0f);
    out.vsiFpm     = static_cast<float>(parseFieldInt(buf_, 45, 4, "____", -1, 10));
    out.source     = EfisSource::Garmin;

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

    // EMS frame carries engine data only. EfisFrame does not have fields for
    // RPM/MAP/FuelFlow/FuelRemaining; return a frame with source set to signal
    // a valid EMS frame arrived, remaining fields at defaults.
    EfisFrame out;
    out.source = EfisSource::Garmin;
    pending_   = out;
}

}   // namespace onspeed::efis
