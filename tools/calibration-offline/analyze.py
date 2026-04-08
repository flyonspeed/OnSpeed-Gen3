#!/usr/bin/env python3
"""
OnSpeed calibration analysis: OLS vs WLS, single vs stacked.

Compares four alternative calibration algorithms against the on-device master
config for each flap setting, on real SD-log stall data:

    1. OLS single-best      (reproduces master: single run, unweighted OLS)
    2. WLS single-best      (PR #82 spec: same run, inverse-variance weighted)
    3. OLS stacked-all      (PR #83 spec sans weighting: all runs concat, OLS)
    4. WLS stacked-all      (PR #82 + #83 combined: all runs concat, WLS)
    5. WLS stacked-KEEP     (best case: quality-filtered concat, WLS)

Writes a markdown report, per-flap PNG plots, and per-step CSV tables.

Source-of-truth references:
    - master: software/OnSpeed-Gen3-ESP32/Web/javascript_calibration.h (OLS baseline)
    - 74c8f1b: software/OnSpeed-Gen3-ESP32/Web/javascript_calibration.h (WLS helpers,
      mergeRunData concat logic — algorithm only, rebase bugs ignored)
    - Stacked Calibration/WLS_Solver_CurrentLayout.bas (Vac's reference VBA)
    - Stacked Calibration/CLAUDE.md (intent, published benchmark numbers)

Invocation (from anywhere):
    uv run --with numpy --with scipy --with pandas --with matplotlib python3 \\
        /Users/sritchie/code/onspeed/scratch/cal_analysis/analyze.py
"""

from __future__ import annotations

import argparse
import math
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


# --------------------------------------------------------------------------- #
# Constants — all verified against source references                          #
# --------------------------------------------------------------------------- #

# Setpoint multipliers and constants from master:javascript_calibration.h lines 5-7
OS_FAST_MULT = 1.35
OS_SLOW_MULT = 1.25
STALL_WARN_MARGIN_KT = 5.0
NAOA_FAST = 1.0 / (OS_FAST_MULT ** 2)  # 0.548697...
NAOA_SLOW = 1.0 / (OS_SLOW_MULT ** 2)  # 0.640000

# EMA smoothing from master:javascript_calibration.h lines 246-247
SMOOTH_IAS_NEW_W = 0.98  # smoothedIAS[i] = IAS[i]*.98 + smoothedIAS[i-1]*.02
SMOOTH_CP_NEW_W = 0.90   # smoothedCP[i] = CP[i]*.90 + smoothedCP[i-1]*.10

# WLS window — Vac's 29 samples tuned for 50 Hz (CLAUDE.md:74, STDEV.S = sample variance)
ROLLING_WIN = 29
ROLLING_DDOF = 1
WEIGHT_FLOOR = 1e-9

# Slicer thresholds
MAP_CRUISE_THRESHOLD = 18.0        # inHg — above this is "power on"
MAP_DROP_THRESHOLD = 15.0          # inHg — below this is "throttled back"
MAP_DROP_WINDOW_S = 5.0            # MAP must cross the gap within this many seconds
CANDIDATE_WINDOW_S = 180.0         # look this far after a MAP drop for the whole run
MIN_DIVE_PEAK_IAS = 85.0           # top-of-decel IAS must be at least this
MIN_DECEL_DURATION_S = 10.0        # at least this much monotonic decel to be a run
MIN_IAS_SPAN_KT = 12.0             # IAS range across the run (flap33 stalls span ~35 kt max from Vfe=87 to Vs=48)
MAX_DECEL_IAS_REBOUND_KT = 3.0     # if IAS rebounds more than this, truncate the window
MAX_RPM_THROTTLED = 2800           # RV-10 windmilling prop at low MP can spin 2200-2700 RPM
DIVE_RECOVERY_MAX_S = 45.0         # look at most this far after MAP drop to find the top of the dive
MIN_MONOTONIC_RUN_S = 5.0          # tracker must make at least this much progress before a rebound counts
FLAP_MATCH_TOLERANCE_DEG = 3.0     # median flapsPos must be within this of a cfg bin

# Run-quality scoring weights
QUALITY_W_IAS_R2 = 0.25
QUALITY_W_CP_R2 = 0.25
QUALITY_W_DECEL = 0.20
QUALITY_W_LENGTH = 0.15
QUALITY_W_K_OUTLIER = 0.15
QUALITY_LENGTH_TARGET_PTS = 300    # 6 s @ 50 Hz
QUALITY_K_OUTLIER_PCT = 15.0
QUALITY_DECEL_TARGET_KTS = 1.0   # docs/calibration/wizard.md: "1-2 kt/s ideal"
QUALITY_DECEL_STD_TARGET = 0.6   # tolerance — generous for real flight conditions
QUALITY_KEEP_THRESHOLD = 0.75
QUALITY_BORDERLINE_THRESHOLD = 0.50

# No personal path defaults in the repo copy — all paths are required via CLI.
# See README.md for the typical invocation shape.


# --------------------------------------------------------------------------- #
# Typed containers                                                            #
# --------------------------------------------------------------------------- #

@dataclass
class FlapCfg:
    degrees: int
    kfit: float
    alpha0: float
    alpha_stall: float
    ldmax: float
    os_fast: float
    os_slow: float
    stall_warn: float
    stall: float
    maneuvering: float
    aoa_curve: tuple[float, float, float, float]  # X3, X2, X1, X0


@dataclass
class AircraftCfg:
    gross_weight: float
    best_glide_ias: float
    vfe: float
    g_limit: float


@dataclass
class DeviceCfg:
    aircraft: AircraftCfg
    flaps: dict[int, FlapCfg]


@dataclass
class CalCsvRef:
    """Metadata from a saved cal CSV header, plus the body for OLS gate."""
    path: Path
    flap_pos: int
    stall_speed: float
    ldmax_setpoint: float
    os_fast_setpoint: float
    os_slow_setpoint: float
    stall_warn_setpoint: float
    stall_angle: float
    man_setpoint: float
    cp_quad: tuple[float, float, float]  # a2, a1, a0
    cp_r2: float
    alpha0: float
    alpha_stall: float
    ias_to_aoa_r2: float
    body: pd.DataFrame  # columns: IAS, CP, DerivedAOA, Pitch, FlightPath, DecelRate


@dataclass
class Run:
    """A single sliced stall-calibration window from the log."""
    run_id: str           # e.g. "f00_r03"
    flap_bin: int
    t_start_ms: int
    t_end_ms: int
    ias_raw: np.ndarray           # kt
    ias_smooth: np.ndarray        # kt
    cp_raw: np.ndarray            # dimensionless
    cp_smooth: np.ndarray         # dimensionless
    derived_aoa: np.ndarray       # deg
    pitch: np.ndarray             # deg
    flight_path: np.ndarray       # deg
    decel_rate: np.ndarray        # kt/s, synthesized
    stall_index: int              # argmax smoothed CP within the run
    stall_ias: float              # smoothed IAS at stall_index
    median_flap: float


@dataclass
class RunFit:
    algo: str                  # 'OLS' or 'WLS'
    K: float
    alpha0: float
    alpha_stall: float
    stall_ias: float
    ias_to_aoa_r2: float        # unweighted R² of the physics fit
    ias_to_aoa_wr2: float       # weighted R²
    cp_a2: float
    cp_a1: float
    cp_a0: float
    cp_r2: float                # unweighted
    cp_wr2: float               # weighted
    n_points: int


@dataclass
class Setpoints:
    ldmax: float
    os_fast: float
    os_slow: float
    stall_warn: float
    stall: float
    maneuvering: float

    def as_list(self) -> list[tuple[str, float]]:
        return [
            ("LDmax", self.ldmax),
            ("OS_fast", self.os_fast),
            ("OS_slow", self.os_slow),
            ("StallWarn", self.stall_warn),
            ("Stall", self.stall),
            ("Maneuvering", self.maneuvering),
        ]


@dataclass
class RunQuality:
    run_id: str
    flap: int
    score: float
    verdict: str               # 'KEEP' / 'BORDERLINE' / 'DISCARD'
    reasons: list[str]
    ias_r2: float
    cp_r2: float
    decel_mean: float
    decel_std: float
    n_points: int
    K: float
    K_dev_pct: float


@dataclass
class ConfigSummary:
    """A single column in the core comparison table.

    Includes BOTH the physics fit (K/α₀/α_stall — used to derive setpoints) and
    the runtime CP→AOA polynomial (cp_a2, cp_a1, cp_a0 — what the firmware
    actually evaluates 50 times per second). A coherent cfg update must change
    BOTH the polynomial AND the setpoint values together; changing only one
    would mean the runtime curve and the setpoint table reference different
    coordinate systems.
    """
    label: str
    K: float
    alpha0: float
    alpha_stall: float
    stall_ias: float
    cp_a2: float                  # CP² coefficient of runtime polynomial
    cp_a1: float                  # CP¹ coefficient
    cp_a0: float                  # constant term
    ias_r2: float | None
    cp_r2: float | None
    n_points: int | None
    source: str
    setpoints: Setpoints | None
    setpoints_ias: dict[str, float] | None  # setpoint name -> IAS at 1G


# --------------------------------------------------------------------------- #
# Section 1: WLS / OLS core math (ported from 74c8f1b JS + Vac's VBA)         #
# --------------------------------------------------------------------------- #

def rolling_sigma(arr: np.ndarray, win_len: int = ROLLING_WIN,
                  ddof: int = ROLLING_DDOF) -> np.ndarray:
    """Centered rolling sample-std-dev over `win_len` samples.

    Edges shrink (no padding), matching Vac's Excel `STDEV.S(C[row-14:row+14])`
    with `MAX(2,...)` / `MIN(..., ROWS(C:C))` clamps.

    ddof=1 → sample variance (Vac's STDEV.S), matches his published WLS numbers.
    The PR #82 JS at rev 74c8f1b uses population variance (sum/cnt) — minor
    rebase-time bug; we use sample variance here to match Vac's intent.
    """
    n = len(arr)
    if n == 0:
        return np.zeros(0)
    half = win_len // 2
    out = np.empty(n)
    for i in range(n):
        lo = max(0, i - half)
        hi = min(n - 1, i + half)
        slc = arr[lo:hi + 1]
        if len(slc) > ddof:
            out[i] = float(np.std(slc, ddof=ddof))
        else:
            out[i] = 0.0
    return out


