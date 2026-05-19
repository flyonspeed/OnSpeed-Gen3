"""Load a cleaned OnSpeed flight log (208 Hz) and align truth signals."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Sequence

import numpy as np
import pandas as pd


# Columns we actually need from the 62-column log file. Loading a subset cuts
# RAM by ~7x and CSV parse time by ~3x.
_REQUIRED_COLS: tuple[str, ...] = (
    "timeStamp",
    # Primary IMU (208 Hz, firmware convention: +1g level, gyro signs flipped
    # on roll/pitch vs aerospace standard)
    "VerticalG", "LateralG", "ForwardG",
    "RollRate", "PitchRate", "YawRate",
    "Pitch", "Roll",                       # firmware-published attitude (Madgwick or EKF6)
    "VSI", "Altitude", "EarthVerticalG", "FlightPath", "DerivedAOA",
    # Pressure / air data (50 Hz upstream; logged at 208 Hz held-stale)
    "PStatic", "Palt", "IAS", "TAS", "AngleofAttack", "flapsPos",
    "Pfwd", "PfwdSmoothed", "P45", "P45Smoothed", "OAT",
    # VN-300 truth (50 Hz attitude/accel/rates, 5 Hz VelNed — held-stale)
    "vnPitch", "vnRoll", "vnYaw",
    "vnVelNedNorth", "vnVelNedEast", "vnVelNedDown",
    "vnLinAccFwd", "vnLinAccLat", "vnLinAccVert",
    "vnAccelFwd", "vnAccelLat", "vnAccelVert",
    "vnAngularRateRoll", "vnAngularRatePitch", "vnAngularRateYaw",
    "vnDataAge",
    "vnPitchSigma", "vnRollSigma", "vnYawSigma",
    "vnGPSFix",
    # Boom (independent alpha vane, 20 Hz)
    "boomAlpha", "boomBeta", "boomIAS", "boomAge",
)

# Knots → m/s
_KT_TO_MPS = 0.514444
# ft → m
_FT_TO_M = 0.3048


@dataclass
class FlightLog:
    """In-memory view of a cleaned flight log.

    All numeric columns are float32 to keep the full 988k-row log inside
    ~250 MB of RAM. Time is float64 (we need millisecond precision over
    79 minutes — float32 loses sub-ms resolution at that magnitude).
    """

    df: pd.DataFrame
    fresh_vn: np.ndarray         # bool[N]: True on rows carrying a *new* VN-300 frame
    fresh_aoa: np.ndarray        # bool[N]: True on rows carrying a *new* pressure-AOA sample
    fresh_baro: np.ndarray       # bool[N]: True on rows carrying a *new* baro sample
    fresh_boom: np.ndarray       # bool[N]: True on rows carrying a *new* boom frame
    dt: np.ndarray               # float32[N]: per-step dt in seconds (varies by ~1 us due to ms quantization)

    # --- convenience accessors (zero-copy views) ---
    @property
    def t(self) -> np.ndarray:
        """Time in seconds, monotonic, starts near zero (offset removed)."""
        return self._t

    @property
    def n(self) -> int:
        return len(self.df)

    def _post_init(self) -> None:
        ts = self.df["timeStamp"].to_numpy(dtype=np.float64)
        self._t = (ts - ts[0]) / 1000.0  # ms → s

    # --- truth in EKF-native units (m/s, m, rad) ---
    def truth_vz_mps(self) -> np.ndarray:
        """vn velocity Down in m/s (+down). VN logs this in m/s already."""
        return self.df["vnVelNedDown"].to_numpy(dtype=np.float32)

    def truth_pitch_rad(self) -> np.ndarray:
        return np.deg2rad(self.df["vnPitch"].to_numpy(dtype=np.float32))

    def truth_roll_rad(self) -> np.ndarray:
        return np.deg2rad(self.df["vnRoll"].to_numpy(dtype=np.float32))

    def baro_z_m(self) -> np.ndarray:
        """Pressure altitude in meters (+up). The CSV stores Palt in feet."""
        return self.df["Palt"].to_numpy(dtype=np.float32) * _FT_TO_M

    def alpha_pressure_rad(self) -> np.ndarray:
        """Calibrated pressure-derived AOA in radians."""
        return np.deg2rad(self.df["AngleofAttack"].to_numpy(dtype=np.float32))


def _detect_fresh(age_col: np.ndarray) -> np.ndarray:
    """Boolean mask: True on rows where the *Age column reset toward zero.

    The firmware writes the per-source age (ms since last fresh frame) into
    these columns every 208 Hz log row. A fresh frame is exactly the row
    immediately following a frame where age was larger — i.e. diff < 0. The
    very first row is treated as fresh too (we don't have prior data to
    compare against).
    """
    fresh = np.zeros_like(age_col, dtype=bool)
    if age_col.size == 0:
        return fresh
    fresh[0] = True
    fresh[1:] = np.diff(age_col) < 0
    return fresh


def load_log(path: str, *, usecols: Sequence[str] | None = None) -> FlightLog:
    """Load a cleaned 208 Hz flight log into a FlightLog object."""
    cols = list(usecols) if usecols is not None else list(_REQUIRED_COLS)
    # Read with float32 for everything except timeStamp (need full precision)
    dtype = {c: np.float32 for c in cols if c != "timeStamp"}
    dtype["timeStamp"] = np.float64
    # vnTimeUTC is the only string column in the file; we skip it from the
    # required list, so dtype map is fine.
    df = pd.read_csv(path, usecols=cols, dtype=dtype, low_memory=False)

    # Compute dt from successive timestamps (ms → s). Use float32 for the
    # vector but compute in float64 to avoid quantization noise.
    ts = df["timeStamp"].to_numpy(dtype=np.float64)
    dt = np.empty(len(df), dtype=np.float32)
    if len(df) > 1:
        dt[1:] = ((ts[1:] - ts[:-1]) / 1000.0).astype(np.float32)
        dt[0] = dt[1]  # seed first frame with the first delta
    else:
        dt[:] = 1.0 / 208.0

    fresh_vn = _detect_fresh(df["vnDataAge"].to_numpy())
    fresh_baro = np.ones(len(df), dtype=bool)  # baro is logged at 208 Hz directly
    # Pressure-AOA & IAS are produced by SensorReadTask at 50 Hz. The log has
    # no dedicated "age" column for them, but Pfwd is the upstream signal — a
    # change in PfwdSmoothed is a strong indicator of a fresh 50 Hz frame.
    pfwd_smoothed = df["PfwdSmoothed"].to_numpy()
    fresh_aoa = np.zeros(len(df), dtype=bool)
    fresh_aoa[0] = True
    fresh_aoa[1:] = pfwd_smoothed[1:] != pfwd_smoothed[:-1]
    # Boom transmits at 20 Hz; boomAge counts ms since last frame.
    fresh_boom = _detect_fresh(df["boomAge"].to_numpy())

    out = FlightLog(df=df, fresh_vn=fresh_vn, fresh_aoa=fresh_aoa,
                    fresh_baro=fresh_baro, fresh_boom=fresh_boom, dt=dt)
    out._post_init()
    return out
