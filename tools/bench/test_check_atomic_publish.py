#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# ///
"""
Tests for check-atomic-publish.py.

Captures the regression class from log_098 (1.6% of stim rows had
Pitch/Roll from frame N + Lat/Lon from frame N-1 because the dual-hit
publish path took the mutex twice).  These tests assert:

  * A clean row (all fields encoded from the same N) produces zero tears.
  * A torn row (Pitch/Roll one frame ahead of Lat/Lon/tNs) produces tears
    on exactly the Pitch and Roll fields.
  * The analyzer ignores pre-stim rows (vnTimeStartupNs == 0).

Run: `python3 tools/bench/test_check_atomic_publish.py`.

The test file imports check-atomic-publish.py as a module.  Python doesn't
let you import a filename with a hyphen, so we use importlib at the top.
"""

import importlib.util
import pathlib
import sys


# Load check-atomic-publish.py as a module (its filename has a hyphen).
_HERE = pathlib.Path(__file__).parent
_SPEC = importlib.util.spec_from_file_location(
    "check_atomic_publish",
    str(_HERE / "check-atomic-publish.py"))
assert _SPEC is not None and _SPEC.loader is not None
cap = importlib.util.module_from_spec(_SPEC)
sys.modules["check_atomic_publish"] = cap  # required for @dataclass on Python 3.13
_SPEC.loader.exec_module(cap)


# Light-weight test runner — same pattern as other Python tests in the
# repo (no pytest dependency).

class TestFailure(Exception):
    pass


def assert_eq(label: str, actual, expected) -> None:
    if actual != expected:
        raise TestFailure(f"{label}: expected {expected!r}, got {actual!r}")


def assert_in(label: str, needle, haystack) -> None:
    if needle not in haystack:
        raise TestFailure(f"{label}: expected {needle!r} in {haystack!r}")


# -----------------------------------------------------------------------------

def test_clean_row_produces_zero_tears() -> None:
    """The canonical-clean encoding must produce 0 tears."""
    for N in [1, 100, 787, 1_000, 50_000, 99_999]:
        row = cap.encode_row_for_N(N)
        had_data, tears = cap.check_row(0, row)
        assert_eq(f"N={N} had_data", had_data, True)
        assert_eq(f"N={N} tear count", len(tears), 0)


def test_pitch_one_frame_skew_is_caught() -> None:
    """Reproduce the log_098 bug: Pitch/Roll from N+1, Lat/Lon/tNs from N.

    The analyzer derives the canonical N from vnTimeStartupNs.  If Pitch
    encodes (N+1) but tNs encodes N, the analyzer should flag a tear on
    vnPitch (and vnRoll, since it's a different multiplier of the same
    N+1).
    """
    N = 787
    # Build a row where Lat/Lon/tNs say N=787 but Pitch/Roll/Yaw say N=788.
    row = cap.encode_row_for_N(N)
    next_frame = cap.encode_row_for_N(N + 1)
    row["vnPitch"] = next_frame["vnPitch"]
    row["vnRoll"]  = next_frame["vnRoll"]
    row["vnYaw"]   = next_frame["vnYaw"]

    had_data, tears = cap.check_row(0, row)
    assert_eq("had_data", had_data, True)

    torn_fields = sorted(t.field for t in tears)
    # All three attitude fields are off-by-one frame from the canonical N
    # derived from tNs.  Yaw's step is 1 per N (within tolerance ±1), so
    # the analyzer correctly catches it ONLY when the gap > tolerance.
    # Pitch step is 7, Roll step is 13 — both well above the ±1 tolerance.
    assert_in("vnPitch tear", "vnPitch", torn_fields)
    assert_in("vnRoll tear",  "vnRoll",  torn_fields)


def test_latlon_multi_frame_skew_is_caught() -> None:
    """The mirror case: Lat/Lon say N+5, tNs/Pitch/Roll say N.

    Lat/Lon tolerance in the analyzer is ±2 frames (because the CSV emits
    %.6f and the encode step is 1e-6, so adjacent N values round to the
    same CSV bin and we want to allow that without false positives).  So
    we use a 5-frame skew here — well outside tolerance — to exercise
    the detection path.  A regression where the suVN300 publish wrote
    Lat/Lon but missed updating tNs in lockstep would show up at any
    skew > ±2 frames.
    """
    N = 1000
    row = cap.encode_row_for_N(N)
    skewed = cap.encode_row_for_N(N + 5)
    row["vnGnssLat"] = skewed["vnGnssLat"]
    row["vnGnssLon"] = skewed["vnGnssLon"]

    had_data, tears = cap.check_row(0, row)
    assert_eq("had_data", had_data, True)

    torn_fields = sorted(t.field for t in tears)
    assert_in("vnGnssLat tear", "vnGnssLat", torn_fields)
    assert_in("vnGnssLon tear", "vnGnssLon", torn_fields)


def test_pre_stim_row_is_ignored() -> None:
    """Rows with tNs=0 are pre-stim (firehose not started yet); skip."""
    row = {"vnTimeStartupNs": "0",
           "vnYaw":   "0.00",
           "vnPitch": "-30.00",
           "vnRoll":  "-30.00",
           "vnGnssLat": "0.000000",
           "vnGnssLon": "0.000000"}
    had_data, tears = cap.check_row(0, row)
    assert_eq("had_data (pre-stim)", had_data, False)
    assert_eq("tear count (pre-stim)", len(tears), 0)


def test_missing_columns_do_not_crash() -> None:
    """A row that lacks vnTimeStartupNs returns (False, [])."""
    row = {"vnYaw": "12.34"}  # missing tNs
    had_data, tears = cap.check_row(0, row)
    assert_eq("had_data (no tNs)", had_data, False)
    assert_eq("tear count (no tNs)", len(tears), 0)


def test_invalid_value_does_not_crash() -> None:
    """A row with a non-numeric Pitch value is silently skipped for that field."""
    N = 787
    row = cap.encode_row_for_N(N)
    row["vnPitch"] = "NaN-ish"   # not a float
    had_data, tears = cap.check_row(0, row)
    assert_eq("had_data (bad pitch)", had_data, True)
    # vnPitch should NOT show up in tears; other fields are still checked.
    torn_fields = [t.field for t in tears]
    if "vnPitch" in torn_fields:
        raise TestFailure(
            f"vnPitch must be skipped on bad value, but tears={torn_fields}")


# -----------------------------------------------------------------------------

ALL_TESTS = [
    test_clean_row_produces_zero_tears,
    test_pitch_one_frame_skew_is_caught,
    test_latlon_multi_frame_skew_is_caught,
    test_pre_stim_row_is_ignored,
    test_missing_columns_do_not_crash,
    test_invalid_value_does_not_crash,
]


def main() -> int:
    failures = []
    for fn in ALL_TESTS:
        try:
            fn()
            print(f"  PASS {fn.__name__}")
        except TestFailure as e:
            print(f"  FAIL {fn.__name__}: {e}")
            failures.append(fn.__name__)
        except Exception as e:
            print(f"  CRASH {fn.__name__}: {type(e).__name__}: {e}")
            failures.append(fn.__name__)
    print(f"\n{len(ALL_TESTS) - len(failures)}/{len(ALL_TESTS)} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
