// DynonSkyview.cpp
//
// Ported verbatim from EfisSerial.cpp (EnDynonSkyview branch). No algorithmic
// changes — characterisation first, correctness improvements belong in a
// follow-on PR once tests pin current behaviour.

#include <efis/DynonSkyview.h>

#include <cstdlib>
#include <cstring>

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

    out.iasKt      = parseFieldFloat(buf_, 23, 4, "XXXX",   -1.0f,   10.0f);
    out.pitchDeg   = parseFieldFloat(buf_, 11, 4, "XXXX",   -100.0f, 10.0f);
    out.rollDeg    = parseFieldFloat(buf_, 15, 5, "XXXXX",  -180.0f, 10.0f);
    out.headingDeg = static_cast<float>(parseFieldInt(buf_, 20, 3, "XXX", -1, 1));
    out.lateralG   = parseFieldFloat(buf_, 37, 3, "XXX",    -100.0f, 100.0f);
    out.verticalG  = parseFieldFloat(buf_, 40, 3, "XXX",    -100.0f, 10.0f);
    out.aoaPercent = static_cast<float>(parseFieldInt(buf_, 43, 2, "XX", -1, 1));
    out.paltFt     = static_cast<float>(parseFieldInt(buf_, 27, 6, "XXXXXX", -10000, 1));
    out.vsiFpm     = static_cast<float>(parseFieldInt(buf_, 45, 4, "XXXX", -10000, 10));
    out.tasKt      = parseFieldFloat(buf_, 52, 4, "XXXX",   -1.0f,   10.0f);
    out.oatCelsius = parseFieldFloat(buf_, 49, 3, "XXX",    -100.0f, 1.0f);
    out.source     = EfisSource::Dynon;
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

    // EMS frame carries engine data only — no attitude fields.
    // Consumers merge frames; here we carry only what the original firmware
    // extracted from the !3 frame. The remaining EfisFrame fields are
    // left at their defaults.
    //
    // NOTE: EfisFrame does not have RPM/MAP/FuelFlow/FuelRemaining/PercentPower
    // fields (those were in SuEfisData, not part of the normalised type).
    // The original firmware stored them in suEfis; for now this parser
    // returns an EfisFrame to signal a valid EMS frame was received.
    // The fields that *do* exist on EfisFrame (none carry EMS data) are
    // left at defaults.
    out.source = EfisSource::Dynon;
    return true;
}

}   // namespace onspeed::efis
