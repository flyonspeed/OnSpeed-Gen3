#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["click"]
# ///
"""
run_snapshot.py — snapshot regression driver for onspeed_core.

Builds the host_main shim (via pio) and runs two golden checks:

  1. LogReplayEngine golden — `host_main replay` feeds a real SD log CSV
     (fixtures/replay_engine_input.csv) through LogReplayEngine and diffs
     against fixtures/replay_engine_golden.csv.  This is the primary gate
     that PRs 2/3 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md will update.

  2. (Legacy) The old fixtures/short_replay.csv and golden.csv are retained
     for reference but are no longer fed through `replay` — that subcommand
     now requires the real SD log format.  golden.csv is kept in the repo
     as a historical artefact of the AHRS+ToneCalc inline path that
     existed before PR #470 landed LogReplayEngine.

Usage:
  ./run_snapshot.py                    # run both checks
  ./run_snapshot.py --update-golden    # regenerate replay_engine_golden.csv
  ./run_snapshot.py --rtol 1e-4        # relative tolerance (default 1e-3)
  ./run_snapshot.py --atol 1e-3        # absolute tolerance (default 1e-4)

Tolerance model
---------------
Uses `math.isclose(a, b, rel_tol=rtol, abs_tol=atol)` — matches if EITHER the
relative or the absolute difference is within tolerance.  Defaults (rtol=1e-3,
atol=1e-4) are chosen so that:

  * atol=1e-4 catches any change that shows up in the %.4f CSV output.
    Two values that round to the same 4-decimal string stay within atol.
  * rtol=1e-3 absorbs cross-platform FP nondeterminism.

  LogReplayEngine's output is dominated by pass-through fields (pitch, roll,
  IMU axes — stored directly from the log row, no trig) and a simple
  pressure-coefficient ratio.  FP nondeterminism is much smaller than for the
  AHRS path, so rtol=1e-3 gives generous headroom.

Exits:
  0 — all checks pass
  1 — build failed
  2 — output differs from golden
  3 — missing input / golden files
"""

from __future__ import annotations

import csv
import math
import subprocess
import sys
from itertools import zip_longest
from pathlib import Path

import click

HERE = Path(__file__).resolve().parent
FIXTURES = HERE / "fixtures"

# LogReplayEngine golden — the primary snapshot gate.
ENGINE_INPUT  = FIXTURES / "replay_engine_input.csv"
ENGINE_GOLDEN = FIXTURES / "replay_engine_golden.csv"

BUILD_DIR  = HERE / ".pio" / "build" / "native"
EXECUTABLE = BUILD_DIR / "program"


