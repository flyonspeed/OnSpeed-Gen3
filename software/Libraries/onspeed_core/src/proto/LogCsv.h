// proto/LogCsv.h — CSV row formatting and parsing for the OnSpeed SD log
//
// The firmware writes CSV rows to the SD card at 50 Hz (default).  The
// LogReplay task reads back those same rows for bench testing.  This
// module is the single source of truth for the column list, their order,
// units, and precision.  Any change here MUST be followed by:
//
//   1. Bumping kFormatVersion in LogCsv.cpp.
//   2. Updating the "Log columns" page of the docs site.
//   3. Updating any offline analysis tooling that depends on column order.
//
// Column layout (always-present groups separated by blank lines):
//
//   timeStamp, Pfwd, PfwdSmoothed, P45, P45Smoothed, PStatic, Palt,
//   IAS, AngleofAttack, flapsPos, DataMark,
//   OAT, TAS,
//   imuTemp, VerticalG, LateralG, ForwardG,
//   RollRate, PitchRate, YawRate, Pitch, Roll,
//
//   [if boom enabled]
//   boomStatic, boomDynamic, boomAlpha, boomBeta, boomIAS, boomAge,
//
//   [if EFIS enabled and type == VN-300]
//   vnAngularRateRoll, vnAngularRatePitch, vnAngularRateYaw,
//   vnVelNedNorth, vnVelNedEast, vnVelNedDown,
//   vnAccelFwd, vnAccelLat, vnAccelVert,
//   vnYaw, vnPitch, vnRoll,
//   vnLinAccFwd, vnLinAccLat, vnLinAccVert,
//   vnYawSigma, vnRollSigma, vnPitchSigma,
//   vnGnssVelNedNorth, vnGnssVelNedEast, vnGnssVelNedDown,
//   vnWindSpd, vnWindDir, vnWindVertical,
//   vnGnssLat, vnGnssLon, vnEstAltFt, vnGPSFix, vnDataAge,
//   vnTimeStartupNs, vnTimeGpsNs, vnTimeStatus,
//
//   [if EFIS enabled and type != VN-300]
//   efisIAS, efisPitch, efisRoll, efisLateralG, efisVerticalG,
//   efisPercentLift, efisPalt, efisVSI, efisTAS, efisOAT,
//   efisFuelRemaining, efisFuelFlow, efisMAP, efisRPM,
//   efisPercentPower, efisMagHeading, efisAge, efisTime,
//
//   EarthVerticalG, FlightPath, VSI, Altitude,
//   DerivedAOA, CoeffP,
//
//   ekfBpDps, ekfBqDps, ekfBrDps, ekfBAzMps2, ekfBetaDeg, ekfYawDeg,
//     (EKFQ-diagnostic columns; finite under EKFQ, "nan" under Madgwick)
//
//   [if flapsRawAdcPresent]
//   flapsRawADC      (raw flap-pot ADC counts; uint16)
//
// Empty-cell convention (format version 3): IAS, AngleofAttack,
// DerivedAOA, and efisPercentLift go empty (Dynon `,,`) when the
// producer's `bIasAlive` gate is false.  ParseRow / ParseRowByIndex
// accept empty for these four columns and decode to NaN (floats) or
// 0 with a separate validity bit (efisPercentLift); every other column
// still rejects empty so the parser detects truncated SD writes.
//
// Reader asymmetry: the on-firmware LogReplay task reads logs through
// proto::log_csv::BuildHeaderIndex + ParseRowByIndex (see
// LogCsvHeaderIndex.h), not through the position-based ParseRow below.
// That path tolerates rename / reorder / addition / removal of columns
// the current parser doesn't require, so old logs replay through new
// firmware. ParseRow stays for symmetric callers (the regression
// harness and same-revision unit tests) where the writer and reader
// share this exact LogCsv.h file.

#ifndef ONSPEED_CORE_PROTO_LOG_CSV_H
#define ONSPEED_CORE_PROTO_LOG_CSV_H

#include <cstddef>
#include <string_view>
#include <types/LogRow.h>

namespace onspeed::proto::log_csv {

// Log format version.  Increment whenever column names, order, units, or
// precision change.  Consumer tools can use this to detect incompatible logs.
// Version 2: added tail-optional `flapsRawADC` column.  Older logs without
// the column still parse cleanly — consumers detect the column by name in
// the header line (HasColumn) and gate ParseRow on the presence flag.
// Version 3: IAS, AngleofAttack, DerivedAOA, and efisPercentLift go empty
// when the producer's `bIasAlive` gate is false (Dynon convention —
// empty cell distinguishes "no valid air data" from "real reading of
// 0.0").  ParseRow / ParseRowByIndex decode empty to NaN (floats) or 0
// with a separate validity bit (efisPercentLift).  Older v2 logs still
// parse: they always emit numbers, which the AllowEmpty path accepts as
// the valid-true case.
// Version 4: added `vnEstAltFt` to the VN-300 column group (INS-estimated
// altitude in feet, sourced from the wire's Common.Position LLA). The
// header-index parser tolerates the column's absence in older logs.
// Version 5: added `vnWindSpd`, `vnWindDir`, `vnWindVertical` to the
// VN-300 column group (wind triangle derived from GnssVelNed + VN-300 yaw +
// ownship TAS).  All three columns are optional within the VN-300 group;
// the header-index parser tolerates absence in older logs.
// Version 6: added `ekfBpDps`, `ekfBqDps`, `ekfBrDps`, `ekfBAzMps2`,
// `ekfBetaDeg`, `ekfYawDeg` — EKFQ-diagnostic columns (gyro biases,
// vertical accel bias, sideslip, yaw). Finite when iAhrsAlgorithm=EKFQ;
// NaN-emitted-as-"nan" when iAhrsAlgorithm=Madgwick. The header-index
// parser tolerates absence in older logs (the fields stay at their NaN
// default on the LogRow).
inline constexpr int kFormatVersion = 6;

// Conservative upper bounds for the two output buffers.
// Both are sized to accommodate the VN-300 variant, which is the widest row.
inline constexpr size_t kHeaderMaxBytes = 2048;
inline constexpr size_t kRowMaxBytes    = 2048;

// ---------------------------------------------------------------------------
// WriteHeader
//
// Writes the CSV header line (column names, comma-separated, no trailing
// newline) into `out`.  Returns the number of bytes written, or 0 on error.
// `out` must have at least kHeaderMaxBytes of space.
// ---------------------------------------------------------------------------
size_t WriteHeader(const onspeed::LogRow& row, char* out, size_t outCapacity);

// ---------------------------------------------------------------------------
// FormatRow
//
// Formats a single data row into `out`.  No trailing newline is appended;
// the caller adds one when writing to a file.
// Returns the number of bytes written, or 0 on error (e.g. truncation).
// `out` must have at least kRowMaxBytes of space.
//
// Issue #182 note: imuPitchRateDps in LogRow holds the raw (un-negated)
// gyro pitch rate.  FormatRow applies the sign flip when emitting the
// PitchRate column so that the CSV file is byte-identical to the previous
// (pre-refactor) format.
// ---------------------------------------------------------------------------
size_t FormatRow(const onspeed::LogRow& row, char* out, size_t outCapacity);

}   // namespace onspeed::proto::log_csv

#endif  // ONSPEED_CORE_PROTO_LOG_CSV_H
