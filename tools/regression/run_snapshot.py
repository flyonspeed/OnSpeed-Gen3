#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["click"]
# ///
"""
run_snapshot.py — snapshot regression driver for onspeed_core.

Builds the host_main shim (via pio), feeds it a CSV of sensor samples,
and diffs the output against a golden CSV committed to the repo. Used
locally before flashing and in CI on every PR that touches onspeed_core
or the sketch-side consumers.

Usage:
  ./run_snapshot.py                   # run against the default golden
  ./run_snapshot.py --update-golden   # regenerate the golden from the current build
  ./run_snapshot.py --input PATH      # use a different input CSV
  ./run_snapshot.py --rtol 1e-4       # relative tolerance (default 1e-5)
  ./run_snapshot.py --atol 1e-3       # absolute tolerance (default 1e-4)

Tolerance model
---------------
Uses `math.isclose(a, b, rel_tol=rtol, abs_tol=atol)` — matches if EITHER the
relative or the absolute difference is within tolerance. Defaults (rtol=1e-3,
atol=1e-4) are chosen so that:

  * atol=1e-4 catches any change that would show up in the %.4f CSV output.
    Two values that round to the same 4-decimal string stay within atol.
  * rtol=1e-3 absorbs cross-platform FP nondeterminism (macOS libc++ vs Linux
    libstdc++ produce up to ~3e-4 relative drift on Madgwick/Kalman intermediate
    values when the harness feeds non-physical input data — see issue #204).
    Real math regressions show up at much larger magnitudes. Issue #204 covers
    the work to fix the input data so this tolerance can tighten back to 1e-5.

An absolute-only tolerance breaks for both extremes: too loose on small values,
too tight on large ones. `math.isclose` gets both right.

Exits:
  0 — output matches the golden within tolerance
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
INPUT_DEFAULT = HERE / "fixtures" / "short_replay.csv"
GOLDEN_DEFAULT = HERE / "fixtures" / "golden.csv"
BUILD_DIR = HERE / ".pio" / "build" / "native"
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


def run_shim(input_csv: Path) -> str:
    """Run the shim's replay subcommand and return its stdout."""
    proc = subprocess.run(
        [str(EXECUTABLE), "replay", "--input", str(input_csv)],
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


@click.command()
@click.option(
    "--input",
    "input_csv",
    type=click.Path(exists=True, path_type=Path),
    default=INPUT_DEFAULT,
    help="Input CSV fed to the shim on stdin.",
)
@click.option(
    "--golden",
    "golden_csv",
    type=click.Path(path_type=Path),
    default=GOLDEN_DEFAULT,
    help="Golden CSV to diff against.",
)
@click.option(
    "--update-golden",
    is_flag=True,
    help="Regenerate the golden from the current build. Commit the result.",
)
@click.option(
    "--rtol",
    type=float,
    default=1e-3,
    help="Relative tolerance for float comparison (default 1e-3 — needs to absorb "
         "up to ~3e-4 cross-platform FP nondeterminism between macOS libc++ and "
         "Linux libstdc++ in the cos/sin/atan paths inside Madgwick + Kalman. "
         "Empirically observed on this PR: a non-saturated flight-path value of "
         "35.67° drifted by 0.009° across platforms (~2.5e-4 relative). 1e-3 gives "
         "us ~3× margin. Real math regressions show up as much larger relative "
         "changes (wrong sign, wrong order of magnitude) and are still caught. "
         "See issue #204 — once the harness drives physically-meaningful 208 Hz "
         "input values (instead of a 50 Hz log treated as 208 Hz), Kalman/Madgwick "
         "intermediate values stop saturating and the FP nondeterminism shrinks "
         "back to ~1e-6. Tighten rtol back to 1e-5 then.",
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
    input_csv: Path,
    golden_csv: Path,
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

    if not input_csv.exists():
        click.echo(f"Input CSV not found: {input_csv}", err=True)
        sys.exit(3)

    output = run_shim(input_csv)

    if update_golden:
        golden_csv.parent.mkdir(parents=True, exist_ok=True)
        golden_csv.write_text(output, encoding="utf-8")
        click.echo(f"Updated golden: {golden_csv}")
        sys.exit(0)

    if not golden_csv.exists():
        click.echo(
            f"Golden not found at {golden_csv}. Run with --update-golden first.",
            err=True,
        )
        sys.exit(3)

    golden_text = golden_csv.read_text(encoding="utf-8")

    g_header, g_rows = read_csv_rows(golden_text)
    a_header, a_rows = read_csv_rows(output)

    if g_header != a_header:
        click.echo(f"Header mismatch:\n  golden: {g_header}\n  actual: {a_header}", err=True)
        sys.exit(2)

    diffs = diff_rows(g_rows, a_rows, rtol, atol)
    if diffs:
        click.echo(
            f"✗ {len(diffs)} row(s) differ from golden (rtol={rtol}, atol={atol}):",
            err=True,
        )
        for idx, g, a in diffs[:10]:
            click.echo(f"  row {idx}:\n    golden: {g}\n    actual: {a}", err=True)
        if len(diffs) > 10:
            click.echo(f"  ... and {len(diffs) - 10} more", err=True)
        sys.exit(2)

    click.echo(
        f"✓ {len(a_rows)} rows match golden (rtol={rtol}, atol={atol})"
    )


if __name__ == "__main__":
    main()
