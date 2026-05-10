// helpers.js — shared formatters/utilities for M5-accurate mode renderers.
//
// PR 2 of Project B2. Each mode component (Energy/Attitude/Indexer/
// Decel/HistoricG) needs the same handful of small helpers — IAS
// dashes, padded altitude, slip↔lateralG inverse, flash cadence. These
// live here rather than per-component so the bundler doesn't see
// duplicate top-level identifiers across the m5modes/ files.
//
// The legacy `lib/modes.js` file declares its own copies of these
// helpers for the legacy 22-field-rec path. Once PR 3 deletes that
// path, these helpers are the canonical home and modes.js can import
// from here. PR 2 keeps both copies temporarily — the legacy ones use
// `r.iasKt` plumbing, ours use `state.displayIAS` plumbing, and the
// shape difference makes a single shared signature unwieldy until
// the legacy path is gone.

import { IAS_DASHES } from '../index.js';

// 4 Hz visual flash (same cadence the M5 firmware uses for indexer
// color toggles). Reads the wall clock — the M5 panel does too — so
// the JS-side flash and the M5's flash run roughly in phase as long
// as JS hasn't been backgrounded.
export const m5FlashFlagNow = () =>
  (Math.floor(performance.now() / 250) % 2) === 1;

// IAS placeholder when invalid: 3 dashes, matching the firmware's
// `snprintf("---")` for the 3-digit IAS field.
export const m5FmtIasKt = (v, valid) => {
  if (!valid) return IAS_DASHES;
  if (v === undefined || v === null || Number.isNaN(v)) return IAS_DASHES;
  const n = Number(v);
  if (!Number.isFinite(n)) return IAS_DASHES;
  return n.toFixed(0);
};

// 5-digit space-padded altitude (e.g. "  500", "12345"). Matches the
// firmware's `snprintf("%5.0f", displayPalt)`.
export const m5FmtPalt = (v) => {
  if (v === undefined || v === null || Number.isNaN(v)) return '—';
  return String(Math.round(v)).padStart(5, ' ');
};
