#!/usr/bin/env python3
"""Plot the highest-quality stall run on its own — clean 4-panel view of the
data and both fits (OLS and WLS).

Usage::

    uv run --with numpy --with scipy --with pandas --with matplotlib python3 \\
        plot_best_run.py \\
        --log    path/to/log_NNN.csv \\
        --cfg    path/to/sam_onspeed.cfg \\
        --out    path/to/figs/  \\
        [--flap 33]
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

import analyze

ap = argparse.ArgumentParser(
    description="Plot the highest-quality stall run (optionally filtered by flap).",
    formatter_class=argparse.ArgumentDefaultsHelpFormatter,
)
ap.add_argument("--log", type=Path, required=True,
                help="Flight log CSV from the OnSpeed SD card (50 Hz).")
ap.add_argument("--cfg", type=Path, required=True,
                help="On-device config XML that was running when the log was captured.")
ap.add_argument("--out", type=Path, required=True,
                help="Output directory for the PNG plot.")
ap.add_argument("--flap", type=int, default=None,
                help="If set, pick the highest-quality run with this flap value. "
                     "Otherwise pick the highest-quality run overall.")
args = ap.parse_args()

LOG = args.log
CFG = args.cfg
OUT = args.out
OUT.mkdir(parents=True, exist_ok=True)

cfg = analyze.parse_config(CFG)
flap_bins = sorted(cfg.flaps.keys())

print(f"Loading log: {LOG}")
log_df = pd.read_csv(LOG, low_memory=False)
log_df.columns = [c.strip() for c in log_df.columns]

windows = analyze.detect_stall_windows(log_df, flap_bins)
runs = analyze.build_runs_from_windows(log_df, windows)

# Re-compute per-run OLS fits + quality so we can pick the best
per_run_ols = {r.run_id: analyze.fit_run(r, "OLS") for r in runs}
per_run_wls = {r.run_id: analyze.fit_run(r, "WLS") for r in runs}
quality = analyze.rank_runs(runs, per_run_ols)

# Find the highest-scoring run (optionally filtered by flap)
candidates = list(quality.values())
if args.flap is not None:
    candidates = [q for q in candidates if q.flap == args.flap]
    if not candidates:
        sys.exit(f"No runs at flap {args.flap}°")
best = max(candidates, key=lambda q: q.score)
best_run = next(r for r in runs if r.run_id == best.run_id)
print(f"\nBest run: {best.run_id} (flap {best.flap}°, score {best.score:.3f})")
print(f"  IAS R²: {best.ias_r2:.4f}   CP R²: {best.cp_r2:.4f}")
print(f"  Decel: {best.decel_mean:+.2f} ± {best.decel_std:.2f} kt/s")
print(f"  Window: {best.n_points} points, ~{best.n_points / 50:.1f} s")

# OLS fit for the best run, plus device cfg for that flap
ols_fit = per_run_ols[best.run_id]
wls_fit = per_run_wls[best.run_id]
flap_cfg = cfg.flaps[best.flap]

# Slice data through the stall break
hi = best_run.stall_index + 1
ias_raw = best_run.ias_raw[:hi]
cp_smooth = best_run.cp_smooth[:hi]
derived_aoa = best_run.derived_aoa[:hi]

# Time axis (seconds from window start)
n = len(ias_raw)
t_s = np.arange(n) / 50.0  # 50 Hz log

# Build a 4-panel figure: time series + CP→AOA + 1/IAS² → AOA + bottom-line summary
fig, axs = plt.subplots(2, 2, figsize=(14, 10))

# --- Panel 1: time series of IAS and AOA ---
ax = axs[0, 0]
ax2 = ax.twinx()
ax.plot(t_s, ias_raw, color="#1f77b4", linewidth=1.0, label="IAS (raw)")
ax.plot(t_s, best_run.ias_smooth[:hi], color="#1f77b4", linewidth=2.0, alpha=0.6,
        label="IAS (smoothed)")
ax2.plot(t_s, derived_aoa, color="#d62728", linewidth=1.0, alpha=0.7,
         label="DerivedAOA")
ax.axvline(t_s[-1], color="black", linestyle="--", alpha=0.4, label=None)
ax.text(t_s[-1] - 0.5, ax.get_ylim()[0] + 2, "stall break →", ha="right", fontsize=9, color="black")

ax.set_xlabel("Time in window (s)")
ax.set_ylabel("IAS (kt)", color="#1f77b4")
ax2.set_ylabel("DerivedAOA (deg)", color="#d62728")
ax.tick_params(axis="y", labelcolor="#1f77b4")
ax2.tick_params(axis="y", labelcolor="#d62728")
ax.set_title(f"{best.run_id}: IAS bleed-down to stall ({n / 50:.1f} s)")
ax.grid(True, alpha=0.3)

lines1, labels1 = ax.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax.legend(lines1 + lines2, labels1 + labels2, loc="upper right", fontsize=8)

# --- Panel 2: CP → AOA scatter with the runtime polynomials ---
ax = axs[0, 1]
ax.scatter(cp_smooth, derived_aoa, s=10, alpha=0.5, color="#1f77b4",
           label=f"data (n={n})")

cp_grid = np.linspace(cp_smooth.min(), cp_smooth.max(), 200)

# Device cfg polynomial (X3=0 for master)
_, x2_dev, x1_dev, x0_dev = flap_cfg.aoa_curve
y_dev = x2_dev * cp_grid * cp_grid + x1_dev * cp_grid + x0_dev
ax.plot(cp_grid, y_dev, color="#d62728", linestyle="--", linewidth=2.5,
        label=f"device cfg ({x2_dev:+.2f}·CP² {x1_dev:+.2f}·CP {x0_dev:+.2f})")

# OLS single fit polynomial
y_ols = ols_fit.cp_a2 * cp_grid * cp_grid + ols_fit.cp_a1 * cp_grid + ols_fit.cp_a0
ax.plot(cp_grid, y_ols, color="#000000", linestyle="-", linewidth=2.0,
        label=f"OLS this run ({ols_fit.cp_a2:+.2f}·CP² {ols_fit.cp_a1:+.2f}·CP {ols_fit.cp_a0:+.2f})")

# WLS single fit polynomial
y_wls = wls_fit.cp_a2 * cp_grid * cp_grid + wls_fit.cp_a1 * cp_grid + wls_fit.cp_a0
ax.plot(cp_grid, y_wls, color="#2ca02c", linestyle="-", linewidth=1.5,
        label=f"WLS this run ({wls_fit.cp_a2:+.2f}·CP² {wls_fit.cp_a1:+.2f}·CP {wls_fit.cp_a0:+.2f})")

ax.set_xlabel("CP = P45Smoothed / PfwdSmoothed")
ax.set_ylabel("AOA (deg)")
ax.set_title("Runtime CP→AOA polynomial — what flies in the box at 50 Hz")
ax.legend(loc="upper left", fontsize=8, framealpha=0.9)
ax.grid(True, alpha=0.3)

# --- Panel 3: 1/IAS² → AOA scatter with physics fits ---
ax = axs[1, 0]
x_inv = 1.0 / (ias_raw ** 2)
ax.scatter(x_inv, derived_aoa, s=10, alpha=0.5, color="#1f77b4",
           label=f"data (n={n})")

x_grid = np.linspace(x_inv.min(), x_inv.max(), 200)
y_dev_phys = flap_cfg.kfit * x_grid + flap_cfg.alpha0
y_ols_phys = ols_fit.K * x_grid + ols_fit.alpha0
y_wls_phys = wls_fit.K * x_grid + wls_fit.alpha0

ax.plot(x_grid, y_dev_phys, color="#d62728", linestyle="--", linewidth=2.5,
        label=f"device cfg (K={flap_cfg.kfit:.0f}, α₀={flap_cfg.alpha0:+.2f})")
ax.plot(x_grid, y_ols_phys, color="#000000", linestyle="-", linewidth=2.0,
        label=f"OLS this run (K={ols_fit.K:.0f}, α₀={ols_fit.alpha0:+.2f})")
ax.plot(x_grid, y_wls_phys, color="#2ca02c", linestyle="-", linewidth=1.5,
        label=f"WLS this run (K={wls_fit.K:.0f}, α₀={wls_fit.alpha0:+.2f})")

# Secondary IAS axis at top
ax2 = ax.twiny()
ias_ticks = [90, 80, 70, 60, 55, 50]
ias_tick_pos = [1.0 / (v * v) for v in ias_ticks]
mask = [(p >= x_inv.min()) and (p <= x_inv.max()) for p in ias_tick_pos]
ax2.set_xlim(ax.get_xlim())
ax2.set_xticks([p for p, m in zip(ias_tick_pos, mask) if m])
ax2.set_xticklabels([f"{v}" for v, m in zip(ias_ticks, mask) if m])
ax2.set_xlabel("IAS (kt)")

ax.set_xlabel("1 / IAS²  (kt⁻²)")
ax.set_ylabel("DerivedAOA (deg)")
ax.set_title("Physics fit — DerivedAOA = K/IAS² + α₀ (used to derive setpoints)")
ax.legend(loc="upper left", fontsize=8, framealpha=0.9)
ax.grid(True, alpha=0.3)

# --- Panel 4: setpoint IAS comparison ---
ax = axs[1, 1]
ax.axis("off")

# Compute setpoint IAS for device cfg vs OLS this run
dev_sps = analyze.Setpoints(
    ldmax=flap_cfg.ldmax, os_fast=flap_cfg.os_fast, os_slow=flap_cfg.os_slow,
    stall_warn=flap_cfg.stall_warn, stall=flap_cfg.stall,
    maneuvering=flap_cfg.maneuvering,
)
dev_sp_ias = analyze.setpoint_ias_table(flap_cfg.kfit, flap_cfg.alpha0, dev_sps)
ols_sps = analyze.derive_setpoints(ols_fit.K, ols_fit.alpha0, ols_fit.alpha_stall,
                                    ols_fit.stall_ias, best.flap, cfg.aircraft)
ols_sp_ias = analyze.setpoint_ias_table(ols_fit.K, ols_fit.alpha0, ols_sps)

text = f"""Run: {best.run_id}   (flap {best.flap}°)
Quality score: {best.score:.3f}  (KEEP)
Window: {n} points, {n / 50:.1f} s