def _wls_linear_core(x: np.ndarray, y: np.ndarray, w: np.ndarray) -> tuple[float, float, float, float]:
    """Port of wlsLinear (74c8f1b JS lines 126-151).

    Returns (slope, intercept, r2_weighted, r2_unweighted).
    Unweighted R² computed against the weighted fit to give an apples-to-apples
    quality number (what you'd see from eyeball residuals regardless of weights).
    """
    sW = w.sum()
    sWx = (w * x).sum()
    sWy = (w * y).sum()
    sWxx = (w * x * x).sum()
    sWxy = (w * x * y).sum()
    det = sW * sWxx - sWx * sWx
    if abs(det) < 1e-30:
        return 0.0, 0.0, 0.0, 0.0
    slope = (sW * sWxy - sWx * sWy) / det
    intercept = (sWxx * sWy - sWx * sWxy) / det

    pred = slope * x + intercept
    ybar_w = sWy / sW
    ss_res_w = (w * (y - pred) ** 2).sum()
    ss_tot_w = (w * (y - ybar_w) ** 2).sum()
    r2_w = 1.0 - ss_res_w / ss_tot_w if ss_tot_w > 0 else 0.0

    # Unweighted R² — matches Vac's VBA output (WLS_Solver_CurrentLayout.bas:114-119)
    ybar = float(np.mean(y))
    ss_res = float(np.sum((y - pred) ** 2))
    ss_tot = float(np.sum((y - ybar) ** 2))
    r2_u = 1.0 - ss_res / ss_tot if ss_tot > 0 else 0.0

    return float(slope), float(intercept), float(r2_w), float(r2_u)


def _wls_quadratic_core(x: np.ndarray, y: np.ndarray, w: np.ndarray) -> tuple[tuple[float, float, float], float, float]:
    """Port of wlsQuadratic (74c8f1b JS lines 87-122).

    Fits y = a2*x² + a1*x + a0 via Cramer's rule on the 3×3 normal equations.
    Returns ((a2, a1, a0), r2_weighted, r2_unweighted).
    """
    x2 = x * x
    s22 = float((w * x2 * x2).sum())
    s21 = float((w * x2 * x).sum())
    s20 = float((w * x2).sum())
    s11 = float((w * x * x).sum())
    s10 = float((w * x).sum())
    s00 = float(w.sum())
    t2 = float((w * x2 * y).sum())
    t1 = float((w * x * y).sum())
    t0 = float((w * y).sum())

    det = s22 * (s11 * s00 - s10 * s10) - s21 * (s21 * s00 - s10 * s20) + s20 * (s21 * s10 - s11 * s20)
    if abs(det) < 1e-30:
        return (0.0, 0.0, 0.0), 0.0, 0.0

    a2 = (t2 * (s11 * s00 - s10 * s10) - t1 * (s21 * s00 - s10 * s20) + t0 * (s21 * s10 - s11 * s20)) / det
    a1 = (s22 * (t1 * s00 - t0 * s10) - s21 * (t2 * s00 - t0 * s20) + s20 * (t2 * s10 - t1 * s20)) / det
    a0 = (s22 * (s11 * t0 - s10 * t1) - s21 * (s21 * t0 - s10 * t2) + s20 * (s21 * t1 - s11 * t2)) / det

    pred = a2 * x2 + a1 * x + a0
    ybar_w = t0 / s00 if s00 > 0 else 0.0
    ss_res_w = float((w * (y - pred) ** 2).sum())
    ss_tot_w = float((w * (y - ybar_w) ** 2).sum())
    r2_w = 1.0 - ss_res_w / ss_tot_w if ss_tot_w > 0 else 0.0

    ybar = float(np.mean(y))
    ss_res = float(np.sum((y - pred) ** 2))
    ss_tot = float(np.sum((y - ybar) ** 2))
    r2_u = 1.0 - ss_res / ss_tot if ss_tot > 0 else 0.0

    return (float(a2), float(a1), float(a0)), float(r2_w), float(r2_u)


def wls_linear(x: np.ndarray, y: np.ndarray,
               w: np.ndarray | None = None) -> tuple[float, float, float, float]:
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    if w is None:
        w = np.ones_like(x)
    else:
        w = np.asarray(w, dtype=float)
    return _wls_linear_core(x, y, w)


def wls_quadratic(x: np.ndarray, y: np.ndarray,
                  w: np.ndarray | None = None) -> tuple[tuple[float, float, float], float, float]:
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    if w is None:
        w = np.ones_like(x)
    else:
        w = np.asarray(w, dtype=float)
    return _wls_quadratic_core(x, y, w)


# --------------------------------------------------------------------------- #
# Section 2: Smoothing + stall index (master lines 236-254)                   #
# --------------------------------------------------------------------------- #

def smooth_ias_cp(ias: np.ndarray, cp: np.ndarray) -> tuple[np.ndarray, np.ndarray, int, float]:
    """Master's EMA smoothing and stall-index search.

        smoothedIAS[i] = IAS[i]*.98 + smoothedIAS[i-1]*.02
        smoothedCP[i]  = CP[i] *.90 + smoothedCP[i-1] *.10

    Note: the JS weight on the NEW sample is 0.98 (IAS) / 0.90 (CP), i.e. almost
    no smoothing on IAS and light smoothing on CP. Reproducing faithfully — the
    odd constants are intentional in master.
    """
    n = len(ias)
    s_ias = np.empty(n)
    s_cp = np.empty(n)
    s_ias[0] = ias[0]
    s_cp[0] = cp[0]
    stall_cp = s_cp[0]
    stall_index = 0
    stall_ias = float(s_ias[0])
    for i in range(1, n):
        s_ias[i] = ias[i] * SMOOTH_IAS_NEW_W + s_ias[i - 1] * (1.0 - SMOOTH_IAS_NEW_W)
        s_cp[i] = cp[i] * SMOOTH_CP_NEW_W + s_cp[i - 1] * (1.0 - SMOOTH_CP_NEW_W)
        if s_cp[i] > stall_cp:
            stall_cp = s_cp[i]
            stall_ias = float(s_ias[i])
            stall_index = i
    return s_ias, s_cp, stall_index, stall_ias


# --------------------------------------------------------------------------- #
# Section 3: Parsers — cfg XML + cal CSV headers                              #
# --------------------------------------------------------------------------- #

def _xml_float(parent: ET.Element, tag: str, default: float = 0.0) -> float:
    node = parent.find(tag)
    if node is None or node.text is None:
        return default
    return float(node.text.strip())


def _xml_int(parent: ET.Element, tag: str, default: int = 0) -> int:
    return int(_xml_float(parent, tag, default))


def parse_config(path: Path) -> DeviceCfg:
    tree = ET.parse(path)
    root = tree.getroot()

    ac = root.find("AIRCRAFT")
    aircraft = AircraftCfg(
        gross_weight=_xml_float(ac, "GROSS_WEIGHT", 2700.0) if ac is not None else 2700.0,
        best_glide_ias=_xml_float(ac, "BEST_GLIDE_IAS", 95.0) if ac is not None else 95.0,
        vfe=_xml_float(ac, "VFE", 87.0) if ac is not None else 87.0,
        g_limit=_xml_float(ac, "G_LIMIT", 3.8) if ac is not None else 3.8,
    )

    flaps: dict[int, FlapCfg] = {}
    for fp in root.findall("FLAP_POSITION"):
        deg = _xml_int(fp, "DEGREES")
        curve = fp.find("AOA_CURVE")
        if curve is not None:
            aoa_curve = (
                _xml_float(curve, "X3"),
                _xml_float(curve, "X2"),
                _xml_float(curve, "X1"),
                _xml_float(curve, "X0"),
            )
        else:
            aoa_curve = (0.0, 0.0, 0.0, 0.0)
        flaps[deg] = FlapCfg(
            degrees=deg,
            kfit=_xml_float(fp, "KFIT"),
            alpha0=_xml_float(fp, "ALPHA0"),
            alpha_stall=_xml_float(fp, "ALPHASTALL"),
            ldmax=_xml_float(fp, "LDMAXAOA"),
            os_fast=_xml_float(fp, "ONSPEEDFASTAOA"),
            os_slow=_xml_float(fp, "ONSPEEDSLOWAOA"),
            stall_warn=_xml_float(fp, "STALLWARNAOA"),
            stall=_xml_float(fp, "STALLAOA"),
            maneuvering=_xml_float(fp, "MANAOA"),
            aoa_curve=aoa_curve,
        )

    return DeviceCfg(aircraft=aircraft, flaps=flaps)


def parse_cal_csv(path: Path) -> CalCsvRef:
    """Parse a saved calibration wizard CSV (header comments + body).

    Header format is master:javascript_calibration.h saveData() — leading `;`
    comment lines followed by `IAS,CP,DerivedAOA,Pitch,FlightPath,DecelRate`.
    """
    header: dict[str, str] = {}
    data_start_line = None
    with path.open() as f:
        for i, line in enumerate(f):
            if line.startswith(";"):
                # e.g. ";alpha0=-3.0562"
                s = line[1:].strip()
                if "=" in s:
                    k, v = s.split("=", 1)
                    header[k.strip()] = v.strip()
                continue
            # First non-`;` line is the column header
            if line.strip().startswith("IAS,"):
                data_start_line = i + 1
                break

    if data_start_line is None:
        raise ValueError(f"{path}: no data header found")

    body = pd.read_csv(
        path,
        skiprows=data_start_line - 1,  # -1 so read_csv uses the column header row
        names=None,
    )
    # Normalize column names
    body.columns = [c.strip() for c in body.columns]

    # CP-to-AOA curve is formatted as "AOA = A * CP^2 +B * CP +C"
    cp_quad = (0.0, 0.0, 0.0)
    if "CPtoAOACurve: AOA " in header or "CPtoAOACurve" in header:
        curve_str = header.get("CPtoAOACurve: AOA ") or header.get("CPtoAOACurve", "")
        # Strip leading equals-sign if present
        curve_str = curve_str.lstrip("=").strip()
        # Normalize: "-35.6185 * CP^2 +31.0171 * CP +4.8539"
        try:
            # Split on "*" to isolate coefficients
            parts = [p.strip() for p in curve_str.replace(" ", "").replace("*CP^2", "|").replace("*CP", "|").split("|")]
            # parts like ["-35.6185", "+31.0171", "+4.8539"]
            if len(parts) >= 3:
                a2 = float(parts[0])
                a1 = float(parts[1])
                a0 = float(parts[2])
                cp_quad = (a2, a1, a0)
        except (ValueError, IndexError):
            pass

    def _get(name: str, default: float = 0.0) -> float:
        try:
            return float(header.get(name, default))
        except ValueError:
            return default

    # Flap position can be "0 deg" or "0"
    flap_raw = header.get("Flap position", "0")
    flap_pos = int(float(flap_raw.split()[0]))

    stall_speed_raw = header.get("StallSpeed", "0 kts")
    stall_speed = float(stall_speed_raw.split()[0])

    return CalCsvRef(
        path=path,
        flap_pos=flap_pos,
        stall_speed=stall_speed,
        ldmax_setpoint=_get("LDmaxSetpoint"),
        os_fast_setpoint=_get("OSFastSetpoint"),
        os_slow_setpoint=_get("OSSlowSetpoint"),
        stall_warn_setpoint=_get("StallWarnSetpoint"),
        stall_angle=_get("StallAngle"),
        man_setpoint=_get("ManeuveringSetpoint"),
        cp_quad=cp_quad,
        cp_r2=_get("CPtoAOAr2"),
        alpha0=_get("alpha0"),
        alpha_stall=_get("alphaStall"),
        ias_to_aoa_r2=_get("IAStoAOAr2"),
        body=body,
    )


