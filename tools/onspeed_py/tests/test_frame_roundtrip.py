"""Frame builder self-referential tests.

These are fast Python-only checks of length, header, CRC, sign
preservation, and field offsets. The Layer 2 firmware-parser
round-trip lives in `tools/m5-replay/test_replay.py`, which exercises
the same `Frame` builder against the C++ `ParseDisplayFrame`.
"""

from __future__ import annotations

from onspeed_py.frame import FRAME_LEN, PAYLOAD_LEN, Frame


def test_frame_length() -> None:
    wire = Frame().to_bytes()
    assert len(wire) == FRAME_LEN
    assert wire.endswith(b"\r\n")


def test_frame_header() -> None:
    wire = Frame().to_bytes()
    assert wire[:2] == b"#1"


def test_frame_crc_matches_firmware_convention() -> None:
    wire = Frame(
        pitch_deg=5.0,
        roll_deg=-2.0,
        ias_kts=100.0,
        palt_ft=2500,
        percent_lift=42,
    ).to_bytes()
    payload = wire[:PAYLOAD_LEN]
    crc_str = wire[PAYLOAD_LEN:PAYLOAD_LEN + 2].decode("ascii")
    crc_sent = int(crc_str, 16)
    crc_actual = sum(payload) & 0xFF
    assert crc_sent == crc_actual


def test_offsets_round_trip() -> None:
    """Re-implement the firmware parser's offset table in Python and
    check values round-trip. Catches offset/scale mistakes without
    needing the C++ binary."""
    f = Frame(
        pitch_deg=5.0,
        roll_deg=-12.0,
        ias_kts=87.4,
        palt_ft=3120,
        turnrate_dps=3.0,
        lateral_g=0.05,
        vertical_g=1.2,
        percent_lift=42,
        vsi_fpm=-600,
        oat_c=15,
        flightpath_deg=-2.0,
        flap_deg=16,
        tones_on_pct_lift=33,
        onspeed_fast_pct_lift=55,
        onspeed_slow_pct_lift=74,
        stall_warn_pct_lift=88,
        flaps_min_deg=0,
        flaps_max_deg=33,
        g_onset_rate=0.5,
        spin_cue=0,
        data_mark=7,
        pip_pct_lift=53,
    )
    s = f.to_bytes()[:PAYLOAD_LEN].decode("ascii")
    assert abs(int(s[2:6])  / 10 - f.pitch_deg) < 0.1
    assert abs(int(s[6:11]) / 10 - f.roll_deg) < 0.1
    assert abs(int(s[11:15]) / 10 - f.ias_kts) < 0.1
    assert int(s[15:21]) == round(f.palt_ft)
    assert abs(int(s[21:26]) / 10 - f.turnrate_dps) < 0.1
    assert abs(int(s[26:29]) / 100 - f.lateral_g) < 0.01
    assert abs(int(s[29:32]) / 10 - f.vertical_g) < 0.1
    # percent_lift widened to 3 chars (tenths-of-a-percent) at v4.23;
    # offsets from 32 onward are +1 from v4.22.
    assert int(s[32:35]) == f.percent_lift
    assert int(s[35:39]) * 10 == round(f.vsi_fpm / 10) * 10
    assert int(s[39:42]) == f.oat_c
    assert abs(int(s[42:46]) / 10 - f.flightpath_deg) < 0.1
    assert int(s[46:49]) == f.flap_deg
    assert int(s[49:51]) == f.tones_on_pct_lift
    assert int(s[51:53]) == f.onspeed_fast_pct_lift
    assert int(s[53:55]) == f.onspeed_slow_pct_lift
    assert int(s[55:57]) == f.stall_warn_pct_lift
    assert int(s[57:60]) == f.flaps_min_deg
    assert int(s[60:63]) == f.flaps_max_deg
    assert abs(int(s[63:67]) / 100 - f.g_onset_rate) < 0.01
    assert int(s[67:69]) == f.spin_cue
    assert int(s[69:71]) == f.data_mark
    assert int(s[71:73]) == f.pip_pct_lift


def test_negative_values_sign_preserved() -> None:
    wire = Frame(pitch_deg=-5.0, flightpath_deg=-3.5).to_bytes()
    s = wire[:PAYLOAD_LEN].decode("ascii")
    assert s[2]  == "-"
    # flightpath sign at offset 42 (was 41 at v4.22, shifted +1 by the
    # percent_lift widen at v4.23).
    assert s[42] == "-"


def test_clamp_protects_against_out_of_range() -> None:
    wire = Frame(pitch_deg=999.0).to_bytes()
    assert len(wire) == FRAME_LEN
    s = wire[:PAYLOAD_LEN].decode("ascii")
    assert int(s[2:6]) == 999


def test_nan_and_inf_dont_break() -> None:
    wire = Frame(
        pitch_deg=float("nan"),
        ias_kts=float("inf"),
        flightpath_deg=float("-inf"),
    ).to_bytes()
    assert len(wire) == FRAME_LEN
