"""Replay a window of an OnSpeed SD-log CSV as a scenario.

Reads a real flight log (synthetic or recorded) and yields per-tick
LiveSnapshot values that the orchestrator then runs through the same
audio + display pipeline as a synthetic scenario.

Supports two log formats:

  - **Old format** (pre-PR #353, Vac's late-2025 calibration logs):
    `AngleofAttack`, no `DerivedAOA`, no per-flap percent-lift columns.
  - **New format** (post-PR #353): `DerivedAOA`, optional band-edge
    percents, name-keyed parsing.

Either way the adapter normalizes onto the LiveSnapshot schema.

Usage from a custom scenario module:

    from scenarios.from_log import scenario_from_log
    def scenario():
        yield from scenario_from_log(
            log_path=Path("vac_log.csv"),
            cfg_path=Path("vac_config.cfg"),
            t_start_s=2865.0,
            t_end_s=2885.0,
        )
"""

from __future__ import annotations
import sys
import csv
import pathlib
from pathlib import Path
from typing import Iterator

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from live_snapshot import LiveSnapshot
import _envelopes as env
import wire_frame_builder as wfb


# Column-name aliases.  First name in each tuple is what we standardize
# on internally; later names are accepted as alternates.
_AOA_NAMES         = ("DerivedAOA", "AngleofAttack")
_IAS_NAMES         = ("IAS", "ias")
_PALT_NAMES        = ("Palt", "palt", "Altitude")
_PITCH_NAMES       = ("Pitch", "pitch")
_ROLL_NAMES        = ("Roll", "roll")
_YAW_RATE_NAMES    = ("YawRate", "yawRate", "yaw_rate")
_VERT_G_NAMES      = ("VerticalG", "verticalG", "vertical_g")
_LAT_G_NAMES       = ("LateralG", "lateralG", "lateral_g")
_OAT_NAMES         = ("OAT", "oat")
_VSI_NAMES         = ("VSI", "vsi")
_FLAP_DEG_NAMES    = ("flapsPos", "flap_deg", "FlapsPos")
_FLAP_RAW_NAMES    = ("flapsRawADC", "flapsRaw", "flapPotRaw")
_FLIGHT_PATH_NAMES = ("FlightPath", "flight_path")
_TIMESTAMP_NAMES   = ("timeStamp", "Timestamp", "time_ms")


def _first_present(row: dict[str, str], names: tuple[str, ...]):
    for n in names:
        if n in row and row[n] != "":
            return row[n]
        # Logs sometimes have leading spaces in column headers.
        if " " + n in row and row[" " + n] != "":
            return row[" " + n]
    return None


def _ffloat(row: dict[str, str], names: tuple[str, ...], default: float = 0.0) -> float:
    v = _first_present(row, names)
    if v is None:
        return default
    try:
        return float(v)
    except ValueError:
        return default


def _fint(row: dict[str, str], names: tuple[str, ...], default: int = 0) -> int:
    v = _first_present(row, names)
    if v is None:
        return default
    try:
        return int(float(v))
    except ValueError:
        return default


def _resolve_lever_raw(flap_deg: int,
                       cfg: dict[int, wfb.FlapSetpoints]) -> int:
    """Synthesize a lever-ADC reading from a discrete flap-degrees value.

    Old logs don't capture raw flap-pot ADC.  The display anchors path
    needs a raw-ADC value to compute pip slide; we use the configured
    detent's pot_value for the matching flap, snapping to the nearest
    detent if flap_deg doesn't exactly match.
    """
    fs = wfb.setpoints_for_flap(flap_deg, cfg)
    return fs.pot_value


