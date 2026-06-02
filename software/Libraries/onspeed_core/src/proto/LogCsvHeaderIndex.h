// proto/LogCsvHeaderIndex.h — name-keyed CSV log replay support.
//
// LogCsv.h's FormatRow / ParseRow pair is position-based and lives at the
// canonical column layout written by current firmware. LogReplay is the
// asymmetric direction: it must replay logs written by older firmware where
// the column set may have been smaller, larger, or reordered. This module
// builds a column-name -> row-ordinal index from the log's own header line
// and parses each row through that index, so the reader tolerates rename,
// reorder, addition, and removal of columns it doesn't require.
//
// What this module does NOT solve: unit changes (e.g. IAS knots -> m/s
// without renaming the column) or semantic changes (e.g. flapsPos meaning
// shifts from "index" to "degrees"). Those still require kFormatVersion
// bumps and explicit reader handling.

#ifndef ONSPEED_CORE_PROTO_LOG_CSV_HEADER_INDEX_H
#define ONSPEED_CORE_PROTO_LOG_CSV_HEADER_INDEX_H

#include <string_view>

#include <types/LogRow.h>

namespace onspeed::proto::log_csv {

// Maximum number of comma-delimited tokens a single row may carry.
// Worst case today is the VN-300 row at ~75 columns; 96 leaves room
// for unknown extra columns in old logs.
inline constexpr int kHeaderIndexMaxColumns = 96;

struct HeaderIndex {
    // -1 means "column not present in this log". Otherwise the 0-based
    // ordinal of the column in the row.

    // Always-present core columns (BuildHeaderIndex requires all of these).
    int idxTimeStampMs    = -1;

    // Optional µs-resolution timestamp (issue #551). Older logs lack
    // this column; in that case the ordinal stays -1, ParseRowByIndex
    // leaves row.timeStampUs at 0, and consumers should fall back to
    // row.timeStampMs * 1000 for absolute timing on those logs.
    int idxTimeStampUs    = -1;

    int idxPfwd           = -1;
    int idxPfwdSmoothed   = -1;
    int idxP45            = -1;
    int idxP45Smoothed    = -1;
    int idxPStatic        = -1;
    int idxPaltFt         = -1;
    int idxIasKt          = -1;
    int idxAoaDeg         = -1;
    int idxFlapsPos       = -1;
    int idxDataMark       = -1;
    int idxOatCelsius     = -1;
    int idxTasKt          = -1;
    int idxImuTemp        = -1;
    int idxVerticalG      = -1;
    int idxLateralG       = -1;
    int idxForwardG       = -1;
    int idxRollRate       = -1;
    int idxPitchRate      = -1;
    int idxYawRate        = -1;
    int idxPitchDeg       = -1;
    int idxRollDeg        = -1;

    // Boom (optional). All six required to set boomEnabled.
    int idxBoomStatic     = -1;
    int idxBoomDynamic    = -1;
    int idxBoomAlpha      = -1;
    int idxBoomBeta       = -1;
    int idxBoomIas        = -1;
    int idxBoomAge        = -1;

    // EFIS — standard set (optional, mutually exclusive with VN-300).
    // All required to set efisEnabled when efisIsVn300 is false.
    int idxEfisIas             = -1;
    int idxEfisPitch           = -1;
    int idxEfisRoll            = -1;
    int idxEfisLateralG        = -1;
    int idxEfisVerticalG       = -1;
    int idxEfisPercentLift     = -1;
    int idxEfisPalt            = -1;
    int idxEfisVsi             = -1;
    int idxEfisTas             = -1;
    int idxEfisOat             = -1;
    int idxEfisFuelRemaining   = -1;
    int idxEfisFuelFlow        = -1;
    int idxEfisMap             = -1;
    int idxEfisRpm             = -1;
    int idxEfisPercentPower    = -1;
    int idxEfisMagHeading      = -1;
    int idxEfisAge             = -1;
    int idxEfisTime            = -1;

