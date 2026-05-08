"""ALGORITHM CODE LIVES IN C++.

This module is a subprocess wrapper around the host_main CLI binary.
The canonical implementation is in
software/Libraries/onspeed_core/src/config/ (ConfigXmlParse.cpp for V2,
ConfigV1Parse.cpp for V1), compiled to
tools/regression/.pio/build/native/program by `pio run -e native` in
tools/regression/.

Modifying this file does NOT change algorithm behavior. To change
config parsing, edit the C++ in onspeed_core, rebuild host_main, then
re-run the wrapper tests here to verify.

V1 alpha_stall deserialization note:

The C++ V1 parser leaves fAlphaStall at 0.0 when the config file does
not carry a SETPOINT_ALPHASTALL tag (the common case for old Gen2
configs). The Python-facing API preserves the historical convention of
defaulting V1 alpha_stall to stallwarn + 1.5 for backwards
compatibility with replay tools that rely on that as the uncalibrated
ceiling. This is applied in the deserialization step below, not in the
C++ parser.
"""

from __future__ import annotations

import json
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

# Locate the host_main binary built by `pio run -e native` in tools/regression/.
_HOST_MAIN = (
    Path(__file__).resolve().parents[2]
    / "tools" / "regression" / ".pio" / "build" / "native" / "program"
)


@dataclass
class FlapSetpoints:
    degrees:          int
    pot_value:        int   = 0     # iPotPosition for lever ADC interpolation
    ldmax_aoa:        float = 0.0
    onspeed_fast_aoa: float = 0.0
    onspeed_slow_aoa: float = 0.0
    stallwarn_aoa:    float = 0.0
    alpha_0:          float = 0.0   # 0.0 default for V1 configs
    alpha_stall:      float = 0.0   # stallwarn + 1.5 default for V1 configs
    k_fit:            float = 0.0   # IAS-to-AOA fit constant (deg·kt²);
                                    # 0.0 if not stored in the config


def _is_v1_format(cfg_path: Path) -> bool:
    """Return True if the config file is V1 format (<CONFIG> root tag).

    V1 detection mirrors onspeed_core/config/ConfigV1Parse.cpp::IsV1Format:
    look for <CONFIG> but not <CONFIG2>.
    """
    raw = cfg_path.read_text()
    return "<CONFIG>" in raw and "<CONFIG2>" not in raw


def load_flap_setpoints(cfg_path: Path) -> dict[int, FlapSetpoints]:
    """Parse OnSpeed `.cfg` XML for per-flap setpoints.

    Delegates to `host_main parse_config`. Returns `{degrees: FlapSetpoints}`.

    Raises `subprocess.CalledProcessError` if the file cannot be parsed.
    Raises `ValueError` if the config has no flap entries.
    """
    proc = subprocess.run(
        [str(_HOST_MAIN), "parse_config", "--in", str(cfg_path)],
        capture_output=True,
        text=True,
        check=True,
    )
    raw = json.loads(proc.stdout)

    is_v1 = _is_v1_format(Path(cfg_path))
    flaps_raw = raw.get("flapsByDeg", {})
    if not flaps_raw:
        raise ValueError(
            f"Config has no flap entries: {cfg_path}"
        )

    out: dict[int, FlapSetpoints] = {}
    for key, f in flaps_raw.items():
        deg          = int(key)
        stallwarn    = float(f.get("stallWarnAoa", 0.0))
        alpha_stall  = float(f.get("alphaStall",   0.0))

        # V1 configs don't carry SETPOINT_ALPHASTALL; C++ leaves alphaStall
        # at 0.0. Apply the Python-API convention: default to stallwarn + 1.5
        # so replay tools get a usable uncalibrated ceiling.
        if is_v1 and alpha_stall == 0.0:
            alpha_stall = stallwarn + 1.5

        out[deg] = FlapSetpoints(
            degrees=deg,
            pot_value=int(f.get("potPosition", 0)),
            ldmax_aoa=float(f.get("ldmaxAoa", 0.0)),
            onspeed_fast_aoa=float(f.get("onSpeedFastAoa", 0.0)),
            onspeed_slow_aoa=float(f.get("onSpeedSlowAoa", 0.0)),
            stallwarn_aoa=stallwarn,
            alpha_0=float(f.get("alpha0", 0.0)),
            alpha_stall=alpha_stall,
            k_fit=float(f.get("kFit", 0.0)),
        )
    return out


def setpoints_for_flap(flap_deg: int,
                       table: dict[int, FlapSetpoints]) -> FlapSetpoints:
    """Return the exact-match setpoints for `flap_deg`, or those of the
    nearest detent if there's no exact match."""
    if flap_deg in table:
        return table[flap_deg]
    return table[min(table.keys(), key=lambda k: abs(k - flap_deg))]