def scenario_from_log(log_path: Path,
                      cfg_path: Path,
                      t_start_s: float,
                      t_end_s: float,
                      *,
                      target_rate_hz: float = 50.0,
                      smooth_accels: bool = True) -> Iterator[LiveSnapshot]:
    """Yield LiveSnapshots from a window of an OnSpeed CSV log.

    `t_start_s` / `t_end_s` are seconds since log epoch — i.e., the
    `timeStamp` column divided by 1000.  Use peeking tools (e.g. awk)
    to find interesting moments first.

    Resamples to `target_rate_hz` (default 50 Hz, matching the audio
    pipeline).

    `smooth_accels` (default True) applies the same EMA smoothing the
    Gen3 firmware applies to lateral, vertical, and forward accels —
    α=0.0609 (`kAccSmoothing` in AHRS.cpp), τ≈0.32s.  Required when
    replaying Gen2 logs, which captured RAW IMU values rather than the
    post-filter values Gen3 logs carry.  Without this the ball / G
    readout / vertical-G jitter unrealistically.

    The Gen3 firmware does NOT EMA-smooth gyro rates (yaw/pitch/roll)
    in the way it smooths accels — gyros go through Madgwick or EKF6
    and the wire `turnRateDps` is the raw current value.  We mirror
    that here: gyros pass through unsmoothed.
    """
    cfg = wfb.load_flap_setpoints(cfg_path)
    target_dt_s = 1.0 / target_rate_hz

    # Gen3 firmware EMA constant for accels (AHRS.cpp:22 kAccSmoothing).
    KACC = 0.0609

    # Persistent EMA state across ticks.
    ema = {"latG": None, "vertG": None, "fwdG": None,
           "pitch": None, "roll": None}

    def step_ema(key: str, value: float, alpha: float) -> float:
        prev = ema[key]
        if prev is None:
            ema[key] = value
            return value
        nxt = (1.0 - alpha) * prev + alpha * value
        ema[key] = nxt
        return nxt

    with log_path.open() as f:
        reader = csv.DictReader(f)
        if reader.fieldnames:
            reader.fieldnames = [name.strip() for name in reader.fieldnames]

        last_emit_t = None

        for row in reader:
            ts_ms = _ffloat(row, _TIMESTAMP_NAMES)
            ts = ts_ms / 1000.0

            if ts < t_start_s:
                # Still feed the smoothing EMA — we want it converged
                # by the time we reach t_start_s rather than starting
                # from the first emitted tick.
                if smooth_accels:
                    step_ema("latG",  _ffloat(row, _LAT_G_NAMES),    KACC)
                    step_ema("vertG", _ffloat(row, _VERT_G_NAMES, 1.0), KACC)
                    step_ema("pitch", _ffloat(row, _PITCH_NAMES),    KACC)
                    step_ema("roll",  _ffloat(row, _ROLL_NAMES),     KACC)
                continue
            if ts > t_end_s:
                break

            # Always feed EMA so it tracks the actual log rate, even on
            # rows we don't emit.
            raw_lat   = _ffloat(row, _LAT_G_NAMES)
            raw_vert  = _ffloat(row, _VERT_G_NAMES, default=1.0)
            raw_pitch = _ffloat(row, _PITCH_NAMES)
            raw_roll  = _ffloat(row, _ROLL_NAMES)
            if smooth_accels:
                lat_g    = step_ema("latG",  raw_lat,   KACC)
                vert_g   = step_ema("vertG", raw_vert,  KACC)
                pitch    = step_ema("pitch", raw_pitch, KACC)
                roll     = step_ema("roll",  raw_roll,  KACC)
            else:
                lat_g, vert_g, pitch, roll = raw_lat, raw_vert, raw_pitch, raw_roll

            # Decimate to target rate.
            if last_emit_t is not None and (ts - last_emit_t) < target_dt_s - 1e-4:
                continue
            last_emit_t = ts

            flap_deg = _fint(row, _FLAP_DEG_NAMES, default=0)
            lever_raw_v = _first_present(row, _FLAP_RAW_NAMES)
            if lever_raw_v is not None and lever_raw_v != "":
                lever_raw = int(float(lever_raw_v))
            else:
                lever_raw = _resolve_lever_raw(flap_deg, cfg)

            yield LiveSnapshot(
                t=ts - t_start_s,
                aoa=_ffloat(row, _AOA_NAMES),
                ias=_ffloat(row, _IAS_NAMES),
                pitch=pitch,
                roll=roll,
                yaw_rate=_ffloat(row, _YAW_RATE_NAMES),
                vertical_g=vert_g,
                lateral_g=lat_g,
                lever_raw=lever_raw,
                flap_deg=flap_deg,
                palt=_ffloat(row, _PALT_NAMES),
                vsi=_ffloat(row, _VSI_NAMES),
                oat=_fint(row, _OAT_NAMES, default=15),
                flight_path=_ffloat(row, _FLIGHT_PATH_NAMES),
            )