# --------------------------------------------------------------------------- #
# Section 4: Fitting — OLS/WLS, per run, stacked                              #
# --------------------------------------------------------------------------- #

def _compute_wls_weights(derived_aoa: np.ndarray) -> np.ndarray:
    sigma = rolling_sigma(derived_aoa, ROLLING_WIN, ROLLING_DDOF)
    return 1.0 / np.maximum(sigma * sigma, WEIGHT_FLOOR)


def fit_run_core(ias_raw: np.ndarray, cp_smooth: np.ndarray,
                 derived_aoa: np.ndarray, stall_ias: float,
                 algo: str) -> RunFit:
    """Reproduce master's fit (OLS) or the PR #82 WLS variant.

    Both algorithms share the same mechanics:
      - fit DerivedAOA vs 1/IAS² linearly: slope=K, intercept=α₀
      - fit DerivedAOA vs smoothedCP quadratically: curve coeffs
      - α_stall = K/stall_ias² + α₀
    The only difference is the weights. For OLS, weights = ones. For WLS,
    weights = 1/max(rolling_sigma(DerivedAOA, 29)², 1e-9).
    """
    n = len(ias_raw)
    if n < 10:
        raise ValueError(f"fit_run: need at least 10 points, got {n}")

    if algo == "OLS":
        w = np.ones(n)
    elif algo == "WLS":
        w = _compute_wls_weights(derived_aoa)
    else:
        raise ValueError(f"unknown algo: {algo}")

    # IAS-to-AOA physics fit: y = K*(1/IAS²) + α₀. Use raw IAS (master line 293).
    valid = ias_raw > 0
    x_ias = 1.0 / (ias_raw[valid] ** 2)
    y_ias = derived_aoa[valid]
    w_ias = w[valid]
    K, alpha0, ias_wr2, ias_r2 = _wls_linear_core(x_ias, y_ias, w_ias)
    alpha_stall = K / (stall_ias ** 2) + alpha0

    # CP-to-AOA quadratic: y = a2*CP² + a1*CP + a0. Use smoothed CP (master line 266).
    (a2, a1, a0), cp_wr2, cp_r2 = _wls_quadratic_core(cp_smooth, derived_aoa, w)

    return RunFit(
        algo=algo, K=K, alpha0=alpha0, alpha_stall=alpha_stall, stall_ias=stall_ias,
        ias_to_aoa_r2=ias_r2, ias_to_aoa_wr2=ias_wr2,
        cp_a2=a2, cp_a1=a1, cp_a0=a0, cp_r2=cp_r2, cp_wr2=cp_wr2,
        n_points=n,
    )


def fit_csv_body(body: pd.DataFrame, algo: str = "OLS") -> RunFit:
    """Fit a saved cal CSV body. Used only by the OLS port correctness gate."""
    ias_raw = body["IAS"].to_numpy(dtype=float)
    cp_raw = body["CP"].to_numpy(dtype=float)
    derived_aoa = body["DerivedAOA"].to_numpy(dtype=float)

    _, cp_smooth, stall_index, stall_ias = smooth_ias_cp(ias_raw, cp_raw)

    # Master trims to [0, stall_index+1] before fitting (JS line 264)
    hi = stall_index + 1
    return fit_run_core(
        ias_raw[:hi],
        cp_smooth[:hi],
        derived_aoa[:hi],
        stall_ias,
        algo,
    )


def fit_run(run: Run, algo: str) -> RunFit:
    """Fit a log-sliced run. Body is already trimmed to [0, stall_index]."""
    hi = run.stall_index + 1
    return fit_run_core(
        run.ias_raw[:hi],
        run.cp_smooth[:hi],
        run.derived_aoa[:hi],
        run.stall_ias,
        algo,
    )


def fit_stacked(runs: list[Run], algo: str) -> RunFit:
    """Concat runs array-by-array, then fit. Port of fitAllData + mergeRunData
    at 74c8f1b JS lines 430-483. Stall IAS is averaged across kept runs.
    """
    if not runs:
        raise ValueError("fit_stacked: no runs")
    ias_chunks, cp_chunks, aoa_chunks = [], [], []
    stall_ias_vals = []
    for r in runs:
        hi = r.stall_index + 1
        ias_chunks.append(r.ias_raw[:hi])
        cp_chunks.append(r.cp_smooth[:hi])
        aoa_chunks.append(r.derived_aoa[:hi])
        stall_ias_vals.append(r.stall_ias)
    ias = np.concatenate(ias_chunks)
    cp_s = np.concatenate(cp_chunks)
    aoa = np.concatenate(aoa_chunks)
    avg_stall_ias = float(np.mean(stall_ias_vals))
    return fit_run_core(ias, cp_s, aoa, avg_stall_ias, algo)


# --------------------------------------------------------------------------- #
# Section 5: Setpoint derivation (master lines 304-326)                       #
# --------------------------------------------------------------------------- #

def derive_setpoints(K: float, alpha0: float, alpha_stall: float, stall_ias: float,
                     flap_deg: int, aircraft: AircraftCfg,
                     current_weight: float | None = None) -> Setpoints:
    """Port of master lines 304-326. LDmax uses Vbg for flap 0, Vfe otherwise.
    """
    if current_weight is None:
        current_weight = aircraft.gross_weight
    if flap_deg > 0 and aircraft.vfe > 0:
        ldmax_ias = aircraft.vfe
    else:
        ldmax_ias = math.sqrt(current_weight / aircraft.gross_weight) * aircraft.best_glide_ias

    alpha_range = alpha_stall - alpha0
    stall_warn_ias = stall_ias + STALL_WARN_MARGIN_KT
    maneuvering_ias = stall_ias * math.sqrt(aircraft.g_limit)

    return Setpoints(
        ldmax=K / (ldmax_ias ** 2) + alpha0,
        os_fast=NAOA_FAST * alpha_range + alpha0,
        os_slow=NAOA_SLOW * alpha_range + alpha0,
        stall_warn=K / (stall_warn_ias ** 2) + alpha0,
        stall=alpha_stall,
        maneuvering=K / (maneuvering_ias ** 2) + alpha0,
    )


def aoa_to_ias(aoa: float, K: float, alpha0: float) -> float:
    """Inverse of DerivedAOA = K/IAS² + α₀. Returns NaN if aoa <= α₀."""
    if aoa <= alpha0 or K <= 0:
        return float("nan")
    return math.sqrt(K / (aoa - alpha0))


def setpoint_ias_table(K: float, alpha0: float, sps: Setpoints) -> dict[str, float]:
    return {name: aoa_to_ias(val, K, alpha0) for name, val in sps.as_list()}


# --------------------------------------------------------------------------- #
# Section 6: Slicer — deterministic stall window detector                     #
# --------------------------------------------------------------------------- #

def _rolling_mean(arr: np.ndarray, win: int) -> np.ndarray:
    """Centered rolling mean with shrink-at-edges (not zero-pad).

    `np.convolve(..., mode='same')` zero-pads outside the array boundary, which
    artificially deflates the smoothed values near the edges. Use a cumulative-
    sum trick so edge samples average only over the points actually present.
    """
    if win <= 1 or len(arr) == 0:
        return arr.astype(float, copy=True)
    arr = arr.astype(float, copy=False)
    n = len(arr)
    half = win // 2
    cs = np.concatenate(([0.0], np.cumsum(arr)))
    out = np.empty(n)
    for i in range(n):
        lo = max(0, i - half)
        hi = min(n, i + half + 1)
        out[i] = (cs[hi] - cs[lo]) / (hi - lo)
    return out


def _first_time_below(values: np.ndarray, times_ms: np.ndarray,
                      threshold: float, start_idx: int,
                      max_ms: int) -> int | None:
    """Return the index of the first value < threshold after start_idx, within
    `max_ms` of `times_ms[start_idx]`. None if no such index."""
    t0 = times_ms[start_idx]
    limit = t0 + max_ms
    for j in range(start_idx + 1, len(values)):
        if times_ms[j] > limit:
            return None
        if values[j] < threshold:
            return j
    return None