def build_shim() -> None:
    """Rebuild the host_main shim. Fails fast on build errors."""
    click.echo("Building host_main shim...")
    result = subprocess.run(
        ["pio", "run", "-e", "native"],
        cwd=HERE,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        click.echo(result.stdout)
        click.echo(result.stderr, err=True)
        sys.exit(1)


def run_engine_replay(input_csv: Path) -> str:
    """Run `host_main replay` on a real SD log CSV, return stdout.

    The `replay` subcommand uses LogReplayEngine and requires the real
    SD log format (timeStamp,Pfwd,...,DerivedAOA,CoeffP) — not the
    simplified AHRS fixture format (short_replay.csv).
    """
    proc = subprocess.run(
        [str(EXECUTABLE), "replay", "--input", str(input_csv),
         "--output-format", "csv"],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        click.echo(f"Shim exited with {proc.returncode}:", err=True)
        click.echo(proc.stderr, err=True)
        sys.exit(1)
    return proc.stdout


def read_csv_rows(text: str) -> tuple[list[str], list[list[str]]]:
    """Return (header, rows) from CSV text."""
    reader = csv.reader(text.strip().splitlines())
    header = next(reader)
    rows = list(reader)
    return header, rows


def float_equal(a: str, b: str, rtol: float, atol: float) -> bool:
    """Return True if `a` and `b` parse as floats that are close per rtol+atol,
    or (if either fails to parse as a float) if they're string-equal.

    Uses math.isclose semantics: match if either the relative or absolute
    difference is within tolerance. This is correct for both small values
    (where absolute tolerance dominates) and large values (where relative
    tolerance dominates).
    """
    try:
        return math.isclose(float(a), float(b), rel_tol=rtol, abs_tol=atol)
    except ValueError:
        return a == b


def diff_rows(
    golden: list[list[str]],
    actual: list[list[str]],
    rtol: float,
    atol: float,
) -> list[tuple[int, list[str], list[str]]]:
    """Return a list of (row_index, golden_row, actual_row) tuples for mismatches.

    Uses zip_longest so that rows present in one list but missing in the other
    are reported individually rather than silently truncated.
    """
    diffs = []
    for i, (g, a) in enumerate(zip_longest(golden, actual, fillvalue=None)):
        if g is None:
            diffs.append((i, ["<missing>"], a))
            continue
        if a is None:
            diffs.append((i, g, ["<missing>"]))
            continue
        if len(g) != len(a):
            diffs.append((i, g, a))
            continue
        if not all(float_equal(g[j], a[j], rtol, atol) for j in range(len(g))):
            diffs.append((i, g, a))
    return diffs


def check_golden(
    label: str,
    actual_text: str,
    golden_path: Path,
    rtol: float,
    atol: float,
) -> bool:
    """Diff actual_text against the committed golden.

    Returns True if they match (within tolerance), False on any mismatch.
    Prints a detailed diff on failure.  The caller decides whether to exit.
    """
    if not golden_path.exists():
        click.echo(
            f"Golden not found at {golden_path}. Run with --update-golden first.",
            err=True,
        )
        sys.exit(3)

    golden_text = golden_path.read_text(encoding="utf-8")
    g_header, g_rows = read_csv_rows(golden_text)
    a_header, a_rows = read_csv_rows(actual_text)

    if g_header != a_header:
        click.echo(
            f"[{label}] Header mismatch:\n"
            f"  golden: {g_header}\n  actual: {a_header}",
            err=True,
        )
        return False

    diffs = diff_rows(g_rows, a_rows, rtol, atol)
    if diffs:
        click.echo(
            f"[{label}] {len(diffs)} row(s) differ from golden "
            f"(rtol={rtol}, atol={atol}):",
            err=True,
        )
        for idx, g, a in diffs[:10]:
            click.echo(f"  row {idx}:\n    golden: {g}\n    actual: {a}", err=True)
        if len(diffs) > 10:
            click.echo(f"  ... and {len(diffs) - 10} more", err=True)
        return False

    click.echo(
        f"[{label}] {len(a_rows)} rows match golden (rtol={rtol}, atol={atol})"
    )
    return True


@click.command()
@click.option(
    "--update-golden",
    is_flag=True,
    help="Regenerate replay_engine_golden.csv from the current build.",
)
@click.option(
    "--rtol",
    type=float,
    default=1e-3,
    help="Relative tolerance for float comparison (default 1e-3).",
)
@click.option(
    "--atol",
    type=float,
    default=1e-4,
    help="Absolute tolerance for float comparison (default 1e-4, matches %.4f wire precision).",
)
@click.option(
    "--no-build",
    is_flag=True,
    help="Skip rebuild — use the existing shim binary.",
)
def main(
    update_golden: bool,
    rtol: float,
    atol: float,
    no_build: bool,
) -> None:
    if not no_build:
        build_shim()

    if not EXECUTABLE.exists():
        click.echo(f"Executable not found at {EXECUTABLE}", err=True)
        sys.exit(3)

    # --- LogReplayEngine golden ---
    if not ENGINE_INPUT.exists():
        click.echo(f"Engine input CSV not found: {ENGINE_INPUT}", err=True)
        sys.exit(3)

    engine_output = run_engine_replay(ENGINE_INPUT)

    if update_golden:
        ENGINE_GOLDEN.parent.mkdir(parents=True, exist_ok=True)
        ENGINE_GOLDEN.write_text(engine_output, encoding="utf-8")
        click.echo(f"Updated golden: {ENGINE_GOLDEN}")
        sys.exit(0)

    ok = check_golden("replay_engine", engine_output, ENGINE_GOLDEN, rtol, atol)
    sys.exit(0 if ok else 2)


if __name__ == "__main__":
    main()
