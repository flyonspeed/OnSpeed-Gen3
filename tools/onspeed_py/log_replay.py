"""OnSpeed SD-log → `LiveSnapshot` tick stream adapter.

Algorithm code lives in C++. This module is a thin subprocess wrapper
around the `host_main replay` CLI subcommand. The canonical replay
pipeline implementation is in
software/Libraries/onspeed_core/src/replay/LogReplayEngine.{h,cpp},
compiled to tools/regression/.pio/build/native/program by
`cd tools/regression && pio run -e native`.

Modifying this file does NOT change algorithm behavior. To change
replay-pipeline behavior (EMA smoothing, synth lever sweep, AOA
polynomial evaluation), edit the C++ in onspeed_core, rebuild
host_main, then re-run the wrapper tests here to verify.

**Supported log formats**: any SD log accepted by BuildHeaderIndex in
onspeed_core (old format with `AngleofAttack`, new format with
`DerivedAOA`, optional `flapsRawADC` column, etc.). The C++ engine
handles format detection.

**Windowing and decimation**: `host_main replay` processes the full
log. Python reads the `timeStamp` column from the original CSV to
apply the `t_start_s` / `t_end_s` window and `target_rate_hz`
decimation on the output. This matches the semantics of the old
Python-only implementation.

**EMA smoothing**: the C++ engine always applies rate-adjusted
accelerometer EMA (τ ≈ 0.0741 s). The `smooth_accels` parameter
controls which field is mapped to `LiveSnapshot.lateral_g` /
`LiveSnapshot.vertical_g`:
  - True (default): use `accel_lat_smoothed` / `accel_vert_smoothed`
    (C++ rate-adjusted EMA output).
  - False: use `imu_lat_g` / `imu_vert_g` (raw IMU passthrough).

**Synthetic lever sweep**: the C++ engine always synthesizes a smooth
lever-ADC sweep across detent transitions for old logs that lack
`flapsRawADC`. The `fake_lever_sweep` parameter is accepted for API
compatibility but is a no-op — the C++ always sweeps.

**`flap_overrides`**: not supported by host_main. Supplying
`flap_overrides` raises NotImplementedError. This parameter is
preserved in the signature for future integration; callers that need
it should write a modified config to a tempfile and pass it as
`cfg_path`.

**IAS validity / NaN gate**: when the C++ engine outputs
`ias_valid=false` (empty IAS cell in a format-3 log), the Python
wrapper maps `LiveSnapshot.ias` to NaN and `LiveSnapshot.aoa` to
NaN — matching the old Python behavior where empty cells propagated
as NaN. When `ias_valid=true`, `ias` and `aoa` are taken from
`ias_kt` and `aoa_deg` respectively.

**`nan` in JSONL**: the C++ emits literal `nan` (not `null`) for
non-finite floats such as `ias_kt` when IAS is invalid. Standard
JSON does not allow bare `nan`. The wrapper preprocesses each line
before calling `json.loads`. Long-term fix: emit `null` in C++ and
map `null` to `float('nan')` here. Tracked in Issue #499.
"""

from __future__ import annotations

import csv
import json
import math
import re
import subprocess
import warnings
from pathlib import Path
from typing import Iterator

from ._host_main import require_host_main
from .live_snapshot import LiveSnapshot

# Column-name aliases. First name in each tuple is what we standardize
# on internally; later names are accepted as alternates. These are
# still used for timestamp extraction from the original CSV (which
# host_main doesn't emit in its output).
_TIMESTAMP_NAMES = ("timeStamp", "Timestamp", "time_ms")
_FLAP_DEG_NAMES  = ("flapsPos", "flap_deg", "FlapsPos")


# _ffloat / _first_present / _fint — kept for backward compatibility
# with test_format3_nan.py which imports _ffloat to unit-test the
# gated-column CSV-parsing convention. These helpers are also used
# internally for the lightweight CSV scan that extracts timestamps and
# flap positions for windowing and decimation.

def _first_present(row: dict[str, str], names: tuple[str, ...]):
    for n in names:
        if n in row and row[n] != "":
            return row[n]
        # Logs sometimes have leading spaces in column headers.
        if " " + n in row and row[" " + n] != "":
            return row[" " + n]
    return None


def _ffloat(row: dict[str, str], names: tuple[str, ...],
            default: float = 0.0,
            allow_empty: bool = False) -> float:
    """Read a CSV float, optionally letting empty cells through as NaN.

    `allow_empty=False` (the default) keeps the strict semantics:
    missing or unparseable cells return `default`. The gated air-data
    columns (`IAS`, `AngleofAttack` / `DerivedAOA`) pass
    `allow_empty=True` so the format-3 "no valid air data" convention
    propagates as NaN.
    """
    v = _first_present(row, names)
    if v is None:
        if allow_empty and any(
            n in row or " " + n in row for n in names
        ):
            return math.nan
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


