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

// ---------------------------------------------------------------------------
// CSV tokenizer used by ParseRow.
//
// Walks `line`, yielding one token per call.  Tokens are NOT null-terminated;
// the caller receives a string_view into the original buffer.
// The tokenizer handles:
//   - quoted fields (RFC 4180 style — not needed for this log but harmless)
//   - empty trailing fields
//   - trailing CR and/or LF stripped from the last field
// ---------------------------------------------------------------------------

struct CsvTokenizer {
    std::string_view src;
    size_t pos = 0;
    bool done = false;

    explicit CsvTokenizer(std::string_view s) : src(s)
    {
        // Strip trailing CR / LF from the whole line once.
        while (!src.empty() &&
               (src.back() == '\r' || src.back() == '\n'))
            src.remove_suffix(1);
    }

    // Returns false when there are no more tokens.
    bool next(std::string_view& tok)
    {
        if (done)
            return false;

        // An empty string after the last comma means one more empty field.
        if (pos > src.size()) {
            done = true;
            return false;
        }

        size_t start = pos;
        size_t end   = src.find(',', pos);

        if (end == std::string_view::npos) {
            tok  = src.substr(start);
            pos  = src.size() + 1;   // signal exhausted
            done = false;            // this token is valid
            return true;
        }

        tok = src.substr(start, end - start);
        pos = end + 1;
        return true;
    }
};

// Parse helpers — return true on success.
//
// The default helpers reject empty tokens. Most columns are emitted as a
// non-empty string by FormatRow (`%lu`, `%i`, `%.Nf`), so an empty token on
// the parse side means a corrupt row (a truncated SD write that left two
// adjacent commas). Returning false propagates through ParseRow and skips
// the row, which is what replay tools want — silently substituting zero
// would feed a phantom IAS=0 / AOA=0 sample to AHRS and audio.
//
// The `AllowEmpty` variants below are the explicit exception: four columns
// (IAS, AngleofAttack, DerivedAOA, efisPercentLift) are emitted empty when
// the producer's `bIasAlive` gate is false, matching the M5 wire / JSON /
// Dynon convention of distinguishing "no valid air data" from "real
// reading of 0.0". The float variant decodes empty to NaN; the int
// variant decodes empty to 0 with a separate validity flag, since the
// integer column has no NaN sentinel.

static bool ParseFloat(std::string_view tok, float& out)
{
    if (tok.empty()) return false;
    char buf[64];
    size_t n = tok.size() < sizeof(buf) - 1 ? tok.size() : sizeof(buf) - 1;
    memcpy(buf, tok.data(), n);
    buf[n] = '\0';
    char* end = nullptr;
    float v = strtof(buf, &end);
    if (end == buf) return false;
    out = v;
    return true;
}

static bool ParseDouble(std::string_view tok, double& out)
{
    if (tok.empty()) return false;
    char buf[64];
    size_t n = tok.size() < sizeof(buf) - 1 ? tok.size() : sizeof(buf) - 1;
    memcpy(buf, tok.data(), n);
    buf[n] = '\0';
    char* end = nullptr;
    double v = strtod(buf, &end);
    if (end == buf) return false;
    out = v;
    return true;
}

static bool ParseInt(std::string_view tok, int& out)
{
    if (tok.empty()) return false;
    char buf[32];
    size_t n = tok.size() < sizeof(buf) - 1 ? tok.size() : sizeof(buf) - 1;
    memcpy(buf, tok.data(), n);
    buf[n] = '\0';
    char* end = nullptr;
    long v = strtol(buf, &end, 10);
    if (end == buf) return false;
    out = (int)v;
    return true;
}

static bool ParseUint32(std::string_view tok, uint32_t& out)
{
    if (tok.empty()) return false;
    char buf[32];
    size_t n = tok.size() < sizeof(buf) - 1 ? tok.size() : sizeof(buf) - 1;
    memcpy(buf, tok.data(), n);
    buf[n] = '\0';
    char* end = nullptr;
    unsigned long v = strtoul(buf, &end, 10);
    if (end == buf) return false;
    out = (uint32_t)v;
    return true;
}

