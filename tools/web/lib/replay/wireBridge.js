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

// ----------------------------------------------------------------------
// Gen2-style presentation smoothing
// ----------------------------------------------------------------------
//
// Background: 50 Hz SD logs aliased high-frequency IMU vibration into
// the 0-25 Hz band before recording. The firmware's accel-EMA filter
// (running at 208 Hz on Gen3 hardware) was designed against the un-
// aliased 208 Hz IMU data — at 50 Hz log-rate replay, ±0.04 G smoothed-
// residual noise persists, which at the slip-ball gain of 850 produces
// ~30 SVG units of jitter per log row. Visually unwatchable on 50 Hz
// replays.
//
// Gen2 firmware ALWAYS applied a second smoothing pass at the wire-
// output rate, deliberately, before serializing to the M5:
//
//   Software/OnSpeedTeensy_AHRS/DisplaySerial.ino:25
//     LateralGSmoothed = aLatCorr * smoothingAlphaLat
//                       + (1 - smoothingAlphaLat) * LateralGSmoothed;
//
// With Gen2 defaults `serialDisplaySmoothingLat=50`,
// `serialDisplaySmoothingVert=20` (OnSpeedTeensy_AHRS.ino:261-262), and
// the Gen2 wire rate of 10 Hz, this works out to continuous-time time
// constants of 2.50 s for lateral and 0.998 s for vertical.
//
// Vac's flight-replay-on-video work was watched through this layer.
// "It looked fine" because the firmware was deliberately presenting a
// heavily smoothed signal — not the raw post-AHRS value.
//
// Gen3 firmware dropped this layer (because Gen3 IMU runs at 208 Hz so
// the AHRS-side smoothing alone is sufficient on real flight data). For
// 50 Hz LOG REPLAY we need the equivalent compensation since the log
// itself contains aliased noise the live firmware never saw.
//
// The smoother below is applied in wireBridge ONLY — the M5 firmware
// WASM still receives wire bytes in v4.23 format, so the rest of the
// "drift impossible by construction" architecture holds. The smoother
// is opt-in via the caller passing one (or null to skip), so a future
// 208 Hz log replay can disable it entirely and remain bit-for-bit
// faithful to the firmware path.
//
// τ values: matched to Gen2 defaults at the wire rate Gen2 used (10 Hz)
// to give continuous-time τ_lat=2.50s, τ_vert=0.998s. We re-derive the
// α at the actual wire rate the caller uses, by passing the wire rate
// to the smoother constructor.
const GEN2_TAU_LATERAL_S  = 2.50;
const GEN2_TAU_VERTICAL_S = 0.998;

// Gen2-compatible presentation smoother for the wire path. Stateful:
// caller must hold one instance per replay session (NOT per frame).
//
//   const sm = new PresentationSmoother(20.0);  // 20 Hz wire rate
//   const inputs = buildDisplayInputs(stepResult, cfg);
//   sm.apply(inputs);                            // mutates inputs in place
//   const frame = Core.build_display_frame(inputs);
//
// Or pass to buildWireFrame: it'll handle the apply.
//
// Reset on backward scrub / re-init: caller calls smoother.reset() at
// the same time the M5 sim is re-created.
class PresentationSmoother {
  constructor(wireRateHz, tauLatSec, tauVertSec) {
    if (typeof wireRateHz !== 'number' || wireRateHz <= 0) {
      throw new Error('PresentationSmoother: wireRateHz must be > 0');
    }
    const dt = 1.0 / wireRateHz;
    const tauL = (typeof tauLatSec  === 'number' && tauLatSec  > 0)
      ? tauLatSec  : GEN2_TAU_LATERAL_S;
    const tauV = (typeof tauVertSec === 'number' && tauVertSec > 0)
      ? tauVertSec : GEN2_TAU_VERTICAL_S;
    this._alphaLat  = 1.0 - Math.exp(-dt / tauL);
    this._alphaVert = 1.0 - Math.exp(-dt / tauV);
    this._latState  = NaN;
    this._vertState = NaN;
  }
  reset() {
    this._latState  = NaN;
    this._vertState = NaN;
  }
  apply(inputs) {
    const lat  = inputs.lateralG;
    const vert = inputs.verticalG;
    if (Number.isFinite(lat)) {
      this._latState = Number.isNaN(this._latState)
        ? lat
        : this._alphaLat * lat + (1.0 - this._alphaLat) * this._latState;
      inputs.lateralG = this._latState;
    }
    if (Number.isFinite(vert)) {
      this._vertState = Number.isNaN(this._vertState)
        ? vert
        : this._alphaVert * vert + (1.0 - this._alphaVert) * this._vertState;
      inputs.verticalG = this._vertState;
    }
    return inputs;
  }
}

export { PresentationSmoother, GEN2_TAU_LATERAL_S, GEN2_TAU_VERTICAL_S };

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
// presentationSmoother: optional `PresentationSmoother` instance.
// When supplied, applies Gen2-style presentation smoothing to
// lateralG / verticalG before the wire encode (see PresentationSmoother
// at the top of this module for rationale). Caller threads state by
// holding one smoother instance per replay session and resetting on
// backward scrubs. Pass `null` (or omit) for byte-for-byte fidelity to
// the firmware path — the wire-completeness golden test takes this
// branch, since the test is pinned to Gen3 firmware behavior, not
// Gen2-compat presentation.
//
// Returns a Uint8Array of length 77 (v4.23 wire format).
function buildWireFrame(stepResult, cfg, coreModule, presentationSmoother) {
  if (!coreModule || typeof coreModule.build_display_frame !== 'function') {
    throw new Error(
      'wireBridge: coreModule.build_display_frame is not available — ' +
      'rebuild via software/Libraries/onspeed_core/wasm/build_wasm.sh');
  }
  const inputs = buildDisplayInputs(stepResult, cfg);
  if (presentationSmoother) {
    presentationSmoother.apply(inputs);
  }
  return coreModule.build_display_frame(inputs);
}

export { buildDisplayInputs, buildWireFrame, computePercentLift };
