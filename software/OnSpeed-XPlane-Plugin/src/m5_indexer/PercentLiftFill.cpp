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
#include <limits>

// Plugin-side AOA threshold globals.  Defined in aoa_audio.cpp; the
// indexer reads them to derive percent-lift band edges (the visual
// UI of the indexer chevron + donut + pip).
extern float fLDMAXAOA;
extern float fONSPEEDFASTAOA;
extern float fONSPEEDSLOWAOA;
extern float fSTALLWARNAOA;

// 1G clean stall speed (KIAS), used by the on-ground V² formula.
// Owned by aoa_audio.cpp; seeded from acf_Vs and pilot-editable.
// Sentinels (0 and -1) both disable the V² path and fall back to
// the alpha formula — see aoa_audio.cpp for the difference.
extern int   iVs1G;

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

// On-ground percent lift from V², scaled to align with the
// alpha-based percent scale.  See FillPercentLift's contract.
//
//   pct_v² = (Vs / V)² * stallWarnPct
//
// Anchor choice (StallWarn vs literal stall — design decision):
// physically, V == Vs corresponds to alpha == alpha_stall, which
// on the alpha scale reads ~99.9% (the top of the band).  Scaling
// the V² formula by stallWarnPct (~93%) instead means V == Vs
// reads ~93% — the StallWarn pip — not ~100%.  The alpha and V²
// scales disagree at the stall anchor by ~7 percentage points.
//
// We accept that disagreement deliberately:
//   * The takeoff-roll regime (V well below Vs) is the dominant
//     view.  Saturating to stallWarnPct vs 100% is invisible there
//     because pct is already clamped to 99.9.
//   * Aligning at the StallWarn pip — where the chevron transitions
//     into the warning region — gives a clean visual handoff at
//     the regime boundary.  Aligning at 100% would put the V²
//     chevron above the StallWarn pip whenever V <= Vs (including
//     a stationary airplane), which crowds the pip with noise.
//   * The brief V≈Vs window during late roll-out is the only place
//     this matters, and a pilot reading "93%" at V=Vs gets the
//     correct cue ("you're at the warning, rotate now"); the
//     regime then flips to alpha as the gear unloads, where the
//     reading becomes the honest alpha-based percent.
//
// The pilot's defense against the 7-point compression is the
// alpha formula: as soon as `onground_any` flips false (rotation,
// liftoff), the indicator switches to the alpha reading which is
// anchored at literal stall.
//
// Returns NaN when iVs1G or V are non-positive — caller falls
// back to the alpha-based formula in that case.
static float ComputeIasPercentLift(float liveIasKt,
                                   float stallWarnPct)
{
    if (iVs1G <= 0) {
        // Both 0 ("auto, no acf_Vs available") and -1 ("pilot
        // disabled") trip this branch — caller falls back to the
        // alpha-based formula, which is the right behavior for
        // both sentinels.
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (liveIasKt < 0.5f) {
        // V → 0 makes (Vs/V)² blow up; saturate to "max wing
        // effort" instead of dividing by zero.  0.5 kt is a
        // generous threshold matching X-Plane's IAS reading
        // when the airplane is at rest with engine running
        // (sometimes shows ~0.1 kt prop wash).
        return stallWarnPct;
    }
    const float vs    = static_cast<float>(iVs1G);
    const float ratio = vs / liveIasKt;
    float pct = ratio * ratio * stallWarnPct;
    if (pct < 0.0f)  pct = 0.0f;
    if (pct > 99.9f) pct = 99.9f;
    return pct;
}

// Synthesize a wing-AOA value the audio path can compare against the
// f*AOA thresholds, derived from the V² percent rather than X-Plane's
// `sim/flightmodel/position/alpha`.
//
// Why: on the ground at IAS=50 with weight on the gear, X-Plane's
// `alpha` reports the geometric wing-to-relative-wind angle, which is
// well below stall (typically 4-7 degrees for a tail-low rolling
// attitude).  The indicator's V² formula correctly reads ~99% (wing
// would be at max effort if flying), but the audio path comparing
// raw alpha against fLDMAXAOA reads "below LDmax" → no tone.  The
// indicator and audio cues disagree.
//
// Fix: when the V² path is active for the indicator (onGround &&
// iasValid && iVs1G > 0), drive the audio path off the same V²
// percent.  Convert percent back to a wing-AOA value via the inverse
// of ComputePercentLift's formula:
//
//   pct = (alpha - alpha_0) / (alpha_stall - alpha_0) * 100
//   alpha = alpha_0 + (pct/100) * (alpha_stall - alpha_0)
//
// where alpha_0 / alpha_stall are MakeFlapCfg's synthetic anchors (the
// same ones FillPercentLift uses).  Feed the synthesized AOA to
// PlayAOATone, which compares against fLDMAXAOA / fONSPEEDFAST /
// fONSPEEDSLOW / fSTALLWARN — and now produces tones consistent with
// what the indicator shows.
//
// Returns NaN when V² mode is not active for any reason — caller
// falls back to the raw alpha dataref reading.
float MaybeSynthesizeAoaFromVSquared(float liveIasKt,
                                     bool  iasValid,
                                     bool  onGround)
{
    if (!(onGround && iasValid)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto flap = MakeFlapCfg();
    const float stallWarnPct = onspeed::aoa::ComputePercentLift(
        fSTALLWARNAOA, flap, true);
    const float pct = ComputeIasPercentLift(liveIasKt, stallWarnPct);
    if (!std::isfinite(pct)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const float alpha0     = kAlpha0Approx;
    const float alphaStall = AlphaStallApprox();
    return alpha0 + (pct / 100.0f) * (alphaStall - alpha0);
}

void FillPercentLift(onspeed::proto::DisplayBuildInputs& in,
                     float liveAoaDeg,
                     float liveIasKt,
                     float flapHandleRatio,
                     bool  iasValid,
                     bool  onGround)
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
    in.tonesOnPctLift     = static_cast<int>(onspeed::aoa::ComputePercentLift(fLDMAXAOA,       flap, true));
    in.onSpeedFastPctLift = static_cast<int>(onspeed::aoa::ComputePercentLift(fONSPEEDFASTAOA, flap, true));
    in.onSpeedSlowPctLift = static_cast<int>(onspeed::aoa::ComputePercentLift(fONSPEEDSLOWAOA, flap, true));
    in.stallWarnPctLift   = static_cast<int>(onspeed::aoa::ComputePercentLift(fSTALLWARNAOA,   flap, true));

    // Live percent: V²-based on the ground above the IAS-mute floor,
    // alpha-based in the air, 0 below the IAS-mute floor (in either
    // ground state — matches firmware's display gate so taxi below
    // the user's configured mute IAS shows nothing rather than a
    // saturated chevron).
    //
    // The V² path uses the StallWarn anchor as its scaling factor
    // so V == Vs lands right at the StallWarn pip — visually
    // consistent with where the alpha formula puts an aircraft at
    // alpha_stall in level flight.
    //
    // Why the iasValid gate is on V² too:
    //
    // The firmware's DisplaySerial.cpp:252 computes percent via
    // ComputePercentLift(g_Sensors.AOA, flap, bIasValidForOutput)
    // where bIasValidForOutput = (IAS >= iMuteAudioUnderIAS).
    // Below the user's mute floor, the firmware's percent is 0 and
    // the M5 indexer chevron sits at the bottom of the band — no
    // saturated reading during taxi.  The pilot has explicitly
    // configured "below this IAS, don't tell me anything"; the
    // plugin honors that same setting for the V² path.
    //
    // A pilot taxiing at 10-20 kt sees an unlit indexer in the real
    // airplane (firmware sends 0).  Without this gate, the X-Plane
    // plugin would show a fully-saturated chevron during taxi —
    // visually distinct from the real airplane and contrary to the
    // pilot's "mute under X kt" UX choice.  With the gate, taxi
    // shows nothing (matching firmware), and the V² formula kicks
    // in only once the airplane crosses the user's mute floor —
    // which happens during the takeoff roll and continues smoothly
    // through Vr into the alpha regime after liftoff.
    float livePct = std::numeric_limits<float>::quiet_NaN();
    if (onGround && iasValid) {
        livePct = ComputeIasPercentLift(
            liveIasKt, static_cast<float>(in.stallWarnPctLift));
    }
    if (!std::isfinite(livePct)) {
        // In flight, or on-ground with no Vs configured (iVs1G == 0
        // "auto-no-data" or iVs1G == -1 "disabled"), or on-ground
        // below the mute floor.  Use the firmware-shared alpha
        // formula.  Same gate the firmware applies: iasValid false
        // returns 0.
        livePct = onspeed::aoa::ComputePercentLift(liveAoaDeg, flap, iasValid);
    }
    in.percentLiftPct = livePct;

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

// Pure-function on_ground debounce.  See DataRefAdapter.h for
// rationale.  Held in this TU (rather than DataRefAdapter.cpp)
// because the unit test in tests/iasground_pct.cpp links
// PercentLiftFill.cpp directly without XPLM stubs, and we want
// debounce coverage in the same harness as the V² formula.
bool DebounceOnGround(bool rawOnGround, OnGroundDebounceState& state)
{
    if (rawOnGround == state.debounced) {
        // No change pending; reset the counter so a transient
        // toward the same value as `debounced` doesn't leave a
        // stale partial count behind.
        state.pending       = state.debounced;
        state.pendingFrames = 0;
        return state.debounced;
    }

    if (rawOnGround == state.pending) {
        // Already counting toward this value — extend the run.
        ++state.pendingFrames;
    } else {
        // Direction reversed; restart the count toward the new
        // value.  First tick observed counts as 1.
        state.pending       = rawOnGround;
        state.pendingFrames = 1;
    }

    if (state.pendingFrames >= kOnGroundHoldFrames) {
        state.debounced     = rawOnGround;
        state.pendingFrames = 0;
    }
    return state.debounced;
}

}  // namespace onspeed_xplane::indexer