def _log_to_wire_lateral_g(body_frame_g: float) -> float:
    """Pass through. At v4.23 the log, the wire, and
    `LiveSnapshot.lateral_g` all use body-frame (positive = airframe
    accelerating rightward), so no conversion is needed. Kept as a
    function call for grep-ability — every log→snapshot lateral-G
    handoff goes through this name, which makes future convention
    changes auditable.
    """
    return body_frame_g


# ---------------------------------------------------------------------------
# JSONL parsing with nan-tolerance
# ---------------------------------------------------------------------------

# Match bare nan, -nan, NaN (any case), inf, -inf, INFINITY, etc.
# libc printf() output varies by platform: Linux emits lowercase "nan",
# macOS can emit "-nan" or mixed-case "NaN"; isinf() values may appear
# as "inf" or "infinity".
# See Issue #499 — once host_main emits null directly this regex can
# simplify or be deleted.
_NAN_INF_RE = re.compile(r':\s*-?(?i:nan|inf(?:inity)?)\b')


def _parse_jsonl_with_nan(line: str) -> dict:
    """Parse a JSONL row, tolerating C++-style non-finite float output.

    The C++ host_main emits literal `nan` (or platform variants: `-nan`,
    `NaN`, `inf`, `-inf`) for non-finite floats — e.g. `ias_kt` when IAS
    is invalid. None of these are valid JSON tokens.  Pre-process: replace
    any such token with `null`, then map null → NaN after parsing.

    Long-term fix: have host_main.cpp emit `null` instead of these tokens
    for non-finite floats. Tracked in Issue #499. This wrapper
    pre-processing keeps the Python side self-contained until that lands.
    """
    # Replace non-finite float literals with `null` so json.loads accepts
    # the line.  Replacement is ': null' (with a space) to preserve the
    # JSON value slot after the colon.
    cleaned = _NAN_INF_RE.sub(': null', line)
    obj = json.loads(cleaned)
    # Map null (None in Python) back to NaN for any field that could be
    # legitimately non-finite (currently only ias_kt).
    return {k: (float('nan') if v is None else v) for k, v in obj.items()}


# ---------------------------------------------------------------------------
# JSONL row → LiveSnapshot
# ---------------------------------------------------------------------------

