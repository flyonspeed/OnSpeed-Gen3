// wireBridgeForTest.js — local helper used by test_replay_wire_completeness.js
//
// Maps a ReplayStepResult + a config object + a snapshot of cached state
// (anchors / flap min/max) into a `DisplayBuildInputs`-shaped object suitable
// for `Core.build_display_frame()`.
//
// PR 1.5 lives here (in the test directory) by design: the wireBridge is a
// PR-2 deliverable for `tools/web/lib/replay/`. This helper exists to drive
// the wire-completeness test without coupling it to PR 2's filesystem
// layout. PR 2's `wireBridge.js` will:
//   - Subsume this helper's logic.
//   - Use cached anchors (computed once per (flapsIndex, flapsRawAdc)
//     state, not per row) for performance.
//   - Source `flapsMinDeg` / `flapsMaxDeg` from cfg directly.
//
// When PR 2 lands, delete this file and update the test to import the
// production wireBridge.

'use strict';

// Computes percentLift (whole percent, 0..99.9) from body-angle AOA and
// per-flap alpha_0/alpha_stall. Mirrors `onspeed_core/aoa/PercentLift.cpp`.
//
// Pre-flap-state snapshot is held by the caller (the test); we read it
// here so the bridge stays a pure function of (engine output, cfg).
function computePercentLift(aoaDeg, alpha0, alphaStall, iasValid) {
  if (!iasValid) return 0.0;
  if (alphaStall <= 0.0 || alpha0 >= alphaStall) return 0.0;
  const span = alphaStall - alpha0;
  let pct = ((aoaDeg - alpha0) / span) * 100.0;
  if (pct < 0.0) pct = 0.0;
  if (pct > 99.9) pct = 99.9;
  return pct;
}

// Build DisplayBuildInputs from one ReplayStepResult + a cfg.aFlaps[] array.
//
// `flapsArray` is the cfg.aFlaps[] shape used by parse_config:
//   [{ degrees, alpha0, alphaStall, ldmaxAoa, onSpeedFastAoa, onSpeedSlowAoa,
//      stallWarnAoa, ... }, ...]
//
// The active entry is selected by `stepResult.flapsIndex` (engine-populated).
// This is the field the audit table classifies as the carrier for `flapsDeg`
// and all per-flap anchor lookups: the engine resolves the index from
// row.flapsPos, the bridge follows `cfg.aFlaps[flapsIndex]` everywhere
// downstream. A sabotage that corrupts `out.flapsIndex` therefore shifts
// flapsDeg + every anchor on the same frame.
//
// `flapsMinDeg` / `flapsMaxDeg` are pre-computed from min/max of the
// cfg.aFlaps[].degrees values; passed in to keep this helper pure.
//
// Anchor pcts are computed via `computePercentLift` against the active
// flap's alpha_0/alpha_stall. PR 2's wireBridge.js will instead call
// the WASM `compute_anchors()` which handles the multi-detent
// interpolation (bracket walk for `pipPctLift`, etc.).
//
// For the wire-completeness test, this simplified per-flap evaluation is
// sufficient — the goal is to assert byte-for-byte match against a golden,
// not to verify multi-detent anchor logic (covered by
// `test_display_pct_anchors`).
function buildDisplayInputs(stepResult, flapsArray, flapsMinDeg, flapsMaxDeg) {
  const r = stepResult;
  const iasValid = r.iasValid !== false;

  // Select the active flap by the engine's resolved index. Out-of-range
  // index falls back to the first entry (matches engine ResolveFlapIndex_
  // when no detent matches).
  let idx = (typeof r.flapsIndex === 'number') ? r.flapsIndex : 0;
  if (idx < 0 || idx >= flapsArray.length) idx = 0;
  const flap = flapsArray[idx];

  const percentLiftPct = computePercentLift(
    r.aoaDeg, flap.alpha0, flap.alphaStall, iasValid);

  const tonesOnPctLift     = Math.round(computePercentLift(
    flap.ldmaxAoa,        flap.alpha0, flap.alphaStall, true));
  const onSpeedFastPctLift = Math.round(computePercentLift(
    flap.onSpeedFastAoa,  flap.alpha0, flap.alphaStall, true));
  const onSpeedSlowPctLift = Math.round(computePercentLift(
    flap.onSpeedSlowAoa,  flap.alpha0, flap.alphaStall, true));
  const stallWarnPctLift   = Math.round(computePercentLift(
    flap.stallWarnAoa,    flap.alpha0, flap.alphaStall, true));

  // pip uses the active-flap tonesOn position. Multi-detent interpolation
  // (sliding pip between detents during a flap transition) is PR 2's job
  // — the wire-completeness test verifies the byte-for-byte contract for
  // discrete detent frames, not the interpolation curve.
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
    // Engine emits kalmanVSI in m/s (per ReplayStepResult contract).
    // build_display_frame expects raw fpm via "vsiFpm" and applies the
    // /10 floor. Convert m/s -> fpm via the same factor BuildDisplayFrame
    // would have applied if fed by the firmware producer
    // (see DisplaySerial.cpp::BuildDisplayFrame, which calls
    // mps2fpm(KalmanVSI) before /10).
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
    spinRecoveryCue:     0,                  // reserved; constant by design
    dataMark:            r.dataMark,
    pipPctLift:          pipPctLift,
  };
}

module.exports = { computePercentLift, buildDisplayInputs };
