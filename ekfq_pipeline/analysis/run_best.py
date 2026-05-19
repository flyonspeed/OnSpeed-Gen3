"""Replay a log with the best Optuna parameters and produce residual plots.

Usage:
    python3 analysis/run_best.py                       # uses studies/ekf9_full.db
    python3 analysis/run_best.py --study-name ekf9_v2
    python3 analysis/run_best.py --rows 200000         # subset

Outputs (PNG):
    analysis/out/residuals_pitch.png
    analysis/out/residuals_roll.png
    analysis/out/residuals_vz.png
    analysis/out/residuals_alpha.png
    analysis/out/biases_trace.png
    analysis/out/altitude_trace.png

And a text summary on stdout with train/val RMSE on each channel.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import numpy as np
import optuna
import pandas as pd

from onspeed_ekf import (
    EKFQConfig, PipelineQuat, PipelineQuatConfig,
    load_log,
)
from tune_ekf import build_train_val_split


_FT_TO_M = 0.3048


def _cfgs_from_params(p: dict):
    """Construct an (EKFQConfig, PipelineQuatConfig) pair from an Optuna
    best-trial parameter dict. Installation biases fall back to the
    PipelineQuatConfig defaults if the study didn't search them
    (current studies pin them to the per-aircraft calibration)."""
    ekf_cfg = EKFQConfig(
        q_quat       = p["q_quat"],
        q_bias       = p["q_bias"],
        q_z          = p["q_z"],
        q_vz         = p["q_vz"],
        q_b_az       = p["q_b_az"],
        q_beta       = p["q_beta"],
        r_ax         = p["r_ax"],
        r_ay         = p["r_ay"],
        r_az         = p["r_az"],
        r_baro       = p["r_baro"],
        r_beta_prior = p["r_beta_prior"],
        r_bias_prior = p["r_bias_prior"],
        k_beta_R     = p["k_beta_R"],
    )
    pipe_cfg = PipelineQuatConfig(
        pitch_bias_deg     = p.get("pitch_bias_deg",
                                   PipelineQuatConfig.__dataclass_fields__["pitch_bias_deg"].default),
        roll_bias_deg      = p.get("roll_bias_deg",
                                   PipelineQuatConfig.__dataclass_fields__["roll_bias_deg"].default),
        accel_ema_alpha    = p["accel_ema_alpha"],
        comp_fade_tau_sec  = p["comp_fade_tau"],
        ias_alive_kt       = p["ias_alive_kt"],
        tasdot_ema_alpha   = p["tasdot_ema_alpha"],
    )
    return ekf_cfg, pipe_cfg


def _channel_rmse(pred: np.ndarray, truth: np.ndarray, mask: np.ndarray) -> float:
    if mask.sum() == 0:
        return float("nan")
    res = pred[mask] - truth[mask]
    return float(np.sqrt(np.mean(res ** 2)))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default="log_007_fixed.csv")
    ap.add_argument("--study-name", default="ekfq_v15")
    ap.add_argument("--storage", default=None)
    ap.add_argument("--rows", type=int, default=0)
    args = ap.parse_args()

    storage = args.storage or f"sqlite:///studies/{args.study_name}.db"
    study = optuna.load_study(study_name=args.study_name, storage=storage)
    print(f"Study has {len(study.trials)} trials. Best value: {study.best_value:.4f}", flush=True)
    p = study.best_trial.params
    print("Best params:")
    for k, v in p.items():
        print(f"  {k} = {v}")

    log = load_log(args.log)
    if args.rows:
        class _Sub: pass
        sub = _Sub()
        sub.df = log.df.iloc[: args.rows].reset_index(drop=True)
        sub.dt = log.dt[: args.rows]
        sub.fresh_vn   = log.fresh_vn[: args.rows]
        sub.fresh_aoa  = log.fresh_aoa[: args.rows]
        sub.fresh_baro = log.fresh_baro[: args.rows]
        sub.fresh_boom = log.fresh_boom[: args.rows]
        sub.n = args.rows
        log = sub

    ekf_cfg, pipe_cfg = _cfgs_from_params(p)
    pipe = PipelineQuat(ekf_cfg, pipe_cfg)
    h = pipe.run(log)

    train_mask, val_mask = build_train_val_split(log.df)
    fresh_train = log.fresh_vn & train_mask
    fresh_val   = log.fresh_vn & val_mask

    df = log.df
    pitch_truth = df["vnPitch"].to_numpy(dtype=np.float32)
    roll_truth  = df["vnRoll"].to_numpy(dtype=np.float32)
    vz_truth    = df["vnVelNedDown"].to_numpy(dtype=np.float32)
    aoa_truth   = df["AngleofAttack"].to_numpy(dtype=np.float32)
    pitch_fw    = df["Pitch"].to_numpy(dtype=np.float32)
    roll_fw     = df["Roll"].to_numpy(dtype=np.float32)
    # Firmware VSI is in fpm, +up. Convert to m/s, +down.
    vsi_fw_fpm = df["VSI"].to_numpy(dtype=np.float32)
    vsi_fw_mps = -vsi_fw_fpm * 0.00508
    derived_aoa_fw = df["DerivedAOA"].to_numpy(dtype=np.float32)

    # RMSE table — truth is VN-300 ONLY. We deliberately do not compare
    # against the firmware Madgwick output here; that signal is logged in
    # the CSVs for reference but is not what we are tuning to match.
    fresh_all = log.fresh_vn
    aoa_valid = h["ias_alive"] & np.isfinite(df["AngleofAttack"].to_numpy())

    # Rate RMS at fresh-VN samples — quantifies signal smoothness/wiggle.
    def _rate_rmse(arr: np.ndarray, truth: np.ndarray, mask: np.ndarray,
                   times_arr: np.ndarray) -> float:
        idx = np.where(mask)[0]
        if idx.size < 2:
            return float("nan")
        dt = np.diff(times_arr[idx])
        dt = np.where(dt > 1e-6, dt, 1.0 / 208.0)
        d_pred  = np.diff(arr[idx])
        d_truth = np.diff(truth[idx])
        # Wrap if dealing with angles (we use this only for non-wrapping
        # channels here; roll wrap is handled upstream where needed).
        rate_res = (d_pred - d_truth) / dt
        return float(np.sqrt(np.mean(rate_res ** 2)))

    times_arr = np.arange(log.n) * (1.0 / 208.0)
    print()
    print("=== Tracking VS VN-300 (truth) — fresh-VN samples only ===")
    print(f"                       full       train         val")
    print(f"  pitch  RMSE   :  {_channel_rmse(h['pitch_deg'], pitch_truth, fresh_all):>7.3f}°     {_channel_rmse(h['pitch_deg'], pitch_truth, fresh_train):>7.3f}°     {_channel_rmse(h['pitch_deg'], pitch_truth, fresh_val):>7.3f}°")
    print(f"  roll   RMSE   :  {_channel_rmse(h['roll_deg'],  roll_truth,  fresh_all):>7.3f}°     {_channel_rmse(h['roll_deg'],  roll_truth,  fresh_train):>7.3f}°     {_channel_rmse(h['roll_deg'],  roll_truth,  fresh_val):>7.3f}°")
    print(f"  vz     RMSE   :  {_channel_rmse(h['vz_mps'],    vz_truth,    fresh_all):>7.3f}      {_channel_rmse(h['vz_mps'],    vz_truth,    fresh_train):>7.3f}      {_channel_rmse(h['vz_mps'],    vz_truth,    fresh_val):>7.3f}")
    print()
    print("=== Rate-residual RMSE (deg/s, deg/s, (m/s)/s) — wiggle metric ===")
    print(f"  pitch_dot     :  {_rate_rmse(h['pitch_deg'], pitch_truth, fresh_all, times_arr):>7.3f}     {_rate_rmse(h['pitch_deg'], pitch_truth, fresh_train, times_arr):>7.3f}     {_rate_rmse(h['pitch_deg'], pitch_truth, fresh_val, times_arr):>7.3f}")
    print(f"  roll_dot      :  {_rate_rmse(h['roll_deg'],  roll_truth,  fresh_all, times_arr):>7.3f}     {_rate_rmse(h['roll_deg'],  roll_truth,  fresh_train, times_arr):>7.3f}     {_rate_rmse(h['roll_deg'],  roll_truth,  fresh_val, times_arr):>7.3f}")
    print(f"  vz_dot        :  {_rate_rmse(h['vz_mps'],    vz_truth,    fresh_all, times_arr):>7.3f}     {_rate_rmse(h['vz_mps'],    vz_truth,    fresh_train, times_arr):>7.3f}     {_rate_rmse(h['vz_mps'],    vz_truth,    fresh_val, times_arr):>7.3f}")

    # P45 divergence watchdog: alpha_kinematic vs firmware's calibrated
    # AngleofAttack. The two should agree to within a few degrees during
    # clean cruise and diverge predictably in turns/pulls.
    aoa_press = df["AngleofAttack"].to_numpy(dtype=np.float32)
    alpha_diff = h["alpha_deg"] - aoa_press
    valid_diff = aoa_valid & np.isfinite(alpha_diff)
    coord_mask = valid_diff & (np.abs(roll_truth) < 30.0)  # only "coordinated"-ish
    print()
    print("=== P45 divergence watchdog: alpha_kinematic vs AngleofAttack ===")
    if valid_diff.sum():
        print(f"  overall     : mean={alpha_diff[valid_diff].mean():+.2f}°  std={alpha_diff[valid_diff].std():.2f}°")
    if coord_mask.sum():
        print(f"  low-bank<30°: mean={alpha_diff[coord_mask].mean():+.2f}°  std={alpha_diff[coord_mask].std():.2f}°")
    # Count "sustained divergence" events: >5° for >1 s (208 samples)
    big = np.abs(alpha_diff) > 5.0
    if valid_diff.any():
        run_start = None
        events = 0
        for i in range(len(big)):
            if big[i] and valid_diff[i]:
                if run_start is None:
                    run_start = i
            else:
                if run_start is not None and (i - run_start) > 208:
                    events += 1
                run_start = None
        print(f"  sustained-divergence events (|Δ|>5° for >1 s): {events}")

    # Final bias sanity
    print()
    print("=== Final state ===")
    print(f"  bp = {h['bp_dps'][-1]:.3f} deg/s   bq = {h['bq_dps'][-1]:.3f} deg/s   br = {h['br_dps'][-1]:.3f} deg/s")
    print(f"  b_az = {h['b_az_mps2'][-1]:.4f} m/s²")
    final_baro = df["Palt"].iloc[-1] * _FT_TO_M
    print(f"  z_ekf = {h['z_m'][-1]:.2f} m   baro = {final_baro:.2f} m   drift = {h['z_m'][-1] - final_baro:+.2f} m")

    # CSV exports — column-aligned and time-stamped so Matlab can
    # readtable(...) them directly and slice with logical indexing. Each
    # study writes to its own subfolder so v4/v5/v6/... outputs can be
    # compared side-by-side without overwriting.
    out_dir = Path("analysis/out") / args.study_name
    out_dir.mkdir(parents=True, exist_ok=True)
    times = np.arange(log.n) * (1.0 / 208.0)

    # Cast all arrays to float32 to keep CSVs lean; bool columns become 0/1.
    fresh_all_int  = log.fresh_vn.astype(np.uint8)
    fresh_aoa_int  = (h["ias_alive"] & np.isfinite(df["AngleofAttack"].to_numpy())).astype(np.uint8)
    ias_alive_int  = h["ias_alive"].astype(np.uint8)

    def _write_csv(path: Path, columns: dict[str, np.ndarray]) -> None:
        out_df = pd.DataFrame(columns)
        out_df.to_csv(path, index=False, float_format="%.6g")

    # 1. Attitude trace: EKF vs VN-300 truth vs firmware Madgwick.
    _write_csv(out_dir / "attitude.csv", {
        "t_s":           times,
        "pitch_ekf":     h["pitch_deg"],
        "pitch_vn":      pitch_truth,
        "pitch_fw":      pitch_fw,
        "roll_ekf":      h["roll_deg"],
        "roll_vn":       roll_truth,
        "roll_fw":       roll_fw,
        "yaw_ekf":       h["yaw_deg"],
        "yaw_vn":        df["vnYaw"].to_numpy(dtype=np.float32),
        "fresh_vn":      fresh_all_int,
    })

    # 2. Vertical channel (VSI + altitude).
    _write_csv(out_dir / "vertical.csv", {
        "t_s":          times,
        "vz_ekf_mps":   h["vz_mps"],
        "vz_vn_mps":    vz_truth,
        "vsi_fw_mps":   vsi_fw_mps,
        "z_ekf_m":      h["z_m"],
        "baro_z_m":     df["Palt"].to_numpy(dtype=np.float32) * _FT_TO_M,
        "fresh_vn":     fresh_all_int,
    })

    # 3. Alpha trace: kinematic EKF output vs pressure-cal AngleofAttack
    #    vs firmware EKF6 DerivedAOA, plus the divergence diff.
    aoa_press_arr = df["AngleofAttack"].to_numpy(dtype=np.float32)
    alpha_diff_arr = (h["alpha_deg"].astype(np.float32) - aoa_press_arr).astype(np.float32)
    _write_csv(out_dir / "alpha.csv", {
        "t_s":               times,
        "alpha_kin_ekf":     h["alpha_deg"],
        "alpha_pressure":    aoa_press_arr,
        "alpha_derived_fw":  derived_aoa_fw,
        "alpha_diff":        alpha_diff_arr,
        "ias_alive":         ias_alive_int,
        "aoa_valid":         fresh_aoa_int,
    })

    # 4. Beta trace + lateral-G + IAS (no truth, internal-consistency only).
    _write_csv(out_dir / "beta.csv", {
        "t_s":          times,
        "beta_ekf_deg": h["beta_deg"],
        "lateral_g":    df["LateralG"].to_numpy(dtype=np.float32),
        "ias_kt":       df["IAS"].to_numpy(dtype=np.float32),
        "vn_roll":      roll_truth,
        "yaw_rate_dps": df["YawRate"].to_numpy(dtype=np.float32),
    })

    # 5. Filter internals: bias states and ias_alive / comp_fade gates.
    _write_csv(out_dir / "biases.csv", {
        "t_s":          times,
        "bp_dps":       h["bp_dps"],
        "bq_dps":       h["bq_dps"],
        "br_dps":       h["br_dps"],
        "b_az_mps2":    h["b_az_mps2"],
        "ias_alive":    ias_alive_int,
        "comp_fade":    h["comp_fade"],
    })

    # 6. Residuals at fresh-VN timestamps — easy for Matlab to histogram
    #    or plot vs flight phase. NaN where no fresh VN sample.
    pitch_res = np.where(fresh_all_int.astype(bool), h["pitch_deg"] - pitch_truth, np.nan).astype(np.float32)
    roll_res  = np.where(fresh_all_int.astype(bool), h["roll_deg"]  - roll_truth,  np.nan).astype(np.float32)
    vz_res    = np.where(fresh_all_int.astype(bool), h["vz_mps"]    - vz_truth,    np.nan).astype(np.float32)
    # Wrap roll residual to (-180, 180]
    roll_res = ((roll_res + 180.0) % 360.0 - 180.0).astype(np.float32)
    _write_csv(out_dir / "residuals.csv", {
        "t_s":              times,
        "pitch_residual":   pitch_res,
        "roll_residual":    roll_res,
        "vz_residual":      vz_res,
        "fresh_vn":         fresh_all_int,
    })

    print()
    print(f"CSV outputs written to {out_dir}/")
    for f in sorted(out_dir.glob("*.csv")):
        rows = sum(1 for _ in open(f)) - 1
        print(f"  {f.name:18s}  {rows:>7d} rows  ({f.stat().st_size/1e6:.1f} MB)")


if __name__ == "__main__":
    main()
