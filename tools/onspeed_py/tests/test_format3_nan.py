"""Format-3 CSV gated-column tests.

Pins the contract that empty cells for the four format-3 gated columns
(`IAS`, `AngleofAttack`, `DerivedAOA`, `efisPercentLift`) propagate as
NaN rather than coercing to 0.0, so an at-rest pre-takeoff segment in a
v3 log doesn't render as IAS=0/AOA=0 in offline analysis.

Companion to `test_log_csv_format_three_gate` on the firmware side
(format emission).  Together these pin the producer/consumer contract.
"""

from __future__ import annotations

import math
import tempfile
from pathlib import Path

from onspeed_py.config import FlapSetpoints
from onspeed_py.log_replay import _ffloat, scenario_from_log
from onspeed_py.percent_lift import compute_percent_lift

FIX = Path(__file__).resolve().parent / "fixtures"


# ---------------------------------------------------------------------------
# _ffloat helper — gated vs. non-gated semantics
# ---------------------------------------------------------------------------


def test_ffloat_strict_default_treats_empty_as_default() -> None:
    """Non-gated columns keep the strict behavior: empty cell → default.
    This preserves truncation detection — an SD write that lost a Palt
    cell still surfaces as the documented default rather than NaN, so
    downstream math doesn't get poisoned by a column that the wire
    contract says must be numeric.
    """
    row = {"Palt": ""}
    v = _ffloat(row, ("Palt",), default=0.0)
    assert v == 0.0
    assert not math.isnan(v)


def test_ffloat_allow_empty_yields_nan_on_empty_cell() -> None:
    """Gated columns opt in: empty cell → NaN."""
    row = {"IAS": ""}
    v = _ffloat(row, ("IAS",), allow_empty=True)
    assert math.isnan(v)


def test_ffloat_allow_empty_still_returns_default_when_column_absent() -> None:
    """If the column is missing from the header entirely (old-format
    log that never carried it), fall back to the default — that's a
    different signal from a producer-gated empty cell.
    """
    row = {"OtherCol": "1.0"}
    v = _ffloat(row, ("DerivedAOA",), default=0.0, allow_empty=True)
    assert v == 0.0


def test_ffloat_allow_empty_parses_numeric_normally() -> None:
    """The opt-in doesn't change behavior for numeric cells — v2 logs
    with all-numeric rows parse identically.
    """
    row = {"IAS": "73.1"}
    v = _ffloat(row, ("IAS",), allow_empty=True)
    assert v == 73.1


def test_ffloat_default_allow_empty_unchanged_for_existing_callers() -> None:
    """Sanity: callers that don't pass allow_empty get the original
    default-on-empty behavior.  Pins back-compat for the non-gated
    column callsites.
    """
    row = {"Pitch": ""}
    v = _ffloat(row, ("Pitch",), default=2.5)
    assert v == 2.5


# ---------------------------------------------------------------------------
# scenario_from_log against a v3-shaped log with empty air-data cells
# ---------------------------------------------------------------------------


def _v3_csv_at_rest(n_rows: int = 10) -> str:
    """Synthetic v3 log: empty IAS / AngleofAttack cells (the format-3
    "no valid air data" convention), other columns populated.  Matches
    what `LogSensor::Write` emits when `bIasAlive` is false on a
    pre-takeoff segment.
    """
    header = (
        "timeStamp,IAS,AngleofAttack,Pitch,Roll,YawRate,VerticalG,LateralG,"
        "flapsPos,DataMark,OAT,Palt,VSI,FlightPath\n"
    )
    rows = []
    for i in range(n_rows):
        ts = i * 20  # 50 Hz
        # IAS and AngleofAttack are empty; everything else numeric.
        rows.append(
            f"{ts},,,2.0,0.0,0.0,1.0,0.0,0,0,15,2500,0,0\n"
        )
    return header + "".join(rows)


def _v2_csv_in_flight(n_rows: int = 10) -> str:
    """Synthetic v2 log: every cell numeric.  Pins that the gated path
    parses identically to the original behavior on existing logs.
    """
    header = (
        "timeStamp,IAS,AngleofAttack,Pitch,Roll,YawRate,VerticalG,LateralG,"
        "flapsPos,DataMark,OAT,Palt,VSI,FlightPath\n"
    )
    rows = []
    for i in range(n_rows):
        ts = i * 20
        rows.append(
            f"{ts},73.1,8.0,2.0,0.0,0.0,1.0,0.0,0,0,15,2500,0,0\n"
        )
    return header + "".join(rows)


def test_v3_at_rest_yields_nan_ias_and_aoa() -> None:
    """End-to-end: a v3 log with empty IAS/AOA cells produces
    LiveSnapshot ticks where ias and aoa are NaN — not 0.0.  This is
    the observable behavior format-3 was introduced to make possible.
    """
    csv_text = _v3_csv_at_rest(n_rows=10)
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        log_path = Path(f.name)

    try:
        states = list(scenario_from_log(
            log_path=log_path,
            cfg_path=FIX / "vac_config.cfg",
            t_start_s=0.0,
            t_end_s=0.2,
            target_rate_hz=50.0,
            fake_lever_sweep=False,
        ))
    finally:
        log_path.unlink()

    assert len(states) > 0
    for s in states:
        assert math.isnan(s.ias), f"expected NaN ias on at-rest v3 row, got {s.ias}"
        assert math.isnan(s.aoa), f"expected NaN aoa on at-rest v3 row, got {s.aoa}"
        # Non-gated fields stay numeric.
        assert s.pitch == 2.0
        assert s.palt == 2500.0


