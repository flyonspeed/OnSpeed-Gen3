// DisplayPctAnchors.h — display percent anchors with operational/aerodynamic split.
//
// Used by:
//   - DisplaySerial.cpp     (`#1` wire: `tonesOnPctLift`, `onSpeedFastPctLift`,
//                            `onSpeedSlowPctLift`, `stallWarnPctLift`,
//                            `flapsDeg`, `pipPctLift`)
//   - DataServer.cpp        (parallel WebSocket JSON producer)
//   - any future native consumer of the same percent contract
//
// Two cues, two rules (Vac, ld_max.pdf §8): "L/Dmax pips are aerodynamic
// references. Fast tone is an operational limit cue. They must remain
// independent."
//
//   * `tonesOnPctLift` (active-detent L/Dmax percent) — SNAPPED to the
//     active detent (`activeIndex`). This is the operational cue: the
//     M5 bottom-chevron gate reads this as the lower edge of the "low
//     tone is playing" band, exactly matching the audio path's gate
//     against `g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA`. Snaps in
//     lockstep with the audio at every detent transition.
//   * `pipPctLift` (visual L/Dmax pip) — INTERPOLATED across the
//     ENTIRE configured pot range, from the cleanest detent's L/Dmax
//     percent to the most-deployed detent's bottom-half-of-donut target
//     ((3*fast + slow) / 4 — one quarter from the fast edge into the
//     OnSpeed band). Slides smoothly as the lever moves; ignores
//     intermediate detents and does NOT depend on the active-detent
//     index. The pip is purely visual; aerodynamically the L/Dmax body
//     angle slides toward the OnSpeed band as flaps deploy, and the pip
//     mirrors that intuition.
//   * `onSpeedFastPctLift` / `onSpeedSlowPctLift` / `stallWarnPctLift`
//     — SNAPPED to the active detent. These set the audio cue
//     thresholds and the donut / chevron screen positions on the
//     indexer; they snap together with the audio path.
//   * `flapsDeg` (numeric flap-angle readout) — INTERPOLATED across
//     the ACTIVE BRACKET (per-bracket lerp of `iDegrees`). The
//     mechanical lever position visits every detent's `iDegrees`
//     exactly when the lever pot equals that detent's `iPotPosition`.
//     Different math from the pip; the pip skips intermediate detents,
//     `flapsDeg` does not.
//
// The audio path is unchanged — audio compares `g_Sensors.AOA` directly
// to `g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA`, which still snaps on
// the midpoint. `tonesOnPctLift` mirrors that snap in percent space so
// the M5 chevron and the audio fire together.
//
// pip / tones-on coincidence
// --------------------------
// `pipPctLift == tonesOnPctLift` exactly at the cleanest detent's pot
// position (when `activeIndex == 0` and `rawAdc == flapEntries[0].iPotPosition`).
// The pip and the chevron edge line up visually only there. They diverge
// during deployment, by design.
//
// Endpoint clamping
// -----------------
// `pipPctLift` lerps with lambda clamped to [0, 1] — out-of-range
// `rawAdc` (above the cleanest pot or below the most-deployed pot)
// pins the pip at the corresponding endpoint.
// `tonesOnPctLift` and the band edges always come from `activeIndex`,
// which is also clamped to [0, entryCount-1]. No extrapolation anywhere.

#ifndef ONSPEED_CORE_AOA_DISPLAY_PCT_ANCHORS_H
#define ONSPEED_CORE_AOA_DISPLAY_PCT_ANCHORS_H

#include <cstddef>
#include <cstdint>
#include <config/OnSpeedConfig.h>

namespace onspeed::aoa {

// Display percent anchors for the indexer.
//
// All five percent fields are 0..99 (saturation convention shared with
// `ComputePercentLift`).  `flapsDeg` is the linearly interpolated lever
// position in the same units the M5 displays.
struct DisplayPctAnchors {
    int pipPctLift         = 0;   // Aerodynamic pip — INTERPOLATED clean→fullflap
    int tonesOnPctLift     = 0;   // Operational L/Dmax — SNAPPED to active detent
    int onSpeedFastPctLift = 0;   // SNAPPED to active detent
    int onSpeedSlowPctLift = 0;   // SNAPPED to active detent
    int stallWarnPctLift   = 0;   // SNAPPED to active detent
    int flapsDeg           = 0;   // INTERPOLATED across active bracket
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
