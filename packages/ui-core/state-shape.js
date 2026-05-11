// state-shape.js — the canonical M5State produced by every adapter
// and consumed by every m5modes renderer.
//
// Two adapters exist today, both produce this shape:
//   - tools/web/lib/replay/...    (replay adapter — reads from M5Sim.read())
//   - tools/web/lib/ws/...        (live indexer adapter — reads from
//                                  WebSocket record via wsRecordToState)
//
// Field names follow the M5 firmware's native vocabulary. The live
// adapter maps from the WebSocket record field names (r.iasKt etc.) to
// the canonical names below.
//
// CADENCE
// =======
// Fields prefixed `display*` are 500 ms snapshots — matching the M5
// firmware's `updateRateNumbers` text-readout cadence in main.cpp.
// Both adapters latch these every 500 ms so corner readouts don't
// flicker at 20 Hz on either page.
//
// All other fields update on each wire frame (~20 Hz).
//
// VALIDITY
// ========
// `IasIsValid` gates IAS-derived fields: displayIAS, displayPercentLift,
// PercentLift, displayDecelRate. When false, renderers show dashes
// (IAS_DASHES / PCT_DASHES) instead of zeros.
//
// HISTORY
// =======
// `gHistory` is a 300-element ring of VerticalG samples at 5 Hz (200 ms
// gate). The replay adapter reads it from the M5Sim WASM directly; the
// live adapter maintains an equivalent JS-side ring via
// IndexerPage::useGHistory(). Pre-flight g samples are NOT recoverable
// on the live page (the ring starts fresh on tab-open) — see issue #523
// PR-B audit Q2.

/**
 * @typedef {Object} M5State
 *
 * -- Validity gate --
 * @property {boolean} IasIsValid   Air-data valid flag. When false,
 *   IAS-derived display fields render as dashes.
 *
 * -- 500 ms snapshot fields (corner/text readouts) --
 * @property {number}  displayIAS          Knots IAS, 500 ms snapshot.
 * @property {number}  displayPalt         Pressure altitude, ft, 500 ms snapshot.
 * @property {number}  displayPitch        Pitch, degrees, 500 ms snapshot.
 * @property {number}  displayVerticalG    Vertical G, g, 500 ms snapshot.
 * @property {number}  displayPercentLift  Integer 0–99 percent lift, 500 ms snapshot.
 *   0 when !IasIsValid. Renderers show PCT_DASHES instead.
 * @property {number}  displayDecelRate    IAS rate, kt/s (EMA-smoothed), 500 ms snapshot.
 *   0 when !IasIsValid.
 *
 * -- Every-frame fields (geometry / animation) --
 * @property {number}  PercentLift  Float 0.0–99.9, 20 Hz. Drives indexer
 *   bar position and chevron color comparisons. 0 when !IasIsValid.
 * @property {number}  LateralG     Body-frame lateral G, positive = right.
 *   20 Hz. Used by SlipBall (clamped to ±99 slip int at the render boundary).
 * @property {number}  VerticalG    Vertical G, 20 Hz. Fed into gHistory ring.
 * @property {number}  gOnsetRate   G onset rate, g/s (250 ms tau EMA). 20 Hz.
 *   Drives EdgeTape.
 * @property {number}  IAS          Raw IAS, kt, 20 Hz. Adapters use this for
 *   their own computations; renderers should prefer displayIAS.
 * @property {number}  Palt         Raw pressure altitude, ft, 20 Hz.
 * @property {number}  iVSI         Vertical speed, fpm, 20 Hz.
 * @property {number}  FlightPath   Flight path angle, degrees, 20 Hz.
 * @property {number}  Pitch        Pitch, degrees, 20 Hz.
 * @property {number}  Roll         Roll, degrees, 20 Hz.
 * @property {number}  OAT          Outside air temperature, °C. Not rendered
 *   by any current mode; included so future modes can adopt it without
 *   needing both adapters touched again.
 *
 * -- Percent-lift anchors (every frame, wire-derived) --
 * @property {number}  TonesOnPctLift      L/Dmax pct for active detent.
 * @property {number}  OnSpeedFastPctLift  OnSpeed fast boundary pct.
 * @property {number}  OnSpeedSlowPctLift  OnSpeed slow boundary pct.
 * @property {number}  StallWarnPctLift    Stall warning pct (SlipBall flash gate).
 * @property {number}  PipPctLift          Visual L/Dmax pip pct (interpolated detents).
 *
 * -- Flap geometry --
 * @property {number}  FlapPos      Current flap position, degrees (interpolated).
 * @property {number}  FlapsMinDeg  Minimum configured flap angle, degrees.
 * @property {number}  FlapsMaxDeg  Maximum configured flap angle, degrees.
 *
 * -- Mode 4 (Historic G) --
 * @property {Float32Array} gHistory       300-element ring buffer of VerticalG.
 *   Replay reads from M5Sim WASM. Live indexer fills client-side from
 *   tab-open onward.
 * @property {number}       gHistoryIndex  Next write index (= oldest sample).
 *
 * -- Annotations --
 * @property {number}  DataMark         DataMark badge value (0 = none).
 * @property {number}  SpinRecoveryCue  Spin recovery cue state (0 = none).
 *   Note: live indexer defaults this to 0 until the firmware WebSocket
 *   JSON producer adds the field (tracked separately).
 */

// `Slip` is explicitly NOT part of the canonical shape. SlipBall reads
// LateralG directly and clamps internally. Keeping Slip out prevents
// re-introducing the saturation-loses-smoothing bug from PR #512.