static bool ParseUint16(std::string_view tok, uint16_t& out)
{
    uint32_t v32 = 0;
    if (!ParseUint32(tok, v32)) return false;
    if (v32 > 0xFFFFu) return false;
    out = (uint16_t)v32;
    return true;
}

static bool ParseString(std::string_view tok, char* out, size_t outCap)
{
    if (outCap == 0) return false;
    size_t n = tok.size() < outCap - 1 ? tok.size() : outCap - 1;
    memcpy(out, tok.data(), n);
    out[n] = '\0';
    return true;
}

// Empty-tolerant parse for the four `bIasAlive`-gated columns.
// Empty token sets `out` to NaN and `outValid` to false; otherwise behaves
// like ParseFloat and sets `outValid` to true on success.
static bool ParseFloatAllowEmpty(std::string_view tok, float& out, bool& outValid)
{
    if (tok.empty()) {
        out = std::nanf("");
        outValid = false;
        return true;
    }
    if (!ParseFloat(tok, out)) return false;
    outValid = true;
    return true;
}

// Empty-tolerant parse for the int `efisPercentLift` column.  Empty token
// sets `out` to 0 and `outValid` to false; otherwise behaves like ParseInt
// and sets `outValid` to true on success.
static bool ParseIntAllowEmpty(std::string_view tok, int& out, bool& outValid)
{
    if (tok.empty()) {
        out = 0;
        outValid = false;
        return true;
    }
    if (!ParseInt(tok, out)) return false;
    outValid = true;
    return true;
}

// Format helpers for the gated columns.  When `valid`, formats `val` with
// `fmt`; when not, emits an empty cell.  `fmt` must include the leading
// comma to match the existing FormatRow style (the comma-prefix scheme
// keeps Appendf calls readable in column-group blocks).  The trampoline
// passes its caller-supplied `fmt` through to Appendf, which is marked
// printf-format; suppress the `-Wformat-nonliteral` complaint locally.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
static bool AppendFloatOrEmpty(char* buf, size_t cap, size_t* pLen,
                               bool valid, const char* fmt, float val)
{
    return valid ? Appendf(buf, cap, pLen, fmt, val)
                 : Appendf(buf, cap, pLen, ",");
}

