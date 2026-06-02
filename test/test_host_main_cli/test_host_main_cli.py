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


def test_ahrs_tone_jsonl_emits_deprecated_kalman_aliases():
    """Pin the one-release deprecation aliases on the alt/vsi JSONL fields.

    host_main.cpp emits both the new (`alt_ft`, `vsi_fpm`) and the legacy
    (`kalman_alt_ft`, `kalman_vsi_fpm`) keys so downstream consumers
    (notably tools/onspeed_py/log_replay.py) can migrate without breaking.

    REMOVE THIS TEST when the deprecated `kalman_*` emissions are dropped.
    The companion fallback in tools/onspeed_py/log_replay.py
    (`raw.get('vsi_mps') or raw.get('kalman_vsi_mps')`) should be deleted
    at the same time. Tracked in the deprecation-removal issue filed
    alongside the rename PR.
    """
    if not SHORT_REPLAY.exists():
        pytest.skip(f"short_replay.csv not found: {SHORT_REPLAY}")
    r = run([
        "ahrs_tone",
        "--input", str(SHORT_REPLAY),
        "--output-format", "jsonl",
    ])
    assert r.returncode == 0
    lines = r.stdout.strip().splitlines()
    assert len(lines) > 0
    obj = json.loads(lines[0])
    # New names — these are the long-term keys.
    assert "alt_ft" in obj
    assert "vsi_fpm" in obj
    # Deprecated aliases — must remain until the one-release migration
    # window closes. If you delete these emissions in host_main.cpp,
    # delete this test in the same PR.
    assert "kalman_alt_ft" in obj, (
        "deprecated alias kalman_alt_ft is missing — if intentional, "
        "delete this test and the Python fallback in log_replay.py"
    )
    assert "kalman_vsi_fpm" in obj, (
        "deprecated alias kalman_vsi_fpm is missing — see test docstring"
    )
    # Values must match — the alias points at the same float.
    assert obj["alt_ft"] == obj["kalman_alt_ft"]
    assert obj["vsi_fpm"] == obj["kalman_vsi_fpm"]


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


SYNTH_ADC_INPUT  = REPO_ROOT / "tools" / "regression" / "fixtures" / "synth_adc_input.csv"
SYNTH_ADC_CONFIG = REPO_ROOT / "tools" / "regression" / "fixtures" / "synth_adc_config.cfg"


