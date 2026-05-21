// DynonD10.cpp
//
// Dynon D10 serial parser. Same hot-path optimizations as DynonSkyview:
// FastParse fixed-decimal field reads, copy-free pending-frame, presence
// bitmask.

#include <efis/DynonD10.h>
#include <efis/FastParse.h>
#include <util/OnSpeedTypes.h>

#include <cstdio>
#include <cstring>

namespace onspeed::efis {

using onspeed::EfisField::AoaPercent;
using onspeed::EfisField::Ias;
using onspeed::EfisField::LateralG;
using onspeed::EfisField::Palt;
using onspeed::EfisField::Pitch;
using onspeed::EfisField::Roll;
using onspeed::EfisField::VerticalG;
using onspeed::EfisField::Vsi;
using onspeed::efis::fastparse::parseFixedSigned;
using onspeed::efis::fastparse::parseFixedUnsigned;
using onspeed::efis::fastparse::parseHex1;
using onspeed::efis::fastparse::parseHex2;
using onspeed::efis::fastparse::sumChecksum;

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

    // D10 has no leading magic byte. We start collecting only after the
    // FIRST '\n' has been seen (matches original `iBufferLen > 0 ||
    // PrevInChar == 0x0A` gating).
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
        lineStart_ = true;
    }
}

bool DynonD10Parser::TryTakeFrame(EfisFrame& out)
{
    if (!pendingReady_) return false;
    out = pending_;
    pendingReady_ = false;
    return true;
}

std::optional<EfisFrame> DynonD10Parser::TakeFrame()
{
    if (!pendingReady_) return std::nullopt;
    EfisFrame out = pending_;
    pendingReady_ = false;
    return out;
}

void DynonD10Parser::Reset()
{
    bufLen_       = 0;
    lineStart_    = false;
    pendingReady_ = false;
}

void DynonD10Parser::Decode()
{
    if (bufLen_ != kFrameLen)
        return;

    // Checksum: sum bytes 0..48 mod 256, compare to hex at 49..50.
    const uint8_t calc = sumChecksum(buf_, 0, 49);
    const int wireCrc  = parseHex2(buf_, 49);
    if (wireCrc < 0 || static_cast<int>(calc) != wireCrc)
        return;

    EfisFrame out;

    // IAS m/s → knots, scale 10. Signed parse: matches strtof which would
    // accept a leading '-' if the wire ever produced one.
    out.iasKt    = onspeed::mps2kts(
                       static_cast<float>(parseFixedSigned(buf_, 20, 4)) / 10.0f);
    out.fieldsPresent |= Ias;

    out.pitchDeg = static_cast<float>(parseFixedSigned(buf_, 8, 4)) / 10.0f;
    out.fieldsPresent |= Pitch;

    out.rollDeg  = static_cast<float>(parseFixedSigned(buf_, 12, 5)) / 10.0f;
    out.fieldsPresent |= Roll;

    out.lateralG = static_cast<float>(parseFixedSigned(buf_, 33, 3)) / 100.0f;
    out.fieldsPresent |= LateralG;

    out.verticalG = static_cast<float>(parseFixedSigned(buf_, 36, 3)) / 10.0f;
    out.fieldsPresent |= VerticalG;

    out.aoaPercent = static_cast<float>(parseFixedUnsigned(buf_, 39, 2));
    out.fieldsPresent |= AoaPercent;

    // Status nibble at position 46 (one hex char). Bit 0 = 1 → Palt/VSI valid.
    const int statusNibble = parseHex1(buf_, 46);
    if (statusNibble >= 0 && (statusNibble & 0x1))
    {
        out.paltFt = onspeed::m2ft(
                         static_cast<float>(parseFixedSigned(buf_, 24, 5)));
        out.fieldsPresent |= Palt;

        // VSI: parsed as feet/sec (after /10 scale), then × 60 → fpm.
        out.vsiFpm = static_cast<float>(parseFixedSigned(buf_, 29, 4)) / 10.0f * 60.0f;
        out.fieldsPresent |= Vsi;
    }

    // Time-of-day: bytes 0..7 carry "HHMMSSFF" (FF = centiseconds).
    auto twoDigit = [](char a, char b) -> int {
        if (a < '0' || a > '9' || b < '0' || b > '9') return -1;
        return (a - '0') * 10 + (b - '0');
    };
    const int hh = twoDigit(buf_[0], buf_[1]);
    const int mm = twoDigit(buf_[2], buf_[3]);
    const int ss = twoDigit(buf_[4], buf_[5]);
    const int ff = twoDigit(buf_[6], buf_[7]);
    if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59 &&
        ss >= 0 && ss <= 59 && ff >= 0 && ff <= 99)
    {
        snprintf(out.timeOfDayHms, sizeof(out.timeOfDayHms),
                 "%02d:%02d:%02d.%02d", hh, mm, ss, ff);
    }

    out.source = EfisSource::Dynon;
    pending_      = out;
    pendingReady_ = true;
}

}   // namespace onspeed::efis
