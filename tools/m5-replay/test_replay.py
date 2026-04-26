#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pyserial>=3.5"]
# ///
"""Unit tests for the replay tool's frame builder.

Two layers:

1. **Self-referential tests** — fast Python-only checks that the builder
   emits the right length, header, CRC convention, and field offsets.
   These catch typos and offset drift inside the Python builder itself
   but cannot catch wire-format drift between Python and the firmware.

2. **Firmware-parser interop test** — builds a frame in Python, pipes it
   into the native `parse_frame` binary (which links onspeed_core's
   ParseDisplayFrame, the same code that runs on the M5), and asserts
   every parsed field matches the original input within wire resolution.
   Skipped with a clear message if the binary hasn't been built —
   build with `pio run -e native` from this directory.

Run with:
    pio run -e native      # build the firmware-parser harness
    uv run test_replay.py  # run the tests
"""
from __future__ import annotations

import math
import subprocess
import sys
from pathlib import Path

from replay import Frame, FlapSetpoints, FRAME_LEN, PAYLOAD_LEN, compute_percent_lift

HERE = Path(__file__).resolve().parent
PARSE_BIN = HERE / ".pio" / "build" / "native" / "program"

# CRC byte position is parameterised on PAYLOAD_LEN so the tests catch
# any drift if the wire layout changes again.
CRC_HEX_START = PAYLOAD_LEN
CRC_HEX_END   = PAYLOAD_LEN + 2


# ---------------------------------------------------------------------------
# Layer 1 — Self-referential tests
# ---------------------------------------------------------------------------


def test_frame_length() -> None:
    wire = Frame().to_bytes()
    # PAYLOAD_LEN payload + 2 CRC hex + 2 CRLF = FRAME_LEN
    assert len(wire) == FRAME_LEN, f"expected {FRAME_LEN} bytes, got {len(wire)}: {wire!r}"
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
    payload = wire[:PAYLOAD_LEN]
    crc_str = wire[CRC_HEX_START:CRC_HEX_END].decode("ascii")
    crc_sent = int(crc_str, 16)
    crc_actual = sum(payload) & 0xFF
    assert crc_sent == crc_actual, f"CRC mismatch: sent {crc_sent:02X} actual {crc_actual:02X}"


def test_m5_parser_offsets_round_trip() -> None:
    """Re-implement the firmware's parser in Python against our output and
    check the values round-trip. Catches offset/scale mistakes without
    needing the firmware binary.

    Field offsets must match onspeed_core/proto/DisplaySerial.h byte-for-byte.
    """
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
        alpha_0=-3.7,
        alpha_stall=10.3,
        flaps_min_deg=0,
        flaps_max_deg=33,
        g_onset_rate=0.5,
        spin_cue=0,
        data_mark=7,
    )
    wire = f.to_bytes()
    s = wire[:PAYLOAD_LEN].decode("ascii")

    assert abs(int(s[2:6])  / 10 - f.pitch_deg) < 0.1
    assert abs(int(s[6:11]) / 10 - f.roll_deg) < 0.1
    assert abs(int(s[11:15]) / 10 - f.ias_kts) < 0.1
    assert int(s[15:21]) == round(f.palt_ft)
    assert abs(int(s[21:26]) / 10 - f.turnrate_dps) < 0.1
    assert abs(int(s[26:29]) / 100 - f.lateral_g) < 0.01
    assert abs(int(s[29:32]) / 10 - f.vertical_g) < 0.1
    assert int(s[32:34]) == f.percent_lift
    assert abs(int(s[34:38]) / 10 - f.aoa_deg) < 0.1
    # VSI: payload stores fpm/10. M5 multiplies by 10 on receive.
    assert int(s[38:42]) * 10 == round(f.vsi_fpm / 10) * 10
    assert int(s[42:45]) == f.oat_c
    assert abs(int(s[45:49]) / 10 - f.flightpath_deg) < 0.1
    assert int(s[49:52]) == f.flap_deg
    assert abs(int(s[52:56]) / 10 - f.stallwarn_aoa) < 0.1
    assert abs(int(s[56:60]) / 10 - f.onspeed_slow_aoa) < 0.1
    assert abs(int(s[60:64]) / 10 - f.onspeed_fast_aoa) < 0.1
    assert abs(int(s[64:68]) / 10 - f.tones_on_aoa) < 0.1
    assert abs(int(s[68:72]) / 10 - f.alpha_0) < 0.1
    assert abs(int(s[72:76]) / 10 - f.alpha_stall) < 0.1
    assert int(s[76:79]) == f.flaps_min_deg
    assert int(s[79:82]) == f.flaps_max_deg
    assert abs(int(s[82:86]) / 100 - f.g_onset_rate) < 0.01
    assert int(s[86:88]) == f.spin_cue
    assert int(s[88:90]) == f.data_mark


