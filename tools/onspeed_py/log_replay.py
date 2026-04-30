"""OnSpeed SD-log → `LiveSnapshot` tick stream adapter.

Reads an OnSpeed CSV log and emits per-tick `LiveSnapshot` values that
downstream code (the synth-record orchestrator, future analysis pages)
runs through the same display + audio pipeline as a synthetic scenario.

Supports two log formats:

  - **Old format** (pre-PR #353, e.g. late-2025 calibration logs):
    `AngleofAttack`, no `DerivedAOA`, no per-flap percent-lift columns.
  - **New format** (post-PR #353): `DerivedAOA`, optional band-edge
    percents, name-keyed parsing.

Either way the adapter normalizes onto the `LiveSnapshot` schema.

Workarounds carried here:

  - **Body→ball lateralG sign negation** — the SD log's `LateralG`
    column is body-frame (positive = airframe accelerating right);
    `LiveSnapshot.lateral_g` is ball-frame (positive = airframe
    accelerating left). `_log_to_wire_lateral_g` negates the value so
    the M5 ball renders on the correct side.
  - **EMA smoothing for accels and attitude** — Gen2 logs captured raw
    pre-filter values; Gen3 firmware applies an EMA with α = 0.0609
    (`kAccSmoothing` in `AHRS.cpp`, τ ≈ 0.32 s). When `smooth_accels`
    is True (default) the adapter applies the same EMA so the replayed
    ball / G readout doesn't jitter.
  - **Synthetic lever sweep** — old logs only carry the integer
    detected detent (`flapsPos`), not the raw flap-pot ADC. Without
    a fake sweep the L/Dmax pip would jump at every detent change.
    The sweep is centered on the snap tick because the firmware's
    detector flips at the midpoint between detents — see issue #372.
"""

from __future__ import annotations

import csv
from pathlib import Path
from typing import Iterator

from .config import FlapSetpoints, load_flap_setpoints, setpoints_for_flap
from .live_snapshot import LiveSnapshot


# Column-name aliases. First name in each tuple is what we standardize
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


def _log_to_wire_lateral_g(body_frame_g: float) -> float:
    """Convert log's body-frame lateralG (positive = right) to the
    wire's ball-frame convention (positive = leftward), which is what
    `LiveSnapshot.lateral_g` and the wire `lateralG` field both expect.
    """
    return -body_frame_g


def _first_present(row: dict[str, str], names: tuple[str, ...]):
    for n in names:
        if n in row and row[n] != "":
            return row[n]
        # Logs sometimes have leading spaces in column headers.
        if " " + n in row and row[" " + n] != "":
            return row[" " + n]
    return None


def _ffloat(row: dict[str, str], names: tuple[str, ...],
            default: float = 0.0) -> float:
    v = _first_present(row, names)
    if v is None:
        return default
    try:
        return float(v)
    except ValueError:
        return default


def _fint(row: dict[str, str], names: tuple[str, ...],
          default: int = 0) -> int:
    v = _first_present(row, names)
    if v is None:
        return default
    try:
        return int(float(v))
    except ValueError:
        return default


def _resolve_lever_raw(flap_deg: int,
                       cfg: dict[int, FlapSetpoints]) -> int:
    """Synthesize a lever-ADC reading from a discrete flap-degrees value.

    Old logs don't capture raw flap-pot ADC. The display-anchors path
    needs a raw-ADC value to compute pip slide; we use the configured
    detent's pot_value for the matching flap, snapping to the nearest
    detent if `flap_deg` doesn't exactly match.
    """
    fs = setpoints_for_flap(flap_deg, cfg)
    return fs.pot_value


def _fake_lever_sweep(states: list[LiveSnapshot],
                      cfg: dict[int, FlapSetpoints],
                      sweep_window_s: float = 4.0) -> list[LiveSnapshot]:
    """Replace snapped `lever_raw` with a smooth sweep across detent
    transitions, **centered** on the detection-flip tick.

    Workaround for issue #372: SD logs only carry the integer detected
    detent (`flapsPos`), not the raw flap-pot ADC. Without this fake
    sweep, the L/Dmax pip jumps at every flap-deg change in the log
    instead of sliding through the physical lever travel.

    Why centered, not just-before: the firmware's `FlapsDetector` flips
    the detected detent when the lever crosses the **midpoint** between
    adjacent detents' pot positions. By the time the log records
    "flap=16" for the first time, the lever is already at the midpoint
    between 0 and 16 and still moving. Centering the sweep on the snap
    tick puts the lever AT the midpoint at the moment the log first
    reads the new detent — matching the physics.
    """
    if len(states) < 2:
        return states

    sweep_ticks = max(2, int(round(sweep_window_s * 50)))   # 50 Hz
    half = sweep_ticks // 2
    last_flap = states[0].flap_deg

    for i, s in enumerate(states):
        if s.flap_deg != last_flap and i > 0:
            prev_flap = last_flap
            new_flap = s.flap_deg
            prev_fs = setpoints_for_flap(prev_flap, cfg)
            new_fs  = setpoints_for_flap(new_flap,  cfg)

            start = max(0, i - half)
            end   = min(len(states) - 1, i + half)
            n     = end - start
            for j, k in enumerate(range(start, end + 1)):
                u = j / max(1, n)
                # Smoothstep for a cleaner visual ease.
                u_smooth = u * u * (3.0 - 2.0 * u)
                states[k].lever_raw = int(round(
                    (1.0 - u_smooth) * prev_fs.pot_value
                    + u_smooth        * new_fs.pot_value
                ))
        last_flap = s.flap_deg

    return states


