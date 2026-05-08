// bindings.cpp — Emscripten embind exports for onspeed_core.
//
// Step 0 (this PR): one export to prove the pipeline.
// Step 1 (next PR): extend with compute_anchors, parse_config, etc.
//
// Each exported function is a thin C-shaped wrapper that constructs
// whatever struct the C++ API requires, calls it, and returns a
// primitive that Emscripten can copy across the WASM boundary without
// requiring a shared memory view on the JS side.
//
// Build: included by wasm/build_wasm.sh via the SOURCES list.
// The --bind flag enables EMSCRIPTEN_BINDINGS.

#include <emscripten/bind.h>

#include <aoa/PercentLift.h>
#include <config/OnSpeedConfig.h>

using namespace emscripten;

// ---------------------------------------------------------------------------
// compute_percent_lift
//
// Maps a body-angle reading (aoaDeg) to percent-of-stall using the
// honest single-linear normalization from onspeed_core/aoa/PercentLift.cpp.
//
// Parameters:
//   aoaDeg      — body angle in degrees (DerivedAOA from the IMU/pressure
//                 fusion, same units as the firmware's fLDMAXAOA etc.)
//   alpha_0     — per-flap zero-lift body angle (typically negative, e.g. -3.7)
//                 stored as fAlpha0 in OnSpeedConfig::SuFlaps
//   alpha_stall — per-flap stall body angle (positive, e.g. 10.3)
//                 stored as fAlphaStall in OnSpeedConfig::SuFlaps
//   stallwarn   — per-flap stall-warning body angle setpoint (fSTALLWARNAOA)
//                 used by the defensive ceiling when alpha_stall is uncalibrated
//   ias_valid   — true when IAS is above the audio mute floor; false on the
//                 ground or at very low speed.  Returns 0.0 when false.
//
// Returns: percent-of-stall in [0.0, 99.9].  See PercentLift.h for the
// full contract including the alpha_0 floor and uncalibrated-stall fallback.
// ---------------------------------------------------------------------------
static float compute_percent_lift(
    float aoaDeg,
    float alpha_0,
    float alpha_stall,
    float stallwarn,
    bool  ias_valid)
{
    ::onspeed::config::OnSpeedConfig::SuFlaps flapCfg;
    flapCfg.fAlpha0      = alpha_0;
    flapCfg.fAlphaStall  = alpha_stall;
    flapCfg.fSTALLWARNAOA = stallwarn;
    // Other SuFlaps fields default to 0.0 from the constructor and are
    // not consulted by ComputePercentLift.

    return ::onspeed::aoa::ComputePercentLift(aoaDeg, flapCfg, ias_valid);
}

EMSCRIPTEN_BINDINGS(onspeed_core_module) {
    // Step 0: single export to prove the pipeline.
    // Step 1 will add compute_anchors, parse_config, etc.
    function("compute_percent_lift", &compute_percent_lift);
}
