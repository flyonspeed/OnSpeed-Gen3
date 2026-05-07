// DataRefAdapter — convert X-Plane datarefs into a DisplayBuildInputs
// suitable for onspeed::proto::BuildDisplayFrame.
//
// Most fields are 1:1 dataref reads with unit conversions.  The
// "percent lift" set (tonesOnPctLift / onSpeedFastPctLift /
// onSpeedSlowPctLift / stallWarnPctLift / pipPctLift) is derived
// from the plugin's existing AOA setpoint configuration via
// onspeed_core's ComputePercentLift, using a stack-local SuFlaps
// built from the plugin's four threshold globals plus alpha_0 /
// alpha_stall approximations (see spec section "alpha_0 / alpha_stall
// derivation" — replaced by issue #392).

#pragma once

#include <proto/DisplaySerial.h>

namespace onspeed_xplane::indexer {

// One-time dataref lookup.  Idempotent.  Called from indexer Init().
void InitDataRefs();

// Populate a DisplayBuildInputs from current X-Plane state.
// Returns a fully-populated struct ready for BuildDisplayFrame.
onspeed::proto::DisplayBuildInputs BuildInputsFromDatarefs();

// Pure-function helper: fill the five percent-lift fields
// (percentLiftPct + four anchors) and pipPctLift on `in` from the
// given inputs.  Extracted from BuildInputsFromDatarefs so it can be
// unit-tested without linking XPLM.  Reads the AOA-threshold globals
// (fLDMAXAOA et al.) and the synthetic alpha_0 / alpha_stall via the
// plugin's MakeFlapCfg path; callers of the unit test override the
// globals before invoking, the production path leaves them as the
// pilot configured.
//
// liveAoaDeg is X-Plane's `sim/flightmodel/position/alpha` reading.
// liveIasKt is `sim/flightmodel/position/indicated_airspeed`.
// flapHandleRatio is `sim/cockpit2/controls/flap_handle_deploy_ratio`,
// already clamped to [0, 1].  iasValid is true when IAS is at or
// above iMuteAudioUnderIAS.  (The audio path uses a hysteretic gate;
// this derivation is non-hysteretic, so inside the 5-kt hysteresis
// band audio is muted but iasValid is true.)
// onGround comes from `sim/flightmodel/failures/onground_any` —
// true while any landing gear is touching.
//
// Two regimes for the live percent reading:
//   * In flight (onGround=false): percent comes from the alpha-
//     based formula in onspeed_core ComputePercentLift, identical
//     to the firmware.
//   * On the ground (onGround=true): percent comes from the V²
//     formula `(Vs / V)²`, scaled to land at the StallWarn anchor
//     when V == Vs.  This describes "how loaded the wing is at
//     this airspeed if it had to make weight in 1G level" — which
//     is the right pilot mental model on the takeoff roll.  Body
//     angle isn't a useful AOA proxy on the ground (gear is
//     loading the airframe), so blindly running the alpha formula
//     pre-takeoff produces the wrong reading.
//
// The on-ground formula is gated on the iVs1G global (KIAS, owned
// by aoa_audio.cpp).  If iVs1G == 0 (Vs is unknown — neither acf_Vs
// nor the pilot has supplied a value), the live reading falls back
// to the alpha-based path.  Old behavior is the worst-case
// fallback; new behavior requires data the formula needs.
//
// Important contract (unchanged from the iasValid fix):
//   - The four band-edge anchors (tonesOnPctLift, onSpeedFastPctLift,
//     onSpeedSlowPctLift, stallWarnPctLift) and the pip lerp endpoints
//     derived from them are computed with iasValid=true unconditionally.
//     Anchor positions are pure functions of calibration; collapsing
//     them when air isn't moving pins every visual reference (and the
//     pip) to the bottom of the indexer regardless of live alpha.
//   - Only the live percentLiftPct field gates on the caller's
//     iasValid (when running the alpha path) or onGround (which
//     selects the V² path).
// Mirrors the firmware contract pinned in onspeed_core
// DisplayPctAnchors.h.
void FillPercentLift(onspeed::proto::DisplayBuildInputs& in,
                     float liveAoaDeg,
                     float liveIasKt,
                     float flapHandleRatio,
                     bool  iasValid,
                     bool  onGround);

// Synthesize a wing-AOA value the audio path can compare against the
// f*AOA thresholds, derived from the V² percent rather than X-Plane's
// raw alpha dataref.
//
// On the ground at IAS=50 with weight on the gear, X-Plane's alpha
// reports the geometric wing-to-relative-wind angle, which is well
// below stall (typically 4-7 degrees for a tail-low rolling attitude).
// The indicator's V² formula correctly reads ~99% (wing would be at
// max effort if flying), but the audio path comparing raw alpha
// against fLDMAXAOA reads "below LDmax" → no tone.  Indicator and
// audio cues disagree.
//
// When V² mode is active for the indicator (onGround && iasValid &&
// iVs1G > 0), this function returns a synthesized wing AOA equivalent
// to where the V² percent would land on the alpha scale.  Caller
// feeds this synthesized value into PlayAOATone in place of the raw
// alpha dataref so audio cues track the indicator.
//
// Returns NaN when V² mode is not active — caller should use the raw
// alpha dataref reading instead.
float MaybeSynthesizeAoaFromVSquared(float liveIasKt,
                                     bool  iasValid,
                                     bool  onGround);

// Debounce filter for sim/flightmodel/failures/onground_any.
//
// X-Plane reports "any gear touching" as a single-frame boolean.
// On a rough taxi, gear bounce on landing, or simulator hiccups it
// can flicker for a single tick.  Combined with the regime swap in
// FillPercentLift (alpha vs V²) and the iasValid gate in the alpha
// path, a flicker at low taxi speed could oscillate the live percent
// between near-0% (alpha-path with iasValid=false) and ~99% (V²
// saturated below Vs).
//
// Pattern: the new state must hold for `kHoldFrames` consecutive
// ticks before the debounced output flips.  At a typical X-Plane
// flight-loop rate of 30-60 Hz, kHoldFrames=5 is ~80-160 ms — long
// enough to absorb a one-frame gear bounce, short enough to not be
// perceptible at takeoff/landing.
//
// `state` is the debouncer's persistent storage; the caller owns it.
// Initialize to {.debounced=false, .pending=false, .pendingFrames=0}.
struct OnGroundDebounceState {
    bool debounced     = false;   // current debounced output
    bool pending       = false;   // last raw input observed
    int  pendingFrames = 0;       // consecutive ticks of `pending`
};

// Pure-function debounce: pass current raw `onground_any` value,
// receive debounced output.  Mutates `state` in place.
//
// Held to a constant 5 ticks for now; if X-Plane changes its
// flight-loop rate dramatically we can switch to time-based.
constexpr int kOnGroundHoldFrames = 5;

bool DebounceOnGround(bool rawOnGround, OnGroundDebounceState& state);

}  // namespace onspeed_xplane::indexer
