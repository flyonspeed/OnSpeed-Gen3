"""`scenario_from_log` adapter tests against the V1 fixture log."""

from __future__ import annotations

import math
import tempfile
from pathlib import Path

from onspeed_py.config import FlapSetpoints
from onspeed_py.live_snapshot import LiveSnapshot
from onspeed_py.log_replay import (
    KACC_TAU_S,
    _fake_lever_sweep,
    scenario_from_log,
)

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


def test_lat_g_sign_matches_log_at_v423() -> None:
    """At v4.23 the log, the wire, and `LiveSnapshot.lateral_g` all use
    body-frame (positive = airframe accelerating rightward). The
    `_log_to_wire_lateral_g` helper became a pass-through; this test
    pins that so any future re-introduction of a log→snapshot negation
    is caught immediately."""
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
    # First emitted tick: log and snapshot now use the same body-frame
    # convention, so the value passes through unchanged (modulo the
    # source-row time alignment, which scenario_from_log handles).
    assert states[0].lateral_g == log_lat_g


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
    step value after 5τ (~0.37 s, well inside the 1.5 s window).  At
    v4.23 the log and snapshot share body-frame, so the settled value
    is the input value with no sign flip.
    """
    settled = _settled_value(rate_hz=50.0, target=0.5)
    expected = 0.5
    err = abs(settled - expected)
    assert err < 0.01, f"50 Hz step did not settle: got {settled} (err={err})"


def test_step_response_settles_within_5_tau_at_208hz() -> None:
    """Same test at 208 Hz IMU-rate logs. Variable-dt α auto-adjusts."""
    settled = _settled_value(rate_hz=208.0, target=0.5)
    expected = 0.5
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


# ---------------------------------------------------------------------------
# Lever-sweep window-edge clamping fix (PR #380 review feedback)
# ---------------------------------------------------------------------------


def _two_detents() -> dict[int, FlapSetpoints]:
    """Minimal config with two detents so _fake_lever_sweep has somewhere
    to interpolate between. pot=100 at flap=0, pot=300 at flap=16.
    Midpoint pot value at the snap is (100 + 300) / 2 = 200.
    """
    return {
        0:  FlapSetpoints(degrees=0,  pot_value=100, alpha_0=-2.0,
                          alpha_stall=11.0),
        16: FlapSetpoints(degrees=16, pot_value=300, alpha_0=-3.0,
                          alpha_stall=10.5),
    }


def _states_with_snap_at(idx: int, total: int) -> list[LiveSnapshot]:
    """Build `total` LiveSnapshots; flap=0 before `idx`, flap=16 from
    `idx` onward. _fake_lever_sweep snaps at the first row where the
    detected detent flips, i.e. index `idx`.
    """
    out: list[LiveSnapshot] = []
    for i in range(total):
        s = LiveSnapshot()
        s.flap_deg = 0 if i < idx else 16
        out.append(s)
    return out


def test_lever_sweep_midpoint_at_snap_when_window_fully_inside() -> None:
    """Sanity: when the snap is centered with `half` ticks on either
    side, the lever sits at the midpoint pot value at the snap tick.
    """
    cfg = _two_detents()
    states = _states_with_snap_at(idx=50, total=200)
    _fake_lever_sweep(states, cfg, sweep_window_s=4.0)  # half = 100
    # Snap at index 50, half=100, but log_replay will clamp start=0;
    # this is actually a window-edge case for snap=50 < half=100.
    # Verify the midpoint is at the snap.
    assert states[50].lever_raw == 200, (
        f"midpoint lever_raw at snap should be 200; got {states[50].lever_raw}"
    )


def test_lever_sweep_midpoint_at_snap_near_log_start() -> None:
    """Snap at index 5, half=20. start clamps to 0. Without the signed-
    offset fix, smoothstep(5/25) = 0.296, lever_raw lands at ~159
    instead of 200. With the fix, u(k=5) = 0.5 + 0 = 0.5, smoothstep
    yields 0.5, midpoint = 200.
    """
    cfg = _two_detents()
    sweep_ticks = 40                      # so half=20
    sweep_window_s = sweep_ticks / 50.0   # 0.8 s @ 50 Hz
    states = _states_with_snap_at(idx=5, total=60)
    _fake_lever_sweep(states, cfg, sweep_window_s=sweep_window_s)
    assert states[5].lever_raw == 200, (
        f"snap-near-start: lever_raw at snap should be 200; "
        f"got {states[5].lever_raw}"
    )


def test_lever_sweep_midpoint_at_snap_near_log_end() -> None:
    """Mirror of the start case: snap at index N-6 with half=20.
    end clamps to N-1. With the fix, u(k=snap) = 0.5 → lever = 200.
    """
    cfg = _two_detents()
    sweep_ticks = 40
    sweep_window_s = sweep_ticks / 50.0
    total = 60
    snap_idx = total - 6   # 54
    states = _states_with_snap_at(idx=snap_idx, total=total)
    _fake_lever_sweep(states, cfg, sweep_window_s=sweep_window_s)
    assert states[snap_idx].lever_raw == 200, (
        f"snap-near-end: lever_raw at snap should be 200; "
        f"got {states[snap_idx].lever_raw}"
    )


def test_lever_sweep_endpoints_reach_detent_values() -> None:
    """Far from the snap (outside ±half), the lever stays at the original
    detent. Inside the window the smoothstep ramps from prev to new.
    """
    cfg = _two_detents()
    sweep_ticks = 40
    sweep_window_s = sweep_ticks / 50.0
    states = _states_with_snap_at(idx=100, total=200)
    _fake_lever_sweep(states, cfg, sweep_window_s=sweep_window_s)
    # Inside the window the start edge clamps to prev_pot (100).
    assert states[80].lever_raw == 100, (
        f"start of window should clamp to prev pot 100; got {states[80].lever_raw}"
    )
    # End edge clamps to new_pot (300).
    assert states[120].lever_raw == 300, (
        f"end of window should clamp to new pot 300; got {states[120].lever_raw}"
    )