def detect_stall_windows(log_df: pd.DataFrame, cfg_flap_bins: list[int]) -> list[dict]:
    """Deterministic scan for calibration stall runs.

    Algorithm:
      1. Smooth efisMAP over ~1 s.
      2. Find MAP drops: cruise (>MAP_CRUISE_THRESHOLD) → throttled (<MAP_DROP_THRESHOLD)
         within MAP_DROP_WINDOW_S seconds.
      3. For each drop, open a candidate window up to CANDIDATE_WINDOW_S long.
      4. Inside the candidate, find the local IAS max after the drop — requires
         IAS ≥ MIN_DIVE_PEAK_IAS (real recovery dive).
      5. From the IAS max, walk forward as long as IAS is monotonic-decreasing
         (allow small excursions). Truncate at the first MAX_DECEL_IAS_REBOUND_KT
         reversal.
      6. End = argmax of smoothed CP within the monotonic window (the stall break).
      7. Bin median flap to nearest cfg_flap_bin; reject if outside tolerance.
      8. Reject runs too short, too narrow in IAS, or with engine still running.
    """
    t = log_df["timeStamp"].to_numpy()
    ias = log_df["IAS"].to_numpy(dtype=float)
    flap = log_df["flapsPos"].to_numpy(dtype=float)
    map_raw = log_df["efisMAP"].to_numpy(dtype=float)
    rpm = log_df["efisRPM"].to_numpy(dtype=float) if "efisRPM" in log_df.columns else np.full(len(t), np.nan)
    cp = log_df["CoeffP"].to_numpy(dtype=float)
    derived_aoa = log_df["DerivedAOA"].to_numpy(dtype=float)

    # Smooth MAP over ~1 s = 50 samples
    map_smooth = _rolling_mean(map_raw, 50)

    # Scan for MAP drops. Between consecutive calibration stalls the pilot may
    # recover power only briefly before pulling throttle again (especially at
    # high flap settings where drag is high). We therefore use a rolling-history
    # "saw cruise MAP in the last N seconds" flag rather than requiring sustained
    # cruise just before the drop.
    cruise_lookback_ms = 20_000    # 20 s
    cruise_seen_ms: list[int] = []  # sliding list of timestamps where MAP > threshold
    drop_candidates: list[int] = []
    last_drop_ms = -1_000_000
    min_gap_between_drops_ms = 15_000  # don't double-trigger on a single stall

    for i in range(len(t)):
        ti = int(t[i])
        # Maintain sliding cruise history
        if map_smooth[i] > MAP_CRUISE_THRESHOLD - 1:  # slight hysteresis
            cruise_seen_ms.append(ti)
        # Drop old history
        while cruise_seen_ms and cruise_seen_ms[0] < ti - cruise_lookback_ms:
            cruise_seen_ms.pop(0)

        if map_smooth[i] < MAP_DROP_THRESHOLD and cruise_seen_ms:
            # Require that we've seen cruise-level MAP within the last N seconds
            if ti - last_drop_ms >= min_gap_between_drops_ms:
                drop_candidates.append(i)
                last_drop_ms = ti
                # Once we trigger, clear the history so we need fresh cruise
                # confirmation before the next drop can trigger
                cruise_seen_ms.clear()

    windows: list[dict] = []
    next_allowed_start_idx = 0  # drop_idx must be at or after this to be considered

    def _bin_flap(f_med: float) -> int | None:
        if not cfg_flap_bins:
            return None
        nearest = min(cfg_flap_bins, key=lambda fb: abs(fb - f_med))
        if abs(nearest - f_med) > FLAP_MATCH_TOLERANCE_DEG:
            return None
        return nearest

    for drop_idx in drop_candidates:
        # Skip drops whose candidate window would overlap an already-accepted window.
        # Without this, two consecutive MAP drops 60s apart both find their "local IAS
        # max" at the same position (the top of the first stall's recovery dive), and
        # we emit duplicate windows.
        if drop_idx < next_allowed_start_idx:
            continue
        t0 = t[drop_idx]
        end_cap_ms = t0 + int(CANDIDATE_WINDOW_S * 1000)
        j_end_cap = drop_idx
        while j_end_cap < len(t) and t[j_end_cap] <= end_cap_ms:
            j_end_cap += 1

        # Local IAS max after the drop (top of decel). Cap the lookahead window
        # for the argmax at DIVE_RECOVERY_MAX_S so it can't wander into the
        # next stall run — the pilot's recovery dive to the peak takes at most
        # 15-20 seconds in practice.
        sub_slice_max = slice(
            drop_idx,
            min(drop_idx + int(DIVE_RECOVERY_MAX_S * 50), j_end_cap),
        )
        if sub_slice_max.stop - sub_slice_max.start < 100:
            continue
        sub_ias_for_peak = _rolling_mean(ias[sub_slice_max], 25)
        if sub_ias_for_peak.max() < MIN_DIVE_PEAK_IAS:
            continue
        local_max_rel = int(np.argmax(sub_ias_for_peak))
        start_idx = drop_idx + local_max_rel

        # Walk forward maintaining monotonic decel, using smoothed IAS. Require
        # a minimum monotonic-run duration before we accept a rebound as the
        # true end — so 1–2 s of jitter at the top doesn't truncate immediately.
        # Also halt immediately if the flap position changes mid-run (the pilot
        # started a different calibration flap setting).
        smooth_full_slice = _rolling_mean(ias[start_idx:j_end_cap], 25)
        run_flap_initial = int(round(flap[start_idx]))
        trough_ias = float(smooth_full_slice[0])
        end_idx_rel = 0
        for k_rel in range(1, len(smooth_full_slice)):
            abs_idx = start_idx + k_rel
            # Flap change detection — stop the window at the change point
            if abs(flap[abs_idx] - run_flap_initial) > FLAP_MATCH_TOLERANCE_DEG:
                break
            if smooth_full_slice[k_rel] < trough_ias:
                trough_ias = float(smooth_full_slice[k_rel])
                end_idx_rel = k_rel
            elif smooth_full_slice[k_rel] - trough_ias > MAX_DECEL_IAS_REBOUND_KT:
                run_so_far_s = (t[abs_idx] - t[start_idx]) / 1000.0
                if run_so_far_s >= MIN_MONOTONIC_RUN_S:
                    break
                # Otherwise ignore the rebound and keep scanning — the peak
                # hadn't really started the deceleration yet.
        end_idx = start_idx + end_idx_rel

        # Require minimum duration and IAS span
        duration_s = (t[end_idx] - t[start_idx]) / 1000.0
        ias_span = float(smooth_full_slice[0] - trough_ias)
        if duration_s < MIN_DECEL_DURATION_S or ias_span < MIN_IAS_SPAN_KT:
            continue

        # Engine must be windmilling / idle (we pulled power)
        run_rpm = rpm[start_idx:end_idx + 1]
        run_map = map_smooth[start_idx:end_idx + 1]
        if np.isfinite(run_rpm).any() and np.nanmean(run_rpm) > MAX_RPM_THROTTLED:
            continue
        if np.nanmean(run_map) > MAP_DROP_THRESHOLD + 3:
            continue

        # Flap binning — only accept runs near a cfg flap value
        run_flap = flap[start_idx:end_idx + 1]
        median_flap = float(np.median(run_flap))
        flap_bin = _bin_flap(median_flap)
        if flap_bin is None:
            # the -3° discard run would fall out here, or any mis-configured run
            continue

        # Stall break: argmax smoothed CP within the decel window
        cp_slice = cp[start_idx:end_idx + 1]
        aoa_slice = derived_aoa[start_idx:end_idx + 1]
        # Smooth CP inside this slice for a stable argmax
        _, cp_s_local, stall_idx_local, stall_ias_local = smooth_ias_cp(
            ias[start_idx:end_idx + 1], cp_slice
        )
        stall_absolute_idx = start_idx + stall_idx_local
        # Final window ends at the stall break
        end_idx = stall_absolute_idx

        # Sanity: max DerivedAOA must be >= 5° (it's a real stall approach)
        if len(aoa_slice) > 0 and aoa_slice[:stall_idx_local + 1].max() < 5:
            continue

        windows.append({
            "flap_bin": flap_bin,
            "start_idx": int(start_idx),
            "end_idx": int(end_idx),
            "t_start_ms": int(t[start_idx]),
            "t_end_ms": int(t[end_idx]),
            "median_flap": median_flap,
            "stall_ias": float(stall_ias_local),
            "n_points": end_idx - start_idx + 1,
            "ias_span_kt": float(ias_span),
            "duration_s": float((t[end_idx] - t[start_idx]) / 1000.0),
        })
        next_allowed_start_idx = end_idx + 1

    # Dedup overlapping windows (can happen if two drops trigger within a run)
    windows.sort(key=lambda w: w["start_idx"])
    deduped: list[dict] = []
    for w in windows:
        if deduped and w["start_idx"] < deduped[-1]["end_idx"]:
            # Keep the earlier one
            continue
        deduped.append(w)
    return deduped


def build_runs_from_windows(log_df: pd.DataFrame, windows: list[dict]) -> list[Run]:
    """Slice log_df into Run dataclasses, one per window, with smoothing applied."""
    t = log_df["timeStamp"].to_numpy()
    ias = log_df["IAS"].to_numpy(dtype=float)
    cp = log_df["CoeffP"].to_numpy(dtype=float)
    derived_aoa = log_df["DerivedAOA"].to_numpy(dtype=float)
    pitch = log_df["Pitch"].to_numpy(dtype=float)
    flight_path = log_df["FlightPath"].to_numpy(dtype=float)
    flap = log_df["flapsPos"].to_numpy(dtype=float)

    # Bucket windows per flap in order so we can number them
    per_flap_idx: dict[int, int] = {}
    runs: list[Run] = []

    for w in windows:
        si = w["start_idx"]
        ei = w["end_idx"]
        slc = slice(si, ei + 1)
        ias_run = ias[slc]
        cp_run = cp[slc]
        aoa_run = derived_aoa[slc]
        s_ias, s_cp, stall_index_local, stall_ias_local = smooth_ias_cp(ias_run, cp_run)

        # Synthesize decel rate: centered finite diff of smoothed IAS over 1 s (~50 pts at 50 Hz)
        decel = np.zeros_like(ias_run)
        half = 25
        for i in range(len(ias_run)):
            lo = max(0, i - half)
            hi = min(len(ias_run) - 1, i + half)
            dt_s = (t[si + hi] - t[si + lo]) / 1000.0
            if dt_s > 0:
                decel[i] = -(s_ias[hi] - s_ias[lo]) / dt_s

        flap_bin = int(w["flap_bin"])
        per_flap_idx[flap_bin] = per_flap_idx.get(flap_bin, 0) + 1
        run_id = f"f{flap_bin:02d}_r{per_flap_idx[flap_bin]:02d}"

        runs.append(Run(
            run_id=run_id,
            flap_bin=flap_bin,
            t_start_ms=int(t[si]),
            t_end_ms=int(t[ei]),
            ias_raw=ias_run,
            ias_smooth=s_ias,
            cp_raw=cp_run,
            cp_smooth=s_cp,
            derived_aoa=aoa_run,
            pitch=pitch[slc],
            flight_path=flight_path[slc],
            decel_rate=decel,
            stall_index=stall_index_local,
            stall_ias=stall_ias_local,
            median_flap=float(np.median(flap[slc])),
        ))

    return runs


# --------------------------------------------------------------------------- #
# Section 7: Run quality ranking                                              #
# --------------------------------------------------------------------------- #

def _clip01(x: float) -> float:
    return max(0.0, min(1.0, x))