def _to_live_snapshot(raw: dict, *,
                      t: float,
                      smooth_accels: bool) -> LiveSnapshot:
    """Map a host_main JSONL row to a LiveSnapshot.

    Field map (host_main JSONL key → LiveSnapshot field):
      ias_kt                → ias      (NaN when ias_valid=false)
      aoa_deg               → aoa      (NaN when ias_valid=false)
      pitch_deg             → pitch
      roll_deg              → roll
      imu_yaw_dps           → yaw_rate
      imu_vert_g            → vertical_g  (raw, when smooth_accels=False)
      accel_vert_smoothed   → vertical_g  (EMA, when smooth_accels=True)
      imu_lat_g             → lateral_g   (raw, when smooth_accels=False)
      accel_lat_smoothed    → lateral_g   (EMA, when smooth_accels=True)
      flaps_raw_adc         → lever_raw
      flaps_pos             → flap_deg
      palt_ft               → palt
      kalman_vsi_mps × 196.85 → vsi      (m/s → fpm)
      data_mark             → data_mark
      flight_path_deg       → flight_path

    Notes:
      - `ias_valid=false` triggers NaN propagation to both ias and aoa,
        matching the old Python behavior for format-3 empty-cell gates.
      - `kalman_vsi_mps` is converted to fpm (1 m/s = 196.85 fpm).
      - `imu_fwd_g` is not currently mapped to LiveSnapshot (no field).
    """
    ias_valid = bool(raw.get('ias_valid', True))
    ias_kt    = float(raw.get('ias_kt', 0.0))
    aoa_deg   = float(raw.get('aoa_deg', 0.0))

    # Propagate NaN when IAS is invalid — matches the format-3 empty-
    # cell gate from the old Python implementation.
    if not ias_valid or math.isnan(ias_kt):
        ias = float('nan')
        aoa = float('nan')
    else:
        ias = ias_kt
        aoa = aoa_deg

    if smooth_accels:
        lat_g  = float(raw.get('accel_lat_smoothed', 0.0))
        vert_g = float(raw.get('accel_vert_smoothed', 1.0))
    else:
        lat_g  = float(raw.get('imu_lat_g', 0.0))
        vert_g = float(raw.get('imu_vert_g', 1.0))

    # kalman_vsi_mps → fpm (1 m/s = 196.8504 fpm)
    vsi_fpm = float(raw.get('kalman_vsi_mps', 0.0)) * 196.8504

    return LiveSnapshot(
        t           = t,
        aoa         = aoa,
        ias         = ias,
        pitch       = float(raw.get('pitch_deg', 0.0)),
        roll        = float(raw.get('roll_deg', 0.0)),
        yaw_rate    = float(raw.get('imu_yaw_dps', 0.0)),
        vertical_g  = vert_g,
        lateral_g   = _log_to_wire_lateral_g(lat_g),
        lever_raw   = int(raw.get('flaps_raw_adc', 0)),
        flap_deg    = int(raw.get('flaps_pos', 0)),
        palt        = float(raw.get('palt_ft', 0.0)),
        vsi         = vsi_fpm,
        oat         = 15,        # not emitted by host_main replay
        flight_path = float(raw.get('flight_path_deg', 0.0)),
        data_mark   = int(raw.get('data_mark', 0)),
    )


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

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

    Calls `host_main replay` on the full log, then windows and decimates
    the output in Python.

    `t_start_s` / `t_end_s` are seconds since the log epoch (i.e.,
    `timeStamp` / 1000). Resamples to `target_rate_hz` (default 50 Hz).

    `smooth_accels` (default True): when True, lateral_g / vertical_g
    come from the C++ rate-adjusted EMA output fields. When False, they
    come from the raw IMU passthrough fields.

    `fake_lever_sweep`: accepted for API compatibility. The C++ engine
    always synthesizes a smooth lever sweep for old logs without
    `flapsRawADC`, so this parameter is a no-op.

    `flap_overrides`: not supported. Raises NotImplementedError if
    supplied with any non-empty value. To override flap setpoints,
    write a modified config file and pass it as `cfg_path`.
    """
    if fake_lever_sweep is False:
        warnings.warn(
            "fake_lever_sweep=False is silently ignored after the "
            "Python Step 2 migration. The C++ LogReplayEngine "
            "always synthesizes a flap-pot sweep when flapsRawADC "
            "is absent from the log; pass-through is no longer a "
            "supported mode. See PLAN_FIRMWARE_LOG_REPLAY_PARITY.md "
            "Sub-task 3.",
            DeprecationWarning,
            stacklevel=2,
        )

    if flap_overrides:
        raise NotImplementedError(
            "flap_overrides is not supported by the host_main wrapper. "
            "Write a modified config to a tempfile and pass it as cfg_path."
        )

    host_main = require_host_main()

    # Read timestamps from the original CSV for windowing and decimation.
    # This is a lightweight pass (no pressure data extraction) — only
    # timeStamp is needed to filter the rows that fall in [t_start_s,
    # t_end_s] and to apply target_rate_hz decimation.
    timestamps_ms: list[float] = []
    with Path(log_path).open() as f:
        reader = csv.DictReader(f)
        if reader.fieldnames:
            reader.fieldnames = [name.strip() for name in reader.fieldnames]
        for row in reader:
            ts_ms = _ffloat(row, _TIMESTAMP_NAMES)
            timestamps_ms.append(ts_ms)

    # Run host_main replay on the full log.
    args = [
        str(host_main), 'replay',
        '--input', str(log_path),
        '--config', str(cfg_path),
        '--output-format', 'jsonl',
    ]
    result = subprocess.run(args, capture_output=True, text=True, check=True)

    # Parse JSONL output and zip with timestamps.
    jsonl_rows = [
        _parse_jsonl_with_nan(line)
        for line in result.stdout.splitlines()
        if line.strip()
    ]

    # The C++ may emit fewer rows than the input (streaming synth lag
    # for old logs). Align from the end: the last N input rows map to
    # the last N output rows.
    n_out = len(jsonl_rows)
    n_in  = len(timestamps_ms)
    # Offset: input row i maps to output row (i - (n_in - n_out)).
    offset = n_in - n_out

    target_dt_s = 1.0 / target_rate_hz
    last_emit_t: float | None = None

    for i, raw in enumerate(jsonl_rows):
        in_idx   = i + offset
        ts_ms    = timestamps_ms[in_idx] if 0 <= in_idx < n_in else float('nan')
        ts       = ts_ms / 1000.0
        t_rel    = ts - t_start_s

        if ts < t_start_s:
            continue
        if ts > t_end_s:
            break

        # Decimate to target_rate_hz.
        if last_emit_t is not None and (ts - last_emit_t) < target_dt_s - 1e-4:
            continue
        last_emit_t = ts

        yield _to_live_snapshot(raw, t=t_rel, smooth_accels=smooth_accels)
