"""Sam's approach — fastest 0→16→33 deployment in the log.

t=836–852s of `sam_onspeed_aoa_4_11_2026.csv`:
  836–839s : clean approach (~85 kt, AOA 3.7° in fast-tone band)
  839      : flap 0 → 16  (fake lever sweep extends 2s before/after)
  839–847s : flap 16 (8 seconds — fastest deploy gap in the log)
  847      : flap 16 → 33
  847–852s : briefly at flap 33 (~5s settle so the final pip slide
             is visible without dwelling)

The whole 0→16→33 deployment happens in under 9 seconds with the
pilot holding chevron position by trimming AOA naturally.  Best
demonstration of the L/Dmax pip slide because:
  - Both deployments are quick — punchy demo
  - AOA stays roughly in the fast-tone-to-ONSPEED zone the entire
    time, so the chevron is visible against the moving pip
  - Settles in ONSPEED at full flaps within a few seconds

NOTE: The log only carries integer flapsPos (0/16/33), so the lever
ADC the orchestrator synthesizes is faked across each detent
transition with a 4s sweep window (centered on the snap tick) to
model the firmware's midpoint-detection behavior.  See issue #372
for the proper fix (capture flapsRawADC in the log).
"""

from __future__ import annotations
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from scenarios.from_log import scenario_from_log


SAM_LOG = pathlib.Path.home() / "Downloads/sam_onspeed_aoa_4_11_2026.csv"
SAM_CFG = pathlib.Path.home() / "Downloads/onspeed2_latest.cfg"


def scenario():
    yield from scenario_from_log(
        log_path=SAM_LOG,
        cfg_path=SAM_CFG,
        t_start_s=836.0,
        t_end_s=852.0,
    )