def rank_runs(runs: list[Run], per_run_ols: dict[str, RunFit]) -> dict[str, RunQuality]:
    """Score and verdict each run. K-outlier is computed per-flap-bin."""
    # Group runs by flap for the K-median calc
    flap_to_ks: dict[int, list[float]] = {}
    for r in runs:
        flap_to_ks.setdefault(r.flap_bin, []).append(per_run_ols[r.run_id].K)
    flap_median_k: dict[int, float] = {f: float(np.median(ks)) for f, ks in flap_to_ks.items()}

    out: dict[str, RunQuality] = {}
    for r in runs:
        fit = per_run_ols[r.run_id]

        # Decel stats over the monotonic window (before stall break)
        hi = r.stall_index + 1
        decel_slice = r.decel_rate[:hi]
        decel_mean = float(np.mean(decel_slice)) if len(decel_slice) else 0.0
        decel_std = float(np.std(decel_slice)) if len(decel_slice) else 0.0

        # Scoring
        ias_r2_s = _clip01((fit.ias_to_aoa_r2 - 0.85) / 0.14)
        cp_r2_s = _clip01((fit.cp_r2 - 0.85) / 0.14)
        # Decel score: gaussian around (mean=target_decel, std=0), penalizing
        # off-target mean AND high std. Positive decel_rate = slowing down
        # (ideal calibration technique is ~0.5 kt/s).
        decel_s = (
            math.exp(-((decel_std) / QUALITY_DECEL_STD_TARGET) ** 2 / 2.0)
            * math.exp(-((decel_mean - QUALITY_DECEL_TARGET_KTS) / 0.4) ** 2 / 2.0)
        )
        length_s = _clip01(fit.n_points / QUALITY_LENGTH_TARGET_PTS)

        med_k = flap_median_k.get(r.flap_bin, fit.K)
        k_dev = 100.0 * (fit.K - med_k) / med_k if med_k else 0.0
        k_s = math.exp(-((k_dev / QUALITY_K_OUTLIER_PCT) ** 2) / 2.0)

        score = (
            QUALITY_W_IAS_R2 * ias_r2_s
            + QUALITY_W_CP_R2 * cp_r2_s
            + QUALITY_W_DECEL * decel_s
            + QUALITY_W_LENGTH * length_s
            + QUALITY_W_K_OUTLIER * k_s
        )

        reasons: list[str] = []
        if ias_r2_s < 0.5:
            reasons.append(f"low IAS-AOA R² ({fit.ias_to_aoa_r2:.4f})")
        if cp_r2_s < 0.5:
            reasons.append(f"low CP-AOA R² ({fit.cp_r2:.4f})")
        if decel_s < 0.3:
            reasons.append(f"noisy or off-target decel (mean={decel_mean:+.2f}, std={decel_std:.2f} kt/s)")
        if length_s < 0.5:
            reasons.append(f"short window ({fit.n_points} pts)")
        if k_s < 0.5:
            reasons.append(f"K outlier ({k_dev:+.1f}% vs flap median)")

        if score >= QUALITY_KEEP_THRESHOLD:
            verdict = "KEEP"
        elif score >= QUALITY_BORDERLINE_THRESHOLD:
            verdict = "BORDERLINE"
        else:
            verdict = "DISCARD"

        out[r.run_id] = RunQuality(
            run_id=r.run_id, flap=r.flap_bin, score=score, verdict=verdict,
            reasons=reasons, ias_r2=fit.ias_to_aoa_r2, cp_r2=fit.cp_r2,
            decel_mean=decel_mean, decel_std=decel_std, n_points=fit.n_points,
            K=fit.K, K_dev_pct=k_dev,
        )
    return out


# --------------------------------------------------------------------------- #
# Section 8: Orchestration                                                    #
# --------------------------------------------------------------------------- #

def _fmt_setpoint_ias_row(ias_map: dict[str, float]) -> str:
    parts = []
    for name in ("LDmax", "OS_fast", "OS_slow", "StallWarn", "Stall", "Maneuvering"):
        v = ias_map.get(name, float("nan"))
        parts.append(f"{v:6.1f}" if math.isfinite(v) else "  nan ")
    return " ".join(parts)


def build_config_summary(label: str, fit: RunFit, flap_deg: int,
                         aircraft: AircraftCfg, n_runs: int, source_runs: str) -> ConfigSummary:
    sps = derive_setpoints(fit.K, fit.alpha0, fit.alpha_stall, fit.stall_ias,
                           flap_deg, aircraft)
    sp_ias = setpoint_ias_table(fit.K, fit.alpha0, sps)
    return ConfigSummary(
        label=label, K=fit.K, alpha0=fit.alpha0, alpha_stall=fit.alpha_stall,
        stall_ias=fit.stall_ias,
        cp_a2=fit.cp_a2, cp_a1=fit.cp_a1, cp_a0=fit.cp_a0,
        ias_r2=fit.ias_to_aoa_r2, cp_r2=fit.cp_r2,
        n_points=fit.n_points, source=source_runs, setpoints=sps,
        setpoints_ias=sp_ias,
    )


def build_device_summary(flap_cfg: FlapCfg, aircraft: AircraftCfg) -> ConfigSummary:
    stall_ias = math.sqrt(flap_cfg.kfit / (flap_cfg.alpha_stall - flap_cfg.alpha0)) \
        if flap_cfg.alpha_stall > flap_cfg.alpha0 and flap_cfg.kfit > 0 else float("nan")
    sps = Setpoints(
        ldmax=flap_cfg.ldmax, os_fast=flap_cfg.os_fast, os_slow=flap_cfg.os_slow,
        stall_warn=flap_cfg.stall_warn, stall=flap_cfg.stall, maneuvering=flap_cfg.maneuvering,
    )
    sp_ias = setpoint_ias_table(flap_cfg.kfit, flap_cfg.alpha0, sps)
    # Cfg's AOA_CURVE is stored as (X3, X2, X1, X0). Master's wizard fits a
    # quadratic, so X3 is always 0; (X2, X1, X0) is the runtime polynomial used
    # in flight to compute AOA from CP.
    _, x2, x1, x0 = flap_cfg.aoa_curve  # X3 ignored (always 0)
    return ConfigSummary(
        label="device cfg", K=flap_cfg.kfit, alpha0=flap_cfg.alpha0,
        alpha_stall=flap_cfg.alpha_stall, stall_ias=stall_ias,
        cp_a2=x2, cp_a1=x1, cp_a0=x0,
        ias_r2=None, cp_r2=None, n_points=None, source="saved on device",
        setpoints=sps, setpoints_ias=sp_ias,
    )


def provenance_match(flap_cfg: FlapCfg, runs_at_flap: list[Run],
                     per_run_ols: dict[str, RunFit]) -> str:
    """Find the log run whose OLS fit matches the cfg values. Returns run_id
    or 'unknown — no match within tolerance'."""
    if flap_cfg.kfit <= 0:
        return "n/a — cfg KFIT is zero"
    best_id = None
    best_dev = float("inf")
    for r in runs_at_flap:
        fit = per_run_ols[r.run_id]
        k_dev = abs(fit.K - flap_cfg.kfit) / max(abs(flap_cfg.kfit), 1e-9)
        a0_dev = abs(fit.alpha0 - flap_cfg.alpha0)
        as_dev = abs(fit.alpha_stall - flap_cfg.alpha_stall)
        combined = k_dev + a0_dev + as_dev
        if combined < best_dev:
            best_dev = combined
            best_id = r.run_id
        if k_dev < 0.01 and a0_dev < 0.05 and as_dev < 0.05:
            return r.run_id
    return f"unknown (closest: {best_id}, combined dev={best_dev:.3f})"


