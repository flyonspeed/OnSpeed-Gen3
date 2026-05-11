// Centralized number-formatter that kills negative zero.
//
// `Number(-0.001).toFixed(2)` returns "-0.00", which renders as "-0.0g"
// when concatenated with " G" or "g".  parseFloat("-0.00") === 0, so
// the guard tests the parsed result and re-formats with Math.abs() if
// it rounds to zero.  Returns "—" for non-finite inputs (undefined,
// null, NaN, Infinity) so callers don't have to repeat that ceremony.
export function fmt(v, d) {
  if (v === undefined || v === null || Number.isNaN(v)) return '—';
  const n = Number(v);
  if (!Number.isFinite(n)) return '—';
  const s = n.toFixed(d);
  if (parseFloat(s) === 0) return Math.abs(n).toFixed(d);
  return s;
}

// Signed variant: prefixes a '+' for non-negative values and runs
// values that round to zero through `fmt` so '-0.0' never appears.
// '+0.0' is the canonical zero rendering.
export function fmtSigned(v, d) {
  const base = fmt(v, d);
  if (base === '—') return base;
  return base.startsWith('-') ? base : '+' + base;
}
