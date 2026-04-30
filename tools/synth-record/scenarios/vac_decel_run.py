"""Vac's deceleration run — first real-log replay test.

Pulls a 28-second window from Vac's late-2025 calibration flight log
(`vac_log.csv`, V1 column format, V1 config) and renders it through
the same pipeline as the synthetic scenarios.

V1 configs don't store alpha_0 / alpha_stall — those fields didn't
exist.  Defaults are alpha_0=0 (matches Gen2's piecewise+0-floor
display) and alpha_stall = stallwarn + 1.5°.  Honest values can only
be recovered by re-fitting against the source log; for the demo we
just use the defaults.

Tone progression (per V1 config flap-0 setpoints
ldmax=8.03, fast=11.25, slow=13.84, warn=16.48):
  t=0–6s   : silent cruise → AOA below ldmax
  t=6–7s   : low-pulse begins as AOA crosses ldmax
  t=7–12s  : ONSPEED solid (Vac dwelling on speed)
  t=12–18s : high-pulse, PPS rising as AOA → stallwarn
  t=18–22s : stall buzz, natural stall break
  t=22–28s : recovery, audio walks back down
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
        t_start_s=2857.0,
        t_end_s=2885.0,
    )