def write_report(report_path: Path, ctx: dict) -> None:
    lines: list[str] = []

    lines.append("# OnSpeed Calibration Analysis Report")
    lines.append("")
    lines.append(f"- **Log**: `{ctx['log_path']}`")
    lines.append(f"- **Cfg**: `{ctx['cfg_path']}`")
    lines.append(f"- **Cal dir**: `{ctx['cal_dir']}`")
    lines.append(f"- **Aircraft**: gross {ctx['aircraft'].gross_weight:.0f} lb, Vbg {ctx['aircraft'].best_glide_ias:.0f} kt, Vfe {ctx['aircraft'].vfe:.0f} kt, G-limit {ctx['aircraft'].g_limit}")
    lines.append("")

    lines.append("## 1. OLS port correctness gate")
    lines.append("")
    gate = ctx["gate_results"]
    if gate is None:
        lines.append("Skipped — no cal CSVs found in cal-dir.")
    else:
        lines.append(f"Reproduced master's OLS fit on {len(gate)} saved cal CSVs:")
        lines.append("")
        lines.append("| File | header K | port K | ΔK% | header α₀ | port α₀ | header α_stall | port α_stall | status |")
        lines.append("|---|---:|---:|---:|---:|---:|---:|---:|:---:|")
        for g in gate:
            lines.append(
                f"| `{g['file']}` | {g['hdr_k']:.2f} | {g['port_k']:.2f} | {g['dk_pct']:+.3f} | "
                f"{g['hdr_a0']:+.4f} | {g['port_a0']:+.4f} | {g['hdr_as']:+.4f} | {g['port_as']:+.4f} | {g['status']} |"
            )
    lines.append("")

    lines.append("## 2. Stall window detection")
    lines.append("")
    wins = ctx["windows"]
    lines.append(f"Slicer detected **{len(wins)} stall runs** in the log:")
    lines.append("")
    lines.append("| Run ID | Flap | t_start (ms) | t_end (ms) | Dur (s) | N pts | IAS span (kt) | Stall IAS (kt) | Median flap |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for r in ctx["runs"]:
        w = next((w for w in wins if w["start_idx"] == ctx["run_to_start_idx"][r.run_id]), None)
        dur = (r.t_end_ms - r.t_start_ms) / 1000.0
        lines.append(
            f"| `{r.run_id}` | {r.flap_bin}° | {r.t_start_ms} | {r.t_end_ms} | {dur:.1f} | "
            f"{len(r.ias_raw)} | {r.ias_raw.max() - r.ias_raw.min():.1f} | {r.stall_ias:.1f} | {r.median_flap:+.1f}° |"
        )
    lines.append("")

    lines.append("## 3. Run quality ranking")
    lines.append("")
    lines.append("Score = weighted blend of IAS-AOA R² (×0.25), CP-AOA R² (×0.25), decel-rate stability (×0.20), window length (×0.15), K-outlier penalty (×0.15). Verdicts: KEEP ≥ 0.75, BORDERLINE 0.50–0.75, DISCARD < 0.50.")
    lines.append("")
    lines.append("| Run ID | Flap | Verdict | Score | IAS R² | CP R² | Decel mean/std | N pts | K dev vs flap median | Reasons |")
    lines.append("|---|---:|:---:|---:|---:|---:|---|---:|---:|---|")
    for q in sorted(ctx["quality"].values(), key=lambda q: (q.flap, -q.score)):
        reasons = "; ".join(q.reasons) if q.reasons else "—"
        lines.append(
            f"| `{q.run_id}` | {q.flap}° | **{q.verdict}** | {q.score:.3f} | "
            f"{q.ias_r2:.4f} | {q.cp_r2:.4f} | {q.decel_mean:+.2f} / {q.decel_std:.2f} kt/s | "
            f"{q.n_points} | {q.K_dev_pct:+.1f}% | {reasons} |"
        )
    lines.append("")

    lines.append("## 4. Core comparison: 2×2 ablation per flap")
    lines.append("")
    lines.append("For each flap, compare the on-device config against five alternative fits computed from the log-sliced runs. ΔR² and ΔK%/Δα₀/Δα_stall are relative to the device cfg.")
    lines.append("")

    for flap in ctx["flap_bins"]:
        lines.append(f"### Flap {flap}°")
        lines.append("")
        summaries: list[ConfigSummary] = ctx["summaries"][flap]
        # Header row
        labels = [s.label for s in summaries]
        lines.append("| Metric | " + " | ".join(labels) + " |")
        lines.append("|---|" + "|".join(["---:"] * len(summaries)) + "|")

        def _row(name: str, vals: list):
            cells = []
            for v in vals:
                if v is None:
                    cells.append("—")
                elif isinstance(v, float):
                    if math.isnan(v):
                        cells.append("—")
                    else:
                        cells.append(f"{v:+.4f}" if "α" in name or "R²" in name else f"{v:.2f}")
                else:
                    cells.append(str(v))
            lines.append(f"| {name} | " + " | ".join(cells) + " |")

        _row("K (deg·kt²)", [s.K for s in summaries])
        _row("α₀ (deg)", [s.alpha0 for s in summaries])
        _row("α_stall (deg)", [s.alpha_stall for s in summaries])
        _row("Stall IAS (kt)", [s.stall_ias for s in summaries])
        _row("IAS-AOA R²", [s.ias_r2 for s in summaries])
        _row("CP-AOA R²", [s.cp_r2 for s in summaries])
        _row("N pts", [s.n_points for s in summaries])
        _row("Source", [s.source for s in summaries])
        lines.append("")

        # Runtime CP-to-AOA polynomial — what actually executes in flight
        lines.append("**Runtime CP→AOA polynomial** (`AOA = a2·CP² + a1·CP + a0`) — this is the curve the firmware evaluates 50× per second:")
        lines.append("")
        lines.append("| Coeff | " + " | ".join([s.label for s in summaries]) + " |")
        lines.append("|---|" + "|".join(["---:"] * len(summaries)) + "|")
        for coef_name, attr in (("a2", "cp_a2"), ("a1", "cp_a1"), ("a0", "cp_a0")):
            row = [f"{getattr(s, attr):+.4f}" for s in summaries]
            lines.append(f"| {coef_name} | " + " | ".join(row) + " |")
        lines.append("")

        # Ablation deltas — vs device cfg
        dev = summaries[0]
        lines.append("**Deltas vs device cfg:**")
        lines.append("")
        lines.append("| Variant | ΔK% | Δα₀ | Δα_stall | R² |")
        lines.append("|---|---:|---:|---:|---:|")
        for s in summaries[1:]:
            dk_pct = 100.0 * (s.K - dev.K) / dev.K if dev.K else float("nan")
            da0 = s.alpha0 - dev.alpha0
            das = s.alpha_stall - dev.alpha_stall
            r2str = f"{s.ias_r2:.4f}" if s.ias_r2 is not None else "—"
            lines.append(f"| {s.label} | {dk_pct:+.2f}% | {da0:+.3f}° | {das:+.3f}° | {r2str} |")
        lines.append("")

        # Setpoint IAS table for this flap
        lines.append("**Setpoint IAS (kt) at 1G under each configuration:**")
        lines.append("")
        header = "| Setpoint | " + " | ".join([s.label for s in summaries]) + " |"
        lines.append(header)
        lines.append("|---|" + "|".join(["---:"] * len(summaries)) + "|")
        for sp_name in ("LDmax", "OS_fast", "OS_slow", "StallWarn", "Stall", "Maneuvering"):
            row_vals = []
            for s in summaries:
                v = (s.setpoints_ias or {}).get(sp_name, float("nan"))
                row_vals.append(f"{v:.1f}" if math.isfinite(v) else "—")
            lines.append(f"| {sp_name} | " + " | ".join(row_vals) + " |")
        lines.append("")

        lines.append(f"**Provenance**: device cfg flap {flap}° matches log run `{ctx['provenance'][flap]}`")
        lines.append("")

    lines.append("## 5. Inter-flap gap analysis (the 16→33 transition question)")
    lines.append("")
    lines.append("For each adjacent flap pair, compute `gap = IAS_OS_slow[lower] - IAS_OS_fast[higher]`. Positive gap = dead zone (no tonal feedback across the transition). Negative gap = overlapping bands.")
    lines.append("")

    flap_bins_sorted = sorted(ctx["flap_bins"])
    pairs = [(flap_bins_sorted[i], flap_bins_sorted[i + 1]) for i in range(len(flap_bins_sorted) - 1)]

    if pairs:
        lines.append("| Pair | " + " | ".join([s.label for s in ctx["summaries"][flap_bins_sorted[0]]]) + " |")
        lines.append("|---|" + "|".join(["---:"] * len(ctx["summaries"][flap_bins_sorted[0]])) + "|")
        for lo, hi in pairs:
            row = [f"{lo}°→{hi}°"]
            for idx in range(len(ctx["summaries"][lo])):
                lo_sp = ctx["summaries"][lo][idx].setpoints_ias or {}
                hi_sp = ctx["summaries"][hi][idx].setpoints_ias or {}
                lo_slow = lo_sp.get("OS_slow", float("nan"))
                hi_fast = hi_sp.get("OS_fast", float("nan"))
                gap = lo_slow - hi_fast
                if math.isfinite(gap):
                    row.append(f"{gap:+.1f}")
                else:
                    row.append("—")
            lines.append("| " + " | ".join(row) + " |")
        lines.append("")

    # Plain-English summary
    lines.append("## 6. Bottom line")
    lines.append("")
    bl = ctx["bottom_line"]
    for line in bl:
        lines.append(f"- {line}")
    lines.append("")

    lines.append("## 7. Recommended setpoints (best alternative)")
    lines.append("")
    lines.append("Per flap, the setpoint AOAs from the highest-R² alternative configuration.")
    lines.append("")
    lines.append("| Flap | Source | LDmax | OS_fast | OS_slow | StallWarn | Stall | Maneuvering |")
    lines.append("|---|---|---:|---:|---:|---:|---:|---:|")
    for flap in flap_bins_sorted:
        best = ctx["recommended"][flap]  # ConfigSummary
        sp = best.setpoints
        if sp is None:
            continue
        lines.append(
            f"| {flap}° | {best.label} | {sp.ldmax:+.2f} | {sp.os_fast:+.2f} | {sp.os_slow:+.2f} | "
            f"{sp.stall_warn:+.2f} | {sp.stall:+.2f} | {sp.maneuvering:+.2f} |"
        )
    lines.append("")

    lines.append("## 8. Proposed full cfg patch (per flap)")
    lines.append("")
    lines.append("The runtime CP→AOA polynomial AND the setpoint AOAs MUST change as a coordinated unit. The polynomial converts pressure to AOA at 50 Hz; the setpoints are the AOA thresholds the polynomial output is compared against. Changing only one would mean the runtime curve and the setpoint table reference different coordinate systems and the tones would be nonsense.")
    lines.append("")
    lines.append("Below is a complete `<FLAP_POSITION>` block per flap, ready to drop into the cfg as a replacement for the existing entry. Each block contains both the new polynomial coefficients (`<X2>`, `<X1>`, `<X0>`) and the new setpoint AOA values, all derived from the SAME alternative fit.")
    lines.append("")
    for flap in flap_bins_sorted:
        best = ctx["recommended"][flap]
        if best.setpoints is None or not math.isfinite(best.K):
            continue
        sp = best.setpoints
        sp_ias = best.setpoints_ias or {}
        # Get the cfg-side equivalent for diff
        dev = ctx["summaries"][flap][0]
        dev_ias = dev.setpoints_ias or {}

        lines.append(f"### Flap {flap}° — source: {best.label}")
        lines.append("")
        lines.append("```xml")
        lines.append("<FLAP_POSITION>")
        lines.append(f"    <DEGREES>{flap}</DEGREES>")
        lines.append(f"    <LDMAXAOA>{sp.ldmax:.4f}</LDMAXAOA>")
        lines.append(f"    <ONSPEEDFASTAOA>{sp.os_fast:.4f}</ONSPEEDFASTAOA>")
        lines.append(f"    <ONSPEEDSLOWAOA>{sp.os_slow:.4f}</ONSPEEDSLOWAOA>")
        lines.append(f"    <STALLWARNAOA>{sp.stall_warn:.4f}</STALLWARNAOA>")
        lines.append(f"    <STALLAOA>{sp.stall:.4f}</STALLAOA>")
        lines.append(f"    <MANAOA>{sp.maneuvering:.4f}</MANAOA>")
        lines.append(f"    <ALPHA0>{best.alpha0:.4f}</ALPHA0>")
        lines.append(f"    <ALPHASTALL>{best.alpha_stall:.4f}</ALPHASTALL>")
        lines.append(f"    <KFIT>{best.K:.3f}</KFIT>")
        lines.append("    <AOA_CURVE>")
        lines.append("        <TYPE>1</TYPE>")
        lines.append("        <X3>0</X3>")
        lines.append(f"        <X2>{best.cp_a2:.6f}</X2>")
        lines.append(f"        <X1>{best.cp_a1:.6f}</X1>")
        lines.append(f"        <X0>{best.cp_a0:.6f}</X0>")
        lines.append("    </AOA_CURVE>")
        lines.append("</FLAP_POSITION>")
        lines.append("```")
        lines.append("")
        lines.append("**Predicted setpoint IAS (kt) at 1G under this config vs current cfg:**")
        lines.append("")
        lines.append("| Setpoint | current cfg | proposed | Δ kt |")
        lines.append("|---|---:|---:|---:|")
        for name in ("LDmax", "OS_fast", "OS_slow", "StallWarn", "Stall", "Maneuvering"):
            cur = dev_ias.get(name, float("nan"))
            new = sp_ias.get(name, float("nan"))
            diff = new - cur if math.isfinite(cur) and math.isfinite(new) else float("nan")
            cur_s = f"{cur:.1f}" if math.isfinite(cur) else "—"
            new_s = f"{new:.1f}" if math.isfinite(new) else "—"
            diff_s = f"{diff:+.1f}" if math.isfinite(diff) else "—"
            lines.append(f"| {name} | {cur_s} | {new_s} | {diff_s} |")
        lines.append("")

    lines.append("## Methodology notes")
    lines.append("")
    lines.append("- **Baseline algorithm**: OLS via `regression.polynomial(..., order: 1/2)` in master:javascript_calibration.h. Python port via Cramer's-rule `wls_linear`/`wls_quadratic` with uniform weights.")
    lines.append("- **Alternative algorithm (PR #82 spec)**: WLS with inverse-variance weights from a 29-sample centered rolling stddev of DerivedAOA. Window matches Vac's 50 Hz tuning (CLAUDE.md:74). Uses sample variance (ddof=1) to match Vac's STDEV.S; the 74c8f1b JS uses population variance which is a minor rebase-time bug.")
    lines.append("- **Stacking (PR #83 spec)**: per-flap concat of raw IAS / smoothed CP / DerivedAOA arrays from each run, then a single fit. Stall IAS is averaged across kept runs.")
    lines.append("- **CP definition**: `CoeffP = P45Smoothed / PfwdSmoothed`, verified identical in the log. EMA smoothing on top (`CP[i]*0.90 + CP[i-1]*0.10`) applied inside the wizard/port; not in Vac's Excel — minor deviation documented here.")
    lines.append("- **R² reporting**: unweighted R² (Vac's convention, VBA line 114-119) in the tables; weighted R² computed internally but not displayed. Unweighted is the apples-to-apples comparison across OLS and WLS.")
    lines.append("- **Current weight assumption**: equals gross weight (2700 lb). Only affects flap-0 LDmax IAS via `sqrt(W_curr/W_max) × Vbg`.")
    lines.append("- **Slicer**: deterministic MAP-drop → post-dive IAS max → monotonic decel → stall break. No pilot marks, no baked-in timestamps; works on any OnSpeed log.")
    lines.append("")

    report_path.write_text("\n".join(lines))


def plot_flap(flap: int, runs: list[Run], summaries: list[ConfigSummary],
              per_run_ols: dict[str, RunFit], per_run_wls: dict[str, RunFit],
              quality: dict[str, RunQuality], out_path: Path) -> None:
    fig, ax = plt.subplots(figsize=(10, 7))

    for r in runs:
        hi = r.stall_index + 1
        x = 1.0 / (r.ias_raw[:hi] ** 2)
        y = r.derived_aoa[:hi]
        q = quality[r.run_id]
        color = {"KEEP": "#1f77b4", "BORDERLINE": "#ff7f0e", "DISCARD": "#d62728"}.get(q.verdict, "gray")
        ax.scatter(x, y, s=6, alpha=0.35, color=color, label=None)

        # Per-run OLS line
        ols = per_run_ols[r.run_id]
        xs = np.linspace(x.min(), x.max(), 50)
        ax.plot(xs, ols.K * xs + ols.alpha0, color="#888888", alpha=0.4, linewidth=0.8)

    # Stacked and cfg lines — they each have .K and .alpha0, draw over full x range
    all_x = np.concatenate([1.0 / (r.ias_raw[:r.stall_index + 1] ** 2) for r in runs])
    x_min, x_max = all_x.min(), all_x.max()
    xs_full = np.linspace(x_min, x_max, 100)

    line_styles = {
        "device cfg":       dict(color="#d62728", linestyle="--", linewidth=2.0, label="device cfg"),
        "OLS single-best":  dict(color="#333333", linestyle=":",  linewidth=1.5, label="OLS single-best"),
        "WLS single-best":  dict(color="#1f77b4", linestyle=":",  linewidth=1.5, label="WLS single-best"),
        "OLS stacked-all":  dict(color="#333333", linestyle="--", linewidth=2.0, label="OLS stacked-all"),
        "WLS stacked-all":  dict(color="#000000", linestyle="-",  linewidth=2.5, label="WLS stacked-all"),
        "WLS stacked-KEEP": dict(color="#2ca02c", linestyle="-",  linewidth=2.0, label="WLS stacked-KEEP"),
    }
    for s in summaries:
        sty = line_styles.get(s.label, dict(color="gray"))
        ax.plot(xs_full, s.K * xs_full + s.alpha0, **sty)

    # Secondary IAS ticks on the top axis
    ax.set_xlabel("1 / IAS²   (kt⁻²)")
    ax.set_ylabel("DerivedAOA (deg)")
    ax.set_title(f"Flap {flap}° — {len(runs)} runs, WLS vs OLS, stacked vs single")
    ax.legend(loc="upper left", fontsize=8, framealpha=0.9)
    ax.grid(True, alpha=0.3)

    # IAS overlay on top x axis
    ax2 = ax.twiny()
    ias_ticks = [140, 120, 100, 90, 80, 70, 60, 55, 50]
    ias_tick_positions = [1.0 / (v ** 2) for v in ias_ticks]
    # Only show ticks that fall inside the range
    mask = [(p >= x_min) and (p <= x_max) for p in ias_tick_positions]
    ax2.set_xlim(ax.get_xlim())
    ax2.set_xticks([p for p, m in zip(ias_tick_positions, mask) if m])
    ax2.set_xticklabels([f"{v}" for v, m in zip(ias_ticks, mask) if m])
    ax2.set_xlabel("IAS (kt)")

    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)


