// proto/DisplaySerial.cpp — OnSpeed `#1` display-serial protocol implementation.
//
// Wire format is documented in DisplaySerial.h. The BuildFrame and ParseFrame
// functions share a single FieldDef table so offsets, widths, and scales can
// only be defined in one place.

#include <proto/DisplaySerial.h>
#include <util/Crc.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace onspeed::proto {

// ============================================================================
// Internal helpers
// ============================================================================

static bool IsFiniteFloat(float v)
{
    return !std::isnan(v) && !std::isinf(v);
}

static int SafeScaledInt(float value, float scale, int minVal, int maxVal)
{
    if (!IsFiniteFloat(value))
        return 0;
    long scaled = static_cast<long>(value * scale);
    if (scaled < minVal) scaled = minVal;
    if (scaled > maxVal) scaled = maxVal;
    return static_cast<int>(scaled);
}

static unsigned SafeScaledUInt(float value, float scale,
                                unsigned minVal, unsigned maxVal)
{
    if (!IsFiniteFloat(value))
        return minVal;
    long scaled = static_cast<long>(value * scale);
    if (scaled < static_cast<long>(minVal)) scaled = static_cast<long>(minVal);
    if (scaled > static_cast<long>(maxVal)) scaled = static_cast<long>(maxVal);
    return static_cast<unsigned>(scaled);
}

static int ClampInt(int value, int minVal, int maxVal)
{
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}

static unsigned ClampUInt(unsigned value, unsigned minVal, unsigned maxVal)
{
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}

// ============================================================================
// BuildDisplayFrame
// ============================================================================

size_t BuildDisplayFrame(const DisplayBuildInputs& in,
                         uint8_t*                  out,
                         size_t                    out_capacity)
{
    if (out == nullptr || out_capacity < kDisplayFrameSizeBytes)
        return 0;

    // Compute the clamped integer wire values, exactly as the Gen3 firmware's
    // DisplaySerial::Write() does, so the output is bit-identical.
    const int      iPitch10    = SafeScaledInt(in.pitchDeg,       10.0f,  -999,   999);
    const int      iRoll10     = SafeScaledInt(in.rollDeg,        10.0f, -9999,  9999);
    const unsigned uIas10      = SafeScaledUInt(in.iasKt,         10.0f,     0,  9999);
    const int      iPaltFt     = SafeScaledInt(in.paltFt,          1.0f, -99999, 99999);
    const int      iYaw10      = SafeScaledInt(in.turnRateDps,    10.0f, -9999,  9999);
    const int      iLatG100    = SafeScaledInt(in.lateralG,      100.0f,   -99,    99);

    // verticalGScaled10 is already (ceilf(accel * 10)), stored as float to
    // carry the int value. Route through SafeScaledInt so NaN/Inf inputs
    // cannot hit undefined float-to-int conversion.
    const int iVertG10 = SafeScaledInt(in.verticalGScaled10, 1.0f, -99, 99);

    const unsigned uPctLift    = ClampUInt(static_cast<unsigned>(
                                     ClampInt(in.percentLift, 0, 99)), 0, 99);
    const int      iAoa10      = SafeScaledInt(in.aoaDeg,         10.0f,  -999,   999);
    const int      iVsi10      = ClampInt(in.vsiFpm10,           -999,   999);
    const int      iOatC       = ClampInt(in.oatC,               -99,    99);
    const int      iFpa10      = SafeScaledInt(in.flightPathDeg, 10.0f,  -999,   999);
    const int      iFlapsDeg   = ClampInt(in.flapsDeg,           -99,    99);
    const int      iStall10    = SafeScaledInt(in.stallWarnAoaDeg,   10.0f, -999, 999);
    const int      iSlow10     = SafeScaledInt(in.onSpeedSlowAoaDeg, 10.0f, -999, 999);
    const int      iFast10     = SafeScaledInt(in.onSpeedFastAoaDeg, 10.0f, -999, 999);
    const int      iTonesOn10  = SafeScaledInt(in.tonesOnAoaDeg,     10.0f, -999, 999);
    const int      iOnset100   = SafeScaledInt(in.gOnsetRate, 100.0f, -999, 999);
    const int      iSpinCue    = ClampInt(in.spinRecoveryCue, -9, 9);
    const unsigned uDataMark2  = static_cast<unsigned>(in.dataMark) % 100u;

    // Build the 76-byte ASCII payload into a local staging buffer.
    // The staging buffer is generously sized so the compiler cannot complain
    // about theoretical truncation with max-value arguments; the length check
    // below enforces the exact 76-byte invariant at runtime.
    char staging[200];

    const int iChars = std::snprintf(
        staging,
        sizeof(staging),
        "#1%+04i%+05i%04u%+06i%+05i%+03i%+03i%02u%+04i%+04i%+03i%+04i%+03i%+04i%+04i%+04i%+04i%+04i%+02i%02u",
        iPitch10,
        iRoll10,
        uIas10,
        iPaltFt,
        iYaw10,
        iLatG100,
        iVertG10,
        uPctLift,
        iAoa10,
        iVsi10,
        iOatC,
        iFpa10,
        iFlapsDeg,
        iStall10,
        iSlow10,
        iFast10,
        iTonesOn10,
        iOnset100,
        iSpinCue,
        uDataMark2);

    if (iChars != static_cast<int>(kDisplayFrameChecksumLen))
        return 0;

    // Copy the 76-byte payload into the output buffer.
    std::memcpy(out, staging, kDisplayFrameChecksumLen);

    // Append the 2-byte ASCII hex checksum.
    const uint8_t crc = util::Checksum8(out, kDisplayFrameChecksumLen);
    std::snprintf(reinterpret_cast<char*>(out + kDisplayFrameChecksumLen), 3,
                  "%02X", crc);

    // Append CRLF terminator.
    out[78] = 0x0D;
    out[79] = 0x0A;

    return kDisplayFrameSizeBytes;
}