def scenario_from_log(log_path: Path,
                      cfg_path: Path,
                      t_start_s: float,
                      t_end_s: float,
                      *,
                      target_rate_hz: float = 50.0,
                      smooth_accels: bool = True,
                      fake_lever_sweep: bool = True,
                      flap_overrides: dict | None = None
                      ) -> Iterator[LiveSnapshot]:
    """Yield `LiveSnapshot` ticks from a window of an OnSpeed CSV log.

    `t_start_s` / `t_end_s` are seconds since the log epoch — i.e.,
    `timeStamp` / 1000. Resamples to `target_rate_hz` (default 50 Hz).

    `smooth_accels` (default True) applies the firmware's α = 0.0609
    EMA to lateral G, vertical G, pitch, roll. Required when replaying
    Gen2 logs, which captured raw IMU values.

    `flap_overrides`: optional `{flap_deg: {alpha_0, alpha_stall, k_fit}}`
    map for substituting fit-derived values when the on-disk config is
    a V1 (which doesn't carry those fields).
    """
    cfg = load_flap_setpoints(cfg_path)

    if flap_overrides:
        for flap_deg, overrides in flap_overrides.items():
            if flap_deg in cfg:
                fs = cfg[flap_deg]
                for k, v in overrides.items():
                    setattr(fs, k, v)

    target_dt_s = 1.0 / target_rate_hz

    # Gen3 firmware EMA constant for accels (AHRS.cpp::kAccSmoothing).
    KACC = 0.0609

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

    states: list[LiveSnapshot] = []

    with Path(log_path).open() as f:
        reader = csv.DictReader(f)
        if reader.fieldnames:
            reader.fieldnames = [name.strip() for name in reader.fieldnames]

        last_emit_t = None

        for row in reader:
            ts_ms = _ffloat(row, _TIMESTAMP_NAMES)
            ts = ts_ms / 1000.0

            if ts < t_start_s:
                # Still feed the smoothing EMA so it's converged by
                # the time we reach t_start_s.
                if smooth_accels:
                    step_ema("latG",  _log_to_wire_lateral_g(_ffloat(row, _LAT_G_NAMES)), KACC)
                    step_ema("vertG", _ffloat(row, _VERT_G_NAMES, 1.0), KACC)
                    step_ema("pitch", _ffloat(row, _PITCH_NAMES),    KACC)
                    step_ema("roll",  _ffloat(row, _ROLL_NAMES),     KACC)
                continue
            if ts > t_end_s:
                break

            # Always feed EMA at the actual log rate, even on rows we
            # don't emit. Convert log's body-frame lateralG to ball
            # frame immediately so the EMA state is consistent with
            # downstream consumers.
            raw_lat   = _log_to_wire_lateral_g(_ffloat(row, _LAT_G_NAMES))
            raw_vert  = _ffloat(row, _VERT_G_NAMES, default=1.0)
            raw_pitch = _ffloat(row, _PITCH_NAMES)
            raw_roll  = _ffloat(row, _ROLL_NAMES)
            if smooth_accels:
                lat_g  = step_ema("latG",  raw_lat,   KACC)
                vert_g = step_ema("vertG", raw_vert,  KACC)
                pitch  = step_ema("pitch", raw_pitch, KACC)
                roll   = step_ema("roll",  raw_roll,  KACC)
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

            states.append(LiveSnapshot(
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
            ))

    if fake_lever_sweep:
        # Issue #372: synthesize a smooth lever sweep across detent
        # transitions since logs only carry integer flapsPos. Drop
        # this once flapsRawADC is captured in the log.
        states = _fake_lever_sweep(states, cfg)

    yield from states
