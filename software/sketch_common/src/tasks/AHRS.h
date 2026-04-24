// AHRS.h — sketch-side wrapper around the platform-free
// onspeed::ahrs::Ahrs core class (PR 3.2 extraction).
//
// As of PR 3.2, all AHRS math lives in
// software/Libraries/onspeed_core/src/ahrs/Ahrs.{h,cpp}. This sketch
// class is reduced to a thin task-side adapter that:
//
//   * Owns one onspeed::ahrs::Ahrs instance configured from g_Config.
//   * Builds an AhrsInputs each frame from g_pIMU + g_Sensors +
//     g_EfisSerial.
//   * Calls Ahrs::Step(...) and mirrors the result into public fields
//     so existing readers (DisplaySerial, LogSensor, ConsoleSerial,
//     web liveview, LogReplay write-back, etc.) keep their old
//     g_AHRS.SmoothedPitch / .AccelFwdFilter.get() / .KalmanVSI access
//     patterns.  A future PR will migrate consumers to a
//     `Published<AhrsOutputs>` snapshot holder; this PR only moves the
//     math into core.
//
// The sketch-side EMA accel filters are kept purely for legacy read
// access — `update()` is never called on them.  Each Process() call
// seeds them with the core's freshly-computed values so consumers
// reading `.get()` see the same numbers the core just produced.

#pragma once

#include "src/Globals.h"

#include <ahrs/Ahrs.h>
#include <filters/EMAFilter.h>
#include <types/AhrsInputs.h>

using onspeed::EMAFilter;

class AHRS
{
public:
    AHRS(int gyroSmoothing);

    // === Public fields kept for legacy consumer compatibility ===

    // Step 1 - IMU raw acceleration corrected for installation bias
    float           AccelFwdCorr;
    float           AccelLatCorr;
    float           AccelVertCorr;

    // Step 2 - Corrected accelerations smoothed (mirror of core EMA state).
    // These filter objects are seeded each frame — `update()` is never
    // called on them.  Consumers read `.get()` directly.
    EMAFilter       AccelFwdFilter;
    EMAFilter       AccelLatFilter;
    EMAFilter       AccelVertFilter;

    // Step 3 - Smoothed acceleration with linear/centripetal compensation
    float           AccelFwdComp;
    float           AccelLatComp;
    float           AccelVertComp;

    // Latest attitude estimate (degrees).
    float           SmoothedPitch;
    float           SmoothedRoll;

    // Derived signals.
    float           TASdotSmoothed;
    float           KalmanAlt;          // meters (legacy convention)
    float           KalmanVSI;          // m/s   (legacy convention)
    float           FlightPath;         // degrees
    float           EarthVertG;
    float           DerivedAOA;

    float           gRoll, gPitch, gYaw;

    float           fImuSampleRate;
    float           fImuDeltaTime;

    bool            bIasWasBelowThreshold;

    float           fTAS;

    // Methods
    void    Init(float fSampleRate);
    void    Process();
    void    Process(float deltaTimeSeconds);

    float   PitchWithBias();
    float   PitchWithBiasSmth();
    float   PitchWithBiasSmthComp();
    float   RollWithBias();
    float   RollWithBiasSmth();
    float   RollWithBiasSmthComp();

private:
    onspeed::ahrs::Ahrs core_;
    int                 iGyroSmoothing_;

    // Build a fresh AhrsConfig from g_Config and the cached
    // gyroSmoothing window.  Called on every Init() so config edits
    // (e.g. user changing pitch bias via the web UI) take effect.
    onspeed::ahrs::AhrsConfig MakeCfg_() const;

    // Snapshot AhrsInputs from globals (g_pIMU, g_Sensors, g_EfisSerial,
    // g_Config).  Called once per Process() frame.
    onspeed::AhrsInputs SnapshotInputs_() const;

    // Mirror core outputs into the public fields above so legacy
    // consumers see the same values they always did.
    void PublishCoreState_();
};