Decel rate: {best.decel_mean:+.2f} ± {best.decel_std:.2f} kt/s
IAS-AOA R² (OLS): {ols_fit.ias_to_aoa_r2:.4f}
IAS-AOA R² (WLS): {wls_fit.ias_to_aoa_r2:.4f}
CP-AOA R² (OLS):  {ols_fit.cp_r2:.4f}
CP-AOA R² (WLS):  {wls_fit.cp_r2:.4f}

Stall point (peak smoothed CP):
  IAS    = {best_run.stall_ias:.1f} kt
  CP     = {best_run.cp_smooth[best_run.stall_index]:+.3f}
  AOA    = {best_run.derived_aoa[best_run.stall_index]:+.2f}°

Physics fit:
  K       OLS={ols_fit.K:.0f}  WLS={wls_fit.K:.0f}  cfg={flap_cfg.kfit:.0f}
  α₀      OLS={ols_fit.alpha0:+.3f}  WLS={wls_fit.alpha0:+.3f}  cfg={flap_cfg.alpha0:+.3f}
  α_stall OLS={ols_fit.alpha_stall:+.3f}  WLS={wls_fit.alpha_stall:+.3f}  cfg={flap_cfg.alpha_stall:+.3f}

Setpoint IAS at 1G (this run's OLS vs device cfg):
"""

text += f"  {'Setpoint':<13} {'cfg':>7} {'this run':>10} {'Δ kt':>8}\n"
for name in ("LDmax", "OS_fast", "OS_slow", "StallWarn", "Stall", "Maneuvering"):
    cur = dev_sp_ias.get(name, float("nan"))
    new = ols_sp_ias.get(name, float("nan"))
    diff = new - cur
    text += f"  {name:<13} {cur:7.1f} {new:10.1f} {diff:+8.1f}\n"

ax.text(0.02, 0.98, text, transform=ax.transAxes, fontsize=9, family="monospace",
        verticalalignment="top")

flap_label = f" at flap {args.flap}°" if args.flap is not None else ""
fig.suptitle(f"Best run analysis — {best.run_id}  (highest quality score{flap_label})",
             fontsize=14, weight="bold")
fig.tight_layout(rect=(0, 0, 1, 0.97))

out_path = OUT / f"best_run_{best.run_id}.png"
fig.savefig(out_path, dpi=120)
plt.close(fig)
print(f"\nSaved: {out_path}")
