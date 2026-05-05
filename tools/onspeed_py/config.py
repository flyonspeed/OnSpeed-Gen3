"""OnSpeed `.cfg` XML loader for per-flap setpoints.

Auto-detects the two on-disk formats:

  - **New per-flap-block format** (`<FLAP_POSITION>` blocks with nested
    `<DEGREES>`, `<POT_VALUE>`, `<LDMAXAOA>`, ...). Used by the current
    firmware (post-PR #320).
  - **V1 list format** (top-level `<FLAPDEGREES>0,20,40</FLAPDEGREES>`,
    `<SETPOINT_LDMAXAOA>8.03,5.73,4.78</SETPOINT_LDMAXAOA>`, etc.). Used
    by older firmware including calibration runs from late 2025.

V1 configs do not carry per-flap `alpha_0`, `alpha_stall`, or `KFIT`.
The loader fills in defaults:

  - `alpha_0 = 0.0` — matches Gen2's piecewise-display floor.
  - `alpha_stall = stallwarn + 1.5°` — typical wizard margin.
  - `k_fit = 0.0` — `ias_from_aoa()` returns 0 when this is unset.

These approximations are sufficient for replay rendering; if precise
values are needed, regenerate the config with the modern wizard.
"""

from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


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


def load_flap_setpoints(cfg_path: Path) -> dict[int, FlapSetpoints]:
    """Parse OnSpeed `.cfg` XML for per-flap setpoints.

    Returns `{degrees: FlapSetpoints}`. Raises `ValueError` if the
    file has neither `<FLAP_POSITION>` blocks nor a V1 `<FLAPDEGREES>`
    list.
    """
    # V1 configs use tag names like `<3DAUDIO>` that aren't valid XML
    # (tag names must start with a letter or underscore). The firmware's
    # tinyxml2 parser is lenient; Python's stdlib parser isn't.
    # Preprocess: rename digit-prefixed tags before parsing. The rewrite
    # is one-way (`<3DAUDIO>` → `<_3DAUDIO>` in both open and close
    # tags); we never write back to the cfg.
    raw = Path(cfg_path).read_text()
    raw = re.sub(r"<(/?)(\d)", r"<\1_\2", raw)
    root = ET.fromstring(raw)

    # Try new per-flap-block format first.
    out: dict[int, FlapSetpoints] = {}
    for fp in root.findall("FLAP_POSITION"):
        deg = int(fp.findtext("DEGREES", "0"))
        out[deg] = FlapSetpoints(
            degrees=deg,
            pot_value=int(fp.findtext("POT_VALUE", "0")),
            ldmax_aoa=float(fp.findtext("LDMAXAOA", "0")),
            onspeed_fast_aoa=float(fp.findtext("ONSPEEDFASTAOA", "0")),
            onspeed_slow_aoa=float(fp.findtext("ONSPEEDSLOWAOA", "0")),
            stallwarn_aoa=float(fp.findtext("STALLWARNAOA", "0")),
            alpha_0=float(fp.findtext("ALPHA0", "0")),
            alpha_stall=float(fp.findtext("ALPHASTALL", "0")),
            k_fit=float(fp.findtext("KFIT", "0")),
        )
    if out:
        return out

    # V1 fallback.
    return _load_flap_setpoints_v1(root, cfg_path)


def _load_flap_setpoints_v1(root: ET.Element,
                            cfg_path: Path) -> dict[int, FlapSetpoints]:
    """Parse the V1 list-style config."""
    def csv_floats(tag: str) -> list[float]:
        text = root.findtext(tag, "").strip()
        return [float(x) for x in text.split(",")] if text else []

    def csv_ints(tag: str) -> list[int]:
        text = root.findtext(tag, "").strip()
        return [int(x) for x in text.split(",")] if text else []

    degrees       = csv_ints("FLAPDEGREES")
    pot_positions = csv_ints("FLAPPOTPOSITIONS")
    ldmax         = csv_floats("SETPOINT_LDMAXAOA")
    onspeed_fast  = csv_floats("SETPOINT_ONSPEEDFASTAOA")
    onspeed_slow  = csv_floats("SETPOINT_ONSPEEDSLOWAOA")
    stallwarn     = csv_floats("SETPOINT_STALLWARNAOA")

    if not degrees:
        raise ValueError(
            f"No FLAP_POSITION blocks AND no V1 FLAPDEGREES list found in {cfg_path}"
        )

    out: dict[int, FlapSetpoints] = {}
    for i, deg in enumerate(degrees):
        sw = stallwarn[i] if i < len(stallwarn) else 0.0
        out[deg] = FlapSetpoints(
            degrees=deg,
            pot_value=pot_positions[i] if i < len(pot_positions) else 0,
            ldmax_aoa=ldmax[i] if i < len(ldmax) else 0.0,
            onspeed_fast_aoa=onspeed_fast[i] if i < len(onspeed_fast) else 0.0,
            onspeed_slow_aoa=onspeed_slow[i] if i < len(onspeed_slow) else 0.0,
            stallwarn_aoa=sw,
            alpha_0=0.0,
            alpha_stall=sw + 1.5,
            k_fit=0.0,
        )
    return out


def setpoints_for_flap(flap_deg: int,
                       table: dict[int, FlapSetpoints]) -> FlapSetpoints:
    """Return the exact-match setpoints for `flap_deg`, or those of the
    nearest detent if there's no exact match."""
    if flap_deg in table:
        return table[flap_deg]
    return table[min(table.keys(), key=lambda k: abs(k - flap_deg))]