static bool AppendIntOrEmpty(char* buf, size_t cap, size_t* pLen,
                             bool valid, const char* fmt, int val)
{
    return valid ? Appendf(buf, cap, pLen, fmt, val)
                 : Appendf(buf, cap, pLen, ",");
}
#pragma GCC diagnostic pop

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

    // Always-present columns
    ok &= Appendf(out, outCapacity, &len,
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,"
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
                ",vnGnssLat,vnGnssLon,vnGPSFix,vnDataAge,vnTimeUTC");
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
    // Columns 1..7 are always numeric; IAS and AngleofAttack go empty when
    // `iasValid` is false (matches the M5 wire / JSON convention so offline
    // analysis tools can distinguish "no air-data yet" from "real reading
    // of 0.0").  flapsPos and DataMark stay numeric — they're meaningful
    // at rest.
    //   %lu,%i,%.2f,%i,%.2f,%.2f,%.2f
    ok &= Appendf(out, outCapacity, &len,
        "%lu,%i,%.2f,%i,%.2f,%.2f,%.2f",
        (unsigned long)row.timeStampMs,
        row.pfwdCounts, row.pfwdSmoothed,
        row.p45Counts,  row.p45Smoothed,
        row.pStaticMbar, row.paltFt);

    // IAS (column 8) and AngleofAttack (column 9): empty when !iasValid.
    ok &= AppendFloatOrEmpty(out, outCapacity, &len, row.iasValid,
                             ",%.2f", row.iasKt);
    ok &= AppendFloatOrEmpty(out, outCapacity, &len, row.iasValid,
                             ",%.2f", row.angleOfAttackDeg);

    //   ,%i,%i  (flapsPos, DataMark — always numeric)
    ok &= Appendf(out, outCapacity, &len, ",%i,%i",
        row.flapsPos, row.dataMark);

    //   ,%.2f,%.2f  (OAT, TAS)
    ok &= Appendf(out, outCapacity, &len,
        ",%.2f,%.2f",
        row.oatCelsius, row.tasKt);

    // IMU columns.  Note: PitchRate column stores -imuPitchRateDps
    // (sign flip relocated from LogSensor::Write into here — issue #182).
    //   ,%.2f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.2f,%.2f
    ok &= Appendf(out, outCapacity, &len,
        ",%.2f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.2f,%.2f",
        row.imuTempCelsius,
        row.imuVerticalG, row.imuLateralG, row.imuForwardG,
        row.imuRollRateDps, -row.imuPitchRateDps, row.imuYawRateDps,
        row.pitchDeg, row.rollDeg);

    // Boom columns (optional)
    if (row.boomEnabled) {
        ok &= Appendf(out, outCapacity, &len,
            ",%.2f,%.2f,%.2f,%.2f,%.2f,%i",
            row.boomStatic, row.boomDynamic,
            row.boomAlpha,  row.boomBeta,
            row.boomIasKt,  row.boomAgeMs);
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
            // VN-300 format
            ok &= Appendf(out, outCapacity, &len,
                ",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f"
                ",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f"
                ",%.2f,%.2f,%.2f,%.6f,%.6f,%i,%i,%s",
                row.vnAngularRateRoll,  row.vnAngularRatePitch, row.vnAngularRateYaw,
                row.vnVelNedNorth,      row.vnVelNedEast,       row.vnVelNedDown,
                row.vnAccelFwd,         row.vnAccelLat,         row.vnAccelVert,
                row.vnYawDeg,           row.vnPitchDeg,         row.vnRollDeg,
                row.vnLinAccFwd,        row.vnLinAccLat,        row.vnLinAccVert,
                row.vnYawSigma,         row.vnRollSigma,        row.vnPitchSigma,
                row.vnGnssVelNedNorth,  row.vnGnssVelNedEast,   row.vnGnssVelNedDown,
                row.vnGnssLat,          row.vnGnssLon,          row.vnGpsFix,
                row.vnDataAgeMs,        row.vnTimeUtc);
        } else {
            // Standard EFIS (Dynon, Garmin, MGL, etc.)
            // efisPercentLift goes empty when !efisPercentLiftValid (mirrors
            // the IAS gate; the EFIS-fed percent-lift is meaningless when
            // the producer's air-data isn't alive).
            //   ,%.2f,%.2f,%.2f,%.2f,%.2f
            ok &= Appendf(out, outCapacity, &len,
                ",%.2f,%.2f,%.2f,%.2f,%.2f",
                row.efisIasKt,    row.efisPitchDeg,  row.efisRollDeg,
                row.efisLateralG, row.efisVerticalG);
            ok &= AppendIntOrEmpty(out, outCapacity, &len,
                                   row.efisPercentLiftValid,
                                   ",%i", row.efisPercentLift);
            //   ,%i,%i,%.2f,%.2f,%.2f,%.2f,%.2f,%i,%i,%i,%i,%lu
            ok &= Appendf(out, outCapacity, &len,
                ",%i,%i,%.2f,%.2f,%.2f,%.2f,%.2f,%i,%i,%i,%i,%lu",
                row.efisPaltFt,        row.efisVsiFpm,
                row.efisTasKt,         row.efisOatCelsius,
                row.efisFuelRemaining, row.efisFuelFlow,      row.efisMap,
                row.efisRpm,           row.efisPercentPower,  row.efisMagHeading,
                row.efisAgeMs,         (unsigned long)row.efisTimestampMs);
        }
    }

    // Post-EFIS derived columns (always present).  EarthVerticalG / FlightPath
    // / VSI / Altitude / CoeffP stay numeric — they're derived from IMU,
    // pressure altitude, and raw pressure ratios that are meaningful at rest.
    // Only DerivedAOA is air-data-gated.
    ok &= Appendf(out, outCapacity, &len,
        ",%.2f,%.2f,%.2f,%.2f",
        row.earthVerticalG, row.flightPathDeg,
        row.vsiFpm, row.altitudeFt);

    ok &= AppendFloatOrEmpty(out, outCapacity, &len, row.iasValid,
                             ",%.4f", row.derivedAoaDeg);
    ok &= Appendf(out, outCapacity, &len, ",%.4f", row.coeffP);

    // Tail-optional flapsRawADC.  Mirrors WriteHeader; rows from sessions
    // without the column omit it entirely.
    if (row.flapsRawAdcPresent)
        ok &= Appendf(out, outCapacity, &len, ",%u", (unsigned)row.flapsRawAdc);

    return ok ? len : 0;
}

