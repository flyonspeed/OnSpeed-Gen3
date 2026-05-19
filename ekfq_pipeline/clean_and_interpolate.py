#!/usr/bin/env python3
"""
Clean log_007.csv and produce log_007_fixed.csv:
  1. Drop malformed lines (wrong column count, logger glitches).
  2. Drop rows with corrupted timestamps (non-integer, negative, non-monotonic).
  3. Map each surviving row to its 208 Hz sample index.
  4. Linear-interpolate missing samples on every numeric column.
  5. Replace integer-ms timestamps with fractional ms (sample_index * 1000/208),
     formatted to 3 decimal places.
"""

import csv
import sys
import numpy as np
import pandas as pd

INPUT = "log_007.csv"
OUTPUT = "log_007_fixed.csv"
RATE_HZ = 208.0
PERIOD_MS = 1000.0 / RATE_HZ
STRING_COLS = {"vnTimeUTC"}

# Sanity bound on per-row timestamp delta. The device runs at ~208 Hz, so any
# forward jump larger than this is treated as a corrupt row and skipped (the
# previous timestamp is preserved). 60 s = 12480 missing samples is far beyond
# anything we'd ever interpolate in practice; observed real gaps are <1 s.
MAX_GAP_MS = 60_000


def stream_clean(path):
    """Yield only well-formed rows with strictly-monotonic non-negative integer timestamps."""
    with open(path, newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        ncols = len(header)
        rows = []
        prev_ts = -1
        n_malformed = 0
        n_corrupt = 0
        for row in reader:
            if len(row) != ncols:
                n_malformed += 1
                continue
            try:
                ts = int(row[0])
            except ValueError:
                n_corrupt += 1
                continue
            if ts < 0:
                n_corrupt += 1
                continue
            if prev_ts >= 0 and (ts <= prev_ts or ts - prev_ts > MAX_GAP_MS):
                # Skip without advancing prev_ts so a single bad spike can't
                # lock out the rest of the file.
                n_corrupt += 1
                continue
            prev_ts = ts
            rows.append(row)
    return header, rows, n_malformed, n_corrupt


def main():
    print(f"Reading {INPUT} ...", flush=True)
    header, rows, n_malformed, n_corrupt = stream_clean(INPUT)
    print(f"  columns         : {len(header)}", flush=True)
    print(f"  malformed lines : {n_malformed}", flush=True)
    print(f"  corrupt-ts lines: {n_corrupt}", flush=True)
    print(f"  clean rows      : {len(rows)}", flush=True)

    df = pd.DataFrame(rows, columns=header)
    del rows
    numeric_cols = [c for c in header if c not in STRING_COLS]
    for c in numeric_cols:
        df[c] = pd.to_numeric(df[c], errors="coerce").astype(np.float32)
    # timeStamp itself: keep as int64 for index math precision
    ts_int = df["timeStamp"].values.astype(np.int64)
    ts0 = int(ts_int[0])
    print(f"  first ts (ms)   : {ts0}", flush=True)
    print(f"  last  ts (ms)   : {int(ts_int[-1])}", flush=True)
    duration_s = (int(ts_int[-1]) - ts0) / 1000.0
    print(f"  duration (s)    : {duration_s:.3f}", flush=True)

    # Map each row to a sample index by walking deltas. A direct
    # round((ts-ts0)/period) mapping is wrong because the device logs integer-ms
    # truncations of a 4.808-ms period; the cumulative phase drift would alias
    # neighbouring samples onto the same grid point after ~2000 rows.
    #
    # Instead: consecutive logged rows are one sample apart unless the delta is
    # clearly multi-sample. Boundary: round(delta/period). Normal deltas (3..6)
    # all round to 1; deltas >=8 round to >=2 and represent dropped samples.
    deltas = np.diff(ts_int)
    steps = np.maximum(1, np.round(deltas / PERIOD_MS).astype(np.int64))
    sample_idx = np.concatenate([[0], np.cumsum(steps)])

    n_samples_total = int(sample_idx[-1] + 1)
    n_missing = n_samples_total - len(sample_idx)
    pct_missing = 100.0 * n_missing / n_samples_total
    print(f"  grid samples    : {n_samples_total}", flush=True)
    print(f"  missing samples : {n_missing}  ({pct_missing:.3f} %)", flush=True)
    # Sanity check: predicted final ts vs logged final ts should be within a
    # few ms (phase error from integer-ms truncation).
    predicted_last_ts = ts0 + sample_idx[-1] * PERIOD_MS
    print(
        f"  last ts logged  : {int(ts_int[-1])} ms  vs  predicted {predicted_last_ts:.3f} ms"
        f"  (diff {predicted_last_ts - ts_int[-1]:+.3f} ms)",
        flush=True,
    )

    # Reindex onto the complete grid; missing rows are introduced as NaN
    df.index = sample_idx
    full_idx = pd.RangeIndex(0, sample_idx[-1] + 1)
    df = df.reindex(full_idx)

    # Interpolate every numeric column except timeStamp (we set that below)
    print("Interpolating ...", flush=True)
    for c in numeric_cols:
        if c == "timeStamp":
            continue
        df[c] = df[c].interpolate(method="linear", limit_direction="both").astype(np.float32)

    # Forward-/back-fill string columns
    for c in STRING_COLS:
        df[c] = df[c].ffill().bfill()

    # Build fractional-ms timestamps: ts0 + idx * (1000/208), 3 decimals
    fractional_ts = ts0 + df.index.values * PERIOD_MS
    # Replace timeStamp column with formatted strings so pandas writes it verbatim
    df["timeStamp"] = np.round(fractional_ts, 3)

    # Reorder to original header order, drop the integer index
    df = df[header].reset_index(drop=True)

    print(f"Writing {OUTPUT} ...", flush=True)
    # Use a custom per-column formatter so timeStamp stays at 3 decimals
    # and other numeric values use compact %.6g (preserves float32 precision).
    def _fmt(v, ndp):
        if pd.isna(v):
            return ""
        return f"{v:.{ndp}f}".rstrip("0").rstrip(".") if ndp > 3 else f"{v:.{ndp}f}"

    # to_csv is fastest if we let pandas format; use float_format for body and
    # pre-format timeStamp as a string column.
    ts_strings = [f"{t:.3f}" for t in df["timeStamp"].values]
    df["timeStamp"] = ts_strings
    df.to_csv(OUTPUT, index=False, float_format="%.6g")
    print("Done.", flush=True)


if __name__ == "__main__":
    main()
