// wireBridge.js — production bridge from LogReplayEngine output to
// `BuildDisplayFrame` wire bytes.
//
// PR 2 of Project B2. Replaces `software/OnSpeed-M5-Display/test/
// wireBridgeForTest.js` once the wire-completeness test is updated to
// import from here.
//
// Pipeline:
//
//   ReplayStepResult (from LogReplayEngine.step) +
//   parsed cfg (from parseConfigXml)
//      → DisplayBuildInputs (this module)
//      → 77-byte v4.23 wire frame (Core.build_display_frame)
//
// The bridge is a pure function of (engine output, cfg). Holds no state.
// Anchor positions are derived from the active flap entry's alpha_0 /
// alpha_stall via PercentLift's `(BodyAngle − alpha_0) / (alpha_stall
// − alpha_0)` formula — see CLAUDE.md §"OnSpeed measures body angle,
// not wing AOA". The active entry is selected by the engine's
// `flapsIndex` (which the engine itself computed from the row's
// flapsPos / flapsRawAdc).
//
// Anchor positions come straight from `cfg.flaps[index]` thresholds
// (the parse_config output shape). The PR-1.5 wireBridgeForTest
// pre-computes them per row by calling `computePercentLift` four
// times; we keep the same approach here for compatibility with the
// wire-completeness golden, and rely on the caller to memoize across
// frames if the per-row math cost matters. (At ~50 ns per JS call ×
// 4 anchors × 60 Hz = 12 µs/sec, it doesn't.)
//
// Why this is its own module rather than logic in m5sim.js: the m5sim
// only knows how to consume wire bytes. The "produce wire bytes from
// log row + cfg" step is shared by replay UI, the wire-completeness
// test, and any future analysis tool that runs the M5 firmware as its
// state engine. Keeping it free of m5sim plumbing makes it reusable.

'use strict';

// Compute percent-of-stall (0..99.9) from body-angle AOA.
//
// Mirrors `software/Libraries/onspeed_core/src/aoa/PercentLift.cpp::
// ComputePercentLift`. The `(aoa - alpha0) / (alphaStall - alpha0)`
// formula is the body-angle convention; alpha0 is typically negative
// (e.g. -3°) since most aircraft mount the wing with positive
// incidence relative to the fuselage. See CLAUDE.md.
//
// Returns 0 when iasValid is false (matches firmware-side gating); the
// firmware's `IasAlive` hysteresis lives upstream in the engine — by
// the time we see a step result, `iasValid` already reflects the
// hysteretic state.
//
// This duplicates onspeed_core's C++ ComputePercentLift in JS rather
// than calling through WASM because it's invoked four times per row
// (one per anchor) and the WASM call overhead would dominate the math
// cost. The JS implementation is a pinned mirror of the C++; the
// wire-completeness golden test catches drift between them.
function computePercentLift(aoaDeg, alpha0, alphaStall, iasValid) {
  if (!iasValid) return 0.0;
  if (alphaStall <= 0.0 || alpha0 >= alphaStall) return 0.0;
  const span = alphaStall - alpha0;
  let pct = ((aoaDeg - alpha0) / span) * 100.0;
  if (pct < 0.0) pct = 0.0;
  if (pct > 99.9) pct = 99.9;
  return pct;
}

