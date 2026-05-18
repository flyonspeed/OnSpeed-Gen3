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
    // Column 1 is timeStamp (ms). Column 2 is timeStampUs (µs).
    // Columns 3..8 are always numeric; IAS and AngleofAttack go empty
    // when `iasValid` is false (matches the M5 wire / JSON convention so
    // offline analysis tools can distinguish "no air-data yet" from
    // "real reading of 0.0"). flapsPos and DataMark stay numeric —
    // they're meaningful at rest.
    //   %lu,%llu,%i,%.2f,%i,%.2f,%.2f,%.2f
    ok &= Appendf(out, outCapacity, &len,
        "%lu,%llu,%i,%.2f,%i,%.2f,%.2f,%.2f",
        (unsigned long)row.timeStampMs,
        (unsigned long long)row.timeStampUs,
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
            // VN-300 format.  Wind columns emit as empty cells when NaN
            // (no GPS fix, TAS below threshold, or NaN attitude).
            ok &= Appendf(out, outCapacity, &len,
                ",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f"
                ",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f"
                ",%.2f,%.2f,%.2f",
                row.vnAngularRateRoll,  row.vnAngularRatePitch, row.vnAngularRateYaw,
                row.vnVelNedNorth,      row.vnVelNedEast,       row.vnVelNedDown,
                row.vnAccelFwd,         row.vnAccelLat,         row.vnAccelVert,
                row.vnYawDeg,           row.vnPitchDeg,         row.vnRollDeg,
                row.vnLinAccFwd,        row.vnLinAccLat,        row.vnLinAccVert,
                row.vnYawSigma,         row.vnRollSigma,        row.vnPitchSigma,
                row.vnGnssVelNedNorth,  row.vnGnssVelNedEast,   row.vnGnssVelNedDown);
            ok &= AppendFloatOrEmpty(out, outCapacity, &len,
                std::isfinite(row.vnWindSpd),      ",%.2f", row.vnWindSpd);
            ok &= AppendFloatOrEmpty(out, outCapacity, &len,
                std::isfinite(row.vnWindDir),     ",%.1f", row.vnWindDir);
            ok &= AppendFloatOrEmpty(out, outCapacity, &len,
                std::isfinite(row.vnWindVertical), ",%.2f", row.vnWindVertical);
            ok &= Appendf(out, outCapacity, &len,
                ",%.6f,%.6f,%.2f,%i,%i,%s",
                row.vnGnssLat,          row.vnGnssLon,          row.vnEstAltFt,
                row.vnGpsFix,           row.vnDataAgeMs,        row.vnTimeUtc);
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

}   // namespace onspeed::proto::log_csv
