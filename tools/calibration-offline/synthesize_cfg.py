#!/usr/bin/env python3
"""Compute a "representative" per-flap calibration by combining multiple stall
runs in several different ways, then print a side-by-side comparison plus a
"recommended" drop-in XML replacement for the device config's FLAP_POSITION
blocks.

This script is downstream of ``analyze.py`` — it re-runs the same slicer,
per-run OLS fits, and quality ranking, then aggregates across runs to produce
a single robust calibration per flap.

Approaches compared per flap:
  A. cfg                current device config (baseline)
  B. single best R²     the individual run with the highest IAS-to-AOA R²
                        (what ``analyze.py``'s "OLS single-best" shows)
  C. median per-run     median K / α₀ / α_stall across all non-DISCARD runs
  D. weighted median    same as C but weighted by each run's IAS-to-AOA R²
  E. concat stacked     what ``analyze.py``'s "OLS stacked-all" shows
  F. RECOMMENDED        weighted-median K / α₀, α_stall tied to the median
                        observed stall IAS across runs, CP polynomial fit
                        once on the concatenated KEEP-run data

The recommended (F) pick uses the most stable parts of the per-run fits
(K from the line slope, α₀ from the line intercept, both medians across runs)
and ties α_stall to the actual stall IAS observed across all runs rather than
extrapolating from any single run's last few points.

Usage::

    uv run --with numpy --with scipy --with pandas --with matplotlib python3 \\
        synthesize_cfg.py \\
        --log     path/to/log_NNN.csv \\
        --cfg     path/to/sam_onspeed.cfg \\
        --weight  2190
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import numpy as np
import pandas as pd

import analyze


def weighted_median(values: list[float], weights: list[float]) -> float:
    """Weighted median via the CDF-at-50% definition."""
    if not values:
        return float("nan")
    arr = sorted(zip(values, weights))
    total = sum(w for _, w in arr)
    half = total / 2
    cum = 0.0
    for v, w in arr:
        cum += w
        if cum >= half:
            return v
    return arr[-1][0]


def compute_recommendation(runs_list: list[analyze.Run],
                           per_run: dict[str, analyze.RunFit],
                           quality: dict[str, analyze.RunQuality]) -> dict:
    """The "F" recommendation:

    1. K and α₀: weighted median (by R²) across the per-run linear fits.
       Linear-fit coefficients are decoupled enough that median-across-runs
       is statistically robust.
    2. α_stall: tied to the median observed stall IAS via the synthesized
       K and α₀, not the per-run α_stall values — this avoids the noise
       that comes from where exactly the stall break landed in each run.
    3. CP polynomial: fit once on the concatenated KEEP-run data with OLS.
       Quadratic coefficients are coupled, so a median-across-runs on (a2,
       a1, a0) gives a polynomial that doesn't pass through any run's data
       cleanly — concat-and-fit is the right move.

    Runs with the DISCARD verdict are excluded upstream.
    """
    keep_runs = [r for r in runs_list if quality[r.run_id].verdict == "KEEP"]
    if len(keep_runs) < 2:
        keep_runs = runs_list  # fall back to whatever non-DISCARD runs we have

    Ks = [per_run[r.run_id].K for r in keep_runs]
    a0s = [per_run[r.run_id].alpha0 for r in keep_runs]
    stall_iases = [r.stall_ias for r in keep_runs]
    r2s = [per_run[r.run_id].ias_to_aoa_r2 for r in keep_runs]

    K_med = weighted_median(Ks, r2s)
    a0_med = weighted_median(a0s, r2s)
    Vs_med = float(np.median(stall_iases))
    a_stall_obs = K_med / (Vs_med * Vs_med) + a0_med

    cp_chunks = [r.cp_smooth[:r.stall_index + 1] for r in keep_runs]
    aoa_chunks = [r.derived_aoa[:r.stall_index + 1] for r in keep_runs]
    cp_concat = np.concatenate(cp_chunks)
    aoa_concat = np.concatenate(aoa_chunks)
    (cp_a2, cp_a1, cp_a0), _, _ = analyze.wls_quadratic(cp_concat, aoa_concat)

    return {
        "K": K_med, "alpha0": a0_med, "alpha_stall": a_stall_obs,
        "stall_ias": Vs_med,
        "cp_a2": cp_a2, "cp_a1": cp_a1, "cp_a0": cp_a0,
        "n_runs": len(keep_runs),
    }


def _print_variant_row(label: str, K: float, a0: float, a_stall: float,
                       stall_ias: float, cp_a2: float, cp_a1: float, cp_a0: float,
                       extra: str = "") -> None:
    print(f"  {label:<20} {K:>8.0f} {a0:>+9.3f} {a_stall:>+9.3f} "
          f"{stall_ias:>10.2f} {cp_a2:>+10.3f} {cp_a1:>+10.3f} {cp_a0:>+10.3f}  {extra}")


def print_comparison(cfg: analyze.DeviceCfg,
                     runs_by_flap: dict[int, list[analyze.Run]],
                     per_run_ols: dict[str, analyze.RunFit],
                     quality: dict[str, analyze.RunQuality]) -> dict[int, dict]:
    """Print the per-flap comparison table and return the F recommendations."""
    flap_bins = sorted(cfg.flaps.keys())
    print()
    print("=" * 100)
    print(f"  {'Variant':<20} {'K':>8} {'α₀':>9} {'α_stall':>9} {'stall IAS':>10} "
          f"{'CP a2':>10} {'CP a1':>10} {'CP a0':>10}")
    print("=" * 100)

    recommendations: dict[int, dict] = {}
    for flap in flap_bins:
        rs = runs_by_flap.get(flap, [])
        if not rs:
            continue
        fc = cfg.flaps[flap]
        print(f"{flap:>3}°   ({len(rs)} runs)")

        # A. cfg
        cfg_stall = (math.sqrt(fc.kfit / (fc.alpha_stall - fc.alpha0))
                     if fc.alpha_stall > fc.alpha0 else float("nan"))
        _print_variant_row("A cfg", fc.kfit, fc.alpha0, fc.alpha_stall, cfg_stall,
                           fc.aoa_curve[1], fc.aoa_curve[2], fc.aoa_curve[3])

        # B. single best R²
        best_r = max(rs, key=lambda r: per_run_ols[r.run_id].ias_to_aoa_r2)
        bf = per_run_ols[best_r.run_id]
        _print_variant_row("B single best (OLS)", bf.K, bf.alpha0, bf.alpha_stall,
                           bf.stall_ias, bf.cp_a2, bf.cp_a1, bf.cp_a0,
                           extra=f"({best_r.run_id}, R²={bf.ias_to_aoa_r2:.4f})")

        # C. median per-run
        Ks = [per_run_ols[r.run_id].K for r in rs]
        a0s = [per_run_ols[r.run_id].alpha0 for r in rs]
        a_stalls = [per_run_ols[r.run_id].alpha_stall for r in rs]
        cp_a2s = [per_run_ols[r.run_id].cp_a2 for r in rs]
        cp_a1s = [per_run_ols[r.run_id].cp_a1 for r in rs]
        cp_a0s = [per_run_ols[r.run_id].cp_a0 for r in rs]
        K_med = float(np.median(Ks))
        a0_med = float(np.median(a0s))
        a_stall_med = float(np.median(a_stalls))
        Vs_med = float(np.median([r.stall_ias for r in rs]))
        _print_variant_row("C median per-run", K_med, a0_med, a_stall_med, Vs_med,
                           float(np.median(cp_a2s)), float(np.median(cp_a1s)),
                           float(np.median(cp_a0s)))

        # D. weighted median
        r2s = [per_run_ols[r.run_id].ias_to_aoa_r2 for r in rs]
        _print_variant_row("D weighted median",
                           weighted_median(Ks, r2s),
                           weighted_median(a0s, r2s),
                           weighted_median(a_stalls, r2s), Vs_med,
                           weighted_median(cp_a2s, r2s),
                           weighted_median(cp_a1s, r2s),
                           weighted_median(cp_a0s, r2s))

        # E. concat stacked OLS
        stacked_fit = analyze.fit_stacked(rs, "OLS")
        _print_variant_row("E concat stacked",
                           stacked_fit.K, stacked_fit.alpha0, stacked_fit.alpha_stall,
                           stacked_fit.stall_ias, stacked_fit.cp_a2, stacked_fit.cp_a1,
                           stacked_fit.cp_a0)

        # F. recommendation
        rec = compute_recommendation(rs, per_run_ols, quality)
        _print_variant_row("F RECOMMENDED",
                           rec["K"], rec["alpha0"], rec["alpha_stall"],
                           rec["stall_ias"], rec["cp_a2"], rec["cp_a1"], rec["cp_a0"],
                           extra=f"({rec['n_runs']} KEEP)")
        recommendations[flap] = rec

        # Per-run spread diagnostics
        print(f"  Per-run K range: {min(Ks):.0f} to {max(Ks):.0f} "
              f"(spread {(max(Ks) - min(Ks)) / np.median(Ks) * 100:.1f}%)")
        print(f"  Per-run α₀ range: {min(a0s):+.3f} to {max(a0s):+.3f} "
              f"(spread {max(a0s) - min(a0s):.3f}°)")
        print(f"  Per-run α_stall range: {min(a_stalls):+.3f} to {max(a_stalls):+.3f} "
              f"(spread {max(a_stalls) - min(a_stalls):.3f}°)")
        print(f"  Per-run stall IAS range: "
              f"{min(r.stall_ias for r in rs):.2f} to {max(r.stall_ias for r in rs):.2f}")
        print()

    return recommendations


def print_setpoint_table(cfg: analyze.DeviceCfg,
                         recommendations: dict[int, dict],
                         current_weight_lb: float) -> None:
    print()
    print("=" * 100)
    print(f"Setpoint IAS at 1G — current cfg vs recommendation (F)  "
          f"[current weight {current_weight_lb:.0f} lb]")
    print("=" * 100)

    for flap in sorted(recommendations.keys()):
        fc = cfg.flaps[flap]
        rec = recommendations[flap]

        cfg_sps = analyze.Setpoints(
            ldmax=fc.ldmax, os_fast=fc.os_fast, os_slow=fc.os_slow,
            stall_warn=fc.stall_warn, stall=fc.stall, maneuvering=fc.maneuvering,
        )
        cfg_ias = analyze.setpoint_ias_table(fc.kfit, fc.alpha0, cfg_sps)

        rec_sps = analyze.derive_setpoints(
            rec["K"], rec["alpha0"], rec["alpha_stall"], rec["stall_ias"],
            flap, cfg.aircraft, current_weight=current_weight_lb,
        )
        rec_ias = analyze.setpoint_ias_table(rec["K"], rec["alpha0"], rec_sps)

        print()
        print(f"Flap {flap}°:")
        print(f"  {'Setpoint':<12} {'AOA cfg':>9} {'AOA rec':>9} "
              f"{'IAS cfg':>9} {'IAS rec':>9} {'Δ kt':>8}")
        for sp_name, attr in [
            ("LDmax", "ldmax"), ("OS_fast", "os_fast"), ("OS_slow", "os_slow"),
            ("StallWarn", "stall_warn"), ("Stall", "stall"), ("Maneuvering", "maneuvering"),
        ]:
            cfg_aoa = getattr(cfg_sps, attr)
            rec_aoa = getattr(rec_sps, attr)
            cfg_v = cfg_ias[sp_name]
            rec_v = rec_ias[sp_name]
            diff = rec_v - cfg_v
            print(f"  {sp_name:<12} {cfg_aoa:>+9.2f} {rec_aoa:>+9.2f} "
                  f"{cfg_v:>9.1f} {rec_v:>9.1f} {diff:>+8.1f}")


def print_gap_analysis(cfg: analyze.DeviceCfg,
                       recommendations: dict[int, dict],
                       current_weight_lb: float) -> None:
    print()
    print("=" * 100)
    print("Inter-flap transition gaps under each option")
    print("=" * 100)
    print()

    def build_ias_dicts(flap: int) -> tuple[dict[str, float], dict[str, float]]:
        fc = cfg.flaps[flap]
        cfg_sps = analyze.Setpoints(
            ldmax=fc.ldmax, os_fast=fc.os_fast, os_slow=fc.os_slow,
            stall_warn=fc.stall_warn, stall=fc.stall, maneuvering=fc.maneuvering,
        )
        cfg_ias = analyze.setpoint_ias_table(fc.kfit, fc.alpha0, cfg_sps)

        rec = recommendations[flap]
        rec_sps = analyze.derive_setpoints(rec["K"], rec["alpha0"], rec["alpha_stall"],
                                           rec["stall_ias"], flap, cfg.aircraft,
                                           current_weight=current_weight_lb)
        rec_ias = analyze.setpoint_ias_table(rec["K"], rec["alpha0"], rec_sps)
        return cfg_ias, rec_ias

    flap_bins = sorted(recommendations.keys())
    for i in range(len(flap_bins) - 1):
        lo, hi = flap_bins[i], flap_bins[i + 1]
        lo_cfg, lo_rec = build_ias_dicts(lo)
        hi_cfg, hi_rec = build_ias_dicts(hi)
        cfg_gap = lo_cfg["OS_slow"] - hi_cfg["OS_fast"]
        rec_gap = lo_rec["OS_slow"] - hi_rec["OS_fast"]
        print(f"  {lo}° → {hi}°  "
              f"cfg: {lo_cfg['OS_slow']:.1f} − {hi_cfg['OS_fast']:.1f} = {cfg_gap:+.1f} kt  |  "
              f"rec: {lo_rec['OS_slow']:.1f} − {hi_rec['OS_fast']:.1f} = {rec_gap:+.1f} kt")


def print_recommended_xml(cfg: analyze.DeviceCfg,
                          recommendations: dict[int, dict],
                          current_weight_lb: float) -> None:
    print()
    print("=" * 100)
    print("Recommended FLAP_POSITION blocks (drop-in replacement)")
    print("=" * 100)
    print()
    for flap in sorted(recommendations.keys()):
        rec = recommendations[flap]
        rec_sps = analyze.derive_setpoints(
            rec["K"], rec["alpha0"], rec["alpha_stall"], rec["stall_ias"],
            flap, cfg.aircraft, current_weight=current_weight_lb,
        )
        print(f"<!-- Flap {flap}° — median-of-{rec['n_runs']}-runs synthesis, "
              f"current weight {current_weight_lb:.0f} lb -->")
        print("<FLAP_POSITION>")
        print(f"    <DEGREES>{flap}</DEGREES>")
        print(f"    <LDMAXAOA>{rec_sps.ldmax:.4f}</LDMAXAOA>")
        print(f"    <ONSPEEDFASTAOA>{rec_sps.os_fast:.4f}</ONSPEEDFASTAOA>")
        print(f"    <ONSPEEDSLOWAOA>{rec_sps.os_slow:.4f}</ONSPEEDSLOWAOA>")
        print(f"    <STALLWARNAOA>{rec_sps.stall_warn:.4f}</STALLWARNAOA>")
        print(f"    <STALLAOA>{rec_sps.stall:.4f}</STALLAOA>")
        print(f"    <MANAOA>{rec_sps.maneuvering:.4f}</MANAOA>")
        print(f"    <ALPHA0>{rec['alpha0']:.4f}</ALPHA0>")
        print(f"    <ALPHASTALL>{rec['alpha_stall']:.4f}</ALPHASTALL>")
        print(f"    <KFIT>{rec['K']:.3f}</KFIT>")
        print("    <AOA_CURVE>")
        print("        <TYPE>1</TYPE>")
        print("        <X3>0</X3>")
        print(f"        <X2>{rec['cp_a2']:.6f}</X2>")
        print(f"        <X1>{rec['cp_a1']:.6f}</X1>")
        print(f"        <X0>{rec['cp_a0']:.6f}</X0>")
        print("    </AOA_CURVE>")
        print("</FLAP_POSITION>")
        print()


def run_synthesis(log_path: Path, cfg_path: Path, current_weight_lb: float):
    """End-to-end synthesis pipeline. Returns a tuple of
    (cfg, runs_by_flap, per_run_ols, quality, recommendations) for callers
    (e.g. write_patched_cfg.py) that want to reuse the computed state.
    """
    cfg = analyze.parse_config(cfg_path)
    flap_bins = sorted(cfg.flaps.keys())

    print(f"Loading log: {log_path}")
    log_df = pd.read_csv(log_path, low_memory=False)
    log_df.columns = [c.strip() for c in log_df.columns]

    windows = analyze.detect_stall_windows(log_df, flap_bins)
    runs = analyze.build_runs_from_windows(log_df, windows)
    per_run_ols = {r.run_id: analyze.fit_run(r, "OLS") for r in runs}
    quality = analyze.rank_runs(runs, per_run_ols)

    runs_by_flap: dict[int, list[analyze.Run]] = {}
    for r in runs:
        if quality[r.run_id].verdict == "DISCARD":
            continue
        runs_by_flap.setdefault(r.flap_bin, []).append(r)

    recommendations = print_comparison(cfg, runs_by_flap, per_run_ols, quality)
    print_setpoint_table(cfg, recommendations, current_weight_lb)
    print_gap_analysis(cfg, recommendations, current_weight_lb)
    print_recommended_xml(cfg, recommendations, current_weight_lb)

    return cfg, runs_by_flap, per_run_ols, quality, recommendations


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Compare calibration synthesis strategies and emit a recommended cfg.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument("--log", type=Path, required=True,
                    help="Flight log CSV from the OnSpeed SD card (50 Hz).")
    ap.add_argument("--cfg", type=Path, required=True,
                    help="On-device config XML that was running when the log was captured.")
    ap.add_argument("--weight", type=float, required=True,
                    help="Aircraft weight in pounds during the calibration flight. "
                         "Only affects flap-0 LDmax IAS via sqrt(W/W_max) × Vbg.")
    args = ap.parse_args()

    run_synthesis(args.log, args.cfg, args.weight)
    return 0


if __name__ == "__main__":
    sys.exit(main())
