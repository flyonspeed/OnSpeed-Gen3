// DataRefAdapter.cpp — see DataRefAdapter.h.

#include "DataRefAdapter.h"

#include "XPLMDataAccess.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include <filters/EMAFilter.h>
#include <filters/GOnsetFilter.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

// Plugin-side IAS-mute global.  Defined in aoa_audio.cpp; the
// indexer reads it to derive the iasValid gate for the live AOA
// reading.  AOA threshold globals (fLDMAXAOA et al.) live in
// PercentLiftFill.cpp where they're consumed.
extern int   iMuteAudioUnderIAS;

namespace onspeed_xplane::indexer {

namespace {

// ------------------------------------------------------------------
// DataRef handles, looked up once at Init.
// ------------------------------------------------------------------
XPLMDataRef s_alpha           = nullptr;   // wing AOA, deg
XPLMDataRef s_pitch           = nullptr;   // theta, deg
XPLMDataRef s_roll            = nullptr;   // phi, deg
XPLMDataRef s_iasKt           = nullptr;
XPLMDataRef s_paltFt          = nullptr;   // pressure altitude
XPLMDataRef s_paltFallback    = nullptr;   // h_ind if paltFt missing
XPLMDataRef s_turnRate        = nullptr;   // deg/s
XPLMDataRef s_lateralG        = nullptr;
XPLMDataRef s_verticalG       = nullptr;
XPLMDataRef s_vsiFpm          = nullptr;
XPLMDataRef s_oatC            = nullptr;
XPLMDataRef s_vpath           = nullptr;
XPLMDataRef s_flapRatio       = nullptr;
XPLMDataRef s_onGroundAny     = nullptr;   // int, 1 when any gear is touching

inline float SafeGetf(XPLMDataRef ref, float fallback = 0.0f)
{
    return ref ? XPLMGetDataf(ref) : fallback;
}

inline bool SafeGetBool(XPLMDataRef ref, bool fallback = false)
{
    return ref ? (XPLMGetDatai(ref) != 0) : fallback;
}

}  // namespace

void InitDataRefs()
{
    s_alpha        = XPLMFindDataRef("sim/flightmodel/position/alpha");
    s_pitch        = XPLMFindDataRef("sim/flightmodel/position/theta");
    s_roll         = XPLMFindDataRef("sim/flightmodel/position/phi");
    s_iasKt        = XPLMFindDataRef("sim/flightmodel/position/indicated_airspeed");
    s_paltFt       = XPLMFindDataRef("sim/flightmodel2/position/pressure_altitude");
    s_paltFallback = XPLMFindDataRef("sim/flightmodel/misc/h_ind");
    s_turnRate     = XPLMFindDataRef("sim/cockpit2/gauges/indicators/turn_rate_heading_deg_per_sec_pilot");
    s_lateralG     = XPLMFindDataRef("sim/flightmodel/forces/g_side");
    s_verticalG    = XPLMFindDataRef("sim/flightmodel/forces/g_nrml");
    s_vsiFpm       = XPLMFindDataRef("sim/flightmodel/position/vh_ind_fpm");
    s_oatC         = XPLMFindDataRef("sim/cockpit2/temperature/outside_air_temp_degc");
    s_vpath        = XPLMFindDataRef("sim/flightmodel/position/vpath");
    s_flapRatio    = XPLMFindDataRef("sim/cockpit2/controls/flap_handle_deploy_ratio");
    s_onGroundAny  = XPLMFindDataRef("sim/flightmodel/failures/onground_any");

    auto warnIfMissing = [](XPLMDataRef ref, const char* name) {
        if (!ref) {
            char buf[200];
            std::snprintf(buf, sizeof(buf),
                          "FlyOnSpeed: indexer dataref missing: %s\n", name);
            XPLMDebugString(buf);
        }
    };
    warnIfMissing(s_alpha,       "sim/flightmodel/position/alpha");
    warnIfMissing(s_pitch,       "sim/flightmodel/position/theta");
    warnIfMissing(s_roll,        "sim/flightmodel/position/phi");
    warnIfMissing(s_iasKt,       "sim/flightmodel/position/indicated_airspeed");
    warnIfMissing(s_turnRate,    "sim/cockpit2/gauges/indicators/turn_rate_heading_deg_per_sec_pilot");
    warnIfMissing(s_lateralG,    "sim/flightmodel/forces/g_side");
    warnIfMissing(s_verticalG,   "sim/flightmodel/forces/g_nrml");
    warnIfMissing(s_vsiFpm,      "sim/flightmodel/position/vh_ind_fpm");
    warnIfMissing(s_vpath,       "sim/flightmodel/position/vpath");
    warnIfMissing(s_onGroundAny, "sim/flightmodel/failures/onground_any");
    if (!s_paltFt && !s_paltFallback) {
        XPLMDebugString("FlyOnSpeed: indexer no altitude dataref found\n");
    }
}

onspeed::proto::DisplayBuildInputs BuildInputsFromDatarefs()
{
    onspeed::proto::DisplayBuildInputs in{};

    const float aoa     = SafeGetf(s_alpha);
    const float ias     = SafeGetf(s_iasKt);
    const float palt    = s_paltFt ? XPLMGetDataf(s_paltFt)
                       : SafeGetf(s_paltFallback);
    const float vsiFpm  = SafeGetf(s_vsiFpm);
    const float vG      = SafeGetf(s_verticalG, 1.0f);
    const float lateral = SafeGetf(s_lateralG);
    const float flapRatio = SafeGetf(s_flapRatio);

    in.pitchDeg          = SafeGetf(s_pitch);
    in.rollDeg           = SafeGetf(s_roll);
    in.iasKt             = ias;
    in.paltFt            = palt;
    in.turnRateDps       = SafeGetf(s_turnRate);

    // BODY-FRAME convention at v4.23: positive = airframe accel right.
    // X-Plane's g_side is already body-frame, so pass through directly
    // (see DisplaySerial.h's DisplayBuildInputs::lateralG block and
    // LATERAL_G_CONVENTION.md).  The M5 ball renderer downstream
    // negates locally for ball-frame display.
    in.lateralG          = lateral;

    // verticalGScaled10 is in tenths × 10, stored as float for the
    // wire-format helper to consume.  Round to nearest tenth.
    in.verticalGScaled10 = std::round(vG * 10.0f);

    // Operational gate: only emit IAS if above the plugin's mute
    // threshold (mirrors the firmware's iMuteAudioUnderIAS behavior).
    const bool iasValid = (iMuteAudioUnderIAS == 0) || (ias >= iMuteAudioUnderIAS);

    // On-ground: percent-lift derives from V² instead of body angle.
    // See FillPercentLift's contract for why; the short version is
    // that body angle isn't aerodynamically meaningful when the gear
    // is loading the airframe, but V² describes wing effort directly
    // (max effort at low V, dropping off as V exceeds Vs).
    //
    // Read the shared g_DebouncedOnGround value the audio path writes
    // earlier in the same flight-loop tick (CheckAOAAndPlayTone runs
    // before this Tick).  Sharing the debounced result rather than
    // running a second debouncer here means audio and indicator can
    // never disagree on the V² regime due to a single-tick flicker:
    // they both consume the same bool.
    const bool onGround = g_DebouncedOnGround;

    FillPercentLift(in, aoa, ias, flapRatio, iasValid, onGround);

    in.vsiFpm10        = static_cast<int>(std::floor(vsiFpm / 10.0f));
    if (in.vsiFpm10 >  999) in.vsiFpm10 =  999;
    if (in.vsiFpm10 < -999) in.vsiFpm10 = -999;

    in.oatC            = static_cast<int>(SafeGetf(s_oatC));
    in.flightPathDeg   = SafeGetf(s_vpath);

    // Flap position in degrees.  X-Plane's flap_handle_deploy_ratio
    // is 0..1; multiply by a sensible max.  Constant 30° for v1 per
    // spec; issue #393 will derive from acf_flap_dn[].
    constexpr int kFlapsMaxDeg = 30;
    in.flapsDeg        = static_cast<int>(std::round(flapRatio * kFlapsMaxDeg));
    in.flapsMinDeg     = 0;
    in.flapsMaxDeg     = kFlapsMaxDeg;

    // G onset rate: low-pass-filtered first derivative of vertical-G,
    // matching the firmware's AHRS::Update path that feeds the wire
    // (sketch_common/src/tasks/AHRS.cpp lines 207-213).
    //
    // Two-stage filter:
    //   1. EMA pre-smooth on raw vG.  X-Plane's `g_nrml` dataref is
    //      noisy (±0.05–0.2 g per-sample even in level flight); the
    //      firmware avoids this by feeding `AccelVertFilter.get()`
    //      (an EMA on the raw IMU accelerometer) into GOnsetFilter,
    //      not the raw signal.  Without pre-smoothing here, the per-
    //      sample noise dominates the difference quotient and the
    //      derivative output rounds to wire-zero (1 LSB = 0.01 g/s).
    //      Alpha 0.33 ≈ 150 ms tau at 20 Hz Tick — balances noise
    //      rejection against responsiveness to real maneuvers.
    //   2. GOnsetFilter (250 ms tau by default) on the smoothed vG.
    //
    // dt comes from XPLMGetElapsedTime so the rate tracks whatever
    // cadence Tick() actually runs at (throttled to ~20 Hz today).
    // After a long Tick gap (indexer hidden, X-Plane paused), reset
    // both filters so the first post-resume sample doesn't compute
    // a derivative against minutes-old G.
    static onspeed::EMAFilter    s_vGSmoother{0.33f};
    static onspeed::GOnsetFilter s_gOnsetFilter;
    static float                 s_lastTickSec = -1.0f;
    const float now    = XPLMGetElapsedTime();
    const float dtSec  = (s_lastTickSec < 0.0f) ? 0.0f : (now - s_lastTickSec);
    if (dtSec > 1.0f) {
        s_vGSmoother.reset();
        s_gOnsetFilter.Reset();
    }
    s_lastTickSec      = now;
    const float vGSmoothed = s_vGSmoother.update(vG);
    in.gOnsetRate      = (dtSec > 0.0f && dtSec <= 1.0f)
                             ? s_gOnsetFilter.Update(vGSmoothed, dtSec)
                             : 0.0f;

    in.spinRecoveryCue = 0;        // reserved
    in.dataMark        = 0;        // v1 placeholder

    return in;
}

}  // namespace onspeed_xplane::indexer
