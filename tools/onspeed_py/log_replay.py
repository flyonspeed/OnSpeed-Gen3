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

  - **lateralG sign convention** — at v4.23 the log, the wire, and
    `LiveSnapshot.lateral_g` all use body-frame (positive = airframe
    accelerating rightward).  No conversion needed; the
    `_log_to_wire_lateral_g` helper is kept as a pass-through for
    grep-ability so future convention changes stay auditable.
  - **EMA smoothing for accels** — Gen2 logs captured raw pre-filter
    accelerometer values; Gen3 firmware EMA-smooths accels at the IMU
    rate (208 Hz) with `kAccSmoothing = 0.060899` (`Ahrs.cpp`),
    corresponding to τ ≈ 0.0741 s. When `smooth_accels` is True
    (default) the adapter applies the equivalent variable-dt EMA at
    the log's actual sample rate so the replayed ball / G readout
    matches the firmware's bandwidth regardless of `iLogRate` (50 or
    208 Hz). Pitch/Roll columns in the log are post-AHRS-fusion
    (`g_AHRS.SmoothedPitch / SmoothedRoll`, see
    `tasks/LogSensor.cpp:541-548`) and pass through verbatim — adding
    a second EMA on top would be double-smoothing.
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

# Firmware-equivalent accelerometer EMA time constant.  Firmware uses
# α = 0.060899 at the IMU rate (208 Hz, dt = 1/208 s) in a fixed-α
# form `y[n] = (1-α)·y[n-1] + α·x[n]`.  Recovering τ from the
# variable-dt form `α = dt / (τ + dt)` gives
# τ = dt·(1/α − 1) = (1/208)·(1/0.060899 − 1) ≈ 0.0741 s.
# This matches the firmware's own variable-dt usage in Ahrs.cpp:
#   const float fIasTauSeconds = imuDeltaTime_ * kIasTauFactor;
#   const float fAlpha   = fIasDtSeconds / (fIasTauSeconds + fIasDtSeconds);
# (Ahrs.cpp:218-219, with kIasTauFactor = 1/kIasSmoothing − 1.)
KACC_TAU_S = 0.0741


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
    """Pass through.  At v4.23 the log, the wire, and `LiveSnapshot.lateral_g`
    all use body-frame (positive = airframe accelerating rightward), so no
    conversion is needed.  Kept as a function call for grep-ability — every
    log→snapshot lateral-G handoff goes through this name, which makes
    future convention changes auditable.
    """
    return body_frame_g


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
            for k in range(start, end + 1):
                # Drive smoothstep from a SIGNED offset around the snap
                # tick so u=0.5 lands at k=i regardless of whether the
                # window is clamped at either edge of the log. Without
                # this, a snap tick within `half` ticks of the start /
                # end would sit off-center (e.g. smoothstep(0.333) ≈
                # 0.26 instead of 0.5 at the snap).
                u = 0.5 + 0.5 * (k - i) / half
                if u < 0.0:
                    u = 0.0
                elif u > 1.0:
                    u = 1.0
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

    `smooth_accels` (default True) applies the firmware's accelerometer
    EMA (τ ≈ 0.0741 s) to lateral G and vertical G, with α computed
    per-row from the actual log dt so a 50 Hz log and a 208 Hz log of
    the same physical event produce matching post-filter bandwidth.
    Pitch and Roll come through unchanged — log columns are already
    AHRS-fused (`g_AHRS.SmoothedPitch / SmoothedRoll`).

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

    # Variable-dt EMA state for accels. Pitch/roll are AHRS-fused in
    # the log already; do not re-smooth.
    ema = {"latG": None, "vertG": None, "fwdG": None}

    def step_ema(key: str, value: float, dt: float) -> float:
        prev = ema[key]
        if prev is None or dt <= 0.0:
            ema[key] = value
            return value
        # Variable-dt EMA matched to firmware's own form:
        #   alpha = dt / (tau + dt)
        #   y[n]  = (1 - alpha) * y[n-1] + alpha * x[n]
        # At dt = 1/208 s, alpha collapses to the firmware's
        # kAccSmoothing = 0.060899.  At the slower 50 Hz log rate,
        # alpha ≈ 0.213 — the same effective bandwidth.
        alpha = dt / (KACC_TAU_S + dt)
        nxt = (1.0 - alpha) * prev + alpha * value
        ema[key] = nxt
        return nxt

    states: list[LiveSnapshot] = []

    with Path(log_path).open() as f:
        reader = csv.DictReader(f)
        if reader.fieldnames:
            reader.fieldnames = [name.strip() for name in reader.fieldnames]

        last_emit_t = None
        prev_ts = None

        for row in reader:
            ts_ms = _ffloat(row, _TIMESTAMP_NAMES)
            ts = ts_ms / 1000.0

            # Per-row dt from log timestamps. First row seeds the EMA
            # via the prev-is-None branch so dt isn't consulted there.
            if prev_ts is None:
                dt = 0.0
            else:
                dt = max(0.0, ts - prev_ts)
            prev_ts = ts

            if ts < t_start_s:
                # Still feed the smoothing EMA so it's converged by
                # the time we reach t_start_s.
                if smooth_accels:
                    step_ema("latG",  _log_to_wire_lateral_g(_ffloat(row, _LAT_G_NAMES)), dt)
                    step_ema("vertG", _ffloat(row, _VERT_G_NAMES, 1.0), dt)
                continue
            if ts > t_end_s:
                break

            # Always feed EMA at the actual log rate, even on rows we
            # don't emit. Convert log's body-frame lateralG to ball
            # frame immediately so the EMA state is consistent with
            # downstream consumers.
            raw_lat   = _log_to_wire_lateral_g(_ffloat(row, _LAT_G_NAMES))
            raw_vert  = _ffloat(row, _VERT_G_NAMES, default=1.0)
            pitch     = _ffloat(row, _PITCH_NAMES)
            roll      = _ffloat(row, _ROLL_NAMES)
            if smooth_accels:
                lat_g  = step_ema("latG",  raw_lat,  dt)
                vert_g = step_ema("vertG", raw_vert, dt)
            else:
                lat_g, vert_g = raw_lat, raw_vert

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
