// GarminG3X.cpp
//
// Garmin G3X serial parser. Hot-path optimizations: FastParse fixed-decimal
// reads, first-byte sentinel ('_') detection, copy-free pending-frame,
// presence bitmask. See GarminG5.cpp for notes on "keep" semantics.

#include <efis/GarminG3X.h>
#include <efis/FastParse.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

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
using onspeed::EfisField::Pitch;
using onspeed::EfisField::Roll;
using onspeed::EfisField::Rpm;
using onspeed::EfisField::VerticalG;
using onspeed::EfisField::Vsi;
using onspeed::efis::fastparse::isSentinel;
using onspeed::efis::fastparse::parseFixedSigned;
using onspeed::efis::fastparse::parseFixedUnsigned;
using onspeed::efis::fastparse::parseHex2;
using onspeed::efis::fastparse::sumChecksum;

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

bool GarminG3XParser::TryTakeFrame(EfisFrame& out)
{
    if (!pendingReady_) return false;
    out = pending_;
    pendingReady_ = false;
    return true;
}

std::optional<EfisFrame> GarminG3XParser::TakeFrame()
{
    if (!pendingReady_) return std::nullopt;
    EfisFrame out = pending_;
    pendingReady_ = false;
    return out;
}

void GarminG3XParser::Reset()
{
    bufLen_       = 0;
    pendingReady_ = false;
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
    const uint8_t calc = sumChecksum(buf_, 0, 55);
    const int wireCrc  = parseHex2(buf_, 55);
    if (wireCrc < 0 || static_cast<int>(calc) != wireCrc)
        return;

    EfisFrame out;

    if (!isSentinel(buf_, 23, '_'))
    {
        out.iasKt = static_cast<float>(parseFixedUnsigned(buf_, 23, 4)) / 10.0f;
        out.fieldsPresent |= Ias;
    }

    if (!isSentinel(buf_, 11, '_'))
    {
        out.pitchDeg = static_cast<float>(parseFixedSigned(buf_, 11, 4)) / 10.0f;
        out.fieldsPresent |= Pitch;
    }

    if (!isSentinel(buf_, 15, '_'))
    {
        out.rollDeg = static_cast<float>(parseFixedSigned(buf_, 15, 5)) / 10.0f;
        out.fieldsPresent |= Roll;
    }

    if (!isSentinel(buf_, 20, '_'))
    {
        out.headingDeg = static_cast<float>(parseFixedUnsigned(buf_, 20, 3));
        out.fieldsPresent |= Heading;
    }

    if (!isSentinel(buf_, 37, '_'))
    {
        out.lateralG = static_cast<float>(parseFixedSigned(buf_, 37, 3)) / 100.0f;
        out.fieldsPresent |= LateralG;
    }

    if (!isSentinel(buf_, 40, '_'))
    {
        out.verticalG = static_cast<float>(parseFixedSigned(buf_, 40, 3)) / 10.0f;
        out.fieldsPresent |= VerticalG;
    }

    if (!isSentinel(buf_, 43, '_'))
    {
        out.aoaPercent = static_cast<float>(parseFixedUnsigned(buf_, 43, 2));
        out.fieldsPresent |= AoaPercent;
    }

    if (!isSentinel(buf_, 27, '_'))
    {
        out.paltFt = static_cast<float>(parseFixedSigned(buf_, 27, 6));
        out.fieldsPresent |= Palt;
    }

    if (!isSentinel(buf_, 49, '_'))
    {
        out.oatCelsius = static_cast<float>(parseFixedSigned(buf_, 49, 3));
        out.fieldsPresent |= OatCelsius;
    }

    if (!isSentinel(buf_, 45, '_'))
    {
        out.vsiFpm = static_cast<float>(parseFixedSigned(buf_, 45, 4) * 10);
        out.fieldsPresent |= Vsi;
    }

    out.source = EfisSource::Garmin;

    // Time-of-day: bytes 3..10 carry "HHMMSSFF" (FF = centiseconds).
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

    pending_      = out;
    pendingReady_ = true;
}

void GarminG3XParser::DecodeEms()
{
    // Checksum: sum bytes 0..216 mod 256.
    const uint8_t calc = sumChecksum(buf_, 0, 217);
    const int wireCrc  = parseHex2(buf_, 217);
    if (wireCrc < 0 || static_cast<int>(calc) != wireCrc)
        return;

    // G3X EMS carries no PercentPower (Dynon SkyView's wire does); leave
    // it absent here.
    EfisFrame out;

    if (!isSentinel(buf_, 18, '_'))
    {
        out.rpm = static_cast<float>(parseFixedUnsigned(buf_, 18, 4));
        out.fieldsPresent |= Rpm;
    }

    if (!isSentinel(buf_, 26, '_'))
    {
        out.mapInchHg = static_cast<float>(parseFixedUnsigned(buf_, 26, 3)) / 10.0f;
        out.fieldsPresent |= MapInchHg;
    }

    if (!isSentinel(buf_, 29, '_'))
    {
        out.fuelFlowGph = static_cast<float>(parseFixedUnsigned(buf_, 29, 3)) / 10.0f;
        out.fieldsPresent |= FuelFlowGph;
    }

    if (!isSentinel(buf_, 44, '_'))
    {
        out.fuelRemainingGal = static_cast<float>(parseFixedUnsigned(buf_, 44, 3)) / 10.0f;
        out.fieldsPresent |= FuelRemainingGal;
    }

    out.source = EfisSource::Garmin;
    pending_      = out;
    pendingReady_ = true;
}

}   // namespace onspeed::efis