    // EFIS — VN-300 set (optional). All required to set efisIsVn300.
    int idxVnAngularRateRoll   = -1;
    int idxVnAngularRatePitch  = -1;
    int idxVnAngularRateYaw    = -1;
    int idxVnVelNedNorth       = -1;
    int idxVnVelNedEast        = -1;
    int idxVnVelNedDown        = -1;
    int idxVnAccelFwd          = -1;
    int idxVnAccelLat          = -1;
    int idxVnAccelVert         = -1;
    int idxVnYaw               = -1;
    int idxVnPitch             = -1;
    int idxVnRoll              = -1;
    int idxVnLinAccFwd         = -1;
    int idxVnLinAccLat         = -1;
    int idxVnLinAccVert        = -1;
    int idxVnYawSigma          = -1;
    int idxVnRollSigma         = -1;
    int idxVnPitchSigma        = -1;
    int idxVnGnssVelNedNorth   = -1;
    int idxVnGnssVelNedEast    = -1;
    int idxVnGnssVelNedDown    = -1;
    // Optional within VN-300 group (format version 5+).
    int idxVnWindSpd         = -1;
    int idxVnWindDir        = -1;
    int idxVnWindVertical    = -1;
    int idxVnGnssLat           = -1;
    int idxVnGnssLon           = -1;
    int idxVnEstAltFt          = -1;
    int idxVnGpsFix            = -1;
    int idxVnDataAge           = -1;
    // Per-sample VN-300 timestamps (issue #637).
    int idxVnTimeStartupNs     = -1;
    int idxVnTimeGpsNs         = -1;
    int idxVnTimeStatus        = -1;

    // Always-present derived columns at end of row.
    int idxEarthVerticalG      = -1;
    int idxFlightPathDeg       = -1;
    int idxVsiFpm              = -1;
    int idxAltitude            = -1;
    int idxDerivedAoa          = -1;
    int idxCoeffP              = -1;

    // EKFQ-diagnostic columns (format version 6+).  Optional — absent in
    // pre-v6 logs, in which case ParseRowByIndex leaves the matching
    // LogRow fields at their NaN default.
    int idxEkfBpDps            = -1;
    int idxEkfBqDps            = -1;
    int idxEkfBrDps            = -1;
    int idxEkfBAzMps2          = -1;
    int idxEkfBetaDeg          = -1;
    int idxEkfYawDeg           = -1;

    // Tail-optional raw flap-pot ADC reading (format version 2). Absent in
    // older logs, in which case ParseRowByIndex leaves row.flapsRawAdc at
    // its default and clears row.flapsRawAdcPresent.
    int idxFlapsRawAdc         = -1;

    int  totalColumns          = 0;
    bool boomEnabled           = false;
    bool efisEnabled           = false;
    bool efisIsVn300           = false;
};

// Sink for missing-column warnings. Called once per missing always-present
// column or per partial optional group, with the static-string column name.
using WarnSink = void (*)(const char* missingColumn);

// Parse a CSV header line into a HeaderIndex.
//
// Missing always-present columns and partial optional groups are reported
// via `warnSink` (one call per warning, with a static-string column name);
// absent column ordinals stay -1 and `boomEnabled`/`efisEnabled`/
// `efisIsVn300` reflect only fully-complete groups. The function returns
// false on two unrecoverable shapes: a header with > kHeaderIndexMaxColumns
// tokens (the row-side tokenizer would truncate, dropping every row), and
// a header with zero recognized OnSpeed columns (no signal to replay
// against). In both cases `warnSink` is called with a parenthesized
// sentinel string before returning false.
//
// Tolerates trailing CR/LF on the last token. Unknown column names are
// counted toward totalColumns but not stored.
bool BuildHeaderIndex(std::string_view headerLine,
                      HeaderIndex& out,
                      WarnSink warnSink = nullptr);

// Parse one data row using the index.
//
// Returns true on success. Returns false if the row has fewer tokens than
// idx.totalColumns, or if any indexed numeric token is empty or fails
// parsing (matches LogCsv::ParseRow behavior). No string-typed columns
// exist on the current VN-300 row (per-sample timestamps are u64 numeric).
//
// Applies the issue #182 sign-flip recovery: the CSV PitchRate column
// stores -imuPitchRateDps, so on parse we negate to recover the raw value.
bool ParseRowByIndex(std::string_view line,
                     const HeaderIndex& idx,
                     onspeed::LogRow& row);

}  // namespace onspeed::proto::log_csv

#endif  // ONSPEED_CORE_PROTO_LOG_CSV_HEADER_INDEX_H
