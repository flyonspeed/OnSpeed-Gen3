// DataRefAdapter.cpp — see DataRefAdapter.h.

#include "DataRefAdapter.h"

#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"

#include <aoa/PercentLift.h>
#include <config/OnSpeedConfig.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

// Plugin-side AOA threshold globals.  Defined in aoa_audio.cpp; the
// indexer reads them to derive percent-lift band edges (the visual
// UI of the indexer chevron + donut + pip).
extern float fLDMAXAOA;
extern float fONSPEEDFASTAOA;
extern float fONSPEEDSLOWAOA;
extern float fSTALLWARNAOA;
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

// ------------------------------------------------------------------
// alpha_0 / alpha_stall approximation per spec.  Replaced by issue #392.
// ------------------------------------------------------------------
constexpr float kAlpha0Approx = -2.0f;          // body-angle convention
inline float AlphaStallApprox() {
    return fSTALLWARNAOA * 1.075f;              // StallWarn ≈ 93% of stall
}

// Build a stack-local SuFlaps just for ComputePercentLift.  Avoids
// pulling the entire OnSpeedConfig parser into the plugin.
::onspeed::config::OnSpeedConfig::SuFlaps MakeFlapCfg()
{
    ::onspeed::config::OnSpeedConfig::SuFlaps f{};
    f.fAlpha0         = kAlpha0Approx;
    f.fAlphaStall     = AlphaStallApprox();
    f.fLDMAXAOA       = fLDMAXAOA;
    f.fONSPEEDFASTAOA = fONSPEEDFASTAOA;
    f.fONSPEEDSLOWAOA = fONSPEEDSLOWAOA;
    f.fSTALLWARNAOA   = fSTALLWARNAOA;
    return f;
}

inline float SafeGetf(XPLMDataRef ref, float fallback = 0.0f)
{
    return ref ? XPLMGetDataf(ref) : fallback;
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

    // Percent-lift derivation.  Uses the plugin's four AOA setpoints
    // plus alpha_0/alpha_stall approximations from MakeFlapCfg.
    // Wire scale at v4.23 is tenths-of-a-percent (0..999) for the live
    // AOA reading; band-edge anchors stay integer percent.
    const auto flap = MakeFlapCfg();
    in.percentLift        = onspeed::aoa::ComputePercentLiftTenths(aoa, flap, iasValid);
    in.tonesOnPctLift     = onspeed::aoa::ComputePercentLift(fLDMAXAOA, flap, iasValid);
    in.onSpeedFastPctLift = onspeed::aoa::ComputePercentLift(fONSPEEDFASTAOA, flap, iasValid);
    in.onSpeedSlowPctLift = onspeed::aoa::ComputePercentLift(fONSPEEDSLOWAOA, flap, iasValid);
    in.stallWarnPctLift   = onspeed::aoa::ComputePercentLift(fSTALLWARNAOA,   flap, iasValid);
    in.pipPctLift         = in.tonesOnPctLift;     // v1: same as tones-on; #392 derives properly

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

    in.gOnsetRate      = 0.0f;     // v1 placeholder per spec
    in.spinRecoveryCue = 0;        // reserved
    in.dataMark        = 0;        // v1 placeholder

    return in;
}

}  // namespace onspeed_xplane::indexer
