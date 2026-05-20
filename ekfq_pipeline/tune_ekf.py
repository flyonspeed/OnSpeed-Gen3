"""Optuna study to tune the 9-state EKF against VN-300 truth.

Usage:
    python3 tune_ekf.py                    # default: 200 trials, full log
    python3 tune_ekf.py --trials 500
    python3 tune_ekf.py --rows 200000      # subset for fast smoke-test
    python3 tune_ekf.py --study-name ekf_run1

Outputs:
    - SQLite study DB at studies/<study-name>.db (resumable)
    - Best-trial parameters printed to stdout when finished
    - analysis/optuna_history.png trace of the search
"""

from __future__ import annotations

import argparse
import math
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import optuna
import pandas as pd
from optuna.samplers import TPESampler

from onspeed_ekf import (
    EKFQConfig, PipelineQuat, PipelineQuatConfig,
    load_log,
)
from run_host_main import run_host_main


# ---------------------------------------------------------------------------
# Loss
# ---------------------------------------------------------------------------


def _huber(residual: np.ndarray, delta: float) -> np.ndarray:
    """Scalar-valued Huber loss applied element-wise."""
    abs_r = np.abs(residual)
    out = np.where(abs_r <= delta, 0.5 * residual ** 2, delta * (abs_r - 0.5 * delta))
    return out


# Per-channel Huber transition points for the "default" loss profile.
# Errors below the knee are quadratic (penalise hard); above the knee
# they're linear (clipped, so outliers don't dominate). The "cruise-aoa"
# profile below overrides these with much tighter knees focused on the
# 0–20° pitch / 0–30° roll envelope used for AOA calibration.
_HUBER_DELTA_PITCH_DEG = 2.0
_HUBER_DELTA_ROLL_DEG  = 3.0
_HUBER_DELTA_VZ_MPS    = 2.0

_HUBER_DELTA_PITCH_RATE_DPS = 10.0
_HUBER_DELTA_ROLL_RATE_DPS  = 20.0
_HUBER_DELTA_VZ_RATE_MPS2   = 2.0


@dataclass
class LossWeights:
    # Value residuals: how close the EKFQ output is to VN-300 truth.
    pitch: float = 1.0
    roll:  float = 1.0
    vz:    float = 1.0     # vz in m/s is ~0.5 m/s noise → comparable to 1 deg
    # Rate residuals: how close the EKFQ derivative is to VN-300 derivative.
    # Weighted higher than value so smoothness/wiggle is penalised more
    # than absolute RMS — the firmware downstream prefers stable signals.
    pitch_rate: float = 1.5
    roll_rate:  float = 1.5
    vz_rate:    float = 1.5


# ---------------------------------------------------------------------------
# Loss-mode profiles
# ---------------------------------------------------------------------------
#
# Three loss shapes are available, selected via the --loss-mode flag:
#
# • "default" — 1:1:1 value weighting, 1.5 rate weights, 0.1 aerobatic
#   regime weight. Produces a filter that's "decent everywhere" but
#   trades cruise accuracy for aerobatic accuracy. Useful for surveying
#   tuning regimes; not the production loss.
#
# • "cruise-pitch" — reshapes the loss to drive cruise PITCH toward
#   zero specifically: pitch Huber knee 0.5°, pitch weight 4×, aerobatic
#   regime ignored, rate weights down to 0.3. Useful for AOA calibration
#   when only pitch matters.
#
# • "cruise-aoa" — production loss. Recognises that kinematic α =
#   θ − arcsin(vz/TAS) depends on BOTH pitch and vz: at TAS = 30 m/s a
#   1 m/s vz error already costs ≈1.9° AOA. Equal 4× weight on pitch
#   and vz value residuals plus an EXPLICIT kinematic-α residual that
#   penalises the compounded error directly. Aerobatic samples ignored;
#   the super-cruise sub-regime (|θ|≤10°, |φ|≤15°) gets an additional
#   2× boost so the heart-of-cruise samples dominate.
_LOSS_PROFILES = {
    "default": dict(
        huber_pitch=2.0, huber_roll=3.0, huber_vz=2.0,
        huber_pitch_rate=10.0, huber_roll_rate=20.0, huber_vz_rate=2.0,
        huber_alpha=10.0,  # disabled-by-loose-knee in default profile
        w_pitch=1.0, w_roll=1.0, w_vz=1.0,
        w_pitch_rate=1.5, w_roll_rate=1.5, w_vz_rate=1.5,
        w_alpha=0.0,       # alpha-kinematic residual not used in default
        cruise_weight=1.0, aero_weight=0.1, super_cruise_weight=1.0,
        alpha_min_tas_mps=12.0,
    ),
    "cruise-pitch": dict(
        huber_pitch=0.5, huber_roll=2.0, huber_vz=0.5,
        huber_pitch_rate=10.0, huber_roll_rate=20.0, huber_vz_rate=2.0,
        huber_alpha=10.0,
        w_pitch=4.0, w_roll=0.5, w_vz=0.5,
        w_pitch_rate=0.3, w_roll_rate=0.3, w_vz_rate=0.3,
        w_alpha=0.0,
        cruise_weight=1.0, aero_weight=0.0, super_cruise_weight=2.0,
        alpha_min_tas_mps=12.0,
    ),
    "cruise-aoa": dict(
        # Production loss. See the comment block at the top of
        # _LOSS_PROFILES for the rationale.
        huber_pitch=0.5, huber_roll=2.0, huber_vz=0.3,
        huber_pitch_rate=10.0, huber_roll_rate=20.0, huber_vz_rate=2.0,
        huber_alpha=0.5,   # ≤0.5° AOA error penalised quadratically
        # Pitch and vz both at 4× — they each move AOA roughly 1° per
        # natural-unit error at typical cruise TAS, so equal weights.
        w_pitch=4.0, w_roll=0.5, w_vz=4.0,
        w_pitch_rate=0.3, w_roll_rate=0.3, w_vz_rate=0.3,
        # Kinematic-α residual at 4× — direct objective for AOA cal.
        w_alpha=4.0,
        # Aerobatic samples ignored; super-cruise (|θ|≤10°,|φ|≤15°) gets
        # an additional 2× — heart-of-cruise dominates the loss.
        cruise_weight=1.0, aero_weight=0.0, super_cruise_weight=2.0,
        # Don't compute kinematic α when TAS is below stall-region threshold.
        alpha_min_tas_mps=12.0,
    ),
}


