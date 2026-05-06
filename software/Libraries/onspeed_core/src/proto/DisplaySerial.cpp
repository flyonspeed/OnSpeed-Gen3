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

    // iasKt: when the producer marks air-data invalid, write the
    // sentinel kIasInvalidWireSentinel (9999) instead of the live
    // value.  A live SafeScaledUInt would clamp a near-stall taxi to 0
    // and a hangar wind gust to a small but nonzero number — neither
    // distinguishable from "data not valid" — so the encoder controls
    // the bit pattern explicitly here.  See iasValid contract in
    // DisplaySerial.h.
    const unsigned uIas10      = in.iasValid
        ? SafeScaledUInt(in.iasKt, 10.0f, 0, 9999)
        : static_cast<unsigned>(kIasInvalidWireSentinel);
    const int      iPaltFt     = SafeScaledInt(in.paltFt,          1.0f, -99999, 99999);
    const int      iYaw10      = SafeScaledInt(in.turnRateDps,    10.0f, -9999,  9999);
    const int      iLatG100    = SafeScaledInt(in.lateralG,      100.0f,   -99,    99);

    // verticalGScaled10 is already (lroundf(accel * 10)), stored as float to
    // carry the int value. Route through SafeScaledInt so NaN/Inf inputs
    // cannot hit undefined float-to-int conversion.
    const int iVertG10 = SafeScaledInt(in.verticalGScaled10, 1.0f, -99, 99);

    // percentLift on the wire carries tenths of a percent (0..999) so the
    // M5's index bar can advance at sub-pixel temporal smoothness off the
    // 20 Hz frame cadence.  Producers fill `percentLiftPct` as a whole-
    // percent float (e.g. 47.3); we encode `int(pct × 10)` here via
    // SafeScaledInt's truncation toward zero.  Wire output agrees with
    // v4.23 master to within ±1 in the wire-tenths field at IEEE 754
    // precision boundaries (the new pipeline uses one extra `*10`
    // float multiply that wasn't there before; ~282 of ~10⁹ representable
    // float fractions differ by 1 ULP at the truncation step — see
    // test_byte_equivalence_with_v423 for the exact characterization).
    // 0.1% on the wire is invisible to pilot and audio path.  Clamp
    // at 999 — the formula's saturation convention never emits 1000
    // (PercentLift.cpp clamps at 99.9).
    const int      iPctLiftRaw = SafeScaledInt(in.percentLiftPct, 10.0f, 0, 999);
    const unsigned uPctLift    = ClampUInt(static_cast<unsigned>(iPctLiftRaw), 0, 999);
    const int      iVsi10      = ClampInt(in.vsiFpm10,           -999,   999);
    const int      iOatC       = ClampInt(in.oatC,               -99,    99);
    const int      iFpa10      = SafeScaledInt(in.flightPathDeg, 10.0f,  -999,   999);
    const int      iFlapsDeg   = ClampInt(in.flapsDeg,           -99,    99);
    const unsigned uTonesOnPct = ClampUInt(static_cast<unsigned>(
                                     ClampInt(in.tonesOnPctLift, 0, 99)), 0, 99);
    const unsigned uFastPct    = ClampUInt(static_cast<unsigned>(
                                     ClampInt(in.onSpeedFastPctLift, 0, 99)), 0, 99);
    const unsigned uSlowPct    = ClampUInt(static_cast<unsigned>(
                                     ClampInt(in.onSpeedSlowPctLift, 0, 99)), 0, 99);
    const unsigned uWarnPct    = ClampUInt(static_cast<unsigned>(
                                     ClampInt(in.stallWarnPctLift, 0, 99)), 0, 99);
    const int      iFlapsMin   = ClampInt(in.flapsMinDeg,        -99,    99);
    const int      iFlapsMax   = ClampInt(in.flapsMaxDeg,        -99,    99);
    const int      iOnset100   = SafeScaledInt(in.gOnsetRate, 100.0f, -999, 999);
    const int      iSpinCue    = ClampInt(in.spinRecoveryCue, -9, 9);
    const unsigned uDataMark2  = static_cast<unsigned>(in.dataMark) % 100u;
    const unsigned uPipPct     = ClampUInt(static_cast<unsigned>(
                                     ClampInt(in.pipPctLift, 0, 99)), 0, 99);

    // Build the kDisplayFrameChecksumLen-byte ASCII payload into a local
    // staging buffer.  The staging buffer is generously sized so the
    // compiler cannot complain about theoretical truncation with
    // max-value arguments; the length check below enforces the exact
    // kDisplayFrameChecksumLen-byte invariant at runtime.
    char staging[200];

    const int iChars = std::snprintf(
        staging,
        sizeof(staging),
        "#1%+04i%+05i%04u%+06i%+05i%+03i%+03i%03u%+04i%+03i%+04i%+03i%02u%02u%02u%02u%+03i%+03i%+04i%+02i%02u%02u",
        iPitch10,
        iRoll10,
        uIas10,
        iPaltFt,
        iYaw10,
        iLatG100,
        iVertG10,
        uPctLift,
        iVsi10,
        iOatC,
        iFpa10,
        iFlapsDeg,
        uTonesOnPct,
        uFastPct,
        uSlowPct,
        uWarnPct,
        iFlapsMin,
        iFlapsMax,
        iOnset100,
        iSpinCue,
        uDataMark2,
        uPipPct);

    if (iChars != static_cast<int>(kDisplayFrameChecksumLen))
        return 0;

    // Copy the kDisplayFrameChecksumLen-byte payload into the output buffer.
    std::memcpy(out, staging, kDisplayFrameChecksumLen);

    // Append the 2-byte ASCII hex checksum.
    const uint8_t crc = util::Checksum8(out, kDisplayFrameChecksumLen);
    std::snprintf(reinterpret_cast<char*>(out + kDisplayFrameChecksumLen), 3,
                  "%02X", crc);

    // Append CRLF terminator. Offsets derived from kDisplayFrameSizeBytes
    // so any future field additions only require updating the size
    // constants and the snprintf format string.
    out[kDisplayFrameSizeBytes - 2] = 0x0D;
    out[kDisplayFrameSizeBytes - 1] = 0x0A;

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

    // Checksum check. Two uppercase hex digits live at the end of the
    // payload, immediately before the CRLF terminator.
    const uint8_t expected = util::Checksum8(buf, kDisplayFrameChecksumLen);

    // Parse the two hex characters.
    char hexStr[3] = {
        static_cast<char>(buf[kDisplayFrameChecksumLen]),
        static_cast<char>(buf[kDisplayFrameChecksumLen + 1]),
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

    int      iPitch10      = 0;
    int      iRoll10       = 0;
    unsigned uIas10        = 0;
    int      iPaltFt       = 0;
    int      iYaw10        = 0;
    int      iLatG100      = 0;
    int      iVertG10      = 0;
    unsigned uPctLift      = 0;
    int      iVsi10        = 0;
    int      iOatC         = 0;
    int      iFpa10        = 0;
    int      iFlapsDeg     = 0;
    unsigned uTonesOnPct   = 0;
    unsigned uFastPct      = 0;
    unsigned uSlowPct      = 0;
    unsigned uWarnPct      = 0;
    int      iFlapsMin     = 0;
    int      iFlapsMax     = 0;
    int      iOnset100     = 0;
    int      iSpinCue      = 0;
    unsigned uDataMark     = 0;
    unsigned uPipPct       = 0;

    if (!extractInt( 2, 4, &iPitch10))      return std::nullopt;
    if (!extractInt( 6, 5, &iRoll10))       return std::nullopt;
    if (!extractUInt(11, 4, &uIas10))       return std::nullopt;
    if (!extractInt(15, 6, &iPaltFt))       return std::nullopt;
    if (!extractInt(21, 5, &iYaw10))        return std::nullopt;
    if (!extractInt(26, 3, &iLatG100))      return std::nullopt;
    if (!extractInt(29, 3, &iVertG10))      return std::nullopt;
    // percentLift widened to %03u (tenths of a percent, 0..999) at v4.23;
    // every subsequent offset shifts +1.  We divide by 10 to surface the
    // value as whole-percent float (e.g. 47.3) for consumers.
    if (!extractUInt(32, 3, &uPctLift))     return std::nullopt;
    if (!extractInt(35, 4, &iVsi10))        return std::nullopt;
    if (!extractInt(39, 3, &iOatC))         return std::nullopt;
    if (!extractInt(42, 4, &iFpa10))        return std::nullopt;
    if (!extractInt(46, 3, &iFlapsDeg))     return std::nullopt;
    if (!extractUInt(49, 2, &uTonesOnPct))  return std::nullopt;
    if (!extractUInt(51, 2, &uFastPct))     return std::nullopt;
    if (!extractUInt(53, 2, &uSlowPct))     return std::nullopt;
    if (!extractUInt(55, 2, &uWarnPct))     return std::nullopt;
    if (!extractInt(57, 3, &iFlapsMin))     return std::nullopt;
    if (!extractInt(60, 3, &iFlapsMax))     return std::nullopt;
    if (!extractInt(63, 4, &iOnset100))     return std::nullopt;
    if (!extractInt(67, 2, &iSpinCue))      return std::nullopt;
    if (!extractUInt(69, 2, &uDataMark))    return std::nullopt;
    if (!extractUInt(71, 2, &uPipPct))      return std::nullopt;

    DisplayFrame f;
    f.pitchDeg           = static_cast<float>(iPitch10)  / 10.0f;
    f.rollDeg            = static_cast<float>(iRoll10)   / 10.0f;
    // iasKt: detect the kIasInvalidWireSentinel and surface it as
    // iasIsValid=false rather than as a 999.9 kt reading.  The raw
    // iasKt field is left set to whatever the wire carried so a
    // diagnostic consumer can still see it; the contract is
    // "consumers must check iasIsValid before trusting iasKt".
    f.iasKt              = static_cast<float>(uIas10)    / 10.0f;
    f.iasIsValid         = (uIas10 != kIasInvalidWireSentinel);
    f.paltFt             = static_cast<float>(iPaltFt);
    f.turnRateDps        = static_cast<float>(iYaw10)    / 10.0f;
    f.lateralG           = static_cast<float>(iLatG100)  / 100.0f;
    f.verticalG          = static_cast<float>(iVertG10)  / 10.0f;
    f.percentLiftPct     = static_cast<float>(uPctLift) / 10.0f;
    f.vsiFpm             = static_cast<float>(iVsi10)    * 10.0f;
    f.oatC               = iOatC;
    f.flightPathDeg      = static_cast<float>(iFpa10)    / 10.0f;
    f.flapsDeg           = iFlapsDeg;
    f.tonesOnPctLift     = static_cast<int>(uTonesOnPct);
    f.onSpeedFastPctLift = static_cast<int>(uFastPct);
    f.onSpeedSlowPctLift = static_cast<int>(uSlowPct);
    f.stallWarnPctLift   = static_cast<int>(uWarnPct);
    f.flapsMinDeg        = iFlapsMin;
    f.flapsMaxDeg        = iFlapsMax;
    f.gOnsetRate         = static_cast<float>(iOnset100)     / 100.0f;
    f.spinRecoveryCue    = iSpinCue;
    f.dataMark           = static_cast<int>(uDataMark);
    f.pipPctLift         = static_cast<int>(uPipPct);

    return f;
}