def plot_flap_cp_curve(flap: int, runs: list[Run], summaries: list[ConfigSummary],
                        per_run_ols: dict[str, RunFit], quality: dict[str, RunQuality],
                        out_path: Path) -> None:
    """CP-to-AOA plot — the curve actually evaluated in flight at 50 Hz.

    This is the more important visualization for "what changes if we update the
    cfg": shows the runtime polynomial AOA = a2·CP² + a1·CP + a0 for the device
    cfg vs each alternative, with the raw scatter underneath.
    """
    fig, ax = plt.subplots(figsize=(10, 7))

    all_cp = []
    all_aoa = []
    for r in runs:
        hi = r.stall_index + 1
        cp = r.cp_smooth[:hi]
        aoa = r.derived_aoa[:hi]
        q = quality[r.run_id]
        color = {"KEEP": "#1f77b4", "BORDERLINE": "#ff7f0e", "DISCARD": "#d62728"}.get(q.verdict, "gray")
        ax.scatter(cp, aoa, s=6, alpha=0.30, color=color, label=None)
        all_cp.append(cp)
        all_aoa.append(aoa)

    cp_concat = np.concatenate(all_cp)
    cp_min, cp_max = cp_concat.min(), cp_concat.max()
    cp_grid = np.linspace(cp_min, cp_max, 200)

    line_styles = {
        "device cfg":       dict(color="#d62728", linestyle="--", linewidth=2.5, label="device cfg (in flight today)"),
        "OLS single-best":  dict(color="#333333", linestyle=":",  linewidth=1.5, label="OLS single-best"),
        "WLS single-best":  dict(color="#1f77b4", linestyle=":",  linewidth=1.5, label="WLS single-best"),
        "OLS stacked-all":  dict(color="#333333", linestyle="--", linewidth=2.0, label="OLS stacked-all"),
        "WLS stacked-all":  dict(color="#000000", linestyle="-",  linewidth=2.5, label="WLS stacked-all"),
        "WLS stacked-KEEP": dict(color="#2ca02c", linestyle="-",  linewidth=2.0, label="WLS stacked-KEEP"),
    }
    for s in summaries:
        if not math.isfinite(s.cp_a2) and not math.isfinite(s.cp_a1) and not math.isfinite(s.cp_a0):
            continue
        sty = line_styles.get(s.label, dict(color="gray"))
        y = s.cp_a2 * cp_grid * cp_grid + s.cp_a1 * cp_grid + s.cp_a0
        ax.plot(cp_grid, y, **sty)

    ax.set_xlabel("Coefficient of Pressure (CP = P45Smoothed / PfwdSmoothed)")
    ax.set_ylabel("AOA (deg)   [polynomial output / DerivedAOA scatter]")
    ax.set_title(f"Flap {flap}° — runtime CP→AOA polynomial vs measured DerivedAOA\n"
                 f"(this is the curve the firmware evaluates 50× per second)")
    ax.legend(loc="upper left", fontsize=8, framealpha=0.9)
    ax.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="OnSpeed offline calibration analysis (OLS vs WLS, single vs stacked).",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--log", type=Path, required=True,
                        help="Flight log CSV from the OnSpeed SD card (50 Hz).")
    parser.add_argument("--cfg", type=Path, required=True,
                        help="On-device config XML that was running when the log was captured.")
    parser.add_argument("--cal-dir", type=Path, default=None,
                        help="Optional directory of saved calibration-wizard CSVs; used only "
                             "for the OLS port correctness gate. If omitted, the gate is skipped.")
    parser.add_argument("--out-dir", type=Path, required=True,
                        help="Output directory for REPORT.md, figs/ and tables/.")
    args = parser.parse_args()

    out_dir: Path = args.out_dir
    fig_dir = out_dir / "figs"
    tables_dir = out_dir / "tables"
    for d in (out_dir, fig_dir, tables_dir):
        d.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / "REPORT.md"

    # --- Step 1: parse cfg ---
    print(f"Parsing cfg: {args.cfg}")
    cfg = parse_config(args.cfg)
    print(f"  aircraft: gross={cfg.aircraft.gross_weight} Vbg={cfg.aircraft.best_glide_ias} Vfe={cfg.aircraft.vfe} G={cfg.aircraft.g_limit}")
    print(f"  flaps: {sorted(cfg.flaps.keys())}")

    # --- Step 2+3: parse saved CSVs + OLS port correctness gate ---
    cal_csvs: list[CalCsvRef] = []
    gate_results: list[dict] = []
    if args.cal_dir is not None and args.cal_dir.is_dir():
        for csv_path in sorted(args.cal_dir.glob("calibration-*.csv")):
            try:
                ref = parse_cal_csv(csv_path)
                cal_csvs.append(ref)
            except Exception as e:
                print(f"  !! failed to parse {csv_path}: {e}")
                continue

        print(f"\nOLS port correctness gate ({len(cal_csvs)} saved CSVs):")
        gate_fail = False
        for ref in cal_csvs:
            try:
                port_fit = fit_csv_body(ref.body, algo="OLS")
            except Exception as e:
                print(f"  FAIL {ref.path.name}: fit failed ({e})")
                gate_fail = True
                continue

            dk = abs(port_fit.K - _extract_kfit_from_csv_header(ref)) if ref.ias_to_aoa_r2 else float("inf")
            hdr_k = _extract_kfit_from_csv_header(ref)
            hdr_a0 = ref.alpha0
            hdr_as = ref.alpha_stall

            dk_pct = (
                100.0 * (port_fit.K - hdr_k) / hdr_k if hdr_k else float("inf")
            )
            da0 = abs(port_fit.alpha0 - hdr_a0)
            das = abs(port_fit.alpha_stall - hdr_as)
            dr2 = abs(port_fit.ias_to_aoa_r2 - ref.ias_to_aoa_r2)

            ok = (abs(dk_pct) < 1.0 and da0 < 0.05 and das < 0.05 and dr2 < 0.005)
            if not ok:
                gate_fail = True

            status = "PASS" if ok else "FAIL"
            gate_results.append({
                "file": ref.path.name,
                "hdr_k": hdr_k,
                "port_k": port_fit.K,
                "dk_pct": dk_pct,
                "hdr_a0": hdr_a0,
                "port_a0": port_fit.alpha0,
                "hdr_as": hdr_as,
                "port_as": port_fit.alpha_stall,
                "status": status,
            })
            print(f"  {status} {ref.path.name}: "
                  f"K {port_fit.K:.2f} (hdr {hdr_k:.2f}, Δ{dk_pct:+.3f}%) "
                  f"α₀ {port_fit.alpha0:+.4f} (hdr {hdr_a0:+.4f}, Δ{da0:.4f}) "
                  f"α_stall {port_fit.alpha_stall:+.4f} (hdr {hdr_as:+.4f}, Δ{das:.4f}) "
                  f"R² {port_fit.ias_to_aoa_r2:.4f} (hdr {ref.ias_to_aoa_r2:.4f})")
        if gate_fail:
            print("\nOLS port gate FAILED — aborting before log analysis.")
            return 2
        print("  all PASS")
    else:
        print(f"\nNo cal-dir ({args.cal_dir}) — skipping OLS port gate.")
        gate_results = None

    # --- Step 4: load flight log ---
    print(f"\nLoading log: {args.log}")
    log_df = pd.read_csv(args.log, low_memory=False)
    log_df.columns = [c.strip() for c in log_df.columns]
    print(f"  {len(log_df)} rows, {log_df['timeStamp'].iloc[-1] / 1000:.0f} s duration")

    # --- Step 5: detect stall windows ---
    flap_bins = sorted(cfg.flaps.keys())
    print(f"\nDetecting stall windows (cfg flaps {flap_bins})...")
    windows = detect_stall_windows(log_df, flap_bins)
    print(f"  {len(windows)} windows detected")
    flap_counts: dict[int, int] = {}
    for w in windows:
        flap_counts[w["flap_bin"]] = flap_counts.get(w["flap_bin"], 0) + 1
    for fb in sorted(flap_counts.keys()):
        print(f"    flap {fb}°: {flap_counts[fb]} runs")

    # --- Step 6: build Run objects ---
    runs = build_runs_from_windows(log_df, windows)
    run_to_start_idx = {r.run_id: windows[i]["start_idx"] for i, r in enumerate(runs)}

    # --- Step 7+8: per-run OLS + WLS fits ---
    print("\nPer-run fits (OLS + WLS)...")
    per_run_ols: dict[str, RunFit] = {}
    per_run_wls: dict[str, RunFit] = {}
    for r in runs:
        per_run_ols[r.run_id] = fit_run(r, "OLS")
        per_run_wls[r.run_id] = fit_run(r, "WLS")

    # --- Step 9: run quality ranking ---
    print("\nRun quality ranking...")
    quality = rank_runs(runs, per_run_ols)
    for q in sorted(quality.values(), key=lambda q: (q.flap, -q.score)):
        print(f"  {q.run_id} flap{q.flap:02d} {q.verdict:10s} score={q.score:.3f}")

    # --- Step 10: stacked fits ---
    print("\nStacked fits per flap...")
    runs_by_flap: dict[int, list[Run]] = {}
    for r in runs:
        runs_by_flap.setdefault(r.flap_bin, []).append(r)

    stacked_ols_all: dict[int, RunFit] = {}
    stacked_wls_all: dict[int, RunFit] = {}
    stacked_wls_keep: dict[int, RunFit | None] = {}

    for flap, rs in runs_by_flap.items():
        stacked_ols_all[flap] = fit_stacked(rs, "OLS")
        stacked_wls_all[flap] = fit_stacked(rs, "WLS")
        keep_runs = [r for r in rs if quality[r.run_id].verdict == "KEEP"]
        if len(keep_runs) < 2:
            borderline = [r for r in rs if quality[r.run_id].verdict == "BORDERLINE"]
            keep_runs = keep_runs + borderline
        if len(keep_runs) >= 2:
            stacked_wls_keep[flap] = fit_stacked(keep_runs, "WLS")
        else:
            stacked_wls_keep[flap] = None

    # --- Step 11: provenance ---
    provenance = {}
    for flap, fc in cfg.flaps.items():
        if flap in runs_by_flap:
            provenance[flap] = provenance_match(fc, runs_by_flap[flap], per_run_ols)
        else:
            provenance[flap] = "no runs sliced at this flap"

    # --- Step 12: best single per flap (OLS best R²), shared with WLS for same run ---
    summaries: dict[int, list[ConfigSummary]] = {}
    recommended: dict[int, ConfigSummary] = {}
    for flap, rs in runs_by_flap.items():
        if flap not in cfg.flaps:
            continue
        dev = build_device_summary(cfg.flaps[flap], cfg.aircraft)

        # Pick best single run by OLS IAS R²
        best_run = max(rs, key=lambda r: per_run_ols[r.run_id].ias_to_aoa_r2)
        ols_fit = per_run_ols[best_run.run_id]
        wls_fit = per_run_wls[best_run.run_id]
        ols_single = build_config_summary(
            "OLS single-best", ols_fit, flap, cfg.aircraft,
            n_runs=1, source_runs=f"{best_run.run_id}"
        )
        wls_single = build_config_summary(
            "WLS single-best", wls_fit, flap, cfg.aircraft,
            n_runs=1, source_runs=f"{best_run.run_id}"
        )

        ols_stack = build_config_summary(
            "OLS stacked-all", stacked_ols_all[flap], flap, cfg.aircraft,
            n_runs=len(rs), source_runs=f"{len(rs)} runs"
        )
        wls_stack = build_config_summary(
            "WLS stacked-all", stacked_wls_all[flap], flap, cfg.aircraft,
            n_runs=len(rs), source_runs=f"{len(rs)} runs"
        )

        keep_count = sum(1 for r in rs if quality[r.run_id].verdict == "KEEP")
        if stacked_wls_keep[flap] is not None:
            wls_keep = build_config_summary(
                "WLS stacked-KEEP", stacked_wls_keep[flap], flap, cfg.aircraft,
                n_runs=keep_count, source_runs=f"{keep_count} KEEP runs"
            )
        else:
            wls_keep = ConfigSummary(
                label="WLS stacked-KEEP", K=float("nan"), alpha0=float("nan"),
                alpha_stall=float("nan"), stall_ias=float("nan"),
                ias_r2=None, cp_r2=None, n_points=None,
                source="insufficient runs", setpoints=None, setpoints_ias=None,
            )

        summaries[flap] = [dev, ols_single, wls_single, ols_stack, wls_stack, wls_keep]

        # Pick "best alternative" for the recommendation — highest IAS R² that isn't cfg
        candidates = [s for s in summaries[flap][1:] if s.ias_r2 is not None and math.isfinite(s.K)]
        if candidates:
            recommended[flap] = max(candidates, key=lambda s: s.ias_r2)
        else:
            recommended[flap] = dev

    # --- Step 13: plots ---
    print("\nPlotting...")
    for flap, rs in runs_by_flap.items():
        if flap in summaries:
            plot_flap(flap, rs, summaries[flap], per_run_ols, per_run_wls, quality,
                      fig_dir / f"flap{flap:02d}_fits.png")
            plot_flap_cp_curve(flap, rs, summaries[flap], per_run_ols, quality,
                               fig_dir / f"flap{flap:02d}_cp_curve.png")

    # --- Step 14: bottom line ---
    bottom_line: list[str] = []
    wls_wins = 0
    ols_wins = 0
    stack_wins = 0
    for flap, cols in summaries.items():
        dev, ols_s, wls_s, ols_st, wls_st, wls_k = cols
        if ols_s.ias_r2 is not None and wls_s.ias_r2 is not None:
            if wls_s.ias_r2 > ols_s.ias_r2:
                wls_wins += 1
            elif wls_s.ias_r2 < ols_s.ias_r2:
                ols_wins += 1
        if wls_st.ias_r2 is not None and wls_s.ias_r2 is not None:
            if wls_st.ias_r2 > wls_s.ias_r2:
                stack_wins += 1

    bottom_line.append(
        f"**Weighting effect**: WLS single beats OLS single in {wls_wins} of "
        f"{len(summaries)} flaps (R² criterion). Vac's benchmark: R² usually bumps "
        f"by ~0.002 but a2 coefficients shift noticeably."
    )
    bottom_line.append(
        f"**Pooling effect**: stacked WLS beats single-best WLS in {stack_wins} of {len(summaries)} flaps."
    )
    # Gap analysis
    if len(flap_bins) >= 2:
        dev_gaps = []
        for i in range(len(flap_bins) - 1):
            lo, hi = flap_bins[i], flap_bins[i + 1]
            lo_sp = summaries.get(lo, [None])[0].setpoints_ias if lo in summaries else None
            hi_sp = summaries.get(hi, [None])[0].setpoints_ias if hi in summaries else None
            if lo_sp and hi_sp:
                gap = lo_sp.get("OS_slow", float("nan")) - hi_sp.get("OS_fast", float("nan"))
                dev_gaps.append((lo, hi, gap))
        for lo, hi, g in dev_gaps:
            if math.isfinite(g):
                if g > 0.5:
                    bottom_line.append(
                        f"**{lo}°→{hi}° transition gap** = {g:+.1f} kt (positive = dead zone) — your complaint confirmed."
                    )
                else:
                    bottom_line.append(
                        f"**{lo}°→{hi}° transition gap** = {g:+.1f} kt (≤0 = bands overlap)."
                    )

    # --- Step 15: report + stdout summary ---
    ctx = {
        "log_path": args.log, "cfg_path": args.cfg, "cal_dir": args.cal_dir,
        "aircraft": cfg.aircraft, "gate_results": gate_results,
        "windows": windows, "runs": runs, "run_to_start_idx": run_to_start_idx,
        "quality": quality, "flap_bins": sorted(summaries.keys()),
        "summaries": summaries, "provenance": provenance,
        "bottom_line": bottom_line, "recommended": recommended,
    }
    write_report(report_path, ctx)

    print(f"\n=== Done ===")
    print(f"Report:  {report_path}")
    print(f"Plots:   {fig_dir}")
    print()
    for flap in sorted(summaries.keys()):
        dev = summaries[flap][0]
        print(f"flap {flap:2d}°:")
        for s in summaries[flap]:
            if s.K is None or not math.isfinite(s.K):
                print(f"  {s.label:20s} — n/a")
                continue
            r2 = f"{s.ias_r2:.4f}" if s.ias_r2 is not None else "—"
            dk = f"{100.0 * (s.K - dev.K) / dev.K:+.2f}%" if dev.K else "—"
            print(f"  {s.label:20s} K={s.K:.1f} ({dk}) α₀={s.alpha0:+.3f} α_stall={s.alpha_stall:+.3f} R²={r2}")

    return 0


def _extract_kfit_from_csv_header(ref: CalCsvRef) -> float:
    """Saved CSV headers at master don't include KFIT as a discrete line; compute
    it from the cp-quad stall curve endpoints: K = (α_stall - α₀) * stall_ias².
    """
    if ref.stall_speed > 0 and ref.alpha_stall > ref.alpha0:
        return (ref.alpha_stall - ref.alpha0) * (ref.stall_speed ** 2)
    return 0.0


if __name__ == "__main__":
    sys.exit(main())
