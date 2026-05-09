"""Integration tests for the host_main multi-subcommand CLI.

Each test runs the host_main binary as a subprocess and asserts the output
for a known input.  The binary must be built before running these tests:

    cd tools/regression && pio run -e native

or from the repo root:

    pio test -e native --without-testing && \
    python -m pytest test/test_host_main_cli/ -v

The binary path is resolved via HOST_MAIN_BIN (env var) or the default
PlatformIO output path used by tools/regression/run_snapshot.py.
"""

from __future__ import annotations

import json
import math
import os
import subprocess
import sys
from pathlib import Path

import pytest

# ---------------------------------------------------------------------------
# Fixture: binary path
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parents[2]
HOST_MAIN_DEFAULT = (
    REPO_ROOT / "tools" / "regression" / ".pio" / "build" / "native" / "program"
)
CONFIG_XML = REPO_ROOT / "test" / "fixtures" / "config_xml" / "n720ak_2_11_26.cfg"
CONFIG_V1  = REPO_ROOT / "test" / "fixtures" / "config_v1" / "sample_v1.cfg"
SHORT_REPLAY = REPO_ROOT / "tools" / "regression" / "fixtures" / "short_replay.csv"
GOLDEN_CSV   = REPO_ROOT / "tools" / "regression" / "fixtures" / "golden.csv"


def host_main_bin() -> Path:
    """Return the path to the host_main binary, checking that it exists."""
    p = Path(os.environ.get("HOST_MAIN_BIN", str(HOST_MAIN_DEFAULT)))
    if not p.exists():
        pytest.skip(
            f"host_main binary not found at {p}. "
            "Run: cd tools/regression && pio run -e native"
        )
    return p


def run(args: list[str], **kwargs) -> subprocess.CompletedProcess:
    """Run host_main with the given args, return the CompletedProcess."""
    return subprocess.run(
        [str(host_main_bin())] + args,
        capture_output=True,
        text=True,
        **kwargs,
    )


# ---------------------------------------------------------------------------
# Subcommand: help / no-args
# ---------------------------------------------------------------------------


def test_help_exits_zero():
    r = run(["help"])
    assert r.returncode == 0
    assert "replay" in r.stdout
    assert "percent_lift" in r.stdout


def test_no_args_exits_nonzero():
    r = run([])
    assert r.returncode != 0


def test_unknown_subcommand_exits_nonzero():
    r = run(["this_is_not_a_subcommand"])
    assert r.returncode != 0


# ---------------------------------------------------------------------------
# Subcommand: percent_lift
# ---------------------------------------------------------------------------


def test_percent_lift_basic():
    """RV-10-style calibration: alpha_0=-3.72, alpha_stall=10.31, stallwarn=8.24."""
    r = run([
        "percent_lift",
        "--aoa", "3.24",
        "--alpha-0", "-3.72",
        "--alpha-stall", "10.31",
        "--stallwarn", "8.24",
    ])
    assert r.returncode == 0
    val = float(r.stdout.strip())
    # (3.24 - (-3.72)) / (10.31 - (-3.72)) * 100 ≈ 49.6
    assert math.isclose(val, 49.6, abs_tol=0.2), f"expected ~49.6, got {val}"


def test_percent_lift_at_alpha0_returns_zero():
    """AOA at alpha_0 (zero-lift) → percent 0."""
    r = run([
        "percent_lift",
        "--aoa", "-3.72",
        "--alpha-0", "-3.72",
        "--alpha-stall", "10.31",
        "--stallwarn", "8.24",
    ])
    assert r.returncode == 0
    val = float(r.stdout.strip())
    assert math.isclose(val, 0.0, abs_tol=0.05), f"expected 0.0, got {val}"


def test_percent_lift_above_stall_clamped():
    """AOA above alpha_stall → clamped to 99.9."""
    r = run([
        "percent_lift",
        "--aoa", "20.0",
        "--alpha-0", "-3.72",
        "--alpha-stall", "10.31",
        "--stallwarn", "8.24",
    ])
    assert r.returncode == 0
    val = float(r.stdout.strip())
    assert val <= 99.9, f"expected <= 99.9, got {val}"
    assert val >= 99.8, f"expected near 99.9, got {val}"


def test_percent_lift_missing_arg_exits_nonzero():
    r = run(["percent_lift", "--aoa", "5.0"])
    assert r.returncode != 0


# ---------------------------------------------------------------------------
# Subcommand: parse_config
# ---------------------------------------------------------------------------


