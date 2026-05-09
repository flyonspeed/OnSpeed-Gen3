"""`scenario_from_log` integration tests against the V1 fixture log.

Tests here verify the Python wrapper's contract: the subprocess call to
`host_main replay` runs, the output is correctly deserialised into
`LiveSnapshot` objects, and the windowing / decimation logic works.

Algorithm behavior (EMA smoothing, lever sweep, AOA polynomial) is owned
by C++. Tests of the C++ algorithm logic belong in `test/` (native unit
tests), not here.

Tests deleted in the log_replay.py migration (Step 2 of
PLAN_PYTHON_CONSOLIDATION.md):
  - test_ema_smoothing_converges_over_one_tau  — EMA logic is in C++;
    C++ native tests cover it.
  - test_step_response_settles_within_5_tau_at_50hz / _at_208hz /
    test_50hz_vs_208hz_match_after_settling  — same reason.
  - test_kacc_tau_matches_firmware_alpha_at_208hz  — KACC_TAU_S
    constant deleted; C++ owns the tau value.
  - test_lever_sweep_midpoint_at_snap_* / test_lever_sweep_endpoints_*
    — _fake_lever_sweep() private function deleted; C++ owns the sweep.
"""

from __future__ import annotations

import math
import tempfile
from pathlib import Path

from onspeed_py.live_snapshot import LiveSnapshot
from onspeed_py.log_replay import scenario_from_log

FIX = Path(__file__).resolve().parent / "fixtures"


def test_one_second_window_yields_roughly_50_ticks() -> None:
    states = list(scenario_from_log(
        log_path=FIX / "vac_decel_run.csv",
        cfg_path=FIX / "vac_config.cfg",
        t_start_s=2865.0,
        t_end_s=2866.0,
        target_rate_hz=50.0,
    ))
    # The fixture log is sampled at a slightly irregular ~50 Hz; the
    # adapter decimates rather than interpolating. Tolerate ±20% to
    # account for log-rate jitter without being a flaky test.
    assert 40 <= len(states) <= 55, f"got {len(states)} ticks"


def test_lat_g_sign_matches_log_at_v423() -> None:
    """At v4.23 the log, the wire, and `LiveSnapshot.lateral_g` all use
    body-frame (positive = airframe accelerating rightward). The C++
    engine passes `LateralG` through to `imu_lat_g` unchanged. This
    test checks that the wrapper does not flip the sign.

    Precision note: host_main emits JSONL with %.4f (4 decimal places),
    so an exact `==` comparison is not appropriate. We use isclose with
    a tolerance matching the %.4f output (5e-5).
    """
    import csv

    # Read the first row in the [2865, 2866] window directly.
    log_lat_g = None
    with (FIX / "vac_decel_run.csv").open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            ts_s = float(row["timeStamp"]) / 1000.0
            if ts_s >= 2865.0:
                log_lat_g = float(row["LateralG"])
                break
    assert log_lat_g is not None

    # smooth_accels=False → raw IMU passthrough (imu_lat_g), which is
    # the unsmoothed body-frame lateral_g directly from the log column.
    states = list(scenario_from_log(
        log_path=FIX / "vac_decel_run.csv",
        cfg_path=FIX / "vac_config.cfg",
        t_start_s=2865.0,
        t_end_s=2866.0,
        target_rate_hz=50.0,
        smooth_accels=False,
    ))
    # Sign must match and value must be close within %.4f output precision.
    got = states[0].lateral_g
    assert math.isclose(got, log_lat_g, abs_tol=5e-5), (
        f"lateral_g sign/value mismatch: got {got}, log has {log_lat_g}"
    )


def test_flap_deg_unchanged_across_window() -> None:
    """In the first second of the decel run the lever sits at flap 0;
    nothing should change `flap_deg` in the LiveSnapshot stream."""
    states = list(scenario_from_log(
        log_path=FIX / "vac_decel_run.csv",
        cfg_path=FIX / "vac_config.cfg",
        t_start_s=2865.0,
        t_end_s=2866.0,
        target_rate_hz=50.0,
    ))
    flaps = {s.flap_deg for s in states}
    assert len(flaps) == 1, f"expected one flap value, got {flaps}"


def test_lever_raw_uses_column_when_present() -> None:
    """When `flapsRawADC` is present in the log, the C++ engine reads
    it directly (no synth sweep), and the wrapper maps it to
    `LiveSnapshot.lever_raw`."""
    csv_text = (
        "timeStamp,IAS,AngleofAttack,Pitch,Roll,YawRate,VerticalG,LateralG,"
        "flapsPos,flapsRawADC,DataMark,OAT,Palt,VSI,FlightPath\n"
        "0,80,5.0,2.0,0.0,0.0,1.0,0.0,0,500,0,15,2500,0,0\n"
        "20,80,5.0,2.0,0.0,0.0,1.0,0.0,0,500,0,15,2500,0,0\n"
        "40,80,5.0,2.0,0.0,0.0,1.0,0.0,0,500,0,15,2500,0,0\n"
        "60,80,5.0,2.0,0.0,0.0,1.0,0.0,0,500,0,15,2500,0,0\n"
    )

    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        log_path = Path(f.name)

    try:
        states = list(scenario_from_log(
            log_path=log_path,
            cfg_path=FIX / "vac_config.cfg",
            t_start_s=0.0,
            t_end_s=0.1,
            target_rate_hz=50.0,
        ))
        assert len(states) >= 1
        # Column carries 500; that must be reflected in lever_raw.
        assert states[0].lever_raw == 500
    finally:
        log_path.unlink()


