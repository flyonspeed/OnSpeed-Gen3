#!/usr/bin/env python3
"""Cross-sensor sanity check: compare OnSpeed's IAS reading against Dynon's
efisIAS through each detected stall run. Also reports Dynon's PercentLift at
the moment OnSpeed's slicer identifies the stall break.

Useful when validating OnSpeed calibration results — if both pitots agree on
the stall IAS, the extrapolated α_stall value is physically grounded; if they
disagree, one of the sensors has a significant position error.

Outputs a per-run table:
  run_id  OS IAS@stall  Dynon IAS@stall  diff  OS vs Dynon bias over run  Dynon PctLift@stall

Usage::

    uv run --with numpy --with scipy --with pandas --with matplotlib python3 \\
        supplement_dynon.py \\
        --log  path/to/log_NNN.csv \\
        --cfg  path/to/sam_onspeed.cfg
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import numpy as np
import pandas as pd

import analyze  # reuse parsers and slicer

ap = argparse.ArgumentParser(
    description="Cross-check OnSpeed IAS against Dynon efisIAS at every stall run.",
    formatter_class=argparse.ArgumentDefaultsHelpFormatter,
)
ap.add_argument("--log", type=Path, required=True,
                help="Flight log CSV from the OnSpeed SD card (50 Hz).")
ap.add_argument("--cfg", type=Path, required=True,
                help="On-device config XML that was running when the log was captured.")
args = ap.parse_args()

LOG = args.log
CFG = args.cfg

cfg = analyze.parse_config(CFG)
flap_bins = sorted(cfg.flaps.keys())

print(f"Loading log ({LOG.stat().st_size/1e6:.0f} MB)...")
log_df = pd.read_csv(LOG, low_memory=False)
log_df.columns = [c.strip() for c in log_df.columns]
print(f"  {len(log_df)} rows")

windows = analyze.detect_stall_windows(log_df, flap_bins)
runs = analyze.build_runs_from_windows(log_df, windows)
print(f"  {len(runs)} stall runs detected\n")

# Quick pre-computation: slice the relevant Dynon columns the slicer didn't cache
efis_ias_arr = log_df["efisIAS"].to_numpy(dtype=float)
efis_pctlift_arr = log_df["efisPercentLift"].to_numpy(dtype=float)

# Map run_id -> (start_idx, end_idx) from windows, keyed by iteration order
win_by_idx = [(w["start_idx"], w["end_idx"]) for w in windows]

print(f"{'Run':<10} {'Flap':>5} {'OS pts':>7} {'OS stall IAS':>12} {'Dyn stall IAS':>13} "
      f"{'Δ (OS-Dyn)':>11} {'OS mean IAS':>12} {'Dyn mean IAS':>13} {'bias':>7} "
      f"{'Dyn PctLift@stall':>18}")
print("-" * 120)

for r, (si, ei) in zip(runs, win_by_idx):
    hi = r.stall_index + 1  # [0..stall_index] is the pre-stall window used for the fit
    os_ias_slice = r.ias_raw[:hi]
    os_smooth_ias = r.ias_smooth[:hi]
    # Absolute log indices for the pre-stall slice
    abs_lo = si
    abs_hi = si + hi
    dyn_ias_slice = efis_ias_arr[abs_lo:abs_hi]
    dyn_pctlift_slice = efis_pctlift_arr[abs_lo:abs_hi]

    # Stall point: last index of the pre-stall slice (corresponds to r.stall_index)
    os_stall_ias = float(os_smooth_ias[-1])
    # For Dynon: what was efisIAS at that same moment? Take the mean of the last
    # ~5 samples for a smooth estimate (Dynon broadcasts are asynchronous).
    k_tail = min(5, len(dyn_ias_slice))
    dyn_stall_ias_raw = dyn_ias_slice[-k_tail:]
    dyn_stall_ias = float(np.nanmean(dyn_stall_ias_raw)) if len(dyn_stall_ias_raw) else float("nan")

    dyn_pctlift_raw = dyn_pctlift_slice[-k_tail:]
    dyn_pctlift = float(np.nanmean(dyn_pctlift_raw)) if len(dyn_pctlift_raw) else float("nan")

    # Whole-run bias: mean OS IAS - mean Dynon IAS (non-stall-weighted)
    # Use only where Dynon is reading sensibly (>20 kt)
    valid_dyn = dyn_ias_slice[dyn_ias_slice > 20]
    valid_os = os_ias_slice[dyn_ias_slice > 20]
    if len(valid_dyn) > 10:
        os_mean = float(np.mean(valid_os))
        dyn_mean = float(np.mean(valid_dyn))
        bias = os_mean - dyn_mean
    else:
        os_mean = dyn_mean = bias = float("nan")

    dstall = os_stall_ias - dyn_stall_ias

    def _fmt(v, w=6, p=1):
        if not np.isfinite(v):
            return f"{'—':>{w}}"
        return f"{v:{w}.{p}f}"

    print(
        f"{r.run_id:<10} {r.flap_bin:>4d}° {len(os_ias_slice):>7d} "
        f"{_fmt(os_stall_ias, 12, 1):>12} {_fmt(dyn_stall_ias, 13, 1):>13} "
        f"{_fmt(dstall, 11, 2):>11} {_fmt(os_mean, 12, 1):>12} {_fmt(dyn_mean, 13, 1):>13} "
        f"{_fmt(bias, 7, 2):>7} {_fmt(dyn_pctlift, 18, 1):>18}"
    )

# Summary averages per flap
print()
print("Per-flap averages (stall-break samples only, first 5 Dynon readings before break):")
print(f"{'Flap':<6} {'n runs':>7} {'OS stall IAS':>13} {'Dyn stall IAS':>14} {'Δ':>7} {'Dyn PctLift':>13}")
print("-" * 72)
by_flap: dict[int, list[tuple[float, float, float]]] = {}
for r, (si, ei) in zip(runs, win_by_idx):
    hi = r.stall_index + 1
    abs_lo = si
    abs_hi = si + hi
    dyn_ias_tail = efis_ias_arr[max(abs_lo, abs_hi - 5):abs_hi]
    dyn_pct_tail = efis_pctlift_arr[max(abs_lo, abs_hi - 5):abs_hi]
    os_stall = float(r.ias_smooth[hi - 1])
    dyn_stall = float(np.nanmean(dyn_ias_tail)) if len(dyn_ias_tail) else float("nan")
    dyn_pct = float(np.nanmean(dyn_pct_tail)) if len(dyn_pct_tail) else float("nan")
    by_flap.setdefault(r.flap_bin, []).append((os_stall, dyn_stall, dyn_pct))

for flap in sorted(by_flap.keys()):
    rows = by_flap[flap]
    n = len(rows)
    os_avg = np.mean([x[0] for x in rows])
    dyn_avg = np.mean([x[1] for x in rows if np.isfinite(x[1])])
    pct_avg = np.mean([x[2] for x in rows if np.isfinite(x[2])])
    print(f"{flap}°{'':<4} {n:>7d} {os_avg:>13.2f} {dyn_avg:>14.2f} "
          f"{os_avg - dyn_avg:>7.2f} {pct_avg:>13.1f}")

# Also: what's Dynon's stated stall speed from its own linear lift model?
# Dynon stores AOA% = gain * pressure_ratio + offset per flap, and the cfg's
# .dfg holds V-speeds in meters/second. We don't have the .dfg parsed here,
# but we can look for the Dynon-reported stall IAS implicitly: at the moment
# Dynon reports PercentLift == 99 (its max), what IAS does it show?
print()
print("When Dynon PercentLift first hit ≥ 95 during each run (its 'near stall' threshold):")
print(f"{'Run':<10} {'Flap':>5} {'Dyn PctLift≥95 @IAS':>20} {'OS IAS at same moment':>22}")
print("-" * 70)
for r, (si, ei) in zip(runs, win_by_idx):
    hi = r.stall_index + 1
    abs_lo = si
    abs_hi = si + hi
    pctlift_slice = efis_pctlift_arr[abs_lo:abs_hi]
    dyn_ias_slice = efis_ias_arr[abs_lo:abs_hi]
    os_ias_slice = r.ias_smooth[:hi]
    idx_95 = None
    for i, pl in enumerate(pctlift_slice):
        if np.isfinite(pl) and pl >= 95:
            idx_95 = i
            break
    if idx_95 is None:
        print(f"{r.run_id:<10} {r.flap_bin:>4d}° {'(never reached)':>20} {'':>22}")
    else:
        print(f"{r.run_id:<10} {r.flap_bin:>4d}° "
              f"{dyn_ias_slice[idx_95]:>20.1f} {os_ias_slice[idx_95]:>22.1f}")