def test_parse_config_v2_valid_json():
    if not CONFIG_XML.exists():
        pytest.skip(f"config fixture not found: {CONFIG_XML}")
    r = run(["parse_config", "--in", str(CONFIG_XML)])
    assert r.returncode == 0
    data = json.loads(r.stdout)
    assert "flapsByDeg" in data
    assert "aoaSmoothing" in data


def test_parse_config_v2_flaps_present():
    if not CONFIG_XML.exists():
        pytest.skip(f"config fixture not found: {CONFIG_XML}")
    r = run(["parse_config", "--in", str(CONFIG_XML)])
    assert r.returncode == 0
    data = json.loads(r.stdout)
    flaps = data["flapsByDeg"]
    assert len(flaps) == 3, f"expected 3 flap entries, got {len(flaps)}"
    # The n720ak config has 0°, 16°, 33° detents
    assert "0" in flaps
    assert "16" in flaps
    assert "33" in flaps


def test_parse_config_v2_scalar_fields():
    if not CONFIG_XML.exists():
        pytest.skip(f"config fixture not found: {CONFIG_XML}")
    r = run(["parse_config", "--in", str(CONFIG_XML)])
    assert r.returncode == 0
    data = json.loads(r.stdout)
    assert data["muteUnderIas"] == 35
    assert data["efisType"] == "ADVANCED"
    assert data["serialOutFormat"] == "ONSPEED"


def test_parse_config_v1_valid_json():
    if not CONFIG_V1.exists():
        pytest.skip(f"V1 config fixture not found: {CONFIG_V1}")
    r = run(["parse_config", "--in", str(CONFIG_V1)])
    assert r.returncode == 0
    data = json.loads(r.stdout)
    assert "flapsByDeg" in data


def test_parse_config_missing_file_exits_nonzero():
    r = run(["parse_config", "--in", "/nonexistent/path/foo.cfg"])
    assert r.returncode != 0


def test_parse_config_missing_flag_exits_nonzero():
    r = run(["parse_config"])
    assert r.returncode != 0


# ---------------------------------------------------------------------------
# Subcommand: display_anchors
# ---------------------------------------------------------------------------


def test_display_anchors_basic():
    if not CONFIG_XML.exists():
        pytest.skip(f"config fixture not found: {CONFIG_XML}")
    # Flap index 0, raw ADC at the first detent's pot value (3908)
    r = run([
        "display_anchors",
        "--config", str(CONFIG_XML),
        "--flap", "0",
        "--raw-adc", "3908",
    ])
    assert r.returncode == 0
    data = json.loads(r.stdout)
    assert "pipPctLift" in data
    assert "tonesOnPctLift" in data
    assert "onSpeedFastPctLift" in data
    assert "onSpeedSlowPctLift" in data
    assert "stallWarnPctLift" in data
    assert "flapsDeg" in data


def test_display_anchors_returns_integers():
    if not CONFIG_XML.exists():
        pytest.skip(f"config fixture not found: {CONFIG_XML}")
    r = run([
        "display_anchors",
        "--config", str(CONFIG_XML),
        "--flap", "1",
        "--raw-adc", "2332",
    ])
    assert r.returncode == 0
    data = json.loads(r.stdout)
    for key in ("pipPctLift", "tonesOnPctLift", "onSpeedFastPctLift",
                "onSpeedSlowPctLift", "stallWarnPctLift", "flapsDeg"):
        assert isinstance(data[key], int), f"{key} should be int, got {type(data[key])}"


def test_display_anchors_flap_deg_for_detent0():
    if not CONFIG_XML.exists():
        pytest.skip(f"config fixture not found: {CONFIG_XML}")
    r = run([
        "display_anchors",
        "--config", str(CONFIG_XML),
        "--flap", "0",
        "--raw-adc", "3908",
    ])
    assert r.returncode == 0
    data = json.loads(r.stdout)
    assert data["flapsDeg"] == 0, f"expected 0 deg for detent 0, got {data['flapsDeg']}"


def test_display_anchors_missing_args_exits_nonzero():
    r = run(["display_anchors", "--config", "x.cfg", "--flap", "0"])
    assert r.returncode != 0


# ---------------------------------------------------------------------------
# Subcommand: build_frame
# ---------------------------------------------------------------------------


def test_build_frame_emits_hex():
    r = run([
        "build_frame",
        "--record",
        '{"pitchDeg": 2.5, "rollDeg": -1.0, "iasKt": 90.0, "iasValid": true, '
        '"paltFt": 3000.0, "percentLiftPct": 45.0}',
    ])
    assert r.returncode == 0
    hex_out = r.stdout.strip()
    assert len(hex_out) == 77 * 2, (
        f"expected {77 * 2} hex chars (77 bytes), got {len(hex_out)}: {hex_out!r}"
    )
    assert all(c in "0123456789abcdef" for c in hex_out), "non-hex chars in output"


