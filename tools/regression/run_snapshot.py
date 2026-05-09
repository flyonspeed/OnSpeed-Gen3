#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["click"]
# ///
"""
run_snapshot.py — snapshot regression driver for onspeed_core.

Builds the host_main shim (via pio) and runs two golden checks:

  1. ahrs_tone golden — `host_main ahrs_tone` feeds a simplified sensor CSV
     (fixtures/short_replay.csv) through the AHRS + Madgwick + Kalman +
     ToneCalc pipeline and diffs against fixtures/golden.csv.  This is the
     bedrock regression test (per PLAN_PYTHON_CONSOLIDATION.md line 416-418):
     it must pass before any PR that touches onspeed_core can land.  Post-
     PLAN_WASM_CORE.md the same harness runs against the WASM build for the
     parity check.

  2. replay_engine golden — `host_main replay` feeds a real SD log CSV
     (fixtures/replay_engine_input.csv) through LogReplayEngine and diffs
     against fixtures/replay_engine_golden.csv.  This is the primary gate
     that PRs 2/3 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md will update.

Both checks must pass.  Neither short-circuits the other: all failures are
reported before the script exits non-zero.

Usage:
  ./run_snapshot.py                    # run both checks
  ./run_snapshot.py --update-golden    # regenerate BOTH goldens from current build
  ./run_snapshot.py --rtol 1e-4        # relative tolerance (default 1e-3)
  ./run_snapshot.py --atol 1e-3        # absolute tolerance (default 1e-4)

Tolerance model
---------------
Uses `math.isclose(a, b, rel_tol=rtol, abs_tol=atol)` — a match if EITHER the
relative or absolute difference is within tolerance.  Defaults (rtol=1e-3,
atol=1e-4) are chosen so that:

  * atol=1e-4 catches any change that shows up in the %.4f CSV output.
    Two values that round to the same 4-decimal string stay within atol.
  * rtol=1e-3 absorbs cross-platform FP nondeterminism (macOS libc++ vs Linux
    libstdc++ produce up to ~3e-4 relative drift on Madgwick/Kalman intermediate
    values when the harness feeds non-physical input data — see issue #204).
    Real math regressions show up at much larger magnitudes. Issue #204 covers
    the work to fix the input data so this tolerance can tighten back to 1e-5.

An absolute-only tolerance breaks for both extremes: too loose on small values,
too tight on large ones. `math.isclose` gets both right.

Exits:
  0 — all checks pass
  1 — build failed
  2 — output differs from golden (one or both checks)
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

# AHRS + ToneCalc golden — the bedrock regression test.
AHRS_INPUT  = FIXTURES / "short_replay.csv"
AHRS_GOLDEN = FIXTURES / "golden.csv"

# LogReplayEngine golden — the primary snapshot gate for LogReplayEngine.
ENGINE_INPUT  = FIXTURES / "replay_engine_input.csv"
ENGINE_GOLDEN = FIXTURES / "replay_engine_golden.csv"

# Synth ADC golden — streaming synth flapsRawADC for old logs (PR 3).
# Input has no flapsRawADC column; the engine synthesises a smoothstep
# sweep across the detent transition at row 51 (0→30 degrees).
SYNTH_ADC_INPUT  = FIXTURES / "synth_adc_input.csv"
SYNTH_ADC_GOLDEN = FIXTURES / "synth_adc_golden.csv"
SYNTH_ADC_CONFIG = FIXTURES / "synth_adc_config.cfg"

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


def run_ahrs_tone(input_csv: Path) -> str:
    """Run `host_main ahrs_tone` on the simplified sensor CSV, return stdout.

    The `ahrs_tone` subcommand runs the AHRS + Madgwick + Kalman + ToneCalc
    pipeline against the simplified fixture format (short_replay.csv).
    Gates against fixtures/golden.csv — the bedrock regression test.
    """
    proc = subprocess.run(
        [str(EXECUTABLE), "ahrs_tone", "--input", str(input_csv),
         "--output-format", "csv"],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        click.echo(f"Shim (ahrs_tone) exited with {proc.returncode}:", err=True)
        click.echo(proc.stderr, err=True)
        sys.exit(1)
    return proc.stdout


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
        click.echo(f"Shim (replay) exited with {proc.returncode}:", err=True)
        click.echo(proc.stderr, err=True)
        sys.exit(1)
    return proc.stdout


def run_synth_adc(input_csv: Path, config: Path) -> str:
    """Run `host_main replay` on a pre-PR-#221 log (no flapsRawADC column).

    Uses --config to supply a two-detent config with meaningful pot positions
    so the smoothstep synth produces visible transitions in the golden.
    The engine detects the missing column and synthesises flapsRawADC via the
    streaming bounded-window design (PR 3 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md).
    """
    proc = subprocess.run(
        [str(EXECUTABLE), "replay",
         "--input", str(input_csv),
         "--config", str(config),
         "--output-format", "csv"],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        click.echo(f"Shim (synth_adc replay) exited with {proc.returncode}:", err=True)
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
    help="Regenerate BOTH golden CSVs (golden.csv and replay_engine_golden.csv) "
         "from the current build.",
)
@click.option(
    "--rtol",
    type=float,
    default=1e-3,
    help="Relative tolerance for float comparison (default 1e-3 — needs to absorb "
         "up to ~3e-4 cross-platform FP nondeterminism between macOS libc++ and "
         "Linux libstdc++ in the cos/sin/atan paths inside Madgwick + Kalman. "
         "Empirically observed: a non-saturated flight-path value of 35.67° drifted "
         "by 0.009° across platforms (~2.5e-4 relative). 1e-3 gives ~3× margin. "
         "Real math regressions show up as much larger relative changes (wrong sign, "
         "wrong order of magnitude) and are still caught. "
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

    # --- ahrs_tone golden (bedrock AHRS+ToneCalc regression) ---
    if not AHRS_INPUT.exists():
        click.echo(f"AHRS input CSV not found: {AHRS_INPUT}", err=True)
        sys.exit(3)

    ahrs_tone_output = run_ahrs_tone(AHRS_INPUT)

    if update_golden:
        AHRS_GOLDEN.parent.mkdir(parents=True, exist_ok=True)
        AHRS_GOLDEN.write_text(ahrs_tone_output, encoding="utf-8")
        click.echo(f"Updated golden: {AHRS_GOLDEN}")

    # --- LogReplayEngine golden ---
    if not ENGINE_INPUT.exists():
        click.echo(f"Engine input CSV not found: {ENGINE_INPUT}", err=True)
        sys.exit(3)

    engine_output = run_engine_replay(ENGINE_INPUT)

    # --- Synth ADC golden ---
    if not SYNTH_ADC_INPUT.exists():
        click.echo(f"Synth ADC input CSV not found: {SYNTH_ADC_INPUT}", err=True)
        sys.exit(3)
    if not SYNTH_ADC_CONFIG.exists():
        click.echo(f"Synth ADC config not found: {SYNTH_ADC_CONFIG}", err=True)
        sys.exit(3)

    synth_adc_output = run_synth_adc(SYNTH_ADC_INPUT, SYNTH_ADC_CONFIG)

    if update_golden:
        ENGINE_GOLDEN.parent.mkdir(parents=True, exist_ok=True)
        ENGINE_GOLDEN.write_text(engine_output, encoding="utf-8")
        click.echo(f"Updated golden: {ENGINE_GOLDEN}")

        SYNTH_ADC_GOLDEN.parent.mkdir(parents=True, exist_ok=True)
        SYNTH_ADC_GOLDEN.write_text(synth_adc_output, encoding="utf-8")
        click.echo(f"Updated golden: {SYNTH_ADC_GOLDEN}")
        sys.exit(0)

    # Run all three checks; collect results without short-circuiting.
    ok_ahrs      = check_golden("ahrs_tone",     ahrs_tone_output, AHRS_GOLDEN,      rtol, atol)
    ok_engine    = check_golden("replay_engine", engine_output,    ENGINE_GOLDEN,    rtol, atol)
    ok_synth_adc = check_golden("synth_adc",     synth_adc_output, SYNTH_ADC_GOLDEN, rtol, atol)

    sys.exit(0 if (ok_ahrs and ok_engine and ok_synth_adc) else 2)


if __name__ == "__main__":
    main()
