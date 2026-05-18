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
    WIRE_VERSION,
    Frame,
    _crc8,
)


def test_frame_length() -> None:
    wire = Frame().to_bytes()
    assert len(wire) == FRAME_LEN
    assert wire.endswith(b"\r\n")


def test_frame_header() -> None:
    wire = Frame().to_bytes()
    assert wire[:2] == b"#1"


def test_wire_version_field_matches_constant() -> None:
    """v4.24 frames carry the wire version in the 2-digit ASCII field
    at bytes 2-3."""
    wire = Frame().to_bytes()
    assert wire[2:4] == f"{WIRE_VERSION:02d}".encode("ascii")


def test_frame_crc_matches_firmware_convention() -> None:
    """CRC-8 (poly 0x07, init 0x00, SMBus) over bytes 0..PAYLOAD_LEN-1."""
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
    crc_actual = _crc8(payload)
    assert crc_sent == crc_actual


def test_offsets_round_trip() -> None:
    """Re-implement the firmware parser's offset table in Python and
    check values round-trip. Catches offset/scale mistakes without
    needing the C++ binary.  Offsets shifted +2 from v4.23 because
    wireVersion was inserted at offset 2 (width 2)."""
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
        validity=0x003F,
    )
    s = f.to_bytes()[:PAYLOAD_LEN].decode("ascii")
    # Offsets per onspeed_core/proto/DisplaySerial.h v4.24 table.
    assert abs(int(s[4:8])   / 10 - f.pitch_deg) < 0.1
    assert abs(int(s[8:13])  / 10 - f.roll_deg) < 0.1
    assert abs(int(s[13:17]) / 10 - f.ias_kts) < 0.1
    assert int(s[17:23]) == round(f.palt_ft)
    assert abs(int(s[23:28]) / 10 - f.turnrate_dps) < 0.1
    assert abs(int(s[28:31]) / 100 - f.lateral_g) < 0.01
    assert abs(int(s[31:34]) / 10 - f.vertical_g) < 0.1
    # percent_lift carries tenths-of-a-percent (0..999); Frame field is
    # whole-percent float (e.g. 4.2); to_bytes scales ×10 and truncates.
    assert int(s[34:37]) == int(f.percent_lift_pct * 10)
    assert int(s[37:41]) * 10 == round(f.vsi_fpm / 10) * 10
    assert int(s[41:44]) == f.oat_c
    assert abs(int(s[44:48]) / 10 - f.flightpath_deg) < 0.1
    assert int(s[48:51]) == f.flap_deg
    assert int(s[51:53]) == f.tones_on_pct_lift
    assert int(s[53:55]) == f.onspeed_fast_pct_lift
    assert int(s[55:57]) == f.onspeed_slow_pct_lift
    assert int(s[57:59]) == f.stall_warn_pct_lift
    assert int(s[59:62]) == f.flaps_min_deg
    assert int(s[62:65]) == f.flaps_max_deg
    assert abs(int(s[65:69]) / 100 - f.g_onset_rate) < 0.01
    assert int(s[69:71]) == f.spin_cue
    assert int(s[71:73]) == f.data_mark
    assert int(s[73:75]) == f.pip_pct_lift
    assert int(s[75:79], 16) == (f.validity & 0xFFFF)


def test_negative_values_sign_preserved() -> None:
    wire = Frame(pitch_deg=-5.0, flightpath_deg=-3.5).to_bytes()
    s = wire[:PAYLOAD_LEN].decode("ascii")
    # pitch sign at offset 4 (was 2 at v4.23, shifted +2 by the
    # wireVersion field insert at v4.24).
    assert s[4]  == "-"
    # flightpath sign at offset 44 (shifted +2 from v4.23's 42).
    assert s[44] == "-"


def test_clamp_protects_against_out_of_range() -> None:
    wire = Frame(pitch_deg=999.0).to_bytes()
    assert len(wire) == FRAME_LEN
    s = wire[:PAYLOAD_LEN].decode("ascii")
    assert int(s[4:8]) == 999


def test_nan_and_inf_dont_break() -> None:
    wire = Frame(
        pitch_deg=float("nan"),
        ias_kts=float("inf"),
        flightpath_deg=float("-inf"),
    ).to_bytes()
    assert len(wire) == FRAME_LEN


def test_validity_field_round_trips() -> None:
    """The `validity` int's low 16 bits encode as the wire's %04X
    field at offset 75–78.  Sanity-check several bit patterns."""
    for bits in (0x0000, 0x003F, 0xABCD, 0xFFFF):
        wire = Frame(validity=bits).to_bytes()
        s = wire[:PAYLOAD_LEN].decode("ascii")
        assert int(s[75:79], 16) == bits


def test_validity_field_masks_high_bits() -> None:
    """Producers may have firmware-internal bits in the upper 16; the
    encoder must emit only the low 16."""
    wire = Frame(validity=0xCAFEBABE).to_bytes()
    s = wire[:PAYLOAD_LEN].decode("ascii")
    assert int(s[75:79], 16) == 0xBABE


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
    # iasKt field at offset 13–16 (was 11–14 at v4.23, shifted +2).
    assert s[13:17] == expected_ias_field, (
        f"ias_kts={ias_kts}, ias_valid={ias_valid}: "
        f"expected iasKt field {expected_ias_field!r}, got {s[13:17]!r}"
    )


def test_ias_invalid_wire_sentinel_constant_matches_cpp() -> None:
    """The Python-side constant matches the wire's max-of-field value
    declared in onspeed_core/proto/DisplaySerial.h.  Catches drift if
    the C++ side ever re-picks the sentinel.
    """
    assert IAS_INVALID_WIRE_SENTINEL == 9999