// ============================================================================
// DisplayFrameAccumulator
// ============================================================================

DisplayFrameAccumulator::DisplayFrameAccumulator()
    : length_(0)
{
}

void DisplayFrameAccumulator::Reset()
{
    length_ = 0;
}

std::optional<DisplayFrame> DisplayFrameAccumulator::Inject(uint8_t byte)
{
    // Any '#' byte resets to start-of-frame. This catches the case
    // where a partial frame was abandoned mid-stream and the next
    // good frame starts arriving.
    if (byte == '#') {
        buffer_[0] = byte;
        length_    = 1;
        return std::nullopt;
    }

    // Idle until a '#' has been seen.
    if (length_ == 0) {
        return std::nullopt;
    }

    // length_ is always in [1..kDisplayFrameSizeBytes-1] here: line above
    // resets it to 1 on '#', and every code path below either continues
    // building (length_ < kDisplayFrameSizeBytes) or runs the validation
    // and resets to 0.  No buffer overflow is possible.
    buffer_[length_++] = byte;

    // Not yet a complete frame.
    if (length_ < kDisplayFrameSizeBytes) {
        return std::nullopt;
    }

    // Frame is full. Drop early if the final byte isn't the LF
    // terminator (frame got out of sync mid-stream). The "#1" magic
    // check is left to ParseDisplayFrame; the '#' reset above guarantees
    // buffer_[0] == '#' for every accumulator frame, so re-checking
    // it here would be untestable defensive code.
    if (byte != 0x0A) {
        length_ = 0;
        return std::nullopt;
    }

    auto result = ParseDisplayFrame(buffer_, kDisplayFrameSizeBytes);
    length_ = 0;
    return result;
}

}   // namespace onspeed::proto
