// DynonSkyview.cpp
//
// Dynon SkyView serial parser. Hot-path optimizations:
//   - FastParse fixed-decimal field reads (no strtof/strtol).
//   - First-byte sentinel detection ('X') — exact for fully-repeated
//     wire sentinels; no memcmp over the whole field.
//   - Writes pending EfisFrame in place; TryTakeFrame() consumes it
//     with a single bool check (no optional<> copy round-trip).
//   - Sets EfisField presence bits on every written field so the
//     consumer's apply path can branch on integer bitmask AND rather
//     than std::isfinite() per float.

#include <efis/DynonSkyview.h>
#include <efis/FastParse.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <types/EfisFrame.h>

namespace onspeed::efis {

using onspeed::EfisField::AoaPercent;
using onspeed::EfisField::FuelFlowGph;
using onspeed::EfisField::FuelRemainingGal;
using onspeed::EfisField::Heading;
using onspeed::EfisField::Ias;
using onspeed::EfisField::LateralG;
using onspeed::EfisField::MapInchHg;
using onspeed::EfisField::OatCelsius;
using onspeed::EfisField::Palt;
using onspeed::EfisField::PercentPower;
using onspeed::EfisField::Pitch;
using onspeed::EfisField::Roll;
using onspeed::EfisField::Rpm;
using onspeed::EfisField::Tas;
using onspeed::EfisField::VerticalG;
using onspeed::EfisField::Vsi;
using onspeed::efis::fastparse::isSentinel;
using onspeed::efis::fastparse::parseFixedSigned;
using onspeed::efis::fastparse::parseFixedUnsigned;
using onspeed::efis::fastparse::parseHex2;
using onspeed::efis::fastparse::sumChecksum;

// ---------------------------------------------------------------------------
// Dynon !1 System Time parser. Writes "HH:MM:SS" into `out` (which must
// be at least 9 bytes) or leaves it empty when GPS hasn't locked
// (sentinel '-' anywhere in HHMMSS).
// ---------------------------------------------------------------------------
static void parseSystemTime(const char* buf, char* out)
{
    out[0] = '\0';

    // Any dash in the HHMMSS portion = GPS has never locked. Treat as absent.
    for (int i = 3; i <= 8; i++)
        if (buf[i] == '-')
            return;

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
        if (bufLen_ < static_cast<int>(kBufSize) - 1)
        {
            buf_[bufLen_++] = c;
        }
        return;
    }