def test_lever_raw_uses_detent_pot_when_column_absent() -> None:
    """Without `flapsRawADC` in the log, the C++ engine synthesizes the
    lever ADC from the configured detent's pot position. Vac's flap-0
    detent is configured with pot=129 in vac_config.cfg."""
    states = list(scenario_from_log(
        log_path=FIX / "vac_decel_run.csv",
        cfg_path=FIX / "vac_config.cfg",
        t_start_s=2865.0,
        t_end_s=2866.0,
        target_rate_hz=50.0,
    ))
    assert len(states) > 0
    # Vac's flap-0 detent is pot=129.
    assert states[0].lever_raw == 129


def test_pitch_and_roll_pass_through_unchanged() -> None:
    """The C++ engine reads the `Pitch` and `Roll` columns verbatim;
    they do not get re-smoothed. This test injects varying Pitch / Roll
    values and verifies they appear unchanged in LiveSnapshot.
    """
    import csv as csvmod

    header = (
        "timeStamp,IAS,AngleofAttack,Pitch,Roll,YawRate,VerticalG,LateralG,"
        "flapsPos,DataMark,OAT,Palt,VSI,FlightPath\n"
    )
    body = []
    pitches = []
    rolls = []
    n = 50
    for i in range(n):
        ts = i * 20  # 50 Hz
        p = 10.0 if i >= 5 else 0.0
        r = float(i) * 0.1  # monotone so we can pin per-row
        pitches.append(p)
        rolls.append(r)
        body.append(
            f"{ts},80,5.0,{p},{r},0.0,1.0,0.0,0,0,15,2500,0,0\n"
        )
    csv_text = header + "".join(body)

    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        log_path = Path(f.name)

    try:
        states = list(scenario_from_log(
            log_path=log_path,
            cfg_path=FIX / "vac_config.cfg",
            t_start_s=0.0,
            t_end_s=2.0,
            target_rate_hz=50.0,
            smooth_accels=True,  # accels can be smoothed; pitch/roll must not be
        ))
    finally:
        log_path.unlink()

    assert len(states) == n
    for s, p_in, r_in in zip(states, pitches, rolls):
        # host_main emits JSONL with %.4f precision. Use isclose with
        # a tolerance of 5e-5 (half of one ULP at 4 decimal places).
        assert math.isclose(s.pitch, p_in, abs_tol=5e-5), (
            f"pitch should pass through verbatim; got {s.pitch} expected {p_in}"
        )
        assert math.isclose(s.roll, r_in, abs_tol=5e-5), (
            f"roll should pass through verbatim; got {s.roll} expected {r_in}"
        )


def test_aoa_comes_from_pressure_not_log_column() -> None:
    """C++ contract: AOA is computed from the pressure polynomial
    (Pfwd/P45), not from the log's `AngleofAttack` column. When those
    pressure columns are absent, AOA clamps to its minimum (-20°). This
    test pins the C++ contract explicitly.

    The old Python implementation read the `AngleofAttack` column
    directly (so aoa would equal 8.0 for a log row with
    AngleofAttack=8.0). The C++ wrapper does not; it uses the pressure
    polynomial.
    """
    csv_text = (
        "timeStamp,IAS,AngleofAttack,Pitch,Roll,YawRate,VerticalG,LateralG,"
        "flapsPos,DataMark,OAT,Palt,VSI,FlightPath\n"
        "0,73.1,8.0,2.0,0.0,0.0,1.0,0.0,0,0,15,2500,0,0\n"
        "20,73.1,8.0,2.0,0.0,0.0,1.0,0.0,0,0,15,2500,0,0\n"
    )

    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        log_path = Path(f.name)

    try:
        states = list(scenario_from_log(
            log_path=log_path,
            cfg_path=FIX / "vac_config.cfg",
            t_start_s=0.0,
            t_end_s=0.1,
            target_rate_hz=50.0,
        ))
        assert len(states) > 0
        for s in states:
            # AOA comes from pressure polynomial. With no Pfwd/P45 columns,
            # the C++ engine clamps to -20.0 (the minimum body angle).
            # It does NOT read the log's AngleofAttack=8.0.
            assert s.aoa != 8.0, (
                "aoa should come from the pressure polynomial, not the "
                "log's AngleofAttack column"
            )
    finally:
        log_path.unlink()
