// wsRecordToState.js — adapter that produces an M5State (see
// packages/ui-core/state-shape.js) from the live `/indexer` page's
// inputs: the WebSocket record `r`, a 500 ms display snapshot, and a
// client-side g-history ring.
//
// This is the live-indexer counterpart to the replay tool's
// "M5Sim.read() → M5State" path. After this adapter ships, both pages
// render via the same m5modes/ SVG family.
//
// Inputs:
//   r         — the canonical WebSocket record from wsClient.frameToRecord().
//               Carries live values at 20 Hz.
//   snapshot  — fields latched at 500 ms by useDisplaySnapshot(r, 500).
//               Holds iasKt, paltFt, pitchDeg, verticalG, percentLift,
//               and decelRateSmoothed at the moment of the last latch.
//   gRing     — the {buf, writeIdx, hasSamples} object from
//               IndexerPage.useGHistory(). Float32Array of length 300,
//               sampled at 5 Hz from VerticalG.
//
// Cadence notes:
//   - The display* fields use the 500 ms snapshot. Matches the M5
//     firmware's `updateRateNumbers = 500` corner-readout cadence.
//   - All other fields flow through at 20 Hz wire rate.
//
// Limitations on the live page (vs replay):
//   - gHistory starts flat at 1.0 G when the browser tab opens; the
//     hardware M5 accumulates from power-on. Pre-tab-open events are
//     not recoverable. Documented in issue #523 PR-B audit Q2.
//   - SpinRecoveryCue defaults to 0 until the firmware WebSocket
//     JSON producer adds the field. Tracked separately.

/**
 * @param {Object} r        The WebSocket record from wsClient.frameToRecord().
 * @param {Object} snapshot Display fields latched at 500 ms. Shape:
 *   { iasKt, paltFt, pitchDeg, verticalG, percentLift, decelRateSmoothed }.
 *   Each field may be null/NaN before the first latch.
 * @param {Object} gRing    G-history ring buffer. Shape:
 *   { buf: Float32Array(300), writeIdx: number, hasSamples: boolean }.
 * @returns {Object} M5State (see state-shape.js).
 */
export function wsRecordToState(r, snapshot, gRing) {
  // Per-flap percent-lift integer for the corner readout. Truncate
  // toward zero (the firmware uses (int)x), clamp to [0, 99] to match
  // the M5's 2-digit display field.
  const liveSnap = snapshot || {};
  const ring = gRing || {};
  const pctLiftSnap = Number.isFinite(liveSnap.percentLift)
    ? Math.trunc(Math.max(0, Math.min(99, liveSnap.percentLift)))
    : 0;

  return {
    // ----- Validity gate -----
    IasIsValid: r.aoaIsValid !== false,

    // ----- 500 ms snapshot fields -----
    displayIAS:         Number.isFinite(liveSnap.iasKt) ? liveSnap.iasKt : 0,
    displayPalt:        Number.isFinite(liveSnap.paltFt) ? liveSnap.paltFt : 0,
    displayPitch:       Number.isFinite(liveSnap.pitchDeg) ? liveSnap.pitchDeg : 0,
    displayVerticalG:   Number.isFinite(liveSnap.verticalG) ? liveSnap.verticalG : 0,
    displayPercentLift: pctLiftSnap,
    displayDecelRate:   Number.isFinite(liveSnap.decelRateSmoothed)
                          ? liveSnap.decelRateSmoothed : 0,

    // ----- Every-frame fields -----
    PercentLift: r.percentLift ?? 0,
    LateralG:    r.lateralG    ?? 0,
    VerticalG:   r.verticalG   ?? 1,
    gOnsetRate:  r.gOnsetRate  ?? 0,
    IAS:         r.iasKt       ?? 0,
    Palt:        r.paltFt      ?? 0,
    iVSI:        r.vsiFpm      ?? 0,
    FlightPath:  r.flightPathDeg ?? 0,
    Pitch:       r.pitchDeg    ?? 0,
    Roll:        r.rollDeg     ?? 0,
    OAT:         r.oatC        ?? 0,

    // ----- Anchors -----
    TonesOnPctLift:     r.tonesOnPctLift     ?? 0,
    OnSpeedFastPctLift: r.onSpeedFastPctLift ?? 0,
    OnSpeedSlowPctLift: r.onSpeedSlowPctLift ?? 0,
    StallWarnPctLift:   r.stallWarnPctLift   ?? 0,
    PipPctLift:         r.pipPctLift         ?? 0,

    // ----- Flap geometry -----
    FlapPos:     r.flapsDeg     ?? 0,
    FlapsMinDeg: r.flapsMinDeg  ?? 0,
    FlapsMaxDeg: r.flapsMaxDeg  ?? 33,

    // ----- Historic G ring (built JS-side by useGHistory) -----
    gHistory:      ring.buf || EMPTY_RING,
    gHistoryIndex: ring.writeIdx || 0,

    // ----- Annotations -----
    DataMark:        r.dataMark ?? 0,
    // TODO: replace 0 with `r.spinRecoveryCue ?? 0` once the firmware
    // WebSocket JSON producer adds the field. Tracked separately
    // (the M5 wire format already carries it; just not in JSON yet).
    SpinRecoveryCue: 0,
  };
}

// Fallback when gRing isn't ready yet. Initialized to 1.0 G to match
// the firmware's startup state (M5 sim does the same in setup()).
const EMPTY_RING = new Float32Array(300).fill(1.0);