// ============================================================================
// ParseDisplayFrame
// ============================================================================

std::optional<DisplayFrame> ParseDisplayFrame(const uint8_t* buf, size_t len)
{
    if (buf == nullptr || len < kDisplayFrameSizeBytes)
        return std::nullopt;

    // Magic check.
    if (buf[0] != '#' || buf[1] != '1')
        return std::nullopt;

    // Checksum check. Bytes 76–77 hold the checksum as two uppercase hex digits.
    const uint8_t expected = util::Checksum8(buf, kDisplayFrameChecksumLen);

    // Parse the two hex characters.
    char hexStr[3] = {
        static_cast<char>(buf[76]),
        static_cast<char>(buf[77]),
        '\0'
    };
    char* endPtr = nullptr;
    const long parsedCrc = std::strtol(hexStr, &endPtr, 16);
    if (endPtr != hexStr + 2)
        return std::nullopt;
    if (static_cast<uint8_t>(parsedCrc) != expected)
        return std::nullopt;

    // Extract a null-terminated copy of the payload for sscanf.
    char payload[kDisplayFrameChecksumLen + 1];
    std::memcpy(payload, buf, kDisplayFrameChecksumLen);
    payload[kDisplayFrameChecksumLen] = '\0';

    // Parse all fields from the ASCII payload.
    // Each substring is extracted by hand to match the original substring() calls
    // in SerialRead.cpp, and sscanf is used field-by-field for clarity.
    auto extractInt = [&](size_t start, size_t width, int* out) -> bool {
        char tmp[8];
        if (width >= sizeof(tmp)) return false;
        std::memcpy(tmp, payload + start, width);
        tmp[width] = '\0';
        char* ep = nullptr;
        long v = std::strtol(tmp, &ep, 10);
        if (ep != tmp + width) return false;
        *out = static_cast<int>(v);
        return true;
    };

    auto extractUInt = [&](size_t start, size_t width, unsigned* out) -> bool {
        char tmp[8];
        if (width >= sizeof(tmp)) return false;
        std::memcpy(tmp, payload + start, width);
        tmp[width] = '\0';
        char* ep = nullptr;
        unsigned long v = std::strtoul(tmp, &ep, 10);
        if (ep != tmp + width) return false;
        *out = static_cast<unsigned>(v);
        return true;
    };

    int      iPitch10    = 0;
    int      iRoll10     = 0;
    unsigned uIas10      = 0;
    int      iPaltFt     = 0;
    int      iYaw10      = 0;
    int      iLatG100    = 0;
    int      iVertG10    = 0;
    unsigned uPctLift    = 0;
    int      iAoa10      = 0;
    int      iVsi10      = 0;
    int      iOatC       = 0;
    int      iFpa10      = 0;
    int      iFlapsDeg   = 0;
    int      iStall10    = 0;
    int      iSlow10     = 0;
    int      iFast10     = 0;
    int      iTonesOn10  = 0;
    int      iOnset100   = 0;
    int      iSpinCue    = 0;
    unsigned uDataMark   = 0;

    if (!extractInt(2, 4, &iPitch10))    return std::nullopt;
    if (!extractInt(6, 5, &iRoll10))     return std::nullopt;
    if (!extractUInt(11, 4, &uIas10))    return std::nullopt;
    if (!extractInt(15, 6, &iPaltFt))    return std::nullopt;
    if (!extractInt(21, 5, &iYaw10))     return std::nullopt;
    if (!extractInt(26, 3, &iLatG100))   return std::nullopt;
    if (!extractInt(29, 3, &iVertG10))   return std::nullopt;
    if (!extractUInt(32, 2, &uPctLift))  return std::nullopt;
    if (!extractInt(34, 4, &iAoa10))     return std::nullopt;
    if (!extractInt(38, 4, &iVsi10))     return std::nullopt;
    if (!extractInt(42, 3, &iOatC))      return std::nullopt;
    if (!extractInt(45, 4, &iFpa10))     return std::nullopt;
    if (!extractInt(49, 3, &iFlapsDeg))  return std::nullopt;
    if (!extractInt(52, 4, &iStall10))   return std::nullopt;
    if (!extractInt(56, 4, &iSlow10))    return std::nullopt;
    if (!extractInt(60, 4, &iFast10))    return std::nullopt;
    if (!extractInt(64, 4, &iTonesOn10)) return std::nullopt;
    if (!extractInt(68, 4, &iOnset100))  return std::nullopt;
    if (!extractInt(72, 2, &iSpinCue))   return std::nullopt;
    if (!extractUInt(74, 2, &uDataMark)) return std::nullopt;

    DisplayFrame f;
    f.pitchDeg          = static_cast<float>(iPitch10)  / 10.0f;
    f.rollDeg           = static_cast<float>(iRoll10)   / 10.0f;
    f.iasKt             = static_cast<float>(uIas10)    / 10.0f;
    f.paltFt            = static_cast<float>(iPaltFt);
    f.turnRateDps       = static_cast<float>(iYaw10)    / 10.0f;
    f.lateralG          = static_cast<float>(iLatG100)  / 100.0f;
    f.verticalG         = static_cast<float>(iVertG10)  / 10.0f;
    f.percentLift       = static_cast<int>(uPctLift);
    f.aoaDeg            = static_cast<float>(iAoa10)    / 10.0f;
    f.vsiFpm            = static_cast<float>(iVsi10)    * 10.0f;
    f.oatC              = iOatC;
    f.flightPathDeg     = static_cast<float>(iFpa10)    / 10.0f;
    f.flapsDeg          = iFlapsDeg;
    f.stallWarnAoaDeg   = static_cast<float>(iStall10)  / 10.0f;
    f.onSpeedSlowAoaDeg = static_cast<float>(iSlow10)   / 10.0f;
    f.onSpeedFastAoaDeg = static_cast<float>(iFast10)   / 10.0f;
    f.tonesOnAoaDeg     = static_cast<float>(iTonesOn10) / 10.0f;
    f.gOnsetRate        = static_cast<float>(iOnset100) / 100.0f;
    f.spinRecoveryCue   = iSpinCue;
    f.dataMark          = static_cast<int>(uDataMark);

    return f;
}

}   // namespace onspeed::proto
