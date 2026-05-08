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

from replay import (
    Frame, FlapSetpoints, FRAME_LEN, PAYLOAD_LEN,
    _float_or, _float_or_nan_for_air_data, _int_or,
    compute_percent_lift, csv_frame_stream,
)

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
        percent_lift_pct=4.2,
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
    # Endpoints (whole-percent float, [0.0, 99.9])
    assert compute_percent_lift(fs.alpha_0, fs) == 0.0
    assert abs(compute_percent_lift(fs.alpha_stall, fs) - 99.9) < 0.05  # clamped
    # L/Dmax body angle 2.0 -> (4.5/13.5)*100 = 33.33%.  Not 50.
    ldmax_pct = compute_percent_lift(fs.ldmax_aoa, fs)
    assert 32.0 < ldmax_pct < 34.0, f"unexpected ldmax pct {ldmax_pct}"
    # OnSpeedSlow 7.5 -> (10/13.5)*100 = 74.07%.  Not 66.
    slow_pct = compute_percent_lift(fs.onspeed_slow_aoa, fs)
    assert 73.0 < slow_pct < 75.0, f"unexpected slow pct {slow_pct}"


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
        percent_lift_pct=4.2,
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
    # parse_frame.cpp prints `percentLiftPct=<float>` in whole-percent
    # units (e.g. 4.2000) — round-trip fidelity is the wire's tenths
    # resolution, so a 0.05% tolerance comfortably catches any silent
    # truncation regression.
    close("percentLiftPct",    f.percent_lift_pct,   0.05)
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


# ---------------------------------------------------------------------------
# Format-3 CSV gated-column handling
# ---------------------------------------------------------------------------


def test_float_or_nan_for_air_data_empty_yields_nan() -> None:
    """Empty cell on a gated air-data column → NaN, not 0.0."""
    assert math.isnan(_float_or_nan_for_air_data(""))


def test_float_or_nan_for_air_data_missing_key_yields_zero() -> None:
    """A column missing from the header (old-format log that never
    carried it) returns 0.0 — that's a different signal than a
    producer-gated empty cell on a v3 row.
    """
    assert _float_or_nan_for_air_data(None) == 0.0


def test_float_or_nan_for_air_data_numeric_unchanged() -> None:
    """v2 (all-numeric) rows parse identically."""
    assert _float_or_nan_for_air_data("73.1") == 73.1


def test_float_or_strict_path_unaffected() -> None:
    """Pin that the non-gated `_float_or` keeps its strict default-on-
    empty contract.  Truncation detection on Palt / Pitch / VerticalG
    must not regress.
    """
    assert _float_or("", 0.0) == 0.0
    assert _float_or(None, 0.0) == 0.0
    assert _float_or("3.14", 0.0) == 3.14