def composite_loss(
    history: dict[str, np.ndarray],
    df: pd.DataFrame,
    mask_fresh_vn: np.ndarray,
    mask_aoa_valid: np.ndarray,
    weights: LossWeights,         # legacy — only used in "default" profile
    cfg,
    profile: str = "default",
) -> tuple[float, dict[str, float]]:
    """Return (scalar loss, breakdown dict).

    Per-sample regime weighting tiers (see _LOSS_PROFILES for tuning):
      • super-cruise: |θ|≤10°, |φ|≤15°       (heart of AOA-calibration env)
      • cruise:       |θ|≤20°, |φ|≤30°       (broader normal-flight env)
      • aerobatic:    everything else        (typically near-zero weight)

    Truth is VN-300 always.
    """
    p = _LOSS_PROFILES[profile]
    # When the caller passes a non-default LossWeights, honour it — but
    # only the "default" profile uses the dataclass; other profiles bake
    # their weights into _LOSS_PROFILES.
    if profile == "default":
        wp = weights.pitch;       wr = weights.roll;       wv = weights.vz
        wpr = weights.pitch_rate; wrr = weights.roll_rate; wvr = weights.vz_rate
        w_alpha = 0.0
    else:
        wp  = p["w_pitch"];      wr  = p["w_roll"];      wv  = p["w_vz"]
        wpr = p["w_pitch_rate"]; wrr = p["w_roll_rate"]; wvr = p["w_vz_rate"]
        w_alpha = p.get("w_alpha", 0.0)

    pitch_truth = df["vnPitch"].to_numpy(dtype=np.float32)
    roll_truth  = df["vnRoll"].to_numpy(dtype=np.float32)
    vz_truth    = df["vnVelNedDown"].to_numpy(dtype=np.float32)
    aoa_press_deg = df["AngleofAttack"].to_numpy(dtype=np.float32)
    tas_kt = df["TAS"].to_numpy(dtype=np.float32)
    tas_mps_arr = tas_kt * 0.514444

    def _wrap_deg(x: np.ndarray) -> np.ndarray:
        return (x + 180.0) % 360.0 - 180.0

    # 3-tier regime mask
    abs_p = np.abs(pitch_truth)
    abs_r = np.abs(roll_truth)
    super_cruise = (abs_p <= 10.0) & (abs_r <= 15.0)
    cruise_only  = (abs_p <= 20.0) & (abs_r <= 30.0) & ~super_cruise
    aero         = ~(super_cruise | cruise_only)
    sample_w = np.where(super_cruise, p["cruise_weight"] * p["super_cruise_weight"],
               np.where(cruise_only,  p["cruise_weight"],
                                       p["aero_weight"])).astype(np.float32)

    # Value residuals
    pitch_res_v = _wrap_deg((history["pitch_deg"] - pitch_truth)[mask_fresh_vn])
    roll_res_v  = _wrap_deg((history["roll_deg"]  - roll_truth)[mask_fresh_vn])
    vz_res_v    = (history["vz_mps"] - vz_truth)[mask_fresh_vn]
    w_v         = sample_w[mask_fresh_vn]

    # Rate residuals at fresh-VN sample times
    fresh_idx = np.where(mask_fresh_vn)[0]
    if fresh_idx.size > 1:
        t_arr = np.arange(len(pitch_truth)) * (1.0 / 208.0)
        dt_pairs = np.diff(t_arr[fresh_idx])
        dt_pairs = np.where(dt_pairs > 1e-6, dt_pairs, 1.0 / 208.0)
        pitch_ekf_at_fresh = history["pitch_deg"][fresh_idx]
        pitch_vn_at_fresh  = pitch_truth[fresh_idx]
        roll_ekf_at_fresh  = history["roll_deg"][fresh_idx]
        roll_vn_at_fresh   = roll_truth[fresh_idx]
        vz_ekf_at_fresh    = history["vz_mps"][fresh_idx]
        vz_vn_at_fresh     = vz_truth[fresh_idx]
        pitch_rate_res = (np.diff(pitch_ekf_at_fresh) - np.diff(pitch_vn_at_fresh)) / dt_pairs
        roll_diff_ekf = _wrap_deg(np.diff(roll_ekf_at_fresh))
        roll_diff_vn  = _wrap_deg(np.diff(roll_vn_at_fresh))
        roll_rate_res = (roll_diff_ekf - roll_diff_vn) / dt_pairs
        vz_rate_res   = (np.diff(vz_ekf_at_fresh)   - np.diff(vz_vn_at_fresh))   / dt_pairs
        w_r = sample_w[fresh_idx[1:]]
    else:
        pitch_rate_res = roll_rate_res = vz_rate_res = w_r = np.array([])

    alpha_res = (history["alpha_deg"] - aoa_press_deg)[mask_aoa_valid]

    def _weighted_huber_rms(res, w, delta):
        if res.size == 0:
            return 0.0
        # If all weights are zero (e.g. aero_weight=0 with no cruise mass
        # somehow), return 0 to avoid div-by-zero.
        wsum = float(np.sum(w))
        if wsum <= 0.0:
            return 0.0
        return float(np.sqrt(np.sum(w * _huber(res, delta)) / wsum))

    pitch_v = _weighted_huber_rms(pitch_res_v, w_v, p["huber_pitch"])
    roll_v  = _weighted_huber_rms(roll_res_v,  w_v, p["huber_roll"])
    vz_v    = _weighted_huber_rms(vz_res_v,    w_v, p["huber_vz"])
    pitch_r = _weighted_huber_rms(pitch_rate_res, w_r, p["huber_pitch_rate"])
    roll_r  = _weighted_huber_rms(roll_rate_res,  w_r, p["huber_roll_rate"])
    vz_r    = _weighted_huber_rms(vz_rate_res,    w_r, p["huber_vz_rate"])

    alpha_l = (float(np.sqrt(np.mean(_huber(alpha_res, 3.0))))
               if alpha_res.size else 0.0)

    # Kinematic AOA residual: α_ekf − α_truth, both computed from their
    # own (pitch, vz) using the SAME logged TAS.
    #   α = θ − arcsin(vz / TAS)        (ignoring small-β correction;
    #                                    matches the AOA formula the user
    #                                    will use downstream for pressure-
    #                                    coefficient calibration).
    # Gated to TAS > alpha_min_tas_mps to skip ground/low-speed where
    # vz/TAS becomes singular.
    min_tas = p.get("alpha_min_tas_mps", 12.0)
    tas_ok = (tas_mps_arr > min_tas)
    alpha_kin_mask = mask_fresh_vn & tas_ok
    if w_alpha > 0.0 and alpha_kin_mask.sum() > 0:
        # arcsin guard: clip ratio to (-1, 1).
        ratio_ekf = np.clip(history["vz_mps"][alpha_kin_mask]
                            / tas_mps_arr[alpha_kin_mask], -0.999, 0.999)
        ratio_vn  = np.clip(vz_truth[alpha_kin_mask]
                            / tas_mps_arr[alpha_kin_mask], -0.999, 0.999)
        alpha_ekf = (history["pitch_deg"][alpha_kin_mask]
                     - np.rad2deg(np.arcsin(ratio_ekf)))
        alpha_vn  = (pitch_truth[alpha_kin_mask]
                     - np.rad2deg(np.arcsin(ratio_vn)))
        alpha_kin_res = alpha_ekf - alpha_vn
        w_ak = sample_w[alpha_kin_mask]
        alpha_kin_rms = _weighted_huber_rms(alpha_kin_res, w_ak, p["huber_alpha"])
    else:
        alpha_kin_rms = 0.0

    total = (wp * pitch_v + wr * roll_v + wv * vz_v
             + wpr * pitch_r + wrr * roll_r + wvr * vz_r
             + w_alpha * alpha_kin_rms)
    return total, {
        "pitch_rms": pitch_v, "roll_rms": roll_v, "vz_rms": vz_v,
        "pitch_rate_rms": pitch_r, "roll_rate_rms": roll_r, "vz_rate_rms": vz_r,
        "alpha_pressure_diag": alpha_l,
        "alpha_kin_rms": alpha_kin_rms,
    }


