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
  // Each display* field prefers the 500 ms snapshot (matching the M5
  // panel's text-readout cadence); when the snapshot hasn't fired yet
  // (first ~500 ms after page load or scenario switch), fall back to
  // the live value from r. This avoids the "00 flash" bug where the
  // AOA corner showed "00" instead of the correct value during the
  // gap between the first valid WS frame and the first snapshot tick
  // — bulldog catch on PR-C round 1, see commit 7/8.
  //
  // Final fallbacks: null for IAS- and percent-lift-derived fields (so
  // the formatter renders dashes); 0 for non-air-data fields (palt,
  // pitch, verticalG come from independent sensors and stay numeric
  // even when air data is invalid).
  const liveSnap = snapshot || {};
  const ring = gRing || {};
  const aoaValid = r.aoaIsValid !== false;
  const snapOrLive = (snapVal, liveVal, dflt) => {
    if (Number.isFinite(snapVal)) return snapVal;
    if (Number.isFinite(liveVal)) return liveVal;
    return dflt;
  };
  const pctLiftSnap = !aoaValid
    ? null
    : Number.isFinite(liveSnap.percentLift)
      ? Math.trunc(Math.max(0, Math.min(99, liveSnap.percentLift)))
      : Number.isFinite(r.percentLift)
        ? Math.trunc(Math.max(0, Math.min(99, r.percentLift)))
        : null;

  return {
    // ----- Validity gate -----
    IasIsValid: aoaValid,

    // ----- 500 ms snapshot fields (snap → live → fallback) -----
    displayIAS:         snapOrLive(liveSnap.iasKt,             r.iasKt,        null),
    displayPalt:        snapOrLive(liveSnap.paltFt,            r.paltFt,       0),
    displayPitch:       snapOrLive(liveSnap.pitchDeg,          r.pitchDeg,     0),
    displayVerticalG:   snapOrLive(liveSnap.verticalG,         r.verticalG,    0),
    displayPercentLift: pctLiftSnap,
    displayDecelRate:   snapOrLive(liveSnap.decelRateSmoothed, r.decelRate,    0),

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