def test_csv_frame_stream_v3_at_rest_does_not_emit_ias_zero() -> None:
    """End-to-end replay of a v3-shaped CSV with empty IAS / AngleofAttack
    cells.  The CSV produces NaN IAS / NaN AOA on those rows; the Frame
    builder pairs the NaN with `ias_valid=False`, which `to_bytes()`
    projects as the 9999 IAS-invalid wire sentinel (matching
    `kIasInvalidWireSentinel` in onspeed_core/proto/DisplaySerial.h).
    The M5 parser detects 9999 and flips `iasIsValid=false`, which the
    M5 firmware uses to render dashes for IAS and percentLift.  The
    percentLift field still encodes as 000 (NaN AOA → NaN percent-lift
    → `_clamp_uint` lower-bound), but consumers gate on `iasIsValid`
    rather than treating 0 as a sentinel.
    """
    csv_text = (
        "timeStamp,Pitch,Roll,IAS,Palt,YawRate,LateralG,VerticalG,"
        "AngleofAttack,VSI,OAT,FlightPath,flapsPos,DataMark\n"
    )
    for i in range(20):
        ts = i * 50  # 20 Hz
        # Empty IAS and AngleofAttack cells, everything else numeric.
        csv_text += (
            f"{ts},2.0,0.0,,2500,0.0,0.0,1.0,,0.0,15,0.0,0,0\n"
        )

    setpoints = {
        0: FlapSetpoints(
            degrees=0, pot_value=129,
            alpha_0=-2.5, ldmax_aoa=2.0,
            onspeed_fast_aoa=5.0, onspeed_slow_aoa=7.5,
            stallwarn_aoa=9.5, alpha_stall=11.0,
        )
    }

    import tempfile
    from pathlib import Path
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        log_path = Path(f.name)

    try:
        frames = list(csv_frame_stream(log_path, setpoints, skip_seconds=0.0))
    finally:
        log_path.unlink()

    assert len(frames) > 0, "expected at least one frame"
    for fr in frames:
        # NaN propagates from the CSV all the way to the Frame fields,
        # and the row builder pairs the NaN IAS with `ias_valid=False`.
        # The Python-side Frame keeps the NaN so callers that
        # introspect frames before serialization see the gap.
        assert math.isnan(fr.ias_kts), (
            f"v3 at-rest: expected NaN ias_kts, got {fr.ias_kts}"
        )
        assert fr.ias_valid is False, (
            f"v3 at-rest: NaN IAS must propagate as ias_valid=False, "
            f"got ias_valid={fr.ias_valid}"
        )
        # C++ contract: NaN AOA → compute_percent_lift returns 0.0
        # (ComputePercentLift sees a non-finite fraction and clamps to 0.0f).
        # Air-data validity is signalled via ias_valid / the 9999 wire
        # sentinel, not via a NaN percent_lift value.
        assert fr.percent_lift_pct == 0.0, (
            f"v3 at-rest: expected percent_lift_pct == 0.0 (C++ NaN-AOA "
            f"contract), got {fr.percent_lift_pct}"
        )
        # to_bytes() projects `ias_valid=False` to the 9999 wire
        # sentinel in the iasKt %04u field; the M5 parser detects that
        # exact value and flips iasIsValid=false to render dashes.
        # The percent-lift field clamps NaN to 000 — consumers gate on
        # iasIsValid, not on the percent-lift bytes.
        wire = fr.to_bytes()
        assert len(wire) == FRAME_LEN
        s = wire[:PAYLOAD_LEN].decode("ascii")
        assert s[11:15] == "9999", (
            f"IAS field should carry the 9999 invalid-wire sentinel, "
            f"got {s[11:15]!r}"
        )
        assert s[32:35] == "000", (
            f"percent_lift field clamps NaN to 000, got {s[32:35]!r}"
        )


def test_csv_frame_stream_v2_in_flight_unchanged() -> None:
    """v2-shaped CSV (all-numeric IAS/AOA) parses identically to before.
    Pins back-compat: existing flight logs replay the same way.
    """
    csv_text = (
        "timeStamp,Pitch,Roll,IAS,Palt,YawRate,LateralG,VerticalG,"
        "AngleofAttack,VSI,OAT,FlightPath,flapsPos,DataMark\n"
    )
    for i in range(20):
        ts = i * 50
        csv_text += (
            f"{ts},2.0,0.0,73.1,2500,0.0,0.0,1.0,8.0,0.0,15,0.0,0,0\n"
        )

    setpoints = {
        0: FlapSetpoints(
            degrees=0, pot_value=129,
            alpha_0=-2.5, ldmax_aoa=2.0,
            onspeed_fast_aoa=5.0, onspeed_slow_aoa=7.5,
            stallwarn_aoa=9.5, alpha_stall=11.0,
        )
    }

    import tempfile
    from pathlib import Path
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        log_path = Path(f.name)

    try:
        frames = list(csv_frame_stream(log_path, setpoints, skip_seconds=0.0))
    finally:
        log_path.unlink()

    assert len(frames) > 0
    for fr in frames:
        assert fr.ias_kts == 73.1
        # AOA=8.0 → (10.5 / 13.5) * 100 ≈ 77.78% on RV-10 clean.
        assert 77.0 < fr.percent_lift_pct < 79.0


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
        # Format-3 CSV gated-column handling
        test_float_or_nan_for_air_data_empty_yields_nan,
        test_float_or_nan_for_air_data_missing_key_yields_zero,
        test_float_or_nan_for_air_data_numeric_unchanged,
        test_float_or_strict_path_unaffected,
        test_csv_frame_stream_v3_at_rest_does_not_emit_ias_zero,
        test_csv_frame_stream_v2_in_flight_unchanged,
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