def test_negative_values_sign_preserved() -> None:
    """The %+04i format in C writes a '+' or '-' sign. Python's :+04d
    matches. Test that negative AOA, negative pitch, and negative
    alpha_0 (typical at flapped settings) all come through."""
    wire = Frame(pitch_deg=-5.0, aoa_deg=-1.5, alpha_0=-9.2).to_bytes()
    s = wire[:PAYLOAD_LEN].decode("ascii")
    assert s[2]  == "-", f"pitch sign missing: {s[2:6]!r}"
    assert s[34] == "-", f"aoa sign missing: {s[34:38]!r}"
    assert s[68] == "-", f"alpha_0 sign missing: {s[68:72]!r}"


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
    assert compute_percent_lift(fs.alpha_0, fs) == 0
    assert compute_percent_lift(fs.ldmax_aoa, fs) == 50
    assert compute_percent_lift(fs.onspeed_slow_aoa, fs) == 66
    assert compute_percent_lift(fs.stallwarn_aoa, fs) == 90
    assert compute_percent_lift(fs.alpha_stall + 5, fs) == 99


def test_clamp_protects_against_out_of_range() -> None:
    # AOA field is %+04i, valid range -99.9 to +99.9 degrees (scaled ×10)
    wire = Frame(aoa_deg=999.0).to_bytes()
    assert len(wire) == FRAME_LEN  # no buffer overflow
    s = wire[:PAYLOAD_LEN].decode("ascii")
    # Should clamp to +999 (9.99°), not break the frame length
    assert int(s[34:38]) == 999


def test_nan_and_inf_dont_break() -> None:
    wire = Frame(
        pitch_deg=float("nan"),
        aoa_deg=float("inf"),
        ias_kts=float("-inf"),
        alpha_0=float("nan"),
    ).to_bytes()
    assert len(wire) == FRAME_LEN


# ---------------------------------------------------------------------------
# Layer 2 — Firmware-parser interop test
#
# Builds a frame in Python, pipes it into the native parse_frame binary
# (which links onspeed_core::ParseDisplayFrame — the actual code that
# runs on the M5), parses the binary's `key=value` stdout, and asserts
# every field round-trips within wire resolution.
#
# This is the test that catches the class of regression where the bench
# tool emits frames the firmware would silently drop. Self-referential
# Python round-trips can't.
# ---------------------------------------------------------------------------


def _parse_via_firmware(wire: bytes) -> dict[str, str]:
    """Pipe `wire` into parse_frame and return its parsed key=value map."""
    if not PARSE_BIN.exists():
        raise FileNotFoundError(
            f"parse_frame binary missing at {PARSE_BIN}.\n"
            f"Build it with:  cd {HERE} && pio run -e native"
        )
    result = subprocess.run(
        [str(PARSE_BIN)],
        input=wire,
        capture_output=True,
        timeout=10,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"parse_frame exited {result.returncode}: "
            f"stdout={result.stdout!r} stderr={result.stderr!r}"
        )
    out: dict[str, str] = {}
    for line in result.stdout.decode("ascii").splitlines():
        if not line or "=" not in line:
            continue
        k, _, v = line.partition("=")
        out[k] = v
    return out


def test_firmware_parser_round_trip() -> None:
    """End-to-end: replay.py's builder → onspeed_core::ParseDisplayFrame.

    This is the contract the bench tool actually has to satisfy.
    """
    if not PARSE_BIN.exists():
        print("    SKIP  parse_frame binary not built; run `pio run -e native` first")
        return

    f = Frame(
        pitch_deg=5.2,
        roll_deg=-12.7,
        ias_kts=87.4,
        palt_ft=3120,
        turnrate_dps=3.5,
        lateral_g=0.05,
        vertical_g=1.2,
        percent_lift=55,
        aoa_deg=4.9,
        vsi_fpm=-630,
        oat_c=15,
        flightpath_deg=-2.3,
        flap_deg=16,
        stallwarn_aoa=7.17,
        onspeed_slow_aoa=3.88,
        onspeed_fast_aoa=2.44,
        tones_on_aoa=1.11,
        alpha_0=-6.22,
        alpha_stall=9.57,
        flaps_min_deg=0,
        flaps_max_deg=33,
        g_onset_rate=0.5,
        spin_cue=0,
        data_mark=7,
    )
    wire = f.to_bytes()
    parsed = _parse_via_firmware(wire)

    # Wire resolution: ×10 fields = 0.1, ×100 fields = 0.01, integers exact.
    def close(field: str, expected: float, tol: float) -> None:
        got = float(parsed[field])
        assert math.isclose(got, expected, abs_tol=tol), \
            f"{field}: parsed={got} expected={expected} tol={tol}"

    close("pitchDeg",          f.pitch_deg,          0.11)
    close("rollDeg",           f.roll_deg,           0.11)
    close("iasKt",             f.ias_kts,            0.11)
    close("paltFt",            f.palt_ft,            1.01)
    close("turnRateDps",       f.turnrate_dps,       0.11)
    close("lateralG",          f.lateral_g,          0.011)
    close("verticalG",         f.vertical_g,         0.11)
    assert int(parsed["percentLift"]) == f.percent_lift
    close("aoaDeg",             f.aoa_deg,            0.11)
    # VSI loses bottom digit (transmitted as fpm/10 truncated to int)
    close("vsiFpm",             round(f.vsi_fpm / 10) * 10, 11.0)
    assert int(parsed["oatC"]) == f.oat_c
    close("flightPathDeg",      f.flightpath_deg,     0.11)
    assert int(parsed["flapsDeg"]) == f.flap_deg
    close("stallWarnAoaDeg",    f.stallwarn_aoa,      0.11)
    close("onSpeedSlowAoaDeg",  f.onspeed_slow_aoa,   0.11)
    close("onSpeedFastAoaDeg",  f.onspeed_fast_aoa,   0.11)
    close("tonesOnAoaDeg",      f.tones_on_aoa,       0.11)
    close("alpha0Deg",          f.alpha_0,            0.11)
    close("alphaStallDeg",      f.alpha_stall,        0.11)
    assert int(parsed["flapsMinDeg"]) == f.flaps_min_deg
    assert int(parsed["flapsMaxDeg"]) == f.flaps_max_deg
    close("gOnsetRate",         f.g_onset_rate,       0.011)
    assert int(parsed["spinRecoveryCue"]) == f.spin_cue
    assert int(parsed["dataMark"]) == f.data_mark