    // Overflow guard (matches original firmware).
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

bool DynonSkyviewParser::TryTakeFrame(EfisFrame& out)
{
    if (!pendingReady_) return false;
    out = pending_;
    pendingReady_ = false;
    return true;
}

std::optional<EfisFrame> DynonSkyviewParser::TakeFrame()
{
    if (!pendingReady_) return std::nullopt;
    EfisFrame out = pending_;
    pendingReady_ = false;
    return out;
}

void DynonSkyviewParser::Reset()
{
    bufLen_       = 0;
    pendingReady_ = false;
}

void DynonSkyviewParser::Decode()
{
    if (bufLen_ == 74 && buf_[0] == '!' && buf_[1] == '1')
    {
        EfisFrame frame;
        if (DecodeAdahrs(frame))
        {
            pending_      = frame;
            pendingReady_ = true;
        }
        return;
    }

    if (bufLen_ == 225 && buf_[0] == '!' && buf_[1] == '3')
    {
        EfisFrame frame;
        if (DecodeEms(frame))
        {
            pending_      = frame;
            pendingReady_ = true;
        }
        return;
    }

    // Unknown length or wrong magic — silently discard.
}

bool DynonSkyviewParser::DecodeAdahrs(EfisFrame& out)
{
    // Checksum: sum bytes 0..69 mod 256, compare to hex digits at 70..71.
    const uint8_t calc = sumChecksum(buf_, 0, 70);
    const int wireCrc  = parseHex2(buf_, 70);
    if (wireCrc < 0 || static_cast<int>(calc) != wireCrc)
        return false;

    // Per-field decode. Sentinel 'X' fills the entire field when the value
    // is missing on the wire; first-byte test is exact (no memcmp). On
    // sentinel match we leave the EfisFrame field at kEfisFieldAbsent (NaN)
    // and do NOT set the presence bit — matches the prior semantics.

    if (!isSentinel(buf_, 23, 'X'))
    {
        out.iasKt = static_cast<float>(parseFixedUnsigned(buf_, 23, 4)) / 10.0f;
        out.fieldsPresent |= Ias;
    }

    if (!isSentinel(buf_, 11, 'X'))
    {
        out.pitchDeg = static_cast<float>(parseFixedSigned(buf_, 11, 4)) / 10.0f;
        out.fieldsPresent |= Pitch;
    }

    if (!isSentinel(buf_, 15, 'X'))
    {
        out.rollDeg = static_cast<float>(parseFixedSigned(buf_, 15, 5)) / 10.0f;
        out.fieldsPresent |= Roll;
    }

    if (!isSentinel(buf_, 20, 'X'))
    {
        out.headingDeg = static_cast<float>(parseFixedUnsigned(buf_, 20, 3));
        out.fieldsPresent |= Heading;
    }

    if (!isSentinel(buf_, 37, 'X'))
    {
        out.lateralG = static_cast<float>(parseFixedSigned(buf_, 37, 3)) / 100.0f;
        out.fieldsPresent |= LateralG;
    }

    if (!isSentinel(buf_, 40, 'X'))
    {
        out.verticalG = static_cast<float>(parseFixedSigned(buf_, 40, 3)) / 10.0f;
        out.fieldsPresent |= VerticalG;
    }

    if (!isSentinel(buf_, 43, 'X'))
    {
        out.aoaPercent = static_cast<float>(parseFixedUnsigned(buf_, 43, 2));
        out.fieldsPresent |= AoaPercent;
    }

    if (!isSentinel(buf_, 27, 'X'))
    {
        out.paltFt = static_cast<float>(parseFixedSigned(buf_, 27, 6));
        out.fieldsPresent |= Palt;
    }

    if (!isSentinel(buf_, 45, 'X'))
    {
        // Legacy parser: signed int * 10 (the wire's hundreds-of-fpm units).
        out.vsiFpm = static_cast<float>(parseFixedSigned(buf_, 45, 4) * 10);
        out.fieldsPresent |= Vsi;
    }

    if (!isSentinel(buf_, 52, 'X'))
    {
        out.tasKt = static_cast<float>(parseFixedUnsigned(buf_, 52, 4)) / 10.0f;
        out.fieldsPresent |= Tas;
    }

    if (!isSentinel(buf_, 49, 'X'))
    {
        out.oatCelsius = static_cast<float>(parseFixedSigned(buf_, 49, 3));
        out.fieldsPresent |= OatCelsius;
    }

    out.source = EfisSource::Dynon;
    parseSystemTime(buf_, out.timeOfDayHms);
    return true;
}

bool DynonSkyviewParser::DecodeEms(EfisFrame& out)
{
    // Checksum: sum bytes 0..220 mod 256, compare to hex digits at 221..222.
    const uint8_t calc = sumChecksum(buf_, 0, 221);
    const int wireCrc  = parseHex2(buf_, 221);
    if (wireCrc < 0 || static_cast<int>(calc) != wireCrc)
        return false;

    if (!isSentinel(buf_, 18, 'X'))
    {
        out.rpm = static_cast<float>(parseFixedUnsigned(buf_, 18, 4));
        out.fieldsPresent |= Rpm;
    }

    if (!isSentinel(buf_, 26, 'X'))
    {
        out.mapInchHg = static_cast<float>(parseFixedUnsigned(buf_, 26, 3)) / 10.0f;
        out.fieldsPresent |= MapInchHg;
    }

    if (!isSentinel(buf_, 29, 'X'))
    {
        out.fuelFlowGph = static_cast<float>(parseFixedUnsigned(buf_, 29, 3)) / 10.0f;
        out.fieldsPresent |= FuelFlowGph;
    }

    if (!isSentinel(buf_, 44, 'X'))
    {
        out.fuelRemainingGal = static_cast<float>(parseFixedUnsigned(buf_, 44, 3)) / 10.0f;
        out.fieldsPresent |= FuelRemainingGal;
    }

    if (!isSentinel(buf_, 217, 'X'))
    {
        out.percentPower = static_cast<float>(parseFixedUnsigned(buf_, 217, 3));
        out.fieldsPresent |= PercentPower;
    }

    out.source = EfisSource::Dynon;
    return true;
}

}   // namespace onspeed::efis
