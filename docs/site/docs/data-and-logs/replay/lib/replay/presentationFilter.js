// presentationFilter.js — render-side smoothing for the replay tool's
// SVG mode renderers.
//
// Scope and intent
// ----------------
// What the airplane M5 actually shows in flight is
//   EMA(raw IMU @ 208 Hz, τ=76.5ms continuous-time)
// where the EMA RUNS at IMU rate. Each output sample is a weighted
// average over ~12 raw samples (~60 ms of integration).
//
// What a 50 Hz SD log captures is ONE raw IMU sample every 20 ms — so
// the log loses ~75% of the smoothing work the AHRS did in flight.
// Replay then runs a rate-adjusted 50 Hz EMA with the same continuous-
// time τ; mathematically this recovers the LOW-frequency content of
// the original signal but cannot recover the HIGH-frequency averaging
// that 208 Hz oversampling provided. Result: replay's slip ball is
// structurally NOISIER than what the pilot saw, even with the engine's
// AHRS-equivalent EMA active.
//
// Tuning: the right τ was found empirically by σ-matching against
// the airplane's primary EFIS (Dynon SkyView ADAHRS) over Sam's
// 286k-row RV-10 flight log. Method:
//   1. Take the engine's accelLatSmoothed (post-rate-adjusted-EMA).
//   2. Sweep τ from 0.1 s to 30 s through a presentation EMA.
//   3. Compute σ of the smoothed signal over the in-flight portion
//      of the log; find the τ that matches the EFIS reference σ.
// The EFIS slip indicator is what the pilot accepts as "the right
// reading," so its variance is the visual target. RMS error against
// the EFIS is a misleading metric here — it monotonically improves
// with longer τ because the bias-free RMS includes uncorrelated
// noise that any heavy-enough filter can suppress at the cost of
// also suppressing real slip information.
//
// Tuning script: tools/replay-tuning/tune_presentation_tau.mjs.
// Different aircraft / IMU mountings produce different vibration
// spectra and thus different best-τ values; the script can be re-run
// per aircraft to validate or re-tune.
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
//
// The 'efis-match' default was tuned offline against Sam's Dynon-
// equipped RV-10 flight log via tools/replay-tuning/tune_presentation_tau.mjs.
// At 50 Hz log replay:
//   - lateral τ = 0.75 s brings σ from 0.043 g (engine direct) down to
//     0.024 g, matching Dynon ADAHRS's σ = 0.025 g over 4+ hours of
//     Sam's flight log.
//   - vertical τ = 0.1 s brings σ from 0.111 g to 0.095 g, matching
//     Dynon's 0.100 g.
// These are aircraft-specific (different IMU mounting / different
// vibration spectrum will shift the right τ); the presets cover a
// range so users can pick what looks right for their data.
//
// Validation method: σ-match against the airplane's primary EFIS.
// The pilot looks at the EFIS slip indicator and accepts it as the
// "right" reading; matching its variance reproduces what the pilot
// saw without false damping that would hide real slip cues.
export const PRESENTATION_PRESETS = [
  { id: 'off',         label: 'Off',                 lateralSec: 0,    verticalSec: 0 },
  { id: 'efis-match',  label: 'EFIS-match',          lateralSec: 0.75, verticalSec: 0.1 },
  { id: 'medium',      label: 'Medium',              lateralSec: 1.5,  verticalSec: 0.5 },
  { id: 'heavy',       label: 'Heavy',               lateralSec: 3.0,  verticalSec: 1.0 },
];

// Default preset id chosen at log load. The replay tool detects log
// rate (50 Hz vs 208 Hz) and picks accordingly: 50 Hz logs default to
// 'efis-match' (the σ-tuned compensation for missing 208 Hz averaging),
// 208 Hz logs default to 'off' (byte-faithful — the firmware-rate
// AHRS EMA does enough smoothing on its own).
export function defaultPresetForLogRate(logSampleRateHz) {
  return (logSampleRateHz >= 200) ? 'off' : 'efis-match';
}
