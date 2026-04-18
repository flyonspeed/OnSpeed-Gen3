#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pyserial>=3.5"]
# ///
"""Unit tests for the replay tool's frame builder.

Verifies the 80-byte wire format matches the firmware's snprintf output
exactly, including CRC computation and field offsets that the M5 parser
reads at SerialRead.cpp:76-134.

Run with:
    uv run test_replay.py
"""
from __future__ import annotations

import sys

from replay import Frame, FlapSetpoints, compute_percent_lift


def test_frame_length() -> None:
    wire = Frame().to_bytes()
    # 76 payload + 2 CRC hex + 2 CRLF = 80
    assert len(wire) == 80, f"expected 80 bytes, got {len(wire)}: {wire!r}"
    assert wire.endswith(b"\r\n"), f"missing CRLF: {wire!r}"


def test_frame_header() -> None:
    wire = Frame().to_bytes()
    assert wire[:2] == b"#1", f"bad header: {wire[:2]!r}"


def test_frame_crc_matches_firmware_convention() -> None:
    wire = Frame(
        pitch_deg=5.0,
        roll_deg=-2.0,
        ias_kts=100.0,
        palt_ft=2500,
        aoa_deg=4.5,
    ).to_bytes()
    payload = wire[:76]
    crc_str = wire[76:78].decode("ascii")
    crc_sent = int(crc_str, 16)
    crc_actual = sum(payload) & 0xFF
    assert crc_sent == crc_actual, f"CRC mismatch: sent {crc_sent:02X} actual {crc_actual:02X}"


def test_m5_parser_offsets_round_trip() -> None:
    """Re-implements the M5's SerialRead.cpp parsing against our output and
    checks the values round-trip. Catches offset/scale mistakes."""
    f = Frame(
        pitch_deg=5.0,
        roll_deg=-12.0,
        ias_kts=87.4,
        palt_ft=3120,
        turnrate_dps=3.0,
        lateral_g=0.05,
        vertical_g=1.2,
        percent_lift=55,
        aoa_deg=4.9,
        vsi_fpm=-600,
        oat_c=15,
        flightpath_deg=-2.0,
        flap_deg=16,
        stallwarn_aoa=7.6,
        onspeed_slow_aoa=4.1,
        onspeed_fast_aoa=2.7,
        tones_on_aoa=2.3,
        g_onset_rate=0.5,
        spin_cue=0,
        data_mark=7,
    )
    wire = f.to_bytes()
    s = wire[:76].decode("ascii")

    # Offsets exactly match M5 SerialRead.cpp:76-134
    assert abs(int(s[2:6]) / 10 - f.pitch_deg) < 0.1
    assert abs(int(s[6:11]) / 10 - f.roll_deg) < 0.1
    assert abs(int(s[11:15]) / 10 - f.ias_kts) < 0.1
    assert int(s[15:21]) == round(f.palt_ft)
    assert abs(int(s[21:26]) / 10 - f.turnrate_dps) < 0.1
    assert abs(int(s[26:29]) / 100 - f.lateral_g) < 0.01
    assert abs(int(s[29:32]) / 10 - f.vertical_g) < 0.1
    assert int(s[32:34]) == f.percent_lift
    assert abs(int(s[34:38]) / 10 - f.aoa_deg) < 0.1
    # VSI: M5 parses s[38:42] and multiplies by 10 (so field stores /10 fpm)
    assert int(s[38:42]) * 10 == round(f.vsi_fpm / 10) * 10
    assert int(s[42:45]) == f.oat_c
    assert abs(int(s[45:49]) / 10 - f.flightpath_deg) < 0.1
    assert int(s[49:52]) == f.flap_deg
    assert abs(int(s[52:56]) / 10 - f.stallwarn_aoa) < 0.1
    assert abs(int(s[56:60]) / 10 - f.onspeed_slow_aoa) < 0.1
    assert abs(int(s[60:64]) / 10 - f.onspeed_fast_aoa) < 0.1
    assert abs(int(s[64:68]) / 10 - f.tones_on_aoa) < 0.1
    assert abs(int(s[68:72]) / 100 - f.g_onset_rate) < 0.01
    assert int(s[72:74]) == f.spin_cue
    assert int(s[74:76]) == f.data_mark


def test_negative_values_sign_preserved() -> None:
    """The %+04i format in C writes a '+' or '-' sign. Python's :+04d
    matches. Test that negative AOA and negative pitch come through."""
    wire = Frame(pitch_deg=-5.0, aoa_deg=-1.5).to_bytes()
    s = wire[:76].decode("ascii")
    # Pitch at s[2:6]
    assert s[2] == "-", f"pitch sign missing: {s[2:6]!r}"
    # AOA at s[34:38]
    assert s[34] == "-", f"aoa sign missing: {s[34:38]!r}"


def test_percent_lift_buckets_match_firmware() -> None:
    fs = FlapSetpoints(
        degrees=0,
        alpha_0=-2.5,
        ldmax_aoa=4.0,
        onspeed_fast_aoa=4.1,
        onspeed_slow_aoa=5.0,
        stallwarn_aoa=8.0,
        alpha_stall=10.5,
    )
    # At alpha_0 -> 0%
    assert compute_percent_lift(fs.alpha_0, fs) == 0
    # At LDmax -> 50%
    assert compute_percent_lift(fs.ldmax_aoa, fs) == 50
    # At OnSpeedSlow -> 66%
    assert compute_percent_lift(fs.onspeed_slow_aoa, fs) == 66
    # At StallWarn -> 90%
    assert compute_percent_lift(fs.stallwarn_aoa, fs) == 90
    # Above stall_alpha -> clamped at 99
    assert compute_percent_lift(fs.alpha_stall + 5, fs) == 99


def test_clamp_protects_against_out_of_range() -> None:
    # AOA field is %+04i, valid range -99.9 to +99.9 degrees (scaled ×10)
    wire = Frame(aoa_deg=999.0).to_bytes()
    assert len(wire) == 80  # no buffer overflow
    s = wire[:76].decode("ascii")
    # Should clamp to +999 (9.99°), not break the frame length
    assert int(s[34:38]) == 999


def test_nan_and_inf_dont_break() -> None:
    wire = Frame(
        pitch_deg=float("nan"),
        aoa_deg=float("inf"),
        ias_kts=float("-inf"),
    ).to_bytes()
    assert len(wire) == 80


def main() -> int:
    tests = [
        test_frame_length,
        test_frame_header,
        test_frame_crc_matches_firmware_convention,
        test_m5_parser_offsets_round_trip,
        test_negative_values_sign_preserved,
        test_percent_lift_buckets_match_firmware,
        test_clamp_protects_against_out_of_range,
        test_nan_and_inf_dont_break,
    ]
    failed = 0
    for t in tests:
        try:
            t()
            print(f"  PASS  {t.__name__}")
        except AssertionError as e:
            print(f"  FAIL  {t.__name__}: {e}")
            failed += 1
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