def test_replay_accepts_config(tmp_path):
    """`--config X.cfg` loads and uses the config (PR #466 deferral lifted).

    Synth flap-pot output uses the per-detent pot values from the config
    (1000 for flaps=0, 3000 for flaps=30) rather than LoadDefaults() zeros.
    The input log has no flapsRawADC column, so the engine synthesises ADC
    values via the streaming smoothstep path.

    Contract pinned here:
      - Command exits 0.
      - Output rows contain non-zero flap_raw_adc values (proves the config
        pot positions were loaded: defaults would be 0 for the second detent).
      - flap_raw_adc values are not all identical across the transition
        (proves the smoothstep is actually running).
      - Steady-state rows at the end (well past the transition window) have
        flap_raw_adc == 3000 (the flaps=30 nominal pot from the config).
    """
    if not SYNTH_ADC_INPUT.exists():
        pytest.skip(f"synth_adc_input.csv not found: {SYNTH_ADC_INPUT}")
    if not SYNTH_ADC_CONFIG.exists():
        pytest.skip(f"synth_adc_config.cfg not found: {SYNTH_ADC_CONFIG}")

    r = run([
        "replay",
        "--input",  str(SYNTH_ADC_INPUT),
        "--config", str(SYNTH_ADC_CONFIG),
    ])
    assert r.returncode == 0, (
        f"replay --config should succeed; got rc={r.returncode}, "
        f"stderr={r.stderr!r}"
    )

    lines = r.stdout.strip().splitlines()
    assert len(lines) >= 2, "expected header + at least one data row"
    header = lines[0].split(",")
    adc_col = header.index("flaps_raw_adc")
    adc_present_col = header.index("flaps_raw_adc_present")
    flaps_pos_col = header.index("flaps_pos")

    rows = [l.split(",") for l in lines[1:]]

    # All rows should have flaps_raw_adc_present == 1 (synth path sets this).
    for row in rows:
        assert row[adc_present_col] == "1", (
            f"expected flaps_raw_adc_present=1, got {row[adc_present_col]!r}"
        )

    # At least some rows should be non-zero (config pot=1000 for flaps=0).
    adc_values = [int(row[adc_col]) for row in rows]
    assert any(v > 0 for v in adc_values), (
        "all flap_raw_adc values are 0 — config pot positions were not loaded"
    )

    # Values should vary across the detent transition (smoothstep is running).
    assert len(set(adc_values)) > 1, (
        "all flap_raw_adc values are identical — smoothstep transition not applied"
    )

    # Steady-state at the end (flaps=30 rows well past the transition window):
    # the last row should be at the flaps=30 nominal pot = 3000.
    assert rows[-1][flaps_pos_col] == "30", (
        f"expected last row flaps_pos=30, got {rows[-1][flaps_pos_col]!r}"
    )
    last_adc = int(rows[-1][adc_col])
    assert last_adc == 3000, (
        f"expected steady-state flap_raw_adc=3000 at end, got {last_adc}"
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


REPLAY_ENGINE_INPUT = REPO_ROOT / "tools" / "regression" / "fixtures" / "replay_engine_input.csv"


def test_replay_log_rate_cli_validation():
    """--log-rate accepts only 50 and 208; rejects everything else.

    PR #476 added --log-rate {50|208} validation to the replay subcommand.
    This test pins the contract so a future refactor that widens or removes
    the validation doesn't slip past CI.
    """
    if not REPLAY_ENGINE_INPUT.exists():
        pytest.skip(f"replay_engine_input.csv not found: {REPLAY_ENGINE_INPUT}")

    # Valid: 50 Hz
    r = run(["replay", "--input", str(REPLAY_ENGINE_INPUT), "--log-rate", "50"])
    assert r.returncode == 0, (
        f"--log-rate 50 should be accepted; rc={r.returncode}, stderr={r.stderr!r}"
    )

    # Valid: 208 Hz
    r = run(["replay", "--input", str(REPLAY_ENGINE_INPUT), "--log-rate", "208"])
    assert r.returncode == 0, (
        f"--log-rate 208 should be accepted; rc={r.returncode}, stderr={r.stderr!r}"
    )

    # Invalid values: should be rejected with a non-zero exit code and a
    # helpful error message mentioning the valid values (50 and/or 208).
    for invalid in ["100", "0", "-1"]:
        r = run(["replay", "--input", str(REPLAY_ENGINE_INPUT), "--log-rate", invalid])
        assert r.returncode != 0, (
            f"--log-rate {invalid!r} should be rejected (returncode was 0)"
        )
        combined = r.stderr.lower() + r.stdout.lower()
        assert "log-rate" in combined or "50" in combined or "208" in combined, (
            f"error message for --log-rate {invalid!r} should mention valid values; "
            f"stderr={r.stderr!r}"
        )


def test_replay_jsonl_emits_deprecated_kalman_vsi_mps_alias():
    """Pin the one-release `kalman_vsi_mps` deprecation alias on the replay
    subcommand's JSONL output.

    REMOVE THIS TEST when the deprecated `kalman_vsi_mps` emission is dropped
    from `EmitJsonlRow` in tools/regression/host_main.cpp. Companion changes
    at that time: delete the fallback in tools/onspeed_py/log_replay.py
    (`raw.get('vsi_mps') or raw.get('kalman_vsi_mps')`).
    """
    if not REPLAY_ENGINE_INPUT.exists():
        pytest.skip(f"replay_engine_input.csv not found: {REPLAY_ENGINE_INPUT}")
    r = run([
        "replay",
        "--input", str(REPLAY_ENGINE_INPUT),
        "--output-format", "jsonl",
    ])
    assert r.returncode == 0, f"replay --output-format jsonl rc={r.returncode}, stderr={r.stderr!r}"
    lines = r.stdout.strip().splitlines()
    assert len(lines) > 0
    obj = json.loads(lines[0])
    assert "vsi_mps" in obj
    assert "kalman_vsi_mps" in obj, (
        "deprecated alias kalman_vsi_mps is missing — if intentional, "
        "delete this test and the Python fallback in log_replay.py"
    )
    assert obj["vsi_mps"] == obj["kalman_vsi_mps"]


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
