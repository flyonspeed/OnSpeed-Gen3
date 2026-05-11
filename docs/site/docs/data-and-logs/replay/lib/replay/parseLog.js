// CSV log parser for OnSpeed SD-card logs, plus WASM engine helpers.
//
// Pure-JS, no deps. Reads the header to find column ordinals by name
// (matches the C++ LogCsvHeaderIndex pattern), then streams rows into
// typed arrays. Returns parallel arrays so the indexer's per-frame
// lookup is a single bsearch + array indexing.
//
// Empty cells (format-version 3+ Dynon-style) parse to NaN for floats
// and -1 for ints (mirrors the wsClient null-handling convention).
//
// Also exports hasFlapsRawAdc() and detectLogSampleRate() used by
// LogReplayEngine.create() (see logReplay.js).

const FLOAT_COLUMNS = [
  'timeStamp',
  'IAS', 'AngleofAttack', 'OAT', 'TAS', 'Palt', 'PStatic',
  'VerticalG', 'LateralG', 'ForwardG',
  'RollRate', 'PitchRate', 'YawRate',
  'Pitch', 'Roll',
  'EarthVerticalG', 'FlightPath', 'VSI', 'Altitude',
  'DerivedAOA', 'CoeffP',
  // Pressure sensor columns — used by the WASM AOA calculator.
  'Pfwd', 'PfwdSmoothed', 'P45', 'P45Smoothed',
  // EFIS group (when present)
  'efisIAS', 'efisPitch', 'efisRoll', 'efisLateralG', 'efisVerticalG',
  'efisPalt', 'efisVSI', 'efisTAS', 'efisOAT',
];

const INT_COLUMNS = [
  'flapsPos', 'DataMark', 'efisPercentLift', 'flapsRawADC',
];

// Parse a CSV. Returns:
//   {
//     timeStamp:   Float64Array (ms),
//     IAS:         Float32Array,
//     AOA:         Float32Array,    // sourced from "AngleofAttack"
//     ...,
//     Length:      number (row count),
//     ColumnsSeen: Set<string>,
//   }
export function parseLog(text) {
  const lines = text.split(/\r?\n/);
  if (lines.length < 2) throw new Error('CSV log: need at least a header + one row');

  const headers = lines[0].split(',').map(s => s.trim());
  const idx = {};
  for (let i = 0; i < headers.length; i++) idx[headers[i]] = i;

  // Allocate parallel arrays. We allocate to the row count and let
  // unparseable rows leave NaN behind — simpler than a two-pass count.
  const N = lines.length - 1;
  const out = {
    Length: 0,
    ColumnsSeen: new Set(headers),
    columns: headers,   // for hasFlapsRawAdc()
  };
  out.timeStamp = new Float64Array(N);
  for (const k of FLOAT_COLUMNS) {
    if (k === 'timeStamp') continue;
    if (k in idx) out[k] = new Float32Array(N);
  }
  for (const k of INT_COLUMNS) {
    if (k in idx) out[k] = new Int32Array(N);
  }

  // Convenience aliases used by the rest of the pipeline.
  if ('AngleofAttack' in idx) out.AOA = out.AngleofAttack;

  let row = 0;
  for (let r = 1; r < lines.length; r++) {
    const line = lines[r];
    if (!line) continue;
    const tokens = line.split(',');

    const t = parseFloat(tokens[idx.timeStamp]);
    if (!Number.isFinite(t)) continue;       // skip malformed rows
    out.timeStamp[row] = t;

    for (const k of FLOAT_COLUMNS) {
      if (k === 'timeStamp') continue;
      if (!(k in idx) || !out[k]) continue;
      const tok = tokens[idx[k]];
      if (tok == null || tok === '') {
        out[k][row] = NaN;
      } else {
        const v = parseFloat(tok);
        out[k][row] = Number.isFinite(v) ? v : NaN;
      }
    }
    for (const k of INT_COLUMNS) {
      if (!(k in idx) || !out[k]) continue;
      const tok = tokens[idx[k]];
      if (tok == null || tok === '') {
        out[k][row] = -1;
      } else {
        const v = parseInt(tok, 10);
        out[k][row] = Number.isFinite(v) ? v : -1;
      }
    }
    row++;
  }

  out.Length = row;

  // Truncate any over-allocated tails so .length reflects rows seen.
  // (Float32Array has no resize; subarray() gives a view, not a copy.)
  if (row !== N) {
    out.timeStamp = out.timeStamp.subarray(0, row);
    for (const k of FLOAT_COLUMNS) {
      if (out[k]) out[k] = out[k].subarray(0, row);
    }
    for (const k of INT_COLUMNS) {
      if (out[k]) out[k] = out[k].subarray(0, row);
    }
    if (out.AOA) out.AOA = out.AOA.subarray(0, row);
  }

  return out;
}

// Binary-search the timestamp array for the row whose timestamp is
// closest to the requested time (in the same units as timeStamp[i],
// i.e. milliseconds since power-on).
//
// Returns the row index (0..Length-1) or -1 if the log is empty.
export function findRowAt(log, tMs) {
  const ts = log.timeStamp;
  const n = log.Length;
  if (n === 0) return -1;
  if (tMs <= ts[0]) return 0;
  if (tMs >= ts[n - 1]) return n - 1;

  let lo = 0, hi = n - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (ts[mid] <= tMs) lo = mid;
    else hi = mid;
  }
  // Pick the closer of lo and hi.
  return (tMs - ts[lo] < ts[hi] - tMs) ? lo : hi;
}

// ---------------------------------------------------------------------------
// hasFlapsRawAdc(parsedLog) -> boolean
//
// Returns true when the log CSV carries a raw flap-pot ADC column.
// Logs written before ~PR #372 don't have this column; without it the
// LogReplayEngine cannot reproduce the smooth L/Dmax pip interpolation
// across detent transitions.
//
// Works with the columnar format returned by parseLog() above:
// checks parsedLog.columns (the CSV header string[]).
// ---------------------------------------------------------------------------
export function hasFlapsRawAdc(parsedLog) {
  if (!parsedLog || !Array.isArray(parsedLog.columns)) return false;
  return parsedLog.columns.some(
    c => c.toLowerCase() === 'flapsrawadc'
  );
}

// ---------------------------------------------------------------------------
// detectLogSampleRate(parsedLog) -> 50 | 208
//
// Infers the log's sample rate from the average inter-row timestamp delta.
// Returns 50 or 208; defaults to 50 for ambiguous or short logs.
//
// Works with the columnar format returned by parseLog() above:
// reads from parsedLog.timeStamp (Float64Array) and parsedLog.Length.
// ---------------------------------------------------------------------------
export function detectLogSampleRate(parsedLog) {
  if (!parsedLog || !parsedLog.timeStamp || parsedLog.Length < 10) {
    return 50;  // fallback: not enough rows to measure
  }

  const ts = parsedLog.timeStamp;
  const t0 = ts[0];
  const t9 = ts[9];
  if (!Number.isFinite(t0) || !Number.isFinite(t9)) return 50;

  const avgDeltaMs = (t9 - t0) / 9;

  if (avgDeltaMs >= 15 && avgDeltaMs <= 25) return 50;
  if (avgDeltaMs >= 3  && avgDeltaMs <= 7)  return 208;
  return 50;  // unknown rate — default to 50
}
