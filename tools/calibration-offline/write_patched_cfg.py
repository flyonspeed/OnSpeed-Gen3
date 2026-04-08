#!/usr/bin/env python3
"""Generate a complete patched cfg file by replacing the FLAP_POSITION blocks
of the on-device config with the F-recommended values from synthesize_cfg.py.

Everything outside the FLAP_POSITION blocks is preserved byte-for-byte from
the original cfg (regex substitution with DOTALL on the FLAP_POSITION pattern).
After writing, the output file is re-parsed via analyze.parse_config() as a
sanity check.

Usage::

    uv run --with numpy --with scipy --with pandas --with matplotlib python3 \\
        write_patched_cfg.py \\
        --log     path/to/log_NNN.csv \\
        --cfg     path/to/sam_onspeed.cfg \\
        --weight  2190 \\
        --out     path/to/sam_onspeed_recommended.cfg
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import pandas as pd

import analyze
from synthesize_cfg import compute_recommendation


# Regex that captures a single FLAP_POSITION block, including the DEGREES value
# for routing to the right replacement block. DOTALL lets `.` match newlines.
_FLAP_BLOCK_RE = re.compile(
    r"(<FLAP_POSITION>.*?<DEGREES>(-?\d+)</DEGREES>.*?"
    r"<POT_VALUE>(\d+)</POT_VALUE>.*?</FLAP_POSITION>)",
    re.DOTALL,
)


def _build_flap_block(flap: int, rec: dict, rec_sps: analyze.Setpoints,
                      pot_value: str) -> str:
    """Build the replacement <FLAP_POSITION> block body (no leading indent
    on the opening tag — the original cfg's existing 4-space prefix remains
    at the substitution point)."""
    return (
        f"<FLAP_POSITION>\n"
        f"        <DEGREES>{flap}</DEGREES>\n"
        f"        <POT_VALUE>{pot_value}</POT_VALUE>\n"
        f"        <LDMAXAOA>{rec_sps.ldmax:.7f}</LDMAXAOA>\n"
        f"        <ONSPEEDFASTAOA>{rec_sps.os_fast:.7f}</ONSPEEDFASTAOA>\n"
        f"        <ONSPEEDSLOWAOA>{rec_sps.os_slow:.7f}</ONSPEEDSLOWAOA>\n"
        f"        <STALLWARNAOA>{rec_sps.stall_warn:.7f}</STALLWARNAOA>\n"
        f"        <STALLAOA>{rec_sps.stall:.7f}</STALLAOA>\n"
        f"        <MANAOA>{rec_sps.maneuvering:.7f}</MANAOA>\n"
        f"        <ALPHA0>{rec['alpha0']:.7f}</ALPHA0>\n"
        f"        <ALPHASTALL>{rec['alpha_stall']:.7f}</ALPHASTALL>\n"
        f"        <KFIT>{rec['K']:.3f}</KFIT>\n"
        f"        <AOA_CURVE>\n"
        f"            <TYPE>1</TYPE>\n"
        f"            <X3>0</X3>\n"
        f"            <X2>{rec['cp_a2']:.7f}</X2>\n"
        f"            <X1>{rec['cp_a1']:.7f}</X1>\n"
        f"            <X0>{rec['cp_a0']:.7f}</X0>\n"
        f"        </AOA_CURVE>\n"
        f"    </FLAP_POSITION>"
    )


def patch_config(log_path: Path, cfg_path: Path, current_weight_lb: float,
                 out_path: Path) -> None:
    cfg = analyze.parse_config(cfg_path)
    flap_bins = sorted(cfg.flaps.keys())

    print(f"Loading log: {log_path}")
    log_df = pd.read_csv(log_path, low_memory=False)
    log_df.columns = [c.strip() for c in log_df.columns]
    windows = analyze.detect_stall_windows(log_df, flap_bins)
    runs = analyze.build_runs_from_windows(log_df, windows)
    per_run_ols = {r.run_id: analyze.fit_run(r, "OLS") for r in runs}
    quality = analyze.rank_runs(runs, per_run_ols)

    runs_by_flap: dict[int, list[analyze.Run]] = {}
    for r in runs:
        if quality[r.run_id].verdict == "DISCARD":
            continue
        runs_by_flap.setdefault(r.flap_bin, []).append(r)

    recommendations = {
        flap: compute_recommendation(rs, per_run_ols, quality)
        for flap, rs in runs_by_flap.items()
    }

    # Extract POT_VALUE per flap from the original cfg so we preserve it.
    original = cfg_path.read_text()
    pot_values: dict[int, str] = {}
    for match in _FLAP_BLOCK_RE.finditer(original):
        pot_values[int(match.group(2))] = match.group(3)
    print(f"Original cfg POT_VALUEs: {pot_values}")

    # Build replacement blocks keyed by flap degrees.
    final_blocks: dict[int, str] = {}
    for flap, rec in recommendations.items():
        rec_sps = analyze.derive_setpoints(
            rec["K"], rec["alpha0"], rec["alpha_stall"], rec["stall_ias"],
            flap, cfg.aircraft, current_weight=current_weight_lb,
        )
        pot = pot_values.get(flap, "0")
        final_blocks[flap] = _build_flap_block(flap, rec, rec_sps, pot)

    def repl(match: re.Match) -> str:
        deg = int(match.group(2))
        return final_blocks.get(deg, match.group(0))

    patched = _FLAP_BLOCK_RE.sub(repl, original)

    out_path.write_text(patched)
    print(f"Wrote {out_path}")
    print(f"  size: {out_path.stat().st_size} bytes (original: {cfg_path.stat().st_size} bytes)")

    # Sanity-check: re-parse the output cfg and report per-flap values.
    print()
    print("Sanity check — parse the new cfg and print per-flap setpoints:")
    new_cfg = analyze.parse_config(out_path)
    for flap in sorted(new_cfg.flaps.keys()):
        fc = new_cfg.flaps[flap]
        print(f"  flap {flap}°: K={fc.kfit:.0f} α₀={fc.alpha0:+.3f} α_stall={fc.alpha_stall:+.3f}")
        print(f"             LDmax={fc.ldmax:+.2f}° OSfast={fc.os_fast:+.2f}° "
              f"OSslow={fc.os_slow:+.2f}° Stall={fc.stall:+.2f}°")
        print(f"             AOA_CURVE: {fc.aoa_curve}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Generate a patched cfg file from synthesize_cfg.py's F recommendation.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument("--log", type=Path, required=True,
                    help="Flight log CSV from the OnSpeed SD card (50 Hz).")
    ap.add_argument("--cfg", type=Path, required=True,
                    help="On-device config XML that was running when the log was captured.")
    ap.add_argument("--weight", type=float, required=True,
                    help="Aircraft weight in pounds during the calibration flight. "
                         "Only affects flap-0 LDmax IAS.")
    ap.add_argument("--out", type=Path, required=True,
                    help="Output path for the patched cfg file.")
    args = ap.parse_args()

    patch_config(args.log, args.cfg, args.weight, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
