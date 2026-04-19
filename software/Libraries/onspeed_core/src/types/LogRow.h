// LogRow.h
//
// One row of the OnSpeed SD log CSV, with each column typed. Produced by task
// code snapshotting AHRS/Sensors/Config; consumed by proto::LogCsv::FormatRow
// in PR 2.3 to produce the actual comma-delimited line.
//
// Column order matches the CSV header written by LogSensor::Open():
//   timeStamp, Pfwd, PfwdSmoothed, P45, P45Smoothed, PStatic, Palt, IAS,
//   AngleofAttack, flapsPos, DataMark, OAT, TAS,
//   imuTemp, VerticalG, LateralG, ForwardG, RollRate, PitchRate, YawRate,
//   Pitch, Roll,
//   [boom columns if boom enabled],
//   [EFIS columns if EFIS enabled],
//   EarthVerticalG, FlightPath, VSI, Altitude,
//   DerivedAOA, CoeffP

#ifndef ONSPEED_CORE_TYPES_LOG_ROW_H
#define ONSPEED_CORE_TYPES_LOG_ROW_H

#include <cstdint>

namespace onspeed {

// Maximum length for the VN-300 UTC time string field, including null terminator.
static constexpr int kLogRowUtcTimeLen = 32;

struct LogRow {
    // --- Core sensor columns (always present) ---

    // Wall-clock millisecond timestamp from the sketch (millis()).
    uint32_t timeStampMs = 0;

    // Raw forward (pitot) pressure counts and smoothed value.
    int   pfwdCounts    = 0;
    float pfwdSmoothed  = 0.0f;

    // Raw 45-degree (AOA differential) pressure counts and smoothed value.
    int   p45Counts     = 0;
    float p45Smoothed   = 0.0f;

    // Static pressure (millibars), pressure altitude (feet), and IAS (knots).
    float pStaticMbar = 0.0f;
    float paltFt      = 0.0f;
    float iasKt       = 0.0f;

    // Angle of attack (degrees, running average).
    float angleOfAttackDeg = 0.0f;

    // Detected flap position index.
    int flapsPos = 0;

    // User-set data mark value (from console or button).
    int dataMark = 0;

    // Outside air temperature (Celsius) and true airspeed (knots).
    float oatCelsius = 0.0f;
    float tasKt      = 0.0f;

    // --- IMU columns ---

    // IMU die temperature (Celsius).
    float imuTempCelsius = 0.0f;

    // IMU accelerations in aircraft body axes (g).
    float imuVerticalG  = 0.0f;   // vertical (Az)
    float imuLateralG   = 0.0f;   // lateral  (Ay)
    float imuForwardG   = 0.0f;   // forward  (Ax)

    // IMU gyro rates in aircraft body axes (degrees/second).
    // These are un-negated raw sensor values, consistent with ImuSample.
    // The sign flip for the PitchRate CSV column lives in LogCsv::FormatRow.
    float imuRollRateDps  = 0.0f;   // Gx
    float imuPitchRateDps = 0.0f;   // Gy (raw; FormatRow emits -imuPitchRateDps)
    float imuYawRateDps   = 0.0f;   // Gz

    // Smoothed pitch and roll from the AHRS filter (degrees).
    float pitchDeg = 0.0f;
    float rollDeg  = 0.0f;

    // --- Boom probe columns (only written when boom is enabled) ---

    float boomStatic  = 0.0f;   // boom static pressure
    float boomDynamic = 0.0f;   // boom dynamic pressure
    float boomAlpha   = 0.0f;   // boom alpha (angle of attack, degrees)
    float boomBeta    = 0.0f;   // boom beta  (sideslip, degrees)
    float boomIasKt   = 0.0f;   // boom indicated airspeed (knots)
    int   boomAgeMs   = 0;      // age of most recent boom packet (milliseconds)

    // --- EFIS columns (non-VN300): only written when EFIS is enabled and type != VN300 ---

    float efisIasKt         = 0.0f;
    float efisPitchDeg      = 0.0f;
    float efisRollDeg       = 0.0f;
    float efisLateralG      = 0.0f;
    float efisVerticalG     = 0.0f;
    int   efisPercentLift   = 0;
    int   efisPaltFt        = 0;
    int   efisVsiFpm        = 0;
    float efisTasKt         = 0.0f;
    float efisOatCelsius    = 0.0f;
    float efisFuelRemaining = 0.0f;
    float efisFuelFlow      = 0.0f;
    float efisMap           = 0.0f;
    int   efisRpm           = 0;
    int   efisPercentPower  = 0;
    int   efisMagHeading    = 0;
    int   efisAgeMs         = 0;
    uint32_t efisTimestampMs = 0;

    // --- VN-300 EFIS columns (only written when EFIS type is VN-300) ---

    float vnAngularRateRoll  = 0.0f;
    float vnAngularRatePitch = 0.0f;
    float vnAngularRateYaw   = 0.0f;
    float vnVelNedNorth      = 0.0f;
    float vnVelNedEast       = 0.0f;
    float vnVelNedDown       = 0.0f;
    float vnAccelFwd         = 0.0f;
    float vnAccelLat         = 0.0f;
    float vnAccelVert        = 0.0f;
    float vnYawDeg           = 0.0f;
    float vnPitchDeg         = 0.0f;
    float vnRollDeg          = 0.0f;
    float vnLinAccFwd        = 0.0f;
    float vnLinAccLat        = 0.0f;
    float vnLinAccVert       = 0.0f;
    float vnYawSigma         = 0.0f;
    float vnRollSigma        = 0.0f;
    float vnPitchSigma       = 0.0f;
    float vnGnssVelNedNorth  = 0.0f;
    float vnGnssVelNedEast   = 0.0f;
    float vnGnssVelNedDown   = 0.0f;
    double vnGnssLat         = 0.0;
    double vnGnssLon         = 0.0;
    int   vnGpsFix           = 0;
    int   vnDataAgeMs        = 0;
    char  vnTimeUtc[kLogRowUtcTimeLen] = {};

    // --- Post-EFIS derived columns (always present) ---

    // Earth-frame vertical G-load (g).
    float earthVerticalG = 0.0f;

    // Flight-path angle (degrees) and Kalman-filtered VSI (feet/minute).
    float flightPathDeg = 0.0f;
    float vsiFpm        = 0.0f;

    // Kalman-filtered altitude (feet).
    float altitudeFt = 0.0f;

    // Derived angle of attack (degrees) and pressure coefficient.
    float derivedAoaDeg = 0.0f;
    float coeffP        = 0.0f;

    // --- Feature-presence flags (not logged; used by FormatRow to decide ---
    // --- which optional column groups to emit)                           ---

    bool boomEnabled = false;
    bool efisEnabled = false;
    bool efisIsVn300 = false;
};

}   // namespace onspeed

#endif