def test_v3_at_rest_does_not_emit_ias_zero_frames() -> None:
    """Replay-equivalent assertion: there are no IAS=0 frames in the
    output for an at-rest input.  Defends against any future helper
    accidentally re-introducing the silent 0.0 fallback.
    """
    csv_text = _v3_csv_at_rest(n_rows=10)
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        log_path = Path(f.name)

    try:
        states = list(scenario_from_log(
            log_path=log_path,
            cfg_path=FIX / "vac_config.cfg",
            t_start_s=0.0,
            t_end_s=0.2,
            target_rate_hz=50.0,
            fake_lever_sweep=False,
        ))
    finally:
        log_path.unlink()

    # No tick should report ias == 0.0 — it must be NaN.  A 0.0 here
    # would mean the gate broke and "no valid air data" got coerced
    # into a valid-looking sample.
    bad = [s.ias for s in states if s.ias == 0.0]
    assert not bad, f"unexpected IAS=0 frames in v3 at-rest replay: {bad!r}"


def test_v2_in_flight_parses_numeric_unchanged() -> None:
    """v2 log (pre-format-3, all numeric) parses identically to the
    original behavior.  Pins back-compat for the bulk of historical
    log data.
    """
    csv_text = _v2_csv_in_flight(n_rows=10)
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        log_path = Path(f.name)

    try:
        states = list(scenario_from_log(
            log_path=log_path,
            cfg_path=FIX / "vac_config.cfg",
            t_start_s=0.0,
            t_end_s=0.2,
            target_rate_hz=50.0,
            fake_lever_sweep=False,
        ))
    finally:
        log_path.unlink()

    assert len(states) > 0
    for s in states:
        assert s.ias == 73.1
        assert s.aoa == 8.0


def test_derived_aoa_alias_also_propagates_nan() -> None:
    """`DerivedAOA` is the post-PR-#353 column name and gets the same
    air-data gate as `AngleofAttack`.  An empty `DerivedAOA` cell
    propagates as NaN through the same alias resolution.
    """
    header = (
        "timeStamp,IAS,DerivedAOA,Pitch,Roll,YawRate,VerticalG,LateralG,"
        "flapsPos,DataMark,OAT,Palt,VSI,FlightPath\n"
    )
    body = "".join(
        f"{i*20},,,2.0,0.0,0.0,1.0,0.0,0,0,15,2500,0,0\n"
        for i in range(10)
    )
    csv_text = header + body
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        log_path = Path(f.name)

    try:
        states = list(scenario_from_log(
            log_path=log_path,
            cfg_path=FIX / "vac_config.cfg",
            t_start_s=0.0,
            t_end_s=0.2,
            target_rate_hz=50.0,
            fake_lever_sweep=False,
        ))
    finally:
        log_path.unlink()

    assert len(states) > 0
    for s in states:
        assert math.isnan(s.aoa)


# ---------------------------------------------------------------------------
# compute_percent_lift NaN propagation
# ---------------------------------------------------------------------------


def _rv10_clean_setpoints() -> FlapSetpoints:
    return FlapSetpoints(
        degrees=0,
        alpha_0=-2.5,
        ldmax_aoa=2.0,
        onspeed_fast_aoa=5.0,
        onspeed_slow_aoa=7.5,
        stallwarn_aoa=9.5,
        alpha_stall=11.0,
    )


def test_compute_percent_lift_nan_in_nan_out() -> None:
    """NaN AOA propagates as NaN, not silently coerced to 0.0.

    Python's `min`/`max` drops NaN asymmetrically depending on argument
    order — the explicit isnan guard at the top of compute_percent_lift
    makes the propagation deterministic.  Consumers that read the
    LiveSnapshot directly see "no data" via NaN; the wire-frame
    boundary projects validity through `Frame.ias_valid`, which writes
    the 9999 IAS-invalid sentinel for the M5 parser.
    """
    fs = _rv10_clean_setpoints()
    pct = compute_percent_lift(math.nan, fs)
    assert math.isnan(pct)


def test_compute_percent_lift_numeric_unchanged() -> None:
    """Numeric input still produces the documented honest formula
    output — the NaN guard is a strict superset, not a behavior change
    for valid samples.
    """
    fs = _rv10_clean_setpoints()
    # body-angle 2.0 in a span of 13.5 → 33.33%
    pct = compute_percent_lift(2.0, fs)
    assert 32.0 < pct < 34.0


def test_compute_percent_lift_clamps_low_above_alpha_0() -> None:
    """A body angle below alpha_0 still clamps to 0.0 — the NaN guard
    runs first so a real, low body angle doesn't accidentally hit it.
    """
    fs = _rv10_clean_setpoints()
    pct = compute_percent_lift(-10.0, fs)  # well below alpha_0=-2.5
    assert pct == 0.0
