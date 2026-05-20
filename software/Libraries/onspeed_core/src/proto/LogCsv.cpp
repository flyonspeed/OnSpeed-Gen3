// proto/LogCsv.cpp — CSV row formatting and parsing for the OnSpeed SD log
//
// Bump kFormatVersion here whenever the column list, order, units, or
// precision changes.  See proto/LogCsv.h for the full column layout.

#include <proto/LogCsv.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <types/LogRow.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// =========================================================================
// Fast formatters — replace the snprintf hot path for the CSV row writer.
//
// The CSV row builder is called 208 times per second on the IMU task.
// Each row has ~30 floats and a dozen ints. Routing every cell through
// vsnprintf is the bulk of the LogCsv CPU cost (~25 µs/call × 30 cells =
// ~750 µs per row × 208 Hz = 156 ms/sec).
//
// These hand-rolled formatters produce byte-identical output to the
// printf path for the formats actually used: %i, %u, %lu, %llu, %.1f,
// %.2f, %.4f, %.6f. Other paths still go through Appendf.
//
// All assume the buffer has room (cap check at top); on overflow they
// truncate and null-terminate, matching Appendf's failure mode.
// =========================================================================

// Write a single character. Returns true on success.
static inline bool AppendChar(char* buf, size_t cap, size_t* pLen, char c)
{
    if (*pLen + 1 >= cap) { buf[cap - 1] = '\0'; return false; }
    buf[(*pLen)++] = c;
    return true;
}

// Write an unsigned 64-bit integer using a 20-char temp buffer.
// Worst case 20 digits + sign = 21 chars.
static inline bool AppendUInt64(char* buf, size_t cap, size_t* pLen, uint64_t v)
{
    char tmp[21];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0) {
            tmp[n++] = static_cast<char>('0' + (v % 10));
            v /= 10;
        }
    }
    if (*pLen + (size_t)n >= cap) { buf[cap - 1] = '\0'; return false; }
    while (n > 0) buf[(*pLen)++] = tmp[--n];
    return true;
}

static inline bool AppendInt32(char* buf, size_t cap, size_t* pLen, int32_t v)
{
    if (v < 0) {
        if (!AppendChar(buf, cap, pLen, '-')) return false;
        return AppendUInt64(buf, cap, pLen, (uint64_t)(-(int64_t)v));
    }
    return AppendUInt64(buf, cap, pLen, (uint64_t)v);
}

static inline bool AppendUInt32(char* buf, size_t cap, size_t* pLen, uint32_t v)
{
    return AppendUInt64(buf, cap, pLen, (uint64_t)v);
}

