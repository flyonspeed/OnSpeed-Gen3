"""ALGORITHM CODE LIVES IN C++.

This module is a subprocess wrapper around the host_main CLI binary.
The canonical implementation is in
software/Libraries/onspeed_core/src/aoa/PercentLift.cpp, compiled to
tools/regression/.pio/build/native/program by `pio run -e native` in
tools/regression/.

Modifying this file does NOT change algorithm behavior. To change
percent-lift math, edit the C++ in onspeed_core, rebuild host_main,
then re-run the wrapper tests here to verify.

GAPS requiring a new host_main subcommand (see PLAN_PYTHON_CONSOLIDATION.md):

  - ias_from_aoa(): inverts the K/IAS² + alpha_0 lift equation. No
    host_main subcommand exists for this function. The implementation
    below keeps the Python math as a temporary shim. Step 0 of the
    consolidation plan should add `host_main ias_from_aoa` before this
    function is migrated.
"""

from __future__ import annotations

import math
import subprocess
from pathlib import Path

from .config import FlapSetpoints

# Locate the host_main binary built by `pio run -e native` in tools/regression/.
_HOST_MAIN = (
    Path(__file__).resolve().parents[2]
    / "tools" / "regression" / ".pio" / "build" / "native" / "program"
)


def compute_percent_lift(aoa: float, fs: FlapSetpoints) -> float:
    """Percent-of-stall lift fraction, clamped to [0.0, 99.9].

    Delegates to `host_main percent_lift`. Algorithm lives in
    onspeed_core/aoa/PercentLift.cpp::ComputePercentLift.

    NaN input (the format-3 "no valid air data" sentinel from a v3 SD
    log with iasValid=false) propagates as NaN. This guard runs in
    Python before the subprocess call because the C++ CLI cannot
    represent NaN on its command line — this is presentation-layer
    handling, not algorithm code.
    """
    if math.isnan(aoa):
        return math.nan

    proc = subprocess.run(
        [
            str(_HOST_MAIN),
            "percent_lift",
            "--aoa",        f"{aoa:.6f}",
            "--alpha-0",    f"{fs.alpha_0:.6f}",
            "--alpha-stall", f"{fs.alpha_stall:.6f}",
            "--stallwarn",  f"{fs.stallwarn_aoa:.6f}",
        ],
        capture_output=True,
        text=True,
        check=True,
    )
    return float(proc.stdout.strip())


def ias_from_aoa(aoa: float, fs: FlapSetpoints, n: float = 1.0) -> float:
    """Invert `AOA = K/IAS² + alpha_0` to get IAS for a given body angle.

    TEMPORARY SHIM — no host_main subcommand exists for this function.
    See module docstring. This code will be replaced when Step 0 of
    PLAN_PYTHON_CONSOLIDATION.md adds `host_main ias_from_aoa`.

    Multi-G version: `AOA - alpha_0` scales with `n`, so

        IAS = sqrt(n × K_fit / (AOA - alpha_0))

    Returns 0.0 if `k_fit` is unset (V1 configs) or the AOA is at/below
    `alpha_0`. Returns 999.0 (a "very fast" sentinel) if AOA sits
    barely above `alpha_0`.
    """
    if fs.k_fit <= 0:
        return 0.0
    margin = aoa - fs.alpha_0
    if margin <= 1e-3:
        return 999.0
    return math.sqrt(n * fs.k_fit / margin)
