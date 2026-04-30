"""`scenario_from_log` adapter tests against the V1 fixture log."""

from __future__ import annotations

import math
import tempfile
from pathlib import Path

from onspeed_py.log_replay import KACC_TAU_S, scenario_from_log

FIX = Path(__file__).resolve().parent / "fixtures"


def _step_csv(rate_hz: float, duration_s: float, lat_g: float) -> str:
    """Generate a synthetic CSV at `rate_hz` for `duration_s` with a
    constant body-frame `LateralG` value (to test EMA convergence to
    a step input).
    """
    dt_ms = int(round(1000.0 / rate_hz))
    n = int(round(duration_s * rate_hz))
    header = (
        "timeStamp,IAS,AngleofAttack,Pitch,Roll,YawRate,VerticalG,LateralG,"
        "flapsPos,DataMark,OAT,Palt,VSI,FlightPath\n"
    )
    rows = []
    for i in range(n):
        ts = i * dt_ms
        rows.append(
            f"{ts},80,5.0,2.0,0.0,0.0,1.0,{lat_g},0,0,15,2500,0,0\n"
        )
    return header + "".join(rows)


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


def test_lat_g_sign_negated_relative_to_log() -> None:
    """The log's `LateralG` is body-frame; the wire/LiveSnapshot
    `lateral_g` is ball-frame. Compare the first-tick sign to the
    raw log row to confirm the negation."""
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

    states = list(scenario_from_log(
        log_path=FIX / "vac_decel_run.csv",
        cfg_path=FIX / "vac_config.cfg",
        t_start_s=2865.0,
        t_end_s=2866.0,
        target_rate_hz=50.0,
        smooth_accels=False,   # disable EMA so we can compare values directly
    ))
    # First emitted tick uses _log_to_wire_lateral_g(raw_log) = -raw_log.
    assert states[0].lateral_g == -log_lat_g


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


def test_ema_smoothing_converges_over_one_tau() -> None:
    """With τ ≈ 0.0741 s, smoothed lateral_g should have lower variance
    than raw lateral_g over a multi-second window. Per-row α is derived
    from log dt, so this test should pass at any log rate."""
    raw_states = list(scenario_from_log(
        log_path=FIX / "vac_decel_run.csv",
        cfg_path=FIX / "vac_config.cfg",
        t_start_s=2865.0,
        t_end_s=2867.0,
        target_rate_hz=50.0,
        smooth_accels=False,
    ))
    smoothed_states = list(scenario_from_log(
        log_path=FIX / "vac_decel_run.csv",
        cfg_path=FIX / "vac_config.cfg",
        t_start_s=2865.0,
        t_end_s=2867.0,
        target_rate_hz=50.0,
        smooth_accels=True,
    ))

    def variance(values: list[float]) -> float:
        if not values:
            return 0.0
        mean = sum(values) / len(values)
        return sum((v - mean) ** 2 for v in values) / len(values)

    raw_lat = [s.lateral_g for s in raw_states[10:]]
    smoothed_lat = [s.lateral_g for s in smoothed_states[10:]]
    assert variance(smoothed_lat) <= variance(raw_lat)


def test_lever_raw_uses_column_when_present() -> None:
    """Synthesize a tiny CSV with `flapsRawADC` populated and confirm
    that takes precedence over the synthesized detent value."""
    import io
    import csv as csvmod
    import tempfile

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
            t_end_s=0.05,
            target_rate_hz=50.0,
            fake_lever_sweep=False,
        ))
        assert len(states) >= 1
        # The fixture's `flap=0` detent maps to pot_value=129; the
        # column carries 500. Column should win.
        assert states[0].lever_raw == 500
    finally:
        log_path.unlink()


def test_lever_raw_uses_detent_pot_when_column_absent() -> None:
    """Without `flapsRawADC` in the log (the V1 fixture case), the
    adapter falls back to the configured detent's pot_value."""
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


# ---------------------------------------------------------------------------
# EMA over-smoothing fix (PR #380 review feedback)
# ---------------------------------------------------------------------------


