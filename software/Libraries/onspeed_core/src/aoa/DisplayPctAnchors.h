// DisplayPctAnchors.h — interpolate display percent anchors between flap detents.
//
// Used by:
//   - DisplaySerial.cpp     (`#1` wire: `tonesOnPctLift`, `onSpeedFastPctLift`,
//                            `onSpeedSlowPctLift`, `stallWarnPctLift`,
//                            `flapsDeg`)
//   - DataServer.cpp        (parallel WebSocket JSON producer)
//   - any future native consumer of the same percent contract
//
// Why interpolate at all
// ----------------------
// Each configured flap detent has its own per-flap calibration: `fAlpha0`,
// `fAlphaStall`, and the four AOA setpoints (LDmax, OnSpeedFast/Slow,
// StallWarn).  When the lever sits exactly on a detent, the four
// percent anchors are the per-detent setpoints put through
// `ComputePercentLift`.  That is what `Flaps::Update()` already snaps
// `iIndex` to.
//
// While the lever is in motion between two detents, the snapped index
// stays put — the percent anchors driving the M5 indexer would jump in
// a single frame from one detent's values to the next at the midpoint
// crossover.  This causes the L/Dmax pip and the OnSpeed band edges to
// twitch on the display during flap deployment.  Interpolating in
// percent space makes the anchors slide smoothly between adjacent
// detents and lets the pip track the aerodynamic transition.
//
// The audio path is unchanged — audio compares `g_Sensors.AOA` directly
// to `g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA`, which still snaps on
// the midpoint.  Only the *display* anchors interpolate.
//
// Continuity invariant
// --------------------
// At the moment `iIndex` advances from detent a to detent b (lever
// crosses the midpoint), the bracket the lever is in remains the
// (a, b) pair — we don't depend on `iIndex` here, only on the lever
// ADC value relative to each detent's `iPotPosition`.  The interpolated
// percents are continuous through that snap; pinned by
// test_continuity_through_snap.
//
// Endpoint clamping
// -----------------
// Below the lowest configured pot position, return the lowest detent's
// percents.  Above the highest, return the highest's.  No
// extrapolation; consumers see a flat region at each endpoint.

#ifndef ONSPEED_CORE_AOA_DISPLAY_PCT_ANCHORS_H
#define ONSPEED_CORE_AOA_DISPLAY_PCT_ANCHORS_H

#include <cstddef>
#include <cstdint>
#include <config/OnSpeedConfig.h>

namespace onspeed::aoa {

// Interpolated percent anchors for the display indexer.
//
// All four percent fields are 0..99 (saturation convention shared with
// `ComputePercentLift`).  `flapsDeg` is the linearly interpolated lever
// position in the same units the M5 displays — `int` to match the
// existing wire field width, computed by rounding the float
// interpolation result.
struct DisplayPctAnchors {
    int tonesOnPctLift     = 0;
    int onSpeedFastPctLift = 0;
    int onSpeedSlowPctLift = 0;
    int stallWarnPctLift   = 0;
    int flapsDeg           = 0;
};

// Compute interpolated percent anchors for the display from a raw
// flap-lever ADC reading.
//
// The bracket containing `rawAdc` is found by walking adjacent pairs of
// `flapEntries[i].iPotPosition`.  The two bracket endpoints' percents
// are computed via `ComputePercentLift` and linearly interpolated by
// lambda = (rawAdc − pot_a) / (pot_b − pot_a) clamped to [0, 1].
//
// `iasValid` is forwarded to `ComputePercentLift` for the interpolated
// anchors.  The producer always passes `true` here because the indexer
// geometry must stay stable across the audio mute threshold; anchors
// don't gate on live-AOA validity.
//
// Edge cases:
//   - empty `flapEntries`: returns the zero-initialized struct (all
//     four percents 0, `flapsDeg` 0).  Consumers render this as
//     "uncalibrated".
//   - single entry: returns that entry's percents and `iDegrees`.
//   - lever below the lowest configured pot position: returns the
//     lowest detent's percents (clamp, no extrapolation).
//   - lever above the highest: returns the highest detent's percents.
DisplayPctAnchors ComputeDisplayPctAnchors(
    uint16_t rawAdc,
    const ::onspeed::config::OnSpeedConfig::SuFlaps* flapEntries,
    size_t entryCount,
    bool iasValid);

}   // namespace onspeed::aoa

#endif  // ONSPEED_CORE_AOA_DISPLAY_PCT_ANCHORS_H