def test_build_frame_magic_bytes():
    """First two bytes of the frame must be '#' '1' (0x23 0x31)."""
    r = run([
        "build_frame",
        "--record",
        "{}",
    ])
    assert r.returncode == 0
    hex_out = r.stdout.strip()
    assert hex_out[:4] == "2331", (
        f"expected '#1' (2331) at start, got {hex_out[:4]!r}"
    )


def test_build_frame_terminator():
    """Last two bytes must be CRLF (0x0d 0x0a)."""
    r = run(["build_frame", "--record", "{}"])
    assert r.returncode == 0
    hex_out = r.stdout.strip()
    assert hex_out[-4:] == "0d0a", (
        f"expected CRLF (0d0a) at end, got {hex_out[-4:]!r}"
    )


def test_build_frame_missing_arg_exits_nonzero():
    r = run(["build_frame"])
    assert r.returncode != 0


# ---------------------------------------------------------------------------
# Subcommand: replay (CSV mode — regression against golden)
# ---------------------------------------------------------------------------


def test_ahrs_tone_csv_matches_golden():
    """The ahrs_tone subcommand must produce output byte-identical to the golden CSV.
    (Was previously named test_replay_csv_matches_golden when the AHRS+ToneCalc
    pipeline was the `replay` subcommand. Post-PR-#476, that path is `ahrs_tone`;
    `replay` now drives LogReplayEngine on SD-log format.)"""
    if not SHORT_REPLAY.exists():
        pytest.skip(f"short_replay.csv not found: {SHORT_REPLAY}")
    if not GOLDEN_CSV.exists():
        pytest.skip(f"golden.csv not found: {GOLDEN_CSV}")

    r = run(["ahrs_tone", "--input", str(SHORT_REPLAY)])
    assert r.returncode == 0

    golden = GOLDEN_CSV.read_text(encoding="utf-8").strip().splitlines()
    actual = r.stdout.strip().splitlines()

    assert len(actual) == len(golden), (
        f"row count mismatch: golden={len(golden)}, actual={len(actual)}"
    )
    assert actual[0] == golden[0], f"header mismatch: {actual[0]!r} vs {golden[0]!r}"

    diffs = []
    for i, (g, a) in enumerate(zip(golden[1:], actual[1:]), start=1):
        g_fields = g.split(",")
        a_fields = a.split(",")
        if len(g_fields) != len(a_fields):
            diffs.append((i, g, a))
            continue
        for gf, af in zip(g_fields, a_fields):
            try:
                if not math.isclose(float(gf), float(af), rel_tol=1e-3, abs_tol=1e-4):
                    diffs.append((i, g, a))
                    break
            except ValueError:
                if gf != af:
                    diffs.append((i, g, a))
                    break

    if diffs:
        sample = diffs[:3]
        msg = f"{len(diffs)} row(s) differ from golden:\n"
        for idx, g, a in sample:
            msg += f"  row {idx}:\n    golden: {g}\n    actual: {a}\n"
        pytest.fail(msg)


def test_ahrs_tone_stdin_mode():
    """ahrs_tone --input - reads from stdin; should process the same data."""
    if not SHORT_REPLAY.exists():
        pytest.skip(f"short_replay.csv not found: {SHORT_REPLAY}")

    with SHORT_REPLAY.open("r", encoding="utf-8") as f:
        r = subprocess.run(
            [str(host_main_bin()), "ahrs_tone", "--input", "-"],
            stdin=f,
            capture_output=True,
            text=True,
        )
    assert r.returncode == 0
    lines = r.stdout.strip().splitlines()
    assert len(lines) > 1, "expected header + data rows"


def test_ahrs_tone_jsonl_output():
    if not SHORT_REPLAY.exists():
        pytest.skip(f"short_replay.csv not found: {SHORT_REPLAY}")
    r = run([
        "ahrs_tone",
        "--input", str(SHORT_REPLAY),
        "--output-format", "jsonl",
    ])
    assert r.returncode == 0
    lines = r.stdout.strip().splitlines()
    assert len(lines) > 0, "expected at least one JSONL row"
    # Each line must parse as valid JSON
    for line in lines:
        obj = json.loads(line)
        assert "ias_kt" in obj
        assert "pitch_deg" in obj
        assert "tone_level" in obj