# ---------------------------------------------------------------------------
# Sanity penalties (return +inf to invalidate a trial)
# ---------------------------------------------------------------------------


def _sanity_penalty(history: dict[str, np.ndarray], df: pd.DataFrame) -> float | None:
    """Hard-fail catch for math failures only — NaN/Inf.

    The quaternion EKF can let its gyro-bias states grow large during
    aerobatic excursions (these states absorb model mismatch when the
    aircraft tumbles), but attitude itself still tracks via the accel
    correction. The composite loss already penalises bad attitude
    tracking, so we don't reject trials for "unphysical biases" — just
    catch genuinely diverged math.
    """
    if not np.isfinite(history["pitch_deg"]).all():
        return math.inf
    if not np.isfinite(history["roll_deg"]).all():
        return math.inf
    if not np.isfinite(history["vz_mps"]).all():
        return math.inf
    if not np.isfinite(history["z_m"]).all():
        return math.inf
    return None


# ---------------------------------------------------------------------------
# Train / validation split by flight phase
# ---------------------------------------------------------------------------


def _phase_mask(df: pd.DataFrame, n: int) -> dict[str, np.ndarray]:
    """Coarse flight-phase masks for train/val splitting.

    Phases:
        ground   : IAS < 25 kt (taxi + ramp)
        climb    : VSI < -2 m/s (climbing) AND ias>=25
        cruise   : |vsi| < 2 m/s AND ias>=25 AND |vnRoll|<10 AND |vnPitch|<10
        maneuver : |vnRoll|>=10 OR |vnPitch|>=10 (turns / aerobatics)
        descent  : vsi > 2 m/s AND ias>=25
    Phases are disjoint and exhaustive (a row falls into exactly one).
    """
    ias = df["IAS"].to_numpy(dtype=np.float32)
    vsi = df["vnVelNedDown"].to_numpy(dtype=np.float32)  # +down (m/s)
    roll  = df["vnRoll"].to_numpy(dtype=np.float32)
    pitch = df["vnPitch"].to_numpy(dtype=np.float32)

    ground   = (ias < 25.0)
    airborne = ~ground
    maneuver = airborne & ((np.abs(roll) >= 10.0) | (np.abs(pitch) >= 10.0))
    cruise   = airborne & ~maneuver & (np.abs(vsi) < 2.0)
    climb    = airborne & ~maneuver & ~cruise & (vsi < -2.0)
    descent  = airborne & ~maneuver & ~cruise & ~climb
    return dict(ground=ground, climb=climb, cruise=cruise,
                maneuver=maneuver, descent=descent)


