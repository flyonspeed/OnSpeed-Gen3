// percentLift.js — thin WASM wrapper for percent-of-stall computation.
//
// Pre-PLAN_WASM_CORE.md Step 1 this file contained a JS port of the C++
// ComputePercentLift and ComputeDisplayPctAnchors algorithms.  It now
// delegates entirely to the WASM build of onspeed_core, so the Replay
// tool and the firmware run the exact same compiled code.
//
// Public API (unchanged from the hand-port — callers don't need to change):
//
//   computePercentLift(aoaDeg, flapCfg, iasValid) -> Promise<float>
//   computeAnchors(allFlaps, activeIndex, rawAdc) -> Promise<object>
//
// The alpha_0 floor convention is handled in C++ (PercentLift.cpp).
// See CLAUDE.md §"OnSpeed measures body angle, not wing AOA" for the math.
//
// flapCfg shape for computePercentLift:
//   { alpha_0, alpha_stall, stallwarn_aoa }   (all floats, degrees)
//
// allFlaps element shape for computeAnchors:
//   { degrees, potPosition, ldmaxAoa, onSpeedFastAoa, onSpeedSlowAoa,
//     stallWarnAoa, alpha0, alphaStall }       (int / float, degrees)

import { getWasmCore } from './wasm_core.js';

// Compute percent-of-stall for a single body-angle reading.
//
// aoaDeg      — body angle in degrees (DerivedAOA)
// flapCfg     — active flap configuration object; must have:
//               .alpha_0, .alpha_stall, .stallwarn_aoa  (all floats, degrees)
// iasValid    — true when IAS is above the mute floor; false on the ground.
//               Returns 0.0 when false (matches firmware behaviour).
//
// Returns the percent-of-stall in [0.0, 99.9] as a Promise<float>.
export async function computePercentLift(aoaDeg, flapCfg, iasValid) {
    const w = await getWasmCore();
    return w.compute_percent_lift(
        aoaDeg,
        flapCfg.alpha_0,
        flapCfg.alpha_stall,
        flapCfg.stallwarn_aoa,
        !!iasValid
    );
}

// Compute display percent anchors for the current flap lever position.
//
// allFlaps    — Array of flap-detent objects, ordered clean→deployed.
//               Each element: { degrees, potPosition, ldmaxAoa,
//                               onSpeedFastAoa, onSpeedSlowAoa,
//                               stallWarnAoa, alpha0, alphaStall }
// activeIndex — integer index of the snapped active detent.
// rawAdc      — raw lever-pot ADC reading (0..4095 range).
//
// Returns a Promise resolving to:
//   { pipPctLift, tonesOnPctLift, onSpeedFastPctLift,
//     onSpeedSlowPctLift, stallWarnPctLift, flapsDeg }
// All fields are integers in [0, 99] except flapsDeg which is the
// interpolated lever angle in degrees.
export async function computeAnchors(allFlaps, activeIndex, rawAdc) {
    const w = await getWasmCore();
    return w.compute_anchors(allFlaps, activeIndex, rawAdc);
}
