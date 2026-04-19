// DynonD10.cpp
//
// Ported verbatim from EfisSerial.cpp (EnDynonD10 branch). No algorithmic
// changes — characterisation first.

#include <efis/DynonD10.h>

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
// DynonD10Parser
// ---------------------------------------------------------------------------

void DynonD10Parser::FeedByte(uint8_t b)
{
    const char c = static_cast<char>(b);

    // Buffer overflow guard (mirrors original firmware: reset on > 230).
    if (bufLen_ > 230)
    {
        bufLen_ = 0;
        return;
    }

    // The D10 format has no leading magic byte; we collect everything until
    // '\n', then attempt decode.
    //
    // Wait: when bufLen_ is 0 we must start collecting on the FIRST byte of a
    // new line. The original firmware uses a "PrevInChar == 0x0A" gate meaning
    // it only starts collecting after seeing a '\n'. We reproduce that: we
    // track whether we are at the start of a new line vs mid-collection.
    //
    // Implementation: we begin collecting only after the first '\n' has been
    // seen (captured by lineStart_ flag). This matches the original firmware's
    // `if (iBufferLen > 0 || PrevInChar == char(0x0A))` condition.

    if (!lineStart_)
    {
        if (c == '\n')
            lineStart_ = true;
        return;
    }

    buf_[bufLen_++] = c;

    if (c == '\n')
    {
        buf_[bufLen_] = '\0';
        Decode();
        bufLen_    = 0;
        lineStart_ = true;   // ready for next line immediately
    }
}

std::optional<EfisFrame> DynonD10Parser::TakeFrame()
{
    if (!pending_)
        return std::nullopt;
    EfisFrame out = *pending_;
    pending_.reset();
    return out;
}

void DynonD10Parser::Reset()
{
    bufLen_    = 0;
    lineStart_ = false;
    pending_.reset();
}

void DynonD10Parser::Decode()
{
    if (bufLen_ != kFrameLen)
        return;

    // Checksum: sum bytes 0..48 mod 256, compare to hex at 49..50.
    int calcCRC = 0;
    for (int i = 0; i <= 48; i++)
        calcCRC += static_cast<unsigned char>(buf_[i]);
    calcCRC &= 0xFF;

    if (calcCRC != parseHexCRC(buf_, 49))
        return;

    EfisFrame out;

    out.iasKt    = parseFieldFloat(buf_, 20, 4, nullptr, 0.0f, 10.0f) * 1.94384f; // m/s to kts
    out.pitchDeg = parseFieldFloat(buf_,  8, 4, nullptr, 0.0f, 10.0f);
    out.rollDeg  = parseFieldFloat(buf_, 12, 5, nullptr, 0.0f, 10.0f);
    out.lateralG = parseFieldFloat(buf_, 33, 3, nullptr, 0.0f, 100.0f);
    out.verticalG = parseFieldFloat(buf_, 36, 3, nullptr, 0.0f, 10.0f);
    out.aoaPercent = static_cast<float>(parseFieldInt(buf_, 39, 2, nullptr, 0, 1));

    // Status byte: bit 0 = 1 → Palt and VSI are valid in this frame.
    // Original firmware used: char szStatus[2] = { szBuffer[46], '\0' };
    //                         statusBitInt = strtol(szStatus, nullptr, 16);
    //                         if (bitRead(statusBitInt, 0)) ...
    char szStatus[2] = { buf_[46], '\0' };
    long statusBitInt = strtol(szStatus, nullptr, 16);
    if (statusBitInt & 0x1)
    {
        // altitude in metres (× 3.28084 to feet), VSI feet/sec (× 60 to fpm)
        out.paltFt = parseFieldFloat(buf_, 24, 5, nullptr, 0.0f, 1.0f) * 3.28084f;
        out.vsiFpm = parseFieldFloat(buf_, 29, 4, nullptr, 0.0f, 10.0f) * 60.0f;
    }

    out.source = EfisSource::Dynon;
    pending_ = out;
}

}   // namespace onspeed::efis