def build_train_val_split(
    df: pd.DataFrame, val_frac: float = 0.25,
) -> tuple[np.ndarray, np.ndarray]:
    """Split row indices into (train, val) bools.

    For each phase, take the last `val_frac` of its rows as validation; the
    first (1-val_frac) is training. This keeps each phase represented in
    both sets while still being a temporal hold-out.
    """
    n = len(df)
    phases = _phase_mask(df, n)
    train = np.ones(n, dtype=bool)
    val = np.zeros(n, dtype=bool)
    for name, mask in phases.items():
        idxs = np.where(mask)[0]
        if idxs.size == 0:
            continue
        split = int((1.0 - val_frac) * idxs.size)
        train[idxs[split:]] = False
        val[idxs[split:]] = True
    return train, val


# ---------------------------------------------------------------------------
# Objective
# ---------------------------------------------------------------------------


def make_objective(log, train_mask: np.ndarray, val_mask: np.ndarray,
                   weights: LossWeights,
                   loss_profile: str = "default"):
    """Build an Optuna objective closure for the EKFQ filter.

    The objective replays the full flight log through the 11-state
    quaternion EKFQ for each Optuna trial and scores the result via
    `composite_loss` on the chosen `loss_profile` (see _LOSS_PROFILES
    above).
    """

    df_full = log.df

    def objective(trial: optuna.Trial) -> float:
        # Search-space bounds expanded vs early studies in directions
        # where prior best-trial parameters approached the original limits:
        #   q_quat floor lowered (best near 1.4e-6 against 1e-6 floor),
        #   q_bias ceiling raised (best near 0.08 against 0.1 ceiling),
        #   q_beta floor lowered (best near 3.5e-8 against 1e-8 floor),
        #   accel_ema_alpha floor 0.02 (low enough to find tight smoothing
        #     without dropping into the multi-hundred-ms-lag corner),
        #   ias_alive_kt ceiling raised (best near 33.7 against 35),
        #   tasdot_ema_alpha ceiling raised (best near 0.20 against 0.3).
        q_attitude   = trial.suggest_float("q_quat",       1e-9, 1e-1, log=True)
        q_bias       = trial.suggest_float("q_bias",       1e-6, 10.0, log=True)
        q_z          = trial.suggest_float("q_z",          1e-5, 1.0,  log=True)
        q_vz         = trial.suggest_float("q_vz",         1e-6, 10.0, log=True)
        q_b_az       = trial.suggest_float("q_b_az",       1e-8, 1e-2, log=True)
        q_beta       = trial.suggest_float("q_beta",       1e-10, 1e-1, log=True)
        r_ax         = trial.suggest_float("r_ax",         1e-3, 1000.0, log=True)
        r_ay         = trial.suggest_float("r_ay",         1e-3, 1000.0, log=True)
        r_az         = trial.suggest_float("r_az",         1e-3, 1000.0, log=True)
        r_baro       = trial.suggest_float("r_baro",       1e-2, 20.0, log=True)
        r_beta_prior = trial.suggest_float("r_beta_prior", 1e-4, 1.0,  log=True)
        r_bias_prior = trial.suggest_float("r_bias_prior", 1e-6, 1e-1, log=True)
        k_beta_R     = trial.suggest_float("k_beta_R",     1e-4, 100.0, log=True)

        accel_ema_alpha   = trial.suggest_float("accel_ema_alpha", 2e-2, 0.50, log=True)
        comp_fade_tau_sec = trial.suggest_float("comp_fade_tau",   0.2, 5.0)
        ias_alive_kt      = trial.suggest_float("ias_alive_kt",    15.0, 60.0)
        tasdot_ema_alpha  = trial.suggest_float("tasdot_ema_alpha", 5e-3, 0.5, log=True)

        ekf_cfg = EKFQConfig(
            q_quat=q_attitude, q_bias=q_bias, q_z=q_z, q_vz=q_vz,
            q_b_az=q_b_az, q_beta=q_beta,
            r_ax=r_ax, r_ay=r_ay, r_az=r_az,
            r_baro=r_baro, r_beta_prior=r_beta_prior,
            r_bias_prior=r_bias_prior, k_beta_R=k_beta_R,
            p_quat=0.05, p_bias=0.01, p_z=100.0, p_vz=4.0,
            p_b_az=1.0, p_beta=0.01,
        )
        pipe_cfg = PipelineQuatConfig(
            accel_ema_alpha=accel_ema_alpha,
            comp_fade_tau_sec=comp_fade_tau_sec,
            ias_alive_kt=ias_alive_kt,
            tasdot_ema_alpha=tasdot_ema_alpha,
        )
        try:
            pipe = PipelineQuat(ekf_cfg, pipe_cfg)
            history = pipe.run(log)
        except (np.linalg.LinAlgError, FloatingPointError, ValueError):
            return math.inf

        penalty = _sanity_penalty(history, df_full)
        if penalty is not None:
            return penalty

        fresh_vn_train = log.fresh_vn & train_mask
        fresh_vn_val   = log.fresh_vn & val_mask
        aoa_valid = history["ias_alive"] & np.isfinite(df_full["AngleofAttack"].to_numpy())
        aoa_train = aoa_valid & train_mask
        aoa_val   = aoa_valid & val_mask

        train_loss, train_bd = composite_loss(history, df_full, fresh_vn_train, aoa_train,
                                              weights, ekf_cfg, profile=loss_profile)
        val_loss, val_bd     = composite_loss(history, df_full, fresh_vn_val, aoa_val,
                                              weights, ekf_cfg, profile=loss_profile)
        trial.set_user_attr("train_breakdown", train_bd)
        trial.set_user_attr("val_breakdown",   val_bd)
        trial.set_user_attr("val_loss",        val_loss)
        return float(train_loss)

    return objective


