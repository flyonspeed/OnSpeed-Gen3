// Single-pole exponential-moving-average filter — pure helpers behind
// the live decel-gauge smoothing on /indexer Mode 3 and /calwiz's decel
// step.  Mirrors the M5 hardware's `SmoothedDecelRate` (alpha=0.04 at
// 20 Hz, ~1.25 s τ) so the indexer's decel pointer tracks the M5
// gauge byte-equivalently in time.  The wizard exposes the same filter
// with a SMOOTH ↔ RESPONSIVE slider — Gen2 had this as a per-pilot
// affordance and Vac usually pulls it well toward SMOOTH.
//
// Convention: `smoothed_n = α·x_n + (1 − α)·smoothed_{n−1}`.
// Larger α = more weight on the new sample = more responsive.
// Smaller α = heavier smoothing.  Matches the M5 firmware
// (SerialRead.cpp:344) and the Gen2 wizard slider's labels.
//
// The pure helpers here are exported separately from any Preact glue so
// they can be unit-tested with plain Node (`node tools/web/test/ema.mjs`).
//
// Frame-rate note: α has time-constant meaning τ ≈ 1/(α·rate).  Both
// consumers in this codebase poll the WebSocket at 20 Hz, so a literal
// α matches a literal τ.  A future consumer at a different rate would
// need to convert α to preserve τ — derive `α_new = 1 − (1 − α_ref)^(rate_ref/rate_new)`.

export function makeEmaState(alpha) {
  return { alpha, smoothed: null };
}

// Update with a new sample.  Returns the new smoothed value.
// First valid sample seeds the state directly (no warm-up).  Junk
// samples (null / undefined / NaN / ±Infinity) are dropped — the
// filter holds its current value and the gauge holds its needle.
export function updateEma(state, x) {
  if (x == null || !Number.isFinite(x)) return state.smoothed;
  if (state.smoothed === null) {
    state.smoothed = x;
  } else {
    state.smoothed = state.alpha * x + (1 - state.alpha) * state.smoothed;
  }
  return state.smoothed;
}

// Mutate alpha live — used by the wizard slider's `oninput`.  We do NOT
// re-seed on alpha changes; the smoothed value is what it is, the next
// update applies the new alpha.  This matches Gen2's slider behavior.
export function setAlpha(state, alpha) {
  state.alpha = alpha;
}

// Reset back to the no-sample state.  Next update seeds.
export function resetEma(state) {
  state.smoothed = null;
}
