"""Vac's deceleration run — first real-log replay test.

Pulls a 20-second window from Vac's late-2025 calibration flight log
(`vac_log.csv`, V1 column format, V1 config) and renders it through
the same pipeline as the synthetic scenarios.

Run:
    python3 tools/synth-record/record.py \\
        tools/synth-record/scenarios/vac_decel_run.py \\
        --cfg ~/Downloads/vac_config.cfg \\
        --out tools/synth-record/out/vac-decel.mp4

Window picked: t=2865-2885s.  Vac flying clean, IAS bleeding 65→52 kt
as AOA climbs through 10°→16° (high-pulse region) → wing breaks at
17.7° (t≈2877.5) → recovery at -38° roll, 0.4g → climb back to ~12°
AOA, 70 kt at t=2885.

Tone progression (per V1 config flap-0 setpoints
ldmax=8.03, fast=11.25, slow=13.84, warn=16.48):
  t=0–1s   : low-pulse (between ldmax and fast)
  t=1–7s   : ONSPEED solid (between fast and slow)
  t=7–10s  : high-pulse (between slow and warn), PPS rising
  t=10–12s : stall buzz briefly, then natural stall break
  t=12–15s : audio collapses through ONSPEED back to low-pulse during
             dive recovery, then climbs again as AOA rises post-recovery
"""

from __future__ import annotations
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from scenarios.from_log import scenario_from_log


VAC_LOG = pathlib.Path.home() / "Downloads/vac_log.csv"
VAC_CFG = pathlib.Path.home() / "Downloads/vac_config.cfg"


def scenario():
    yield from scenario_from_log(
        log_path=VAC_LOG,
        cfg_path=VAC_CFG,
        t_start_s=2865.0,
        t_end_s=2885.0,
    )