# ---------------------------------------------------------------------------
# host-main driver: subprocess `host_main ahrs_tone --input-format=sdlog`
# per trial instead of running PipelineQuat in-process. Closes the
# Python↔C++ drift loop — the binary that flies is the binary the tuner
# scores against.
# ---------------------------------------------------------------------------


def make_objective_host_main(
    log,
    host_main_path: str,
    config_path: str,
    train_mask: np.ndarray,
    val_mask: np.ndarray,
    weights: LossWeights,
    loss_profile: str,
):
    """Build an Optuna objective that runs each trial via host_main.

    Parallels `make_objective` but, instead of running PipelineQuat
    in-process, writes the trial's params to a kv file and runs the
    compiled C++ replay. Loss math (composite_loss + fresh-VN masks +
    train/val split) stays here in Python.
    """

    df_full = log.df
    # host_main consumes row 0 as the seed frame and emits n-1 output rows
    # (one per processed sample). Truth columns + fresh-VN masks need to
    # be trimmed to match the output length.
    n_full = len(df_full)
    host_main_pth = Path(host_main_path)
    cfg_pth = Path(config_path)

    def objective(trial: optuna.Trial) -> float:
        # Same search space as make_objective (kept in lockstep).
        q_attitude   = trial.suggest_float("q_quat",       1e-9, 1e-1, log=True)
        q_bias       = trial.suggest_float("q_bias",       1e-6, 10.0, log=True)
        q_z          = trial.suggest_float("q_z",          1e-5, 1.0,  log=True)
        q_vz         = trial.suggest_float("q_vz",         1e-6, 10.0, log=True)
        q_b_az       = trial.suggest_float("q_b_az",       1e-8, 1e-2, log=True)
        q_beta       = trial.suggest_float("q_beta",       1e-10, 1e-1, log=True)
        r_ax         = trial.suggest_float("r_ax",         1e-3, 1000.0, log=True)
        r_ay         = trial.suggest_float("r_ay",         1e-3, 1000.0, log=True)
        r_az         = trial.suggest_float("r_az",         1e-3, 1000.0, log=True)
        r_baro       = trial.suggest_float("r_baro",       1e-2, 20.0, log=True)
        r_beta_prior = trial.suggest_float("r_beta_prior", 1e-4, 1.0,  log=True)
        r_bias_prior = trial.suggest_float("r_bias_prior", 1e-6, 1e-1, log=True)
        k_beta_R     = trial.suggest_float("k_beta_R",     1e-4, 100.0, log=True)

        accel_ema_alpha   = trial.suggest_float("accel_ema_alpha", 2e-2, 0.50, log=True)
        comp_fade_tau_sec = trial.suggest_float("comp_fade_tau",   0.2, 5.0)
        ias_alive_kt      = trial.suggest_float("ias_alive_kt",    15.0, 60.0)
        tasdot_ema_alpha  = trial.suggest_float("tasdot_ema_alpha", 5e-3, 0.5, log=True)

        ekf_cfg = EKFQConfig(
            q_quat=q_attitude, q_bias=q_bias, q_z=q_z, q_vz=q_vz,
            q_b_az=q_b_az, q_beta=q_beta,
            r_ax=r_ax, r_ay=r_ay, r_az=r_az,
            r_baro=r_baro, r_beta_prior=r_beta_prior,
            r_bias_prior=r_bias_prior, k_beta_R=k_beta_R,
            p_quat=0.05, p_bias=0.01, p_z=100.0, p_vz=4.0,
            p_b_az=1.0, p_beta=0.01,
        )
        pipe_cfg = PipelineQuatConfig(
            accel_ema_alpha=accel_ema_alpha,
            comp_fade_tau_sec=comp_fade_tau_sec,
            ias_alive_kt=ias_alive_kt,
            tasdot_ema_alpha=tasdot_ema_alpha,
        )

        try:
            df_out = run_host_main(
                host_main_pth, _resolve_log_path(log),
                cfg_pth, ekf_cfg, pipe_cfg,
                # No passthrough cols: truth comes from df_slice below,
                # aligned via the n_out invariant asserted below.
                passthrough_cols=[],
            )
        except RuntimeError as e:
            # Surface the failure on the first occurrence per study so
            # the operator can diagnose (missing binary, bad kv, OOM,
            # segfault). Subsequent trials swallow silently to keep the
            # study moving — Optuna sees inf and marks the trial pruned.
            if not getattr(make_objective_host_main, "_warned", False):
                print(f"\n[tune_ekf] host_main trial failed: {e}", flush=True)
                make_objective_host_main._warned = True
            return math.inf

        n_out = len(df_out)
        if n_out == 0:
            return math.inf

        # host_main consumes input row 0 as the seed (no output emitted)
        # and emits exactly one row per subsequent input row.  Truth
        # alignment below depends on this invariant: a mismatch means
        # host_main silently dropped data (e.g. CSV row truncation) and
        # the resulting trial loss would compare misaligned timestamps.
        n_expected = n_full - 1
        if n_out != n_expected:
            if not getattr(make_objective_host_main, "_warned_align", False):
                print(f"\n[tune_ekf] WARNING: host_main emitted {n_out} rows, "
                      f"expected {n_expected} (n_full={n_full}). Truth "
                      f"alignment may be off; treating this trial as failed.",
                      flush=True)
                make_objective_host_main._warned_align = True
            return math.inf

        # host_main emits rows [1..n_full-1] of the input. Build a
        # `history` dict in the shape composite_loss expects, then trim
        # the truth-side arrays + masks to the matching slice.
        zeros = np.zeros(n_out, dtype=np.float32)
        ones  = np.ones(n_out,  dtype=np.float32)
        ones_bool = np.ones(n_out, dtype=bool)

        # +climb is convention in PipelineQuat; +down in NED; kalman_vsi_fpm
        # is +climb (firmware convention). Convert fpm→m/s and flip to +down.
        vsi_fpm = df_out["kalman_vsi_fpm"].to_numpy(dtype=np.float32)
        vz_mps_arr = -vsi_fpm * 0.00508    # fpm→m/s = *0.3048/60 = *0.00508
        z_m_arr = df_out["kalman_alt_ft"].to_numpy(dtype=np.float32) * 0.3048

        history = {
            "pitch_deg":  df_out["pitch_deg"].to_numpy(dtype=np.float32),
            "roll_deg":   df_out["roll_deg"].to_numpy(dtype=np.float32),
            "yaw_deg":    zeros,
            "alpha_deg":  df_out["derived_aoa_deg"].to_numpy(dtype=np.float32),
            "beta_deg":   zeros,
            "vz_mps":     vz_mps_arr,
            "z_m":        z_m_arr,
            "bp_dps":     zeros,
            "bq_dps":     zeros,
            "br_dps":     zeros,
            "b_az_mps2":  zeros,
            "ias_alive":  ones_bool,
            "comp_fade":  ones,
        }

        penalty = _sanity_penalty(history, df_full)
        if penalty is not None:
            return penalty

        # Slice the truth-aligned df + masks. host_main consumes row 0 as
        # seed and emits one output row per subsequent input row, so the
        # output corresponds to input rows [n_full - n_out .. n_full).
        # In the common case n_out = n_full - 1, this is rows [1..n_full).
        slice_start = n_full - n_out
        df_slice = df_full.iloc[slice_start:].reset_index(drop=True)
        fresh_vn_slice = log.fresh_vn[slice_start:]
        train_slice = train_mask[slice_start:]
        val_slice   = val_mask[slice_start:]

        fresh_vn_train = fresh_vn_slice & train_slice
        fresh_vn_val   = fresh_vn_slice & val_slice
        aoa_valid = history["ias_alive"] & np.isfinite(df_slice["AngleofAttack"].to_numpy())
        aoa_train = aoa_valid & train_slice
        aoa_val   = aoa_valid & val_slice

        train_loss, train_bd = composite_loss(history, df_slice, fresh_vn_train, aoa_train,
                                              weights, ekf_cfg, profile=loss_profile)
        val_loss, val_bd     = composite_loss(history, df_slice, fresh_vn_val, aoa_val,
                                              weights, ekf_cfg, profile=loss_profile)
        trial.set_user_attr("train_breakdown", train_bd)
        trial.set_user_attr("val_breakdown",   val_bd)
        trial.set_user_attr("val_loss",        val_loss)
        return float(train_loss)

    return objective


