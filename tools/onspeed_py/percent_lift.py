"""ALGORITHM CODE LIVES IN C++.

This module is a subprocess wrapper around the host_main CLI binary.
The canonical implementation is in
software/Libraries/onspeed_core/src/aoa/PercentLift.cpp, compiled to
tools/regression/.pio/build/native/program by `pio run -e native` in
tools/regression/.

Modifying this file does NOT change algorithm behavior. To change
percent-lift math, edit the C++ in onspeed_core, rebuild host_main,
then re-run the wrapper tests here to verify.
"""

from __future__ import annotations

import subprocess

from .config import FlapSetpoints
from ._host_main import require_host_main


def compute_percent_lift(aoa: float, fs: FlapSetpoints) -> float:
    """Percent-of-stall lift fraction, clamped to [0.0, 99.9].

    Delegates to `host_main percent_lift`. Algorithm lives in
    onspeed_core/aoa/PercentLift.cpp::ComputePercentLift.

    NaN or non-finite AOA input: C++ ComputePercentLift returns 0.0
    (defined in PercentLift.cpp:62-64 — the fraction is non-finite,
    so the function clamps to 0.0). This wrapper passes the value
    through to host_main transparently; std::stof("nan") is valid C++.
    """
    proc = subprocess.run(
        [
            str(require_host_main()),
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