def test_firmware_parser_rejects_bad_crc() -> None:
    """Corrupt one byte of the payload; firmware parser must refuse."""
    if not PARSE_BIN.exists():
        print("    SKIP  parse_frame binary not built; run `pio run -e native` first")
        return

    wire = bytearray(Frame(pitch_deg=5.0).to_bytes())
    wire[10] ^= 0x01  # flip a bit in the payload — CRC will mismatch

    result = subprocess.run(
        [str(PARSE_BIN)],
        input=bytes(wire),
        capture_output=True,
        timeout=10,
    )
    assert result.returncode == 1, f"expected exit 1 on bad CRC, got {result.returncode}"
    assert b"parse_failed" in result.stdout, f"expected 'parse_failed', got: {result.stdout!r}"


def test_firmware_parser_round_trips_negative_alpha0() -> None:
    """Regression coverage for the bug PR #320 fixed: negative alpha_0
    (normal at flapped settings) must round-trip without losing its
    sign or magnitude.  Reference values from N720AK full-flap config."""
    if not PARSE_BIN.exists():
        print("    SKIP  parse_frame binary not built; run `pio run -e native` first")
        return

    wire = Frame(
        flap_deg=33,
        alpha_0=-9.2,           # negative — RV-10 full flaps
        alpha_stall=11.6,
        tones_on_aoa=-2.2,      # also negative — L/Dmax body angle
        flaps_min_deg=0,
        flaps_max_deg=33,
    ).to_bytes()
    parsed = _parse_via_firmware(wire)

    assert math.isclose(float(parsed["alpha0Deg"]),     -9.2, abs_tol=0.11)
    assert math.isclose(float(parsed["alphaStallDeg"]), 11.6, abs_tol=0.11)
    assert math.isclose(float(parsed["tonesOnAoaDeg"]), -2.2, abs_tol=0.11)
    assert int(parsed["flapsMinDeg"]) == 0
    assert int(parsed["flapsMaxDeg"]) == 33


def main() -> int:
    tests = [
        # Layer 1: self-referential
        test_frame_length,
        test_frame_header,
        test_frame_crc_matches_firmware_convention,
        test_m5_parser_offsets_round_trip,
        test_negative_values_sign_preserved,
        test_percent_lift_buckets_match_firmware,
        test_clamp_protects_against_out_of_range,
        test_nan_and_inf_dont_break,
        # Layer 2: firmware-parser interop
        test_firmware_parser_round_trip,
        test_firmware_parser_rejects_bad_crc,
        test_firmware_parser_round_trips_negative_alpha0,
    ]
    failed = 0
    skipped = 0
    for t in tests:
        try:
            t()
            print(f"  PASS  {t.__name__}")
        except FileNotFoundError as e:
            print(f"  SKIP  {t.__name__}: {e}")
            skipped += 1
        except AssertionError as e:
            print(f"  FAIL  {t.__name__}: {e}")
            failed += 1

    total = len(tests)
    passed = total - failed - skipped
    print(f"\n{passed}/{total} passed, {skipped} skipped, {failed} failed")
    if not PARSE_BIN.exists():
        print(f"\nNote: build the firmware-parser harness for full coverage:")
        print(f"      cd {HERE} && pio run -e native")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