def _resolve_log_path(log) -> Path:
    """Return the original CSV path the host-main subprocess should read.

    `load_log` doesn't currently stash the source path on the FlightLog
    object, so the objective stashes it on log.df.attrs under
    'source_path' in main(). Kept as a helper so future log loaders can
    expose the same hook.
    """
    p = log.df.attrs.get("source_path")
    if p is None:
        raise RuntimeError(
            "host-main driver needs log.df.attrs['source_path']; "
            "main() must set it before building the objective."
        )
    return Path(p)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default="log_007_fixed.csv")
    ap.add_argument("--trials", type=int, default=200)
    ap.add_argument("--rows", type=int, default=0, help="0 = full log")
    ap.add_argument("--study-name", default="ekfq_v15")
    ap.add_argument("--storage", default=None, help="optuna storage URL (default: SQLite in ./studies)")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--seed-from-study", default=None,
                    help="study name to copy best-trial params from as the initial seed point")
    ap.add_argument("--loss-mode", choices=list(_LOSS_PROFILES.keys()), default="default",
                    help="loss-profile selector (cruise-aoa = the v15 production loss)")
    ap.add_argument("--driver", choices=["python", "host-main"], default="python",
                    help="trial driver: python = in-process PipelineQuat (default), "
                         "host-main = subprocess the compiled C++ host_main per trial "
                         "(closes the Python↔C++ drift loop, requires --config-path)")
    ap.add_argument("--host-main", default="../tools/regression/.pio/build/native/program",
                    help="path to compiled host_main binary (default: regression harness build)")
    ap.add_argument("--config-path", default=None,
                    help="OnSpeed config file path (required with --driver=host-main)")
    ap.add_argument("--n-jobs", type=int, default=1,
                    help="parallel trial workers (-1 = use all cores). "
                         "Only useful with --driver=host-main since each trial "
                         "is a separate subprocess; --driver=python shares the "
                         "GIL and won't speed up.")
    args = ap.parse_args()

    print(f"Loading {args.log} ...", flush=True)
    log = load_log(args.log)
    # Stash the source log path so the host-main driver can re-read the
    # original CSV in each trial subprocess.
    log.df.attrs["source_path"] = str(Path(args.log).resolve())
    if args.rows:
        # Truncated view for fast iteration; build a duck-typed subset.
        class _Sub:
            pass
        sub = _Sub()
        sub.df = log.df.iloc[: args.rows].reset_index(drop=True)
        sub.dt = log.dt[: args.rows]
        sub.fresh_vn   = log.fresh_vn[: args.rows]
        sub.fresh_aoa  = log.fresh_aoa[: args.rows]
        sub.fresh_baro = log.fresh_baro[: args.rows]
        sub.fresh_boom = log.fresh_boom[: args.rows]
        sub.n = args.rows
        log = sub
    print(f"  rows: {log.n}, duration: {log.n / 208 / 60:.1f} min", flush=True)

    # Train/val split.
    train_mask, val_mask = build_train_val_split(log.df)
    print(f"  train rows: {train_mask.sum()} ({100 * train_mask.mean():.1f}%)", flush=True)
    print(f"  val rows:   {val_mask.sum()}   ({100 * val_mask.mean():.1f}%)", flush=True)

    # Storage.
    Path("studies").mkdir(exist_ok=True)
    storage = args.storage or f"sqlite:///studies/{args.study_name}.db"

    study = optuna.create_study(
        direction="minimize",
        study_name=args.study_name,
        storage=storage,
        load_if_exists=True,
        sampler=TPESampler(seed=args.seed, n_startup_trials=25, multivariate=True, group=True),
    )

    # Seed the search with sensible defaults so TPE starts from a known-good
    # corner. q_quat is per quaternion component per second; β-related and
    # bias-prior parameters come from the EKFQConfig defaults.
    seed_defaults = {
        "q_quat":       0.5e-3,
        "q_bias":       2.08e-3,
        "q_z":          1e-3,
        "q_vz":         1.0,
        "q_b_az":       1e-5,
        "q_beta":       5e-4,
        "r_ax":         0.5,
        "r_ay":         0.5,
        "r_az":         0.5,
        "r_baro":       0.79078,
        "r_beta_prior": 0.05,
        "r_bias_prior": 7.6e-4,
        "k_beta_R":     5.0,
        # pitch_bias_deg and roll_bias_deg removed from search space —
        # pinned to onspeed2.cfg values inside the pipeline.
        "accel_ema_alpha": 0.060899,
        "comp_fade_tau":   0.5,
        "ias_alive_kt":    25.0,
        "tasdot_ema_alpha": 0.05,
    }
    if len(study.trials) == 0:
        if args.seed_from_study:
            prior_storage = f"sqlite:///studies/{args.seed_from_study}.db"
            prior = optuna.load_study(study_name=args.seed_from_study, storage=prior_storage)
            best_prior = prior.best_trial
            print(f"  seeding from {args.seed_from_study} best trial "
                  f"#{best_prior.number} (loss={best_prior.value:.4f})", flush=True)
            study.enqueue_trial(best_prior.params)
        study.enqueue_trial(seed_defaults)

    weights = LossWeights()
    if args.driver == "host-main":
        if args.config_path is None:
            print("--driver=host-main requires --config-path", flush=True)
            sys.exit(1)
        objective = make_objective_host_main(
            log, args.host_main, args.config_path,
            train_mask, val_mask, weights, args.loss_mode,
        )
        print(f"  driver: host-main (binary: {args.host_main})", flush=True)
    else:
        objective = make_objective(log, train_mask, val_mask, weights,
                                   loss_profile=args.loss_mode)
        print(f"  driver: python (in-process)", flush=True)
    print(f"  loss profile: {args.loss_mode}", flush=True)

    t_start = time.time()
    study.optimize(objective, n_trials=args.trials, n_jobs=args.n_jobs,
                   show_progress_bar=True)
    elapsed = time.time() - t_start

    best = study.best_trial
    print()
    print(f"=== Best trial after {len(study.trials)} trials in {elapsed/60:.1f} min ===")
    print(f"  train loss: {best.value:.4f}")
    print(f"  val loss:   {best.user_attrs.get('val_loss', float('nan')):.4f}")
    print("  breakdown (train):", best.user_attrs.get("train_breakdown"))
    print("  breakdown (val):  ", best.user_attrs.get("val_breakdown"))
    print("  params:")
    for k, v in best.params.items():
        print(f"    {k} = {v}")


if __name__ == "__main__":
    main()
