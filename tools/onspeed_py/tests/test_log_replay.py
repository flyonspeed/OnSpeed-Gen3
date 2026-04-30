"""`scenario_from_log` adapter tests against the V1 fixture log."""

from __future__ import annotations

from pathlib import Path

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
    """With α = 0.0609 and τ ≈ 0.32 s = 16 ticks at 50 Hz, the EMA
    state should track the rolling-average lateral_g closely after
    ~30 ticks. Compare smoothed vs raw values: smoothed should have
    less variance than raw."""
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
