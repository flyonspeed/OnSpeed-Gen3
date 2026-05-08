"""Frame builder self-referential tests.

These are fast Python-only checks of length, header, CRC, sign
preservation, and field offsets. The Layer 2 firmware-parser
round-trip lives in `tools/m5-replay/test_replay.py`, which exercises
the same `Frame` builder against the C++ `ParseDisplayFrame`.
"""

from __future__ import annotations

import pytest

from onspeed_py.frame import (
    FRAME_LEN,
    IAS_INVALID_WIRE_SENTINEL,
    PAYLOAD_LEN,
    Frame,
)


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
        percent_lift_pct=4.2,
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
        percent_lift_pct=4.2,
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
    # offsets from 32 onward are +1 from v4.22.  The Frame field is
    # whole-percent float (e.g. 4.2); to_bytes scales ×10 and truncates.
    assert int(s[32:35]) == int(f.percent_lift_pct * 10)
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


@pytest.mark.parametrize(
    "ias_kts, ias_valid, expected_ias_field",
    [
        # NaN paired with ias_valid=False: emits the 9999 wire sentinel.
        (float("nan"), False, "9999"),
        # Live numeric value with ias_valid=True: encodes as ias × 10
        # in the %04u field (42.5 → 425).
        (42.5,         True,  "0425"),
        # Defensive: invalidity wins over the live float — even a
        # numeric ias_kts with ias_valid=False emits 9999.  Pins the
        # contract that consumers must treat ias_valid as authoritative
        # rather than inferring validity from the iasKt field alone.
        (42.5,         False, "9999"),
    ],
)
def test_ias_valid_flag_drives_wire_sentinel(
    ias_kts: float, ias_valid: bool, expected_ias_field: str
) -> None:
    """`Frame.ias_valid=False` projects to `IAS_INVALID_WIRE_SENTINEL`
    (9999) in the iasKt %04u field, regardless of the `ias_kts` value.
    Mirrors `kIasInvalidWireSentinel` in
    onspeed_core/proto/DisplaySerial.h — the M5 parser detects this
    exact value and flips `iasIsValid=false` to render dashes for IAS
    and percentLift.
    """
    wire = Frame(ias_kts=ias_kts, ias_valid=ias_valid).to_bytes()
    s = wire[:PAYLOAD_LEN].decode("ascii")
    assert s[11:15] == expected_ias_field, (
        f"ias_kts={ias_kts}, ias_valid={ias_valid}: "
        f"expected iasKt field {expected_ias_field!r}, got {s[11:15]!r}"
    )


def test_ias_invalid_wire_sentinel_constant_matches_cpp() -> None:
    """The Python-side constant matches the wire's max-of-field value
    declared in onspeed_core/proto/DisplaySerial.h.  Catches drift if
    the C++ side ever re-picks the sentinel.
    """
    assert IAS_INVALID_WIRE_SENTINEL == 9999
