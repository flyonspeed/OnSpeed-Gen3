// GarminG5.cpp
//
// Ported verbatim from EfisSerial.cpp (EnGarminG5 branch). No algorithmic
// changes — characterisation first.
//
// Note on "keep" semantics: the original firmware used parseFieldFloatKeep /
// parseFieldIntKeep which left the destination unchanged when the sentinel
// was matched. This pure parser cannot "keep" a previous value (it has no
// accumulated state across frames). Instead, sentinel fields are populated
// with a conventional invalid value (-1.0f for floats, -1 for ints cast to
// float). Callers that need keep semantics should maintain their own last-seen
// state and ignore EfisFrame fields that hold the sentinel.

#include <efis/GarminG5.h>

#include <cstdlib>
#include <cstring>

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

    // Sentinel for G5 is '_' (underscore).
    out.iasKt      = parseFieldFloat(buf_, 23, 4, "____",   -1.0f,  10.0f);
    out.pitchDeg   = parseFieldFloat(buf_, 11, 4, "____",   -1.0f,  10.0f);
    out.rollDeg    = parseFieldFloat(buf_, 15, 5, "_____",  -1.0f,  10.0f);
    out.headingDeg = static_cast<float>(parseFieldInt(buf_, 20, 3, "___", -1, 1));
    out.lateralG   = parseFieldFloat(buf_, 37, 3, "___",    -1.0f, 100.0f);
    out.verticalG  = parseFieldFloat(buf_, 40, 3, "___",    -1.0f,  10.0f);
    out.paltFt     = static_cast<float>(parseFieldInt(buf_, 27, 6, "______", -1, 1));
    out.vsiFpm     = static_cast<float>(parseFieldInt(buf_, 45, 4, "____", -1, 10));
    // G5 does not output AOA%; aoaPercent stays at -1.0f (default "not supported").
    out.source     = EfisSource::Garmin;

    pending_ = out;
}

}   // namespace onspeed::efis
