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
        percent_lift=42,
    ).to_bytes()
    payload = wire[:PAYLOAD_LEN]
    crc_str = wire[CRC_HEX_START:CRC_HEX_END].decode("ascii")
    crc_sent = int(crc_str, 16)
    crc_actual = sum(payload) & 0xFF
    assert crc_sent == crc_actual, f"CRC mismatch: sent {crc_sent:02X} actual {crc_actual:02X}"


def test_offsets_round_trip() -> None:
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
    wire = f.to_bytes()
    s = wire[:PAYLOAD_LEN].decode("ascii")

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
    """The %+04i format in C writes a '+' or '-' sign. Python's :+04d
    matches.  Test that negative pitch and negative flight-path angle
    come through.  All AOA-related fields are unsigned percents so
    they don't have signs to preserve."""
    wire = Frame(pitch_deg=-5.0, flightpath_deg=-3.5).to_bytes()
    s = wire[:PAYLOAD_LEN].decode("ascii")
    assert s[2]  == "-", f"pitch sign missing: {s[2:6]!r}"
    # flightPath sign at offset 42 (was 41 at v4.22; shifted +1 by the
    # percent_lift widen at v4.23).
    assert s[42] == "-", f"flightPath sign missing: {s[42:46]!r}"


def test_compute_percent_lift_honest_formula() -> None:
    """Pin the Python compute_percent_lift to the honest single-linear
    formula.  L/Dmax, OnSpeed band edges, and StallWarn land at
    *whatever* percent the calibration says — the function does not
    pin them to fixed segment break-points.
    """
    # RV-10 clean: span = 11.0 - (-2.5) = 13.5 deg.
    fs = FlapSetpoints(
        degrees=0,
        alpha_0=-2.5,
        ldmax_aoa=2.0,
        onspeed_fast_aoa=5.0,
        onspeed_slow_aoa=7.5,
        stallwarn_aoa=9.5,
        alpha_stall=11.0,
    )
    # Endpoints
    assert compute_percent_lift(fs.alpha_0, fs) == 0
    assert compute_percent_lift(fs.alpha_stall, fs) == 99   # clamped
    # L/Dmax body angle 2.0 -> (4.5/13.5)*100 = 33.33 -> 33.  Not 50.
    ldmax_pct = compute_percent_lift(fs.ldmax_aoa, fs)
    assert ldmax_pct in (32, 33, 34), f"unexpected ldmax pct {ldmax_pct}"
    # OnSpeedSlow 7.5 -> (10/13.5)*100 = 74.07 -> 74.  Not 66.
    slow_pct = compute_percent_lift(fs.onspeed_slow_aoa, fs)
    assert slow_pct in (73, 74, 75), f"unexpected slow pct {slow_pct}"


def test_clamp_protects_against_out_of_range() -> None:
    # pitch field is %+04i, valid range -99.9 to +99.9 degrees (scaled ×10)
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


# ---------------------------------------------------------------------------
# Layer 2 — Firmware-parser interop test
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
        percent_lift=42,
        vsi_fpm=-630,
        oat_c=15,
        flightpath_deg=-2.3,
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
    )
    wire = f.to_bytes()
    parsed = _parse_via_firmware(wire)

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
    close("vsiFpm",             round(f.vsi_fpm / 10) * 10, 11.0)
    assert int(parsed["oatC"]) == f.oat_c
    close("flightPathDeg",      f.flightpath_deg,     0.11)
    assert int(parsed["flapsDeg"]) == f.flap_deg
    assert int(parsed["tonesOnPctLift"])     == f.tones_on_pct_lift
    assert int(parsed["onSpeedFastPctLift"]) == f.onspeed_fast_pct_lift
    assert int(parsed["onSpeedSlowPctLift"]) == f.onspeed_slow_pct_lift
    assert int(parsed["stallWarnPctLift"])   == f.stall_warn_pct_lift
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


def test_firmware_parser_rejects_old_94_byte_frame() -> None:
    """A frame at a non-current wire size must NOT decode against the
    current parser.  Catches the regression where a stale builder is
    paired with new firmware.  (Test name still says "94"; the actual
    test produces FRAME_LEN+20 = 97 bytes against the v4.23 77-byte
    parser, which is still a non-current size.)
    """
    if not PARSE_BIN.exists():
        print("    SKIP  parse_frame binary not built; run `pio run -e native` first")
        return

    # Pad an otherwise-valid frame out by 20 bytes — the parser's size
    # check should reject it before it even starts parsing fields.
    wire = Frame().to_bytes()
    pad = wire[2:22]      # 20 bytes of payload-shaped filler
    extended = wire[:-2] + pad + wire[-2:]   # insert before CRLF
    assert len(extended) == FRAME_LEN + 20

    result = subprocess.run(
        [str(PARSE_BIN)],
        input=extended,
        capture_output=True,
        timeout=10,
    )
    assert result.returncode == 1, f"expected exit 1 on wrong size, got {result.returncode}"
    assert b"wrong_size" in result.stdout, f"expected 'wrong_size', got: {result.stdout!r}"


def main() -> int:
    tests = [
        # Layer 1: self-referential
        test_frame_length,
        test_frame_header,
        test_frame_crc_matches_firmware_convention,
        test_offsets_round_trip,
        test_negative_values_sign_preserved,
        test_compute_percent_lift_honest_formula,
        test_clamp_protects_against_out_of_range,
        test_nan_and_inf_dont_break,
        # Layer 2: firmware-parser interop
        test_firmware_parser_round_trip,
        test_firmware_parser_rejects_bad_crc,
        test_firmware_parser_rejects_old_94_byte_frame,
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