// Build the DisplayBuildInputs object Core.build_display_frame expects.
//
// stepResult: a ReplayStepResult from LogReplayEngine.step(row).
// cfg:        a parsed config from parseConfigXml(text).
//
// Returns a plain JS object suitable for passing to
// Core.build_display_frame(inputs).
//
// Caller is responsible for memoizing the cfg-derived flapsMin/flapsMax
// across rows if the linear scan is a hotspot — for OnSpeed configs
// the flaps[] array is at most a few entries, so the inline min/max
// computation is cheap.
//
// (Declared as `function` then exported separately to keep
// build_web_bundle.py's regex grammar happy — it doesn't recognize
// `export function buildDisplayInputs(...)` inline.)
function buildDisplayInputs(stepResult, cfg) {
  const r = stepResult;
  const flapsArray = cfg && cfg.flaps;
  if (!flapsArray || flapsArray.length === 0) {
    throw new Error(
      'wireBridge: cfg.flaps is empty — config XML must include at least one flap detent');
  }

  // flapsMin / flapsMax: scan the cfg's flap detents. Cheap enough at
  // OnSpeed scales (≤8 detents typically).
  let flapsMinDeg = flapsArray[0].degrees;
  let flapsMaxDeg = flapsArray[0].degrees;
  for (let i = 1; i < flapsArray.length; i++) {
    const d = flapsArray[i].degrees;
    if (d < flapsMinDeg) flapsMinDeg = d;
    if (d > flapsMaxDeg) flapsMaxDeg = d;
  }

  // Active flap entry. Engine-resolved index; out-of-range falls back
  // to the first detent (matches engine's ResolveFlapIndex_ when no
  // detent matches).
  let idx = (typeof r.flapsIndex === 'number') ? r.flapsIndex : 0;
  if (idx < 0 || idx >= flapsArray.length) idx = 0;
  const flap = flapsArray[idx];

  const iasValid = r.iasValid !== false;

  const percentLiftPct = computePercentLift(
    r.aoaDeg, flap.alpha0, flap.alphaStall, iasValid);

  // Per-flap anchor percents. Always computed against the active
  // flap's alpha_0/alpha_stall — pip interpolation across detents
  // during a flap transition is not modelled here (it's a refinement
  // PR 3+ can address, and the wire-completeness test pins the
  // current discrete-detent contract).
  const tonesOnPctLift     = Math.round(computePercentLift(
    flap.ldmaxAoa,        flap.alpha0, flap.alphaStall, true));
  const onSpeedFastPctLift = Math.round(computePercentLift(
    flap.onSpeedFastAoa,  flap.alpha0, flap.alphaStall, true));
  const onSpeedSlowPctLift = Math.round(computePercentLift(
    flap.onSpeedSlowAoa,  flap.alpha0, flap.alphaStall, true));
  const stallWarnPctLift   = Math.round(computePercentLift(
    flap.stallWarnAoa,    flap.alpha0, flap.alphaStall, true));

  // pipPctLift uses the active-flap tonesOn position. Multi-detent
  // interpolation between detents during a flap transition is a
  // refinement; the wire-completeness test pins the discrete-detent
  // contract.
  const pipPctLift = tonesOnPctLift;

  return {
    pitchDeg:            r.pitchDeg,
    rollDeg:             r.rollDeg,
    iasKt:               r.iasKt,
    iasValid:            iasValid,
    paltFt:              r.paltFt,
    turnRateDps:         r.turnRateDps,
    lateralG:            r.accelLatSmoothed,
    verticalG:           r.accelVertSmoothed,
    percentLiftPct:      percentLiftPct,
    // Engine emits kalmanVSI in m/s; build_display_frame's `vsiFpm`
    // input expects fpm. Convert here so the bridge's output mirrors
    // exactly what the firmware producer would emit.
    vsiFpm:              r.kalmanVsiMps * 196.85,
    oatC:                r.oatC,
    flightPathDeg:       r.flightPathDeg,
    flapsDeg:            flap.degrees,
    tonesOnPctLift:      tonesOnPctLift,
    onSpeedFastPctLift:  onSpeedFastPctLift,
    onSpeedSlowPctLift:  onSpeedSlowPctLift,
    stallWarnPctLift:    stallWarnPctLift,
    flapsMinDeg:         flapsMinDeg,
    flapsMaxDeg:         flapsMaxDeg,
    gOnsetRate:          r.gOnsetRate,
    spinRecoveryCue:     0,                   // reserved; constant by design
    dataMark:            r.dataMark,
    pipPctLift:          pipPctLift,
  };
}

// Build a wire frame: stepResult + cfg + WASM core module → 77-byte
// Uint8Array.
//
// coreModule: the loaded onspeed_core WASM module (from getWasmCore()
// in wasm_core.js, or the Node-side loaded module). Must expose
// `build_display_frame`.
//
// Returns a Uint8Array of length 77 (v4.23 wire format).
function buildWireFrame(stepResult, cfg, coreModule) {
  if (!coreModule || typeof coreModule.build_display_frame !== 'function') {
    throw new Error(
      'wireBridge: coreModule.build_display_frame is not available — ' +
      'rebuild via software/Libraries/onspeed_core/wasm/build_wasm.sh');
  }
  const inputs = buildDisplayInputs(stepResult, cfg);
  return coreModule.build_display_frame(inputs);
}

export { buildDisplayInputs, buildWireFrame, computePercentLift };
