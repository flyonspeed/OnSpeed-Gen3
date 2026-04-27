// DisplayPctAnchors.h — display percent anchors with interpolated L/Dmax pip.
//
// Used by:
//   - DisplaySerial.cpp     (`#1` wire: `tonesOnPctLift`, `onSpeedFastPctLift`,
//                            `onSpeedSlowPctLift`, `stallWarnPctLift`,
//                            `flapsDeg`)
//   - DataServer.cpp        (parallel WebSocket JSON producer)
//   - any future native consumer of the same percent contract
//
// What interpolates and what snaps
// --------------------------------
// Per Vac's design rule: visual aerodynamic references interpolate;
// operational thresholds snap.
//
//   * `tonesOnPctLift` (the L/Dmax pip) — INTERPOLATED in percent space
//     between the two bracket endpoints' L/Dmax values.  This is the
//     pip on the indexer that the pilot watches as the flap deploys;
//     snapping would make it twitch.
//   * `onSpeedFastPctLift` / `onSpeedSlowPctLift` / `stallWarnPctLift`
//     — SNAPPED to the active detent (`iIndex`).  These set the
//     audio cue thresholds and the donut / chevron screen positions
//     on the indexer.  They snap together with the audio path so the
//     pilot's audio cues and the donut geometry stay in lockstep.
//   * `flapsDeg` (numeric flap-angle readout) — INTERPOLATED.  A
//     mechanical position display, slides smoothly with the lever.
//
// The audio path is unchanged — audio compares `g_Sensors.AOA` directly
// to `g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA`, which still snaps on
// the midpoint.
//
// Continuity invariant for the L/Dmax pip
// ---------------------------------------
// At the moment `iIndex` advances from detent a to detent b (lever
// crosses the midpoint), the L/Dmax bracket flips from (a, b) at
// lambda≈1 to (b, c) at lambda≈0 (or stays in (a, b) at lambda=1 if
// b is the last detent).  Either way the interpolated value equals
// b's L/Dmax exactly at the snap point — same screen position before
// and after the snap.  Pinned by test_continuity_through_snap.
//
// Endpoint clamping
// -----------------
// Below the lowest configured pot position, return the lowest detent's
// percents (all four).  Above the highest, return the highest's.  No
// extrapolation.

#ifndef ONSPEED_CORE_AOA_DISPLAY_PCT_ANCHORS_H
#define ONSPEED_CORE_AOA_DISPLAY_PCT_ANCHORS_H

#include <cstddef>
#include <cstdint>
#include <config/OnSpeedConfig.h>

namespace onspeed::aoa {

// Display percent anchors for the indexer.
//
// All four percent fields are 0..99 (saturation convention shared with
// `ComputePercentLift`).  `flapsDeg` is the linearly interpolated lever
// position in the same units the M5 displays.
struct DisplayPctAnchors {
    int tonesOnPctLift     = 0;   // L/Dmax — INTERPOLATED across bracket
    int onSpeedFastPctLift = 0;   // SNAPPED to active detent
    int onSpeedSlowPctLift = 0;   // SNAPPED to active detent
    int stallWarnPctLift   = 0;   // SNAPPED to active detent
    int flapsDeg           = 0;   // INTERPOLATED across bracket
};

// Compute display percent anchors with interpolated L/Dmax pip and
// snapped band edges.
//
// `flapEntries` / `entryCount` describe the configured flap detents.
// `activeIndex` is the snapped active detent (i.e., `g_Flaps.iIndex`),
// which sources the snapped band-edge anchors and matches what the
// audio path uses.
//
// `rawAdc` is the raw lever-pot ADC reading.  The bracket containing
// `rawAdc` is found by walking adjacent pairs of
// `flapEntries[i].iPotPosition`.  The two bracket endpoints' L/Dmax
// values (and `iDegrees`) are linearly interpolated by
// lambda = (rawAdc − pot_a) / (pot_b − pot_a) clamped to [0, 1].
//
// `iasValid` is forwarded to `ComputePercentLift`.  The producer
// always passes `true` for the band-edge / L/Dmax anchors because the
// indexer geometry must stay stable across the audio mute threshold;
// anchors don't gate on live-AOA validity.
//
// Edge cases:
//   - empty `flapEntries`: returns the zero-initialized struct
//     (all percents 0, `flapsDeg` 0).  Consumers render this as
//     "uncalibrated".
//   - single entry: returns that entry's percents and `iDegrees`.
//   - `activeIndex` out of range: clamped to a valid index; the
//     function never reads outside `flapEntries`.
//   - lever below the lowest configured pot position: returns the
//     lowest detent's percents (no extrapolation, all four snap).
//   - lever above the highest: returns the highest detent's percents.
DisplayPctAnchors ComputeDisplayPctAnchors(
    uint16_t rawAdc,
    const ::onspeed::config::OnSpeedConfig::SuFlaps* flapEntries,
    size_t entryCount,
    size_t activeIndex,
    bool iasValid);

}   // namespace onspeed::aoa

#endif  // ONSPEED_CORE_AOA_DISPLAY_PCT_ANCHORS_H