// Format a float with N decimal places. Uses round-half-away-from-zero
// to match printf's default rounding mode.
//
// Strategy: multiply by 10^N, round to nearest integer, then split into
// integer and fractional parts and emit. Handles negatives, NaN, and
// infinities the same way printf does ("nan", "inf").
static inline bool AppendFloatFixed(char* buf, size_t cap, size_t* pLen,
                                    float val, int decimals)
{
    if (std::isnan(val)) {
        const char* s = "nan";
        while (*s) { if (!AppendChar(buf, cap, pLen, *s++)) return false; }
        return true;
    }
    if (std::isinf(val)) {
        if (val < 0.0f && !AppendChar(buf, cap, pLen, '-')) return false;
        const char* s = "inf";
        while (*s) { if (!AppendChar(buf, cap, pLen, *s++)) return false; }
        return true;
    }

    bool negative = val < 0.0f;
    if (negative) val = -val;

    // Lookup-table for 10^decimals. Covers the decimals we use (1..6).
    static const uint32_t kPow10[] = {1, 10, 100, 1000, 10000, 100000, 1000000};
    const uint32_t scale = (decimals >= 0 && decimals <= 6)
                               ? kPow10[decimals]
                               : 100;

    // Round-half-away-from-zero by adding 0.5 before truncating.
    // Use double precision for the intermediate to preserve precision
    // for large values; final cast loses the fraction.
    const double scaled = static_cast<double>(val) * (double)scale + 0.5;

    // Guard against overflow into uint64 territory (~1.8e19). A float can
    // hold ~3.4e38 but past ~9.2e18*scale we'd overflow uint64. Fall back
    // to snprintf for those — they don't appear in our log columns.
    if (scaled >= 9.2e18) {
        char fmt[8];
        std::snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
        char tmp[32];
        int n = std::snprintf(tmp, sizeof(tmp), fmt, negative ? -val : val);
        if (n < 0 || *pLen + (size_t)n >= cap) {
            buf[cap - 1] = '\0';
            return false;
        }
        std::memcpy(buf + *pLen, tmp, (size_t)n);
        *pLen += (size_t)n;
        return true;
    }

    const uint64_t intRounded = (uint64_t)scaled;
    const uint64_t intPart    = intRounded / scale;
    const uint32_t fracPart   = (uint32_t)(intRounded - intPart * scale);

    if (negative) {
        if (!AppendChar(buf, cap, pLen, '-')) return false;
    }
    if (!AppendUInt64(buf, cap, pLen, intPart)) return false;
    if (decimals <= 0) return true;
    if (!AppendChar(buf, cap, pLen, '.')) return false;
    // Zero-pad the fractional part to `decimals` width.
    char fbuf[8];
    int fn = 0;
    uint32_t f = fracPart;
    for (int i = 0; i < decimals; ++i) {
        fbuf[fn++] = static_cast<char>('0' + (f % 10));
        f /= 10;
    }
    if (*pLen + (size_t)fn >= cap) { buf[cap - 1] = '\0'; return false; }
    while (fn > 0) buf[(*pLen)++] = fbuf[--fn];
    return true;
}

// Convenience emit-with-leading-comma variants — the format style FormatRow
// uses pervasively (",%.2f", ",%i", ...).
static inline bool CommaFloat(char* b, size_t c, size_t* l, float v, int d) {
    return AppendChar(b, c, l, ',') && AppendFloatFixed(b, c, l, v, d);
}
static inline bool CommaInt32(char* b, size_t c, size_t* l, int32_t v) {
    return AppendChar(b, c, l, ',') && AppendInt32(b, c, l, v);
}
static inline bool CommaUInt32(char* b, size_t c, size_t* l, uint32_t v) {
    return AppendChar(b, c, l, ',') && AppendUInt32(b, c, l, v);
}

// Appends a printf-formatted string into buf[0..cap), tracking the running
// length in *pLen.  Returns true on success, false on truncation.
static bool Appendf(char* buf, size_t cap, size_t* pLen, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

static bool Appendf(char* buf, size_t cap, size_t* pLen, const char* fmt, ...)
{
    if (buf == nullptr || cap == 0 || pLen == nullptr)
        return false;

    size_t used = *pLen;
    if (used >= cap)
        return false;

    va_list args;
    va_start(args, fmt);
    int added = vsnprintf(buf + used, cap - used, fmt, args);
    va_end(args);

    if (added < 0)
        return false;

    if ((size_t)added >= (cap - used)) {
        // Truncation: null-terminate at the end of the buffer.
        buf[cap - 1] = '\0';
        *pLen = cap - 1;
        return false;
    }

    *pLen = used + (size_t)added;
    return true;
}


// Format helpers for the gated columns.  When `valid`, formats `val` with
// `fmt`; when not, emits an empty cell.  `fmt` must include the leading
// comma to match the existing FormatRow style (the comma-prefix scheme
// keeps Appendf calls readable in column-group blocks).  The trampoline
// passes its caller-supplied `fmt` through to Appendf, which is marked
// printf-format; suppress the `-Wformat-nonliteral` complaint locally.
// AppendFloatOrEmpty / AppendIntOrEmpty removed — the fast formatters
// inline this pattern directly at each call site (AppendChar ','; if
// valid then AppendFloatFixed/AppendInt32).

}   // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace onspeed::proto::log_csv {

