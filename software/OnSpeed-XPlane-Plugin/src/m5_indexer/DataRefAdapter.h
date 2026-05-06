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

}  // namespace onspeed_xplane::indexer
