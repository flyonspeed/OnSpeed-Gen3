// presentationFilter.js — render-side smoothing for the replay tool's
// SVG mode renderers.
//
// Scope and intent
// ----------------
// This is a VIEWING AID, not a firmware mirror. The replay tool's
// production wire path (LogReplayTask) is byte-faithful to what the M5
// would have received in flight. But for 50 Hz SD logs (the OnSpeed
// default), the IMU samples in the log carry aliased noise above 25 Hz
// that the firmware's 208 Hz AHRS-side EMA never saw — so the replay-
// computed `lateralG` jitter on the slip ball is structurally LOUDER
// than what the pilot saw on the M5 panel.
//
// There is no information-theoretic recovery from a 50 Hz log to "what
// the airplane showed at 60 Hz panel rate, with a 76.5 ms continuous-
// time AHRS EMA running at 208 Hz". You can only attenuate the residual
// — at the cost of either (a) hiding real low-frequency slip cues or
// (b) introducing display lag.
//
// This module provides a small, opt-in render-side smoother. The user
// chooses τ from a small set of presets:
//   - off    : no smoothing, byte-faithful to the wire (for 208 Hz logs).
//   - 200 ms : minimal additional damping; still responsive.
//   - 500 ms : visibly damped; ~3× the firmware's 76.5 ms AHRS τ.
//   - 1000 ms: heavy; matches Gen2's vertical-G presentation default.
// The 2.5 s lateral default of Gen2's firmware is intentionally NOT
// offered as a preset — at that τ the ball is so unresponsive it
// hides slip cues that matter for stall avoidance.
//
// What this is NOT
// ----------------
// - Not part of the wire-bytes path. The C++ LogReplayTask emits
//   exactly what the firmware would emit. This filter runs AFTER
//   m5sim.read() returns the firmware's display state, before the
//   SVG renderer reads it.
// - Not "the smoothing the airplane had". The airplane has 208 Hz IMU
//   + 76.5 ms continuous-time τ AHRS EMA. This filter cannot reproduce
//   that from 50 Hz log data; it can only attenuate residual aliasing.
// - Not on by default. Users who care about firmware fidelity see what
//   the wire actually carried.
//
// Channels filtered
// -----------------
// - LateralG: drives the slip ball x-position. Aliasing here is the
//   most visually loud at OnSpeed's slip-ball gain (~850 px/g).
// - VerticalG: drives the corner-G readout and the historic-G mode.
//   The corner readout is already 2 Hz-rate-limited by the firmware's
//   numbersUpdateTime gate, so it doesn't need much extra damping.
//
// We don't filter PercentLift / pip / chevrons. Those derive from AOA,
// which goes through the engine's alpha-EMA already.

'use strict';

// Discrete EMA mapping a continuous-time τ at a known sample interval.
//
//   α = 1 − exp(−Δt / τ)
//
// Higher α = less smoothing. Δt is the rate the caller calls apply()
// at — for the replay's per-frame effect that's whatever videoT delta
// the browser produces (RAF-rate, ~60 Hz).
function alphaForTau(dtSec, tauSec) {
  if (!(dtSec > 0) || !(tauSec > 0)) return 1.0;
  return 1.0 - Math.exp(-dtSec / tauSec);
}

// PresentationFilter — stateful, per-render-session.
//
// Lifecycle:
//   const f = new PresentationFilter();
//   f.setTau({ lateralSec: 0.5, verticalSec: 0.5 });
//   ...per frame...
//   const smoothed = f.apply(state, dtSec);
//   // smoothed is { lateralG, verticalG } — render-ready
//   ...on backward scrub or τ change:
//   f.reset();
//
// τ values are settable at any time; α is recomputed lazily on the
// next apply() that observes a new dt. Use 0 to disable a channel.
export class PresentationFilter {
  constructor() {
    this._tauLat  = 0.0;
    this._tauVert = 0.0;
    this._lat  = NaN;
    this._vert = NaN;
  }

  // Set both channel τ in seconds. Set 0 (or any non-positive) to
  // disable a channel — apply() then passes the input through as-is
  // for that field.
  setTau({ lateralSec, verticalSec }) {
    if (typeof lateralSec  === 'number') this._tauLat  = Math.max(0, lateralSec);
    if (typeof verticalSec === 'number') this._tauVert = Math.max(0, verticalSec);
  }

  // Reset filter state. Next apply() seeds with the input value.
  reset() {
    this._lat  = NaN;
    this._vert = NaN;
  }

  // Apply filter to the lateral/vertical G values from m5sim state.
  // Returns { lateralG, verticalG }; mutates internal state.
  //
  // dtSec: time since the previous apply() call. The replay's per-
  // frame effect knows this (videoT delta). Sub-millisecond ticks
  // pass through with α≈0 — caller can skip apply() in that case if
  // they prefer.
  apply(lateralG, verticalG, dtSec) {
    const aLat  = (this._tauLat  > 0) ? alphaForTau(dtSec, this._tauLat)  : 1.0;
    const aVert = (this._tauVert > 0) ? alphaForTau(dtSec, this._tauVert) : 1.0;

    let outLat = lateralG;
    if (Number.isFinite(lateralG)) {
      this._lat = Number.isNaN(this._lat)
        ? lateralG
        : aLat * lateralG + (1.0 - aLat) * this._lat;
      outLat = this._lat;
    }

    let outVert = verticalG;
    if (Number.isFinite(verticalG)) {
      this._vert = Number.isNaN(this._vert)
        ? verticalG
        : aVert * verticalG + (1.0 - aVert) * this._vert;
      outVert = this._vert;
    }

    return { lateralG: outLat, verticalG: outVert };
  }
}

// Preset τ values offered in the UI. Off is encoded as 0.
export const PRESENTATION_PRESETS = [
  { id: 'off',  label: 'Off',     lateralSec: 0,    verticalSec: 0 },
  { id: '200',  label: '200 ms',  lateralSec: 0.2,  verticalSec: 0.2 },
  { id: '500',  label: '500 ms',  lateralSec: 0.5,  verticalSec: 0.5 },
  { id: '1000', label: '1 s',     lateralSec: 1.0,  verticalSec: 1.0 },
];
