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
//     g_AHRS.SmoothedPitch / .AccelFwdFilter.get() / .VsiMps access
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
#include "src/ahrs/AhrsSnapshot.h"

#include <ahrs/Ahrs.h>
#include <filters/EMAFilter.h>
#include <filters/GOnsetFilter.h>
#include <types/AhrsInputs.h>

using onspeed::EMAFilter;
using onspeed::GOnsetFilter;

class AHRS
{
public:
    AHRS(int gyroSmoothing);

    // === Public fields mirroring core AhrsOutputs for sketch consumers ===
    //
    // These cover the four-stage AHRS pipeline (see
    // onspeed_core/src/ahrs/Ahrs.h):
    //   Stage 1 — sensor:    rotation-corrected IMU acceleration.
    //   Stage 3 — smoothing: wire-spec accel EMA for display + log.
    //
    // The Stage 2 (algorithm) post-comp accel is owned internally by
    // each AHRS algorithm class (Madgwick / EkfqPipeline) and not
    // exposed at the sketch-side boundary.

    // Stage 1 — IMU acceleration after installation-bias rotation.
    float           AccelFwdCorr;
    float           AccelLatCorr;
    float           AccelVertCorr;

    // Stage 3 — wire-side accel smoothing.  Mirrors the core's
    // wire-spec EMA state for display / log / serial-protocol
    // consumers.  These filter objects are seeded each frame —
    // `update()` is never called on them.  Read `.get()`.
    EMAFilter       AccelFwdFilter;
    EMAFilter       AccelLatFilter;
    EMAFilter       AccelVertFilter;

    // Latest attitude estimate (degrees).
    float           SmoothedPitch;
    float           SmoothedRoll;

    // Low-pass-filtered first derivative of vertical G (g/s).  Single
    // source of truth for the G-onset rate-tape signal — DisplaySerial
    // (M5 wire format) and DataServer (LiveView JSON) both read this
    // field, so the M5 hardware indicator and the web indicator agree
    // bar-for-bar.  Updated each Process() tick from AccelVertCorr.
    float           gOnsetRate;

    // Derived signals.
    float           TASdotSmoothed;
    float           AltMeters;          // meters
    float           VsiMps;             // m/s
    float           FlightPath;         // degrees
    float           EarthVertG;
    float           DerivedAOA;

    float           gRoll, gPitch, gYaw;

    float           fImuSampleRate;
    float           fImuDeltaTime;

    float           fTAS;

    // Methods
    void    Init(float fSampleRate);
    void    Process();
    void    Process(float deltaTimeSeconds);

    // Publish the public output fields above into g_AhrsSnapshot as one
    // coherent payload (lock-free seqcount).  Called at the end of
    // Process() on the live path (ImuReadTask) and after LogReplay's
    // per-row write-back (LogReplayTask) so replay-mode consumers read
    // the same snapshot.  Single-writer-in-time: the live and replay
    // producers never run concurrently.
    void    PublishSnapshot();

    float   PitchWithBias();
    float   PitchWithBiasSmth();
    float   RollWithBias();
    float   RollWithBiasSmth();

private:
    onspeed::ahrs::Ahrs core_;
    int                 iGyroSmoothing_;

    // G-onset rate filter — Tau = 250 ms, ticked at the AHRS rate.
    // Lives here (not in DisplaySerial) so DataServer can read the
    // same filtered value through `gOnsetRate` above.
    GOnsetFilter        gOnsetFilter_;

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