def test_ahrs_tone_jsonl_same_row_count_as_csv():
    """JSONL mode must produce the same number of rows as CSV mode."""
    if not SHORT_REPLAY.exists():
        pytest.skip(f"short_replay.csv not found: {SHORT_REPLAY}")

    csv_r = run(["ahrs_tone", "--input", str(SHORT_REPLAY)])
    assert csv_r.returncode == 0
    csv_rows = csv_r.stdout.strip().splitlines()[1:]  # skip header

    jl_r = run(["ahrs_tone", "--input", str(SHORT_REPLAY), "--output-format", "jsonl"])
    assert jl_r.returncode == 0
    jl_rows = jl_r.stdout.strip().splitlines()

    assert len(csv_rows) == len(jl_rows), (
        f"CSV rows={len(csv_rows)}, JSONL rows={len(jl_rows)}"
    )


def test_replay_unknown_format_exits_nonzero():
    if not SHORT_REPLAY.exists():
        pytest.skip(f"short_replay.csv not found: {SHORT_REPLAY}")
    r = run([
        "replay",
        "--input", str(SHORT_REPLAY),
        "--output-format", "parquet",
    ])
    assert r.returncode != 0


def test_replay_rejects_config_flag(tmp_path):
    """--config is reserved for Step 2; must error rather than silently ignore."""
    if not SHORT_REPLAY.exists():
        pytest.skip(f"short_replay.csv not found: {SHORT_REPLAY}")
    cfg = tmp_path / "x.cfg"
    cfg.write_text("<CONFIG2></CONFIG2>")
    r = run([
        "replay",
        "--input", str(SHORT_REPLAY),
        "--config", str(cfg),
    ])
    assert r.returncode != 0, (
        "replay --config should exit non-zero (reserved for Step 2)"
    )
    assert "Step 2" in r.stderr or "reserved" in r.stderr.lower(), (
        f"expected 'Step 2' or 'reserved' in stderr, got: {r.stderr!r}"
    )


def test_build_frame_json_parser_flat_numeric_only():
    """Pin the contract: JsonGetValue uses substring search, so the JSON input
    for build_frame must not contain string-typed fields.  This test verifies
    a minimal flat numeric object is parsed correctly; if a future PR adds
    string fields to DisplayBuildInputs the parser must be revisited first.
    See PR #465 review (M3)."""
    # Happy path with the full set of numeric fields that build_frame accepts.
    r = run([
        "build_frame",
        "--record",
        '{"pitchDeg": 2.5, "rollDeg": -1.0, "iasKt": 90.0, "iasValid": true, '
        '"paltFt": 3000.0, "percentLiftPct": 45.0}',
    ])
    assert r.returncode == 0
    hex_out = r.stdout.strip()
    assert len(hex_out) == 77 * 2, (
        f"expected {77 * 2} hex chars, got {len(hex_out)}: {hex_out!r}"
    )


# ---------------------------------------------------------------------------
# Subcommand: replay --log-rate validation
# ---------------------------------------------------------------------------


def test_replay_log_rate_cli_validation(tmp_path):
    """--log-rate accepts only 50 and 208; rejects all other values."""
    fixture = REPO_ROOT / "tools" / "regression" / "fixtures" / "replay_engine_input.csv"
    if not fixture.exists():
        pytest.skip(f"replay_engine_input.csv not found: {fixture}")

    # Valid: 50
    r50 = run(["replay", "--input", str(fixture), "--log-rate", "50"])
    assert r50.returncode == 0, f"--log-rate 50 should be accepted: {r50.stderr}"

    # Valid: 208
    r208 = run(["replay", "--input", str(fixture), "--log-rate", "208"])
    assert r208.returncode == 0, f"--log-rate 208 should be accepted: {r208.stderr}"

    # Invalid: 100
    r100 = run(["replay", "--input", str(fixture), "--log-rate", "100"])
    assert r100.returncode != 0, "--log-rate 100 should be rejected"
    assert "50" in r100.stderr or "208" in r100.stderr, (
        f"error message should mention valid values: {r100.stderr!r}"
    )

    # Invalid: 0
    r0 = run(["replay", "--input", str(fixture), "--log-rate", "0"])
    assert r0.returncode != 0, "--log-rate 0 should be rejected"

    # Invalid: negative (-1 is parsed by atoi as a valid int but rejected by
    # the range check; ArgGet returns argv[i+1] verbatim so "-1" is the value)
    r_neg = run(["replay", "--input", str(fixture), "--log-rate", "-1"])
    # atoi("-1") = -1, which != 50 && != 208, so the check fires
    assert r_neg.returncode != 0, "--log-rate -1 should be rejected"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