size_t WriteHeader(const onspeed::LogRow& row, char* out, size_t outCapacity)
{
    if (out == nullptr || outCapacity == 0)
        return 0;

    size_t len = 0;
    bool ok = true;

    // Always-present columns. timeStampUs sits adjacent to timeStamp so
    // both timestamps for a row are co-located in the schema; downstream
    // tooling reads by name (HeaderIndex), not ordinal, so the inserted
    // position is schema-safe.
    ok &= Appendf(out, outCapacity, &len,
        "timeStamp,timeStampUs,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,"
        "IAS,AngleofAttack,flapsPos,DataMark");
    ok &= Appendf(out, outCapacity, &len, ",OAT,TAS");
    ok &= Appendf(out, outCapacity, &len,
        ",imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll");

    if (row.boomEnabled)
        ok &= Appendf(out, outCapacity, &len,
            ",boomStatic,boomDynamic,boomAlpha,boomBeta,boomIAS,boomAge");

    if (row.efisEnabled) {
        if (row.efisIsVn300) {
            ok &= Appendf(out, outCapacity, &len,
                ",vnAngularRateRoll,vnAngularRatePitch,vnAngularRateYaw"
                ",vnVelNedNorth,vnVelNedEast,vnVelNedDown"
                ",vnAccelFwd,vnAccelLat,vnAccelVert"
                ",vnYaw,vnPitch,vnRoll"
                ",vnLinAccFwd,vnLinAccLat,vnLinAccVert"
                ",vnYawSigma,vnRollSigma,vnPitchSigma"
                ",vnGnssVelNedNorth,vnGnssVelNedEast,vnGnssVelNedDown"
                ",vnWindSpd,vnWindDir,vnWindVertical"
                ",vnGnssLat,vnGnssLon,vnEstAltFt,vnGPSFix,vnDataAge,vnTimeUTC");
        } else {
            ok &= Appendf(out, outCapacity, &len,
                ",efisIAS,efisPitch,efisRoll,efisLateralG,efisVerticalG"
                ",efisPercentLift,efisPalt,efisVSI,efisTAS,efisOAT"
                ",efisFuelRemaining,efisFuelFlow,efisMAP,efisRPM"
                ",efisPercentPower,efisMagHeading,efisAge,efisTime");
        }
    }

    ok &= Appendf(out, outCapacity, &len, ",EarthVerticalG,FlightPath,VSI,Altitude");
    ok &= Appendf(out, outCapacity, &len, ",DerivedAOA,CoeffP");

    // Tail-optional: emit only when this session captures the raw flap-pot
    // ADC.  Placed last so older logs (without the column) parse byte-for-byte
    // identically against the same FormatRow output.
    if (row.flapsRawAdcPresent)
        ok &= Appendf(out, outCapacity, &len, ",flapsRawADC");

    return ok ? len : 0;
}

