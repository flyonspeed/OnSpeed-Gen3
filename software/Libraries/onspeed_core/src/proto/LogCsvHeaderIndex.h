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

#include <cstdint>
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
    int idxVnGnssLat           = -1;
    int idxVnGnssLon           = -1;
    int idxVnGpsFix            = -1;
    int idxVnDataAge           = -1;
    int idxVnTimeUtc           = -1;

    // Always-present derived columns at end of row.
    int idxEarthVerticalG      = -1;
    int idxFlightPathDeg       = -1;
    int idxVsiFpm              = -1;
    int idxAltitude            = -1;
    int idxDerivedAoa          = -1;
    int idxCoeffP              = -1;

    int  totalColumns          = 0;
    bool boomEnabled           = false;
    bool efisEnabled           = false;
    bool efisIsVn300           = false;

    // Provenance — populated at log-open time from the existing .meta
    // sidecar (or, in a future format revision, from a header comment).
    // Pre-format-version logs leave these at defaults.
    int  formatVersion         = 0;        // 0 == "not declared in log"
    char fwVersion[16]         = {0};      // e.g. "4.18", "" if unknown
    char gitSha[12]            = {0};      // 7-char abbrev or "" if unknown
};

// Strictness mode for header validation. Picked at call time by the caller.
//
// Strict — any missing always-present column or partial optional group is
//   a hard fail with the missing column named. Used by unit tests, dev
//   fixtures, and the regression harness; we want our own schema drift
//   to be loud.
//
// Permissive — missing always-present columns log a warning naming the
//   column and leave the corresponding LogRow fields at default. Partial
//   optional groups log a warning and skip the group entirely (the
//   present columns within it stay -1). Used by LogReplay for SD-card
//   replay, including customer-submitted logs from older firmware where
//   we want best-effort consumption rather than refusal.
enum class HeaderStrictness : uint8_t {
    Strict     = 0,
    Permissive = 1,
};

// Sink for permissive-mode warnings. Called once per missing column with
// the static-string column name, plus the caller-supplied userdata.
using WarnSink = void (*)(const char* missingColumn, void* userdata);

// Parse a CSV header line into a HeaderIndex.
//
// In Strict mode: returns false if any always-present column is missing
// or any optional group is partially present, with `*missingOut` (when
// non-null) naming the first offender.
//
// In Permissive mode: always returns true. Missing always-present
// columns and partial optional groups are reported via `warnSink`
// (one call per warning, with a static-string column name) so the
// caller can log them; the index's `idxXxx` fields for absent columns
// stay -1 and `boomEnabled`/`efisEnabled`/`efisIsVn300` reflect only
// fully-complete groups.
//
// Tolerates trailing CR/LF on the last token. Unknown column names are
// counted toward totalColumns but not stored.
bool BuildHeaderIndex(std::string_view headerLine,
                      HeaderIndex& out,
                      HeaderStrictness strictness = HeaderStrictness::Strict,
                      const char** missingOut = nullptr,
                      WarnSink warnSink = nullptr,
                      void* warnUserdata = nullptr);

// Parse one data row using the index.
//
// Returns true on success. Returns false if the row has fewer tokens than
// idx.totalColumns, or if any indexed token fails numeric parsing, or if
// any indexed token is empty (matches LogCsv::ParseRow behavior).
//
// Applies the issue #182 sign-flip recovery: the CSV PitchRate column
// stores -imuPitchRateDps, so on parse we negate to recover the raw value.
bool ParseRowByIndex(std::string_view line,
                     const HeaderIndex& idx,
                     onspeed::LogRow& row);

}  // namespace onspeed::proto::log_csv

#endif  // ONSPEED_CORE_PROTO_LOG_CSV_HEADER_INDEX_H
