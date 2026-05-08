"""New per-FLAP_POSITION-block config parser test."""

from __future__ import annotations

from pathlib import Path

import pytest

from onspeed_py.config import load_flap_setpoints
from onspeed_py.percent_lift import compute_percent_lift

FIX = Path(__file__).resolve().parent / "fixtures"


def test_new_three_detents() -> None:
    table = load_flap_setpoints(FIX / "n720ak_config.cfg")
    assert sorted(table.keys()) == [0, 16, 33]


def test_new_pot_values_match_file() -> None:
    table = load_flap_setpoints(FIX / "n720ak_config.cfg")
    assert table[0].pot_value == 1462
    assert table[16].pot_value == 897
    assert table[33].pot_value == 2


def test_new_setpoints_pinned_for_flap_0() -> None:
    fs = load_flap_setpoints(FIX / "n720ak_config.cfg")[0]
    assert fs.degrees == 0
    assert fs.pot_value == 1462
    assert fs.ldmax_aoa == pytest.approx(3.24)
    assert fs.onspeed_fast_aoa == pytest.approx(3.98)
    # Use abs=1e-3 because the file stores e.g. 5.2600002.
    assert fs.onspeed_slow_aoa == pytest.approx(5.26, abs=1e-3)
    assert fs.stallwarn_aoa == pytest.approx(8.24, abs=1e-3)


def test_new_alpha_0_populated_from_file_not_defaulted() -> None:
    """Verifies the new path picks up calibrated `ALPHA0` rather than
    falling through to the V1 default of 0.0."""
    table = load_flap_setpoints(FIX / "n720ak_config.cfg")
    assert table[0].alpha_0 == pytest.approx(-3.7211, abs=1e-3)
    assert table[16].alpha_0 == pytest.approx(-6.2231, abs=1e-3)
    assert table[33].alpha_0 == pytest.approx(-9.2107, abs=1e-3)


def test_new_alpha_stall_populated_from_file() -> None:
    table = load_flap_setpoints(FIX / "n720ak_config.cfg")
    assert table[0].alpha_stall == pytest.approx(10.309, abs=1e-3)
    assert table[16].alpha_stall == pytest.approx(9.5681, abs=1e-3)
    assert table[33].alpha_stall == pytest.approx(11.5701, abs=1e-3)


def test_new_k_fit_populated_from_file() -> None:
    table = load_flap_setpoints(FIX / "n720ak_config.cfg")
    assert table[0].k_fit == pytest.approx(50960.219, abs=0.1)
    assert table[16].k_fit == pytest.approx(53653.23, abs=0.1)


def test_new_compute_percent_lift_uses_alpha_0() -> None:
    """With calibrated alpha_0 < 0, body angles between alpha_0 and 0
    should yield small POSITIVE percents (the wing is lifting even
    though the fuselage is below horizontal in the wind frame)."""
    fs = load_flap_setpoints(FIX / "n720ak_config.cfg")[0]
    # Body angle 0 is above alpha_0 (-3.72), so percent > 0.
    pct_at_zero_body_angle = compute_percent_lift(0.0, fs)
    assert pct_at_zero_body_angle > 0.0
    # Body angle == alpha_0 → percent == 0.0.
    assert compute_percent_lift(fs.alpha_0, fs) == 0.0
    # Body angle == alpha_stall → percent clamps to 99.9 (saturation
    # convention; wire encoder multiplies by 10 to land at 999, never
    # 1000).
    assert abs(compute_percent_lift(fs.alpha_stall, fs) - 99.9) < 0.05