def test_pitch_and_roll_pass_through_unchanged() -> None:
    """Log Pitch/Roll columns are post-AHRS-fusion (already smoothed by
    Madgwick/EKF6 on-aircraft); the adapter must pass them through
    verbatim. Re-applying an EMA would double-smooth.
    """
    csv_text = _step_csv(rate_hz=50.0, duration_s=1.0, lat_g=0.0)
    # Replace the constant pitch/roll with a varying signal so we can
    # detect any smoothing as a divergence between input and output.
    rows = csv_text.splitlines(keepends=True)
    header = rows[0]
    body = []
    pitches = []
    rolls = []
    for i, row in enumerate(rows[1:]):
        cols = row.rstrip("\n").split(",")
        # Inject a step on pitch (col 3) and a sinusoid on roll (col 4)
        p = 10.0 if i >= 5 else 0.0
        r = 5.0 * math.sin(2 * math.pi * i / 20.0)
        cols[3] = f"{p}"
        cols[4] = f"{r}"
        pitches.append(p)
        rolls.append(r)
        body.append(",".join(cols) + "\n")
    csv_text = header + "".join(body)

    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        log_path = Path(f.name)

    try:
        states = list(scenario_from_log(
            log_path=log_path,
            cfg_path=FIX / "vac_config.cfg",
            t_start_s=0.0,
            t_end_s=10.0,
            target_rate_hz=50.0,
            smooth_accels=True,    # EMA on, but pitch/roll must not be touched
        ))
    finally:
        log_path.unlink()

    assert len(states) == len(pitches)
    for s, p_in, r_in in zip(states, pitches, rolls):
        assert s.pitch == p_in, (
            f"pitch should pass through verbatim; got {s.pitch} expected {p_in}"
        )
        assert s.roll == r_in, (
            f"roll should pass through verbatim; got {s.roll} expected {r_in}"
        )


def _settled_value(rate_hz: float, target: float = 0.5,
                   duration_s: float = 1.5) -> float:
    """Drive a step LateralG=`target` at `rate_hz` for `duration_s` and
    return the smoothed lateral_g from the last emitted tick.
    """
    csv_text = _step_csv(rate_hz=rate_hz, duration_s=duration_s, lat_g=target)
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        log_path = Path(f.name)
    try:
        states = list(scenario_from_log(
            log_path=log_path,
            cfg_path=FIX / "vac_config.cfg",
            t_start_s=0.0,
            t_end_s=duration_s + 1.0,
            target_rate_hz=rate_hz,
            smooth_accels=True,
            fake_lever_sweep=False,
        ))
    finally:
        log_path.unlink()
    assert states, "expected at least one tick"
    return states[-1].lateral_g


def test_step_response_settles_within_5_tau_at_50hz() -> None:
    """EMA at 50 Hz on a 0.5g step LateralG should be within 1% of the
    step value after 5τ (~0.37 s, well inside the 1.5 s window). Note
    the log's body-frame value is negated to ball-frame on output, so
    the settled value approaches -0.5.
    """
    settled = _settled_value(rate_hz=50.0, target=0.5)
    expected = -0.5  # body→ball negation
    err = abs(settled - expected)
    assert err < 0.01, f"50 Hz step did not settle: got {settled} (err={err})"


def test_step_response_settles_within_5_tau_at_208hz() -> None:
    """Same test at 208 Hz IMU-rate logs. Variable-dt α auto-adjusts."""
    settled = _settled_value(rate_hz=208.0, target=0.5)
    expected = -0.5
    err = abs(settled - expected)
    assert err < 0.01, f"208 Hz step did not settle: got {settled} (err={err})"


def test_50hz_vs_208hz_match_after_settling() -> None:
    """A 50 Hz log and a 208 Hz log of the same physical step should
    produce the same settled LateralG within tight tolerance. This is
    the regression test for the original 4× over-smoothing bug — under
    the broken hardcoded-α code, the 50 Hz path would lag and never
    converge in this window.
    """
    settled_50 = _settled_value(rate_hz=50.0, target=0.4, duration_s=1.5)
    settled_208 = _settled_value(rate_hz=208.0, target=0.4, duration_s=1.5)
    assert math.isclose(settled_50, settled_208, abs_tol=0.005), (
        f"rate-mismatch: 50 Hz settled to {settled_50}, "
        f"208 Hz settled to {settled_208}"
    )


def test_kacc_tau_matches_firmware_alpha_at_208hz() -> None:
    """Sanity: the variable-dt formula at the firmware's IMU rate
    reproduces the documented α = 0.060899 within rounding. Catches
    accidental edits to KACC_TAU_S that would silently change replay
    behavior.
    """
    dt = 1.0 / 208.0
    alpha = dt / (KACC_TAU_S + dt)
    assert math.isclose(alpha, 0.060899, abs_tol=5e-4), (
        f"alpha at 208 Hz = {alpha}, expected ~0.060899"
    )
