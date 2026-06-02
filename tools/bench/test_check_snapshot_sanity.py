#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# ///
"""
Tests for check_snapshot_sanity.py — the analyzer's own regression suite,
mirroring test_check_atomic_publish.py. Guards the torn-read detection logic
so it can't silently rot (a broken detector that reports "OK" forever is worse
than no detector).

Asserts:
  * A clean signal (smooth, within bounds) → 0 flags, exit 0.
  * An injected one-sample out-and-back spike on a snapshot column → flagged,
    exit 1.
  * The known blind spot is real: a non-reverting STEP change → NOT flagged
    (documents the heuristic's limit so a future "fix" doesn't silently change
    semantics without updating this test).
  * A real fast transient that does NOT revert in one sample → not flagged.
  * Missing file → exit 2; header with no recognized columns → exit 2.

Run: `uv run tools/bench/test_check_snapshot_sanity.py`
"""
import importlib.util
import pathlib
import sys
import tempfile

_HERE = pathlib.Path(__file__).parent
_SPEC = importlib.util.spec_from_file_location(
    "check_snapshot_sanity", str(_HERE / "check_snapshot_sanity.py"))
assert _SPEC is not None and _SPEC.loader is not None
css = importlib.util.module_from_spec(_SPEC)
sys.modules["check_snapshot_sanity"] = css
_SPEC.loader.exec_module(css)


class TestFailure(Exception):
    pass


def _run_csv(text):
    """Write `text` to a temp CSV, run main(), return its exit code."""
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False, newline="") as f:
        f.write(text)
        path = f.name
    return css.main(path)


def expect(cond, msg):
    if not cond:
        raise TestFailure(msg)


def test_clean_signal_passes():
    # VerticalG hovering at 1g with tiny noise — no flags.
    csv = "timeStampUs,VerticalG\n" + "".join(
        f"{i},{1.0 + (0.01 if i % 2 else -0.01):.3f}\n" for i in range(20))
    expect(_run_csv(csv) == 0, "clean signal should exit 0")


def test_one_sample_revert_is_flagged():
    # 1.0, 1.0, 9.5, 1.0, 1.0 — classic torn-read fingerprint on VerticalG
    # (bound 6.0): spikes out then reverts, neighbors agree.
    csv = "timeStampUs,VerticalG\n0,1.0\n1,1.0\n2,9.5\n3,1.0\n4,1.0\n"
    expect(_run_csv(csv) == 1, "one-sample out-and-back spike should be flagged (exit 1)")


def test_non_reverting_step_is_blind_spot():
    # A genuine step that PERSISTS is the documented blind spot: a tear that
    # doesn't revert in one sample is NOT flagged. This test pins that limit
    # so a future change to the heuristic must consciously update it.
    csv = "timeStampUs,Pitch\n0,2.0\n1,2.0\n2,40.0\n3,40.0\n4,40.0\n"
    expect(_run_csv(csv) == 0, "non-reverting step is the known blind spot — not flagged")


def test_within_bound_transient_not_flagged():
    # A real fast-but-bounded transient (RollRate ramp within the 400 dps
    # one-sample bound) must not trip the detector.
    csv = "timeStampUs,RollRate\n0,0\n1,50\n2,120\n3,200\n4,150\n5,80\n"
    expect(_run_csv(csv) == 0, "bounded transient should not be flagged")


def test_missing_file_exits_2():
    expect(css.main("/tmp/definitely-not-a-real-log-xyz.csv") == 2, "missing file → exit 2")


def test_no_recognized_columns_exits_2():
    csv = "timeStampUs,SomeUnrelatedColumn\n0,1\n1,2\n2,3\n3,4\n"
    expect(_run_csv(csv) == 2, "header with no snapshot columns → exit 2")


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]
    failures = 0
    for t in tests:
        try:
            t()
            print(f"  PASS {t.__name__}")
        except TestFailure as e:
            failures += 1
            print(f"  FAIL {t.__name__}: {e}")
    print(f"\n{len(tests) - failures}/{len(tests)} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