size_t FormatRow(const onspeed::LogRow& row, char* out, size_t outCapacity)
{
    if (out == nullptr || outCapacity == 0)
        return 0;

    size_t len = 0;
    bool ok = true;

    // Core sensor columns — must match LogSensor::Write() exactly.
    //   %lu,%llu,%i,%.2f,%i,%.2f,%.2f,%.2f
    ok &= AppendUInt32(out, outCapacity, &len, row.timeStampMs);
    ok &= AppendChar(out, outCapacity, &len, ',');
    ok &= AppendUInt64(out, outCapacity, &len, row.timeStampUs);
    ok &= CommaInt32(out, outCapacity, &len, row.pfwdCounts);
    ok &= CommaFloat(out, outCapacity, &len, row.pfwdSmoothed,  2);
    ok &= CommaInt32(out, outCapacity, &len, row.p45Counts);
    ok &= CommaFloat(out, outCapacity, &len, row.p45Smoothed,   2);
    ok &= CommaFloat(out, outCapacity, &len, row.pStaticMbar,   2);
    ok &= CommaFloat(out, outCapacity, &len, row.paltFt,        2);

    //   ,%.2f  (IAS)  ,%.2f  (AngleofAttack)
    ok &= CommaFloat(out, outCapacity, &len, row.iasKt,            2);
    ok &= CommaFloat(out, outCapacity, &len, row.angleOfAttackDeg, 2);

    //   ,%i,%i  (flapsPos, DataMark)
    ok &= CommaInt32(out, outCapacity, &len, row.flapsPos);
    ok &= CommaInt32(out, outCapacity, &len, row.dataMark);

    //   ,%.2f,%.2f  (OAT, TAS)
    ok &= CommaFloat(out, outCapacity, &len, row.oatCelsius, 2);
    ok &= CommaFloat(out, outCapacity, &len, row.tasKt,      2);

    // IMU columns. PitchRate column stores -imuPitchRateDps.
    //   ,%.2f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.2f,%.2f
    ok &= CommaFloat(out, outCapacity, &len, row.imuTempCelsius,    2);
    ok &= CommaFloat(out, outCapacity, &len, row.imuVerticalG,      6);
    ok &= CommaFloat(out, outCapacity, &len, row.imuLateralG,       6);
    ok &= CommaFloat(out, outCapacity, &len, row.imuForwardG,       6);
    ok &= CommaFloat(out, outCapacity, &len, row.imuRollRateDps,    6);
    ok &= CommaFloat(out, outCapacity, &len, -row.imuPitchRateDps,  6);
    ok &= CommaFloat(out, outCapacity, &len, row.imuYawRateDps,     6);
    ok &= CommaFloat(out, outCapacity, &len, row.pitchDeg,          2);
    ok &= CommaFloat(out, outCapacity, &len, row.rollDeg,           2);

    // Boom columns (optional)
    if (row.boomEnabled) {
        ok &= CommaFloat(out, outCapacity, &len, row.boomStatic,  2);
        ok &= CommaFloat(out, outCapacity, &len, row.boomDynamic, 2);
        ok &= CommaFloat(out, outCapacity, &len, row.boomAlpha,   2);
        ok &= CommaFloat(out, outCapacity, &len, row.boomBeta,    2);
        ok &= CommaFloat(out, outCapacity, &len, row.boomIasKt,   2);
        ok &= CommaInt32(out, outCapacity, &len, (int32_t)row.boomAgeMs);
    }

    // EFIS columns (optional)
    if (row.efisEnabled) {
        if (row.efisIsVn300) {
            // The VN-300 row's last column is `vnTimeUtc`, written as `%s`.
            // A comma in that string would split into the next CSV column
            // and corrupt every parser downstream (the tokenizer is not
            // RFC-4180 quote-aware).  Today the producer in onspeed_core
            // emits `%u:%u:%u`, but a future format change is the latent
            // risk.  Refuse to emit the row rather than silently corrupt.
            if (memchr(row.vnTimeUtc, ',', strnlen(row.vnTimeUtc, sizeof(row.vnTimeUtc))) != nullptr)
                return 0;
            // VN-300 format.  Wind columns emit as empty cells when NaN.
            ok &= CommaFloat(out, outCapacity, &len, row.vnAngularRateRoll,  2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnAngularRatePitch, 2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnAngularRateYaw,   2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnVelNedNorth,      2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnVelNedEast,       2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnVelNedDown,       2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnAccelFwd,         2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnAccelLat,         2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnAccelVert,        2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnYawDeg,           2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnPitchDeg,         2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnRollDeg,          2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnLinAccFwd,        2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnLinAccLat,        2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnLinAccVert,       2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnYawSigma,         2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnRollSigma,        2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnPitchSigma,       2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnGnssVelNedNorth,  2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnGnssVelNedEast,   2);
            ok &= CommaFloat(out, outCapacity, &len, row.vnGnssVelNedDown,   2);
            // Wind: emit comma + value when finite, just a comma when NaN.
            ok &= AppendChar(out, outCapacity, &len, ',');
            if (std::isfinite(row.vnWindSpd))
                ok &= AppendFloatFixed(out, outCapacity, &len, row.vnWindSpd, 2);
            ok &= AppendChar(out, outCapacity, &len, ',');
            if (std::isfinite(row.vnWindDir))
                ok &= AppendFloatFixed(out, outCapacity, &len, row.vnWindDir, 1);
            ok &= AppendChar(out, outCapacity, &len, ',');
            if (std::isfinite(row.vnWindVertical))
                ok &= AppendFloatFixed(out, outCapacity, &len, row.vnWindVertical, 2);
            // GnssLat/Lon: %.6f doubles. The fast formatter takes float; cast.
            // (Coordinates fit easily in float32 to ~7 decimal places of precision
            // at equator, plenty for the %.6f output.)
            ok &= CommaFloat(out, outCapacity, &len, (float)row.vnGnssLat,  6);
            ok &= CommaFloat(out, outCapacity, &len, (float)row.vnGnssLon,  6);
            ok &= CommaFloat(out, outCapacity, &len, row.vnEstAltFt,         2);
            ok &= CommaInt32(out, outCapacity, &len, (int32_t)row.vnGpsFix);
            ok &= CommaInt32(out, outCapacity, &len, (int32_t)row.vnDataAgeMs);
            // Time-of-day string: keep Appendf for %s.
            ok &= Appendf(out, outCapacity, &len, ",%s", row.vnTimeUtc);
        } else {
            // Standard EFIS (Dynon, Garmin, MGL, etc.)
            ok &= CommaFloat(out, outCapacity, &len, row.efisIasKt,     2);
            ok &= CommaFloat(out, outCapacity, &len, row.efisPitchDeg,  2);
            ok &= CommaFloat(out, outCapacity, &len, row.efisRollDeg,   2);
            ok &= CommaFloat(out, outCapacity, &len, row.efisLateralG,  2);
            ok &= CommaFloat(out, outCapacity, &len, row.efisVerticalG, 2);
            // efisPercentLift goes empty when not valid.
            ok &= AppendChar(out, outCapacity, &len, ',');
            if (row.efisPercentLiftValid)
                ok &= AppendInt32(out, outCapacity, &len, row.efisPercentLift);
            ok &= CommaInt32(out, outCapacity, &len, row.efisPaltFt);
            ok &= CommaInt32(out, outCapacity, &len, row.efisVsiFpm);
            ok &= CommaFloat(out, outCapacity, &len, row.efisTasKt,         2);
            ok &= CommaFloat(out, outCapacity, &len, row.efisOatCelsius,    2);
            ok &= CommaFloat(out, outCapacity, &len, row.efisFuelRemaining, 2);
            ok &= CommaFloat(out, outCapacity, &len, row.efisFuelFlow,      2);
            ok &= CommaFloat(out, outCapacity, &len, row.efisMap,           2);
            ok &= CommaInt32(out, outCapacity, &len, row.efisRpm);
            ok &= CommaInt32(out, outCapacity, &len, row.efisPercentPower);
            ok &= CommaInt32(out, outCapacity, &len, row.efisMagHeading);
            ok &= CommaInt32(out, outCapacity, &len, row.efisAgeMs);
            ok &= CommaUInt32(out, outCapacity, &len, row.efisTimestampMs);
        }
    }

    // Post-EFIS derived columns (always present).
    ok &= CommaFloat(out, outCapacity, &len, row.earthVerticalG, 2);
    ok &= CommaFloat(out, outCapacity, &len, row.flightPathDeg,  2);
    ok &= CommaFloat(out, outCapacity, &len, row.vsiFpm,          2);
    ok &= CommaFloat(out, outCapacity, &len, row.altitudeFt,      2);

    // DerivedAOA, CoeffP — 4 decimal places.
    ok &= CommaFloat(out, outCapacity, &len, row.derivedAoaDeg, 4);
    ok &= CommaFloat(out, outCapacity, &len, row.coeffP,        4);

    if (row.flapsRawAdcPresent)
        ok &= CommaUInt32(out, outCapacity, &len, row.flapsRawAdc);

    return ok ? len : 0;
}

}   // namespace onspeed::proto::log_csv