bool ParseRow(std::string_view line, onspeed::LogRow& row)
{
    CsvTokenizer tok(line);
    std::string_view field;

    // Core sensor columns
    if (!tok.next(field) || !ParseUint32(field, row.timeStampMs))  return false;
    if (!tok.next(field) || !ParseInt(field, row.pfwdCounts))      return false;
    if (!tok.next(field) || !ParseFloat(field, row.pfwdSmoothed))  return false;
    if (!tok.next(field) || !ParseInt(field, row.p45Counts))       return false;
    if (!tok.next(field) || !ParseFloat(field, row.p45Smoothed))   return false;
    if (!tok.next(field) || !ParseFloat(field, row.pStaticMbar))   return false;
    if (!tok.next(field) || !ParseFloat(field, row.paltFt))        return false;
    if (!tok.next(field) ||
        !ParseFloatAllowEmpty(field, row.iasKt, row.iasValid))     return false;
    {
        // AngleofAttack shares the iasValid gate with IAS.  Both are emitted
        // empty together by FormatRow; the parser must observe the same gate
        // (and reject mixed-state rows where one cell is empty and the other
        // numeric, which would mean a corrupt write).
        bool aoaValid = false;
        if (!tok.next(field) ||
            !ParseFloatAllowEmpty(field, row.angleOfAttackDeg, aoaValid)) return false;
        if (aoaValid != row.iasValid) return false;
    }
    if (!tok.next(field) || !ParseInt(field, row.flapsPos))        return false;
    if (!tok.next(field) || !ParseInt(field, row.dataMark))        return false;

    // OAT, TAS
    if (!tok.next(field) || !ParseFloat(field, row.oatCelsius))    return false;
    if (!tok.next(field) || !ParseFloat(field, row.tasKt))         return false;

    // IMU columns.
    // The CSV PitchRate column stores -imuPitchRateDps, so negate on parse
    // to restore the raw gyro value (issue #182).
    if (!tok.next(field) || !ParseFloat(field, row.imuTempCelsius))  return false;
    if (!tok.next(field) || !ParseFloat(field, row.imuVerticalG))    return false;
    if (!tok.next(field) || !ParseFloat(field, row.imuLateralG))     return false;
    if (!tok.next(field) || !ParseFloat(field, row.imuForwardG))     return false;
    if (!tok.next(field) || !ParseFloat(field, row.imuRollRateDps))  return false;
    {
        float pitchRateCsv = 0.0f;
        if (!tok.next(field) || !ParseFloat(field, pitchRateCsv))   return false;
        row.imuPitchRateDps = -pitchRateCsv;  // undo the sign flip applied by FormatRow
    }
    if (!tok.next(field) || !ParseFloat(field, row.imuYawRateDps))   return false;
    if (!tok.next(field) || !ParseFloat(field, row.pitchDeg))        return false;
    if (!tok.next(field) || !ParseFloat(field, row.rollDeg))         return false;

    // Boom columns (optional)
    if (row.boomEnabled) {
        if (!tok.next(field) || !ParseFloat(field, row.boomStatic))  return false;
        if (!tok.next(field) || !ParseFloat(field, row.boomDynamic)) return false;
        if (!tok.next(field) || !ParseFloat(field, row.boomAlpha))   return false;
        if (!tok.next(field) || !ParseFloat(field, row.boomBeta))    return false;
        if (!tok.next(field) || !ParseFloat(field, row.boomIasKt))   return false;
        if (!tok.next(field) || !ParseInt(field, row.boomAgeMs))     return false;
    }

    // EFIS columns (optional)
    if (row.efisEnabled) {
        if (row.efisIsVn300) {
            if (!tok.next(field) || !ParseFloat(field, row.vnAngularRateRoll))  return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnAngularRatePitch)) return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnAngularRateYaw))   return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnVelNedNorth))      return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnVelNedEast))       return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnVelNedDown))       return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnAccelFwd))         return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnAccelLat))         return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnAccelVert))        return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnYawDeg))           return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnPitchDeg))         return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnRollDeg))          return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnLinAccFwd))        return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnLinAccLat))        return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnLinAccVert))       return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnYawSigma))         return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnRollSigma))        return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnPitchSigma))       return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnGnssVelNedNorth))  return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnGnssVelNedEast))   return false;
            if (!tok.next(field) || !ParseFloat(field, row.vnGnssVelNedDown))   return false;
            if (!tok.next(field) || !ParseDouble(field, row.vnGnssLat))         return false;
            if (!tok.next(field) || !ParseDouble(field, row.vnGnssLon))         return false;
            if (!tok.next(field) || !ParseInt(field, row.vnGpsFix))             return false;
            if (!tok.next(field) || !ParseInt(field, row.vnDataAgeMs))          return false;
            if (!tok.next(field) || !ParseString(field, row.vnTimeUtc,
                                                 onspeed::kLogRowUtcTimeLen))   return false;
        } else {
            if (!tok.next(field) || !ParseFloat(field, row.efisIasKt))         return false;
            if (!tok.next(field) || !ParseFloat(field, row.efisPitchDeg))      return false;
            if (!tok.next(field) || !ParseFloat(field, row.efisRollDeg))       return false;
            if (!tok.next(field) || !ParseFloat(field, row.efisLateralG))      return false;
            if (!tok.next(field) || !ParseFloat(field, row.efisVerticalG))     return false;
            if (!tok.next(field) ||
                !ParseIntAllowEmpty(field, row.efisPercentLift,
                                    row.efisPercentLiftValid))         return false;
            if (!tok.next(field) || !ParseInt(field, row.efisPaltFt))          return false;
            if (!tok.next(field) || !ParseInt(field, row.efisVsiFpm))          return false;
            if (!tok.next(field) || !ParseFloat(field, row.efisTasKt))         return false;
            if (!tok.next(field) || !ParseFloat(field, row.efisOatCelsius))    return false;
            if (!tok.next(field) || !ParseFloat(field, row.efisFuelRemaining)) return false;
            if (!tok.next(field) || !ParseFloat(field, row.efisFuelFlow))      return false;
            if (!tok.next(field) || !ParseFloat(field, row.efisMap))           return false;
            if (!tok.next(field) || !ParseInt(field, row.efisRpm))             return false;
            if (!tok.next(field) || !ParseInt(field, row.efisPercentPower))    return false;
            if (!tok.next(field) || !ParseInt(field, row.efisMagHeading))      return false;
            if (!tok.next(field) || !ParseInt(field, row.efisAgeMs))           return false;
            if (!tok.next(field) || !ParseUint32(field, row.efisTimestampMs))  return false;
        }
    }

    // Post-EFIS derived columns (always present)
    if (!tok.next(field) || !ParseFloat(field, row.earthVerticalG))  return false;
    if (!tok.next(field) || !ParseFloat(field, row.flightPathDeg))   return false;
    if (!tok.next(field) || !ParseFloat(field, row.vsiFpm))          return false;
    if (!tok.next(field) || !ParseFloat(field, row.altitudeFt))      return false;
    {
        // DerivedAOA shares the iasValid gate with IAS / AngleofAttack.
        bool derivedValid = false;
        if (!tok.next(field) ||
            !ParseFloatAllowEmpty(field, row.derivedAoaDeg, derivedValid)) return false;
        if (derivedValid != row.iasValid) return false;
    }
    if (!tok.next(field) || !ParseFloat(field, row.coeffP))          return false;

    // Tail-optional flapsRawADC.  When the consumer flag is set, the column
    // must be present and must parse; absence or junk is a malformed row.
    // When the flag is clear, the column must be absent — extra trailing
    // tokens are ignored.  This matches WriteHeader/FormatRow's gate on the
    // same flag.
    if (row.flapsRawAdcPresent) {
        if (!tok.next(field) || !ParseUint16(field, row.flapsRawAdc)) return false;
    }

    return true;
}

}   // namespace onspeed::proto::log_csv
