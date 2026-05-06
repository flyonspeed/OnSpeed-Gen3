// PercentLiftFill.cpp — pure-function helper for the plugin's
// percent-lift derivation.  Split out of DataRefAdapter.cpp so it can
// be unit-tested without linking XPLM stubs.
//
// Reads the plugin-side AOA threshold globals (defined in aoa_audio.cpp)
// and writes the five percent fields (percentLiftPct + four anchors)
// + pipPctLift on a DisplayBuildInputs.  See DataRefAdapter.h for the
// contract, particularly the iasValid rule.

#include "DataRefAdapter.h"

#include <aoa/PercentLift.h>
#include <config/OnSpeedConfig.h>

#include <algorithm>
#include <cmath>

// Plugin-side AOA threshold globals.  Defined in aoa_audio.cpp; the
// indexer reads them to derive percent-lift band edges (the visual
// UI of the indexer chevron + donut + pip).
extern float fLDMAXAOA;
extern float fONSPEEDFASTAOA;
extern float fONSPEEDSLOWAOA;
extern float fSTALLWARNAOA;

namespace onspeed_xplane::indexer {

namespace {

// alpha_0 / alpha_stall approximation per spec.  Replaced by issue #392.
constexpr float kAlpha0Approx = -2.0f;          // body-angle convention

inline float AlphaStallApprox()
{
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

}  // namespace

void FillPercentLift(onspeed::proto::DisplayBuildInputs& in,
                     float liveAoaDeg,
                     float flapHandleRatio,
                     bool  iasValid)
{
    // Percent-lift derivation.  Uses the plugin's four AOA setpoints
    // plus alpha_0/alpha_stall approximations from MakeFlapCfg.
    // The live AOA reading is whole-percent float (the wire encoder
    // in BuildDisplayFrame scales ×10 to wire-tenths); band-edge
    // anchors are int (snapped per detent, integer-percent resolution).
    //
    // iasValid gates the LIVE reading only.  Band-edge anchors are
    // pure functions of calibration — their position on the percent
    // scale doesn't depend on whether air is moving.  Passing iasValid
    // here would collapse all four anchors (and the pip lerp endpoints
    // derived from them) to 0 below the mute IAS threshold, pinning
    // every visual reference to the bottom of the indexer regardless
    // of the live alpha.  Mirrors the firmware contract pinned in
    // onspeed_core DisplayPctAnchors.h ("the producer always passes
    // true for the band-edge / L/Dmax anchors") and the call sites
    // in sketch_common DisplaySerial.cpp / DataServer.cpp.
    const auto flap = MakeFlapCfg();
    in.percentLiftPct     = onspeed::aoa::ComputePercentLift(liveAoaDeg, flap, iasValid);
    in.tonesOnPctLift     = static_cast<int>(onspeed::aoa::ComputePercentLift(fLDMAXAOA,       flap, true));
    in.onSpeedFastPctLift = static_cast<int>(onspeed::aoa::ComputePercentLift(fONSPEEDFASTAOA, flap, true));
    in.onSpeedSlowPctLift = static_cast<int>(onspeed::aoa::ComputePercentLift(fONSPEEDSLOWAOA, flap, true));
    in.stallWarnPctLift   = static_cast<int>(onspeed::aoa::ComputePercentLift(fSTALLWARNAOA,   flap, true));

    // Visual L/Dmax pip: lerp clean→fullflap by flap-handle ratio,
    // where the fullflap target is the bottom-half-of-donut anchor
    // ((3*fast + slow) / 4).  Mirrors the M5 firmware's per-flap
    // target formula (main.cpp:1058-1062) so the pip slides smoothly
    // as the pilot deploys flaps instead of staying nailed to the
    // clean L/Dmax.
    const float clampedRatio  = std::clamp(flapHandleRatio, 0.0f, 1.0f);
    const float cleanPip      = static_cast<float>(in.tonesOnPctLift);
    const float fullFlapPip   = (3.0f * static_cast<float>(in.onSpeedFastPctLift)
                                 + static_cast<float>(in.onSpeedSlowPctLift)) / 4.0f;
    in.pipPctLift = static_cast<int>(std::round(
        cleanPip * (1.0f - clampedRatio) + fullFlapPip * clampedRatio));
}

}  // namespace onspeed_xplane::indexer
