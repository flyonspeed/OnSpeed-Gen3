"""V1 list-style config parser test."""

from __future__ import annotations

from pathlib import Path

import pytest

from onspeed_py.config import load_flap_setpoints, setpoints_for_flap

FIX = Path(__file__).resolve().parent / "fixtures"


def test_v1_three_detents() -> None:
    table = load_flap_setpoints(FIX / "vac_config.cfg")
    assert sorted(table.keys()) == [0, 20, 40]


def test_v1_pot_values() -> None:
    table = load_flap_setpoints(FIX / "vac_config.cfg")
    assert table[0].pot_value == 129
    assert table[20].pot_value == 158
    assert table[40].pot_value == 206


def test_v1_setpoints_pinned_for_flap_0() -> None:
    """Pin a full row of values for flap 0 so any parser change is
    a deliberate edit, not a silent drift."""
    fs = load_flap_setpoints(FIX / "vac_config.cfg")[0]
    assert fs.degrees == 0
    assert fs.pot_value == 129
    assert fs.ldmax_aoa == pytest.approx(8.03)
    assert fs.onspeed_fast_aoa == pytest.approx(11.25)
    assert fs.onspeed_slow_aoa == pytest.approx(13.84)
    assert fs.stallwarn_aoa == pytest.approx(16.48)


def test_v1_alpha_0_defaults_to_zero() -> None:
    """V1 configs don't carry alpha_0 — the loader uses 0.0 as the
    floor for percent-lift math."""
    table = load_flap_setpoints(FIX / "vac_config.cfg")
    for fs in table.values():
        assert fs.alpha_0 == 0.0


def test_v1_alpha_stall_defaults_to_stallwarn_x_100_div_90() -> None:
    """V1 configs that omit SETPOINT_ALPHASTALL get fAlphaStall populated
    by ConfigV1Parse.cpp at parse time using fSTALLWARNAOA * 100.0f / 90.0f.
    This is the firmware's runtime fallback formula (PercentLift.cpp:43-45),
    moved to parse-time so every consumer sees the same populated value.
    """
    table = load_flap_setpoints(FIX / "vac_config.cfg")
    for fs in table.values():
        expected = fs.stallwarn_aoa * 100.0 / 90.0
        assert fs.alpha_stall == pytest.approx(expected, rel=1e-4)


def test_v1_k_fit_defaults_to_zero() -> None:
    table = load_flap_setpoints(FIX / "vac_config.cfg")
    for fs in table.values():
        assert fs.k_fit == 0.0


def test_v1_setpoints_for_flap_nearest_match() -> None:
    table = load_flap_setpoints(FIX / "vac_config.cfg")
    # Exact match.
    assert setpoints_for_flap(20, table).degrees == 20
    # Nearest match — 25 is closer to 20 than to 40.
    assert setpoints_for_flap(25, table).degrees == 20
    # 35 is closer to 40 than 20.
    assert setpoints_for_flap(35, table).degrees == 40


def test_v1_handles_digit_prefixed_tags() -> None:
    """V1 configs use `<3DAUDIO>` etc. — the loader rewrites these to
    `<_3DAUDIO>` before parsing. The test passes if the file parses
    without an XML syntax error."""
    table = load_flap_setpoints(FIX / "vac_config.cfg")
    assert len(table) == 3
