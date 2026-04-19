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
  ./run_snapshot.py --tolerance 1e-3  # float equality tolerance (default 1e-6)

Exits:
  0 — output matches the golden within tolerance
  1 — build failed
  2 — output differs from golden
  3 — missing input / golden files
"""

from __future__ import annotations

import csv
import subprocess
import sys
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
    """Pipe input_csv through the shim and return its stdout."""
    with input_csv.open("r") as f:
        proc = subprocess.run(
            [str(EXECUTABLE)],
            stdin=f,
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


def float_equal(a: str, b: str, tol: float) -> bool:
    try:
        return abs(float(a) - float(b)) <= tol
    except ValueError:
        return a == b


def diff_rows(
    golden: list[list[str]],
    actual: list[list[str]],
    tol: float,
) -> list[tuple[int, list[str], list[str]]]:
    """Return a list of (row_index, golden_row, actual_row) tuples for mismatches."""
    diffs = []
    for i, (g, a) in enumerate(zip(golden, actual)):
        if len(g) != len(a):
            diffs.append((i, g, a))
            continue
        if not all(float_equal(g[j], a[j], tol) for j in range(len(g))):
            diffs.append((i, g, a))
    if len(golden) != len(actual):
        diffs.append((-1, [f"len={len(golden)}"], [f"len={len(actual)}"]))
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
    "--tolerance",
    type=float,
    default=1e-6,
    help="Float equality tolerance (default 1e-6).",
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
    tolerance: float,
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
        golden_csv.write_text(output)
        click.echo(f"Updated golden: {golden_csv}")
        sys.exit(0)

    if not golden_csv.exists():
        click.echo(
            f"Golden not found at {golden_csv}. Run with --update-golden first.",
            err=True,
        )
        sys.exit(3)

    golden_text = golden_csv.read_text()

    g_header, g_rows = read_csv_rows(golden_text)
    a_header, a_rows = read_csv_rows(output)

    if g_header != a_header:
        click.echo(f"Header mismatch:\n  golden: {g_header}\n  actual: {a_header}", err=True)
        sys.exit(2)

    diffs = diff_rows(g_rows, a_rows, tolerance)
    if diffs:
        click.echo(f"✗ {len(diffs)} row(s) differ from golden (tolerance {tolerance}):", err=True)
        for idx, g, a in diffs[:10]:
            click.echo(f"  row {idx}:\n    golden: {g}\n    actual: {a}", err=True)
        if len(diffs) > 10:
            click.echo(f"  ... and {len(diffs) - 10} more", err=True)
        sys.exit(2)

    click.echo(f"✓ {len(a_rows)} rows match golden within tolerance {tolerance}")


if __name__ == "__main__":
    main()
