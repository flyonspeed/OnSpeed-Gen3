"""OnSpeed `#1` 74-byte wire frame builder + per-flap config loader.

Lifted from tools/m5-replay/replay.py with the serial-drain code removed
so this module has no external dependencies (pyserial, etc.). Schema
discipline is provided by the (Layer 2) round-trip tests in
tools/m5-replay/test_replay.py — that file's frame builder and ours
share the same byte-level format defined in
software/Libraries/onspeed_core/src/proto/DisplaySerial.h.

If you change anything about the wire format here, also update m5-replay
and rerun its Layer 2 tests, or vice versa.
"""

from __future__ import annotations

import math
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

PAYLOAD_LEN = 72   # v4.22: bytes 0..71 — adds pipPctLift at offset 70
FRAME_LEN   = 76   # PAYLOAD_LEN + 2 ascii-hex CRC + CRLF


def _clamp_int(v: float, lo: int, hi: int) -> int:
    """C-style truncation toward zero + clamp, matching SafeScaledInt."""
    if not math.isfinite(v):
        return 0
    i = int(v)
    if i < lo:
        return lo
    if i > hi:
        return hi
    return i


def _clamp_uint(v: float, lo: int, hi: int) -> int:
    if not math.isfinite(v):
        return lo
    i = int(v)
    if i < lo:
        return lo
    if i > hi:
        return hi
    return i


@dataclass
class Frame:
    """All fields transmitted in one #1 payload.

    Field name and units mirror DisplaySerial.h's DisplayBuildInputs.
    """

    pitch_deg:             float = 0.0
    roll_deg:              float = 0.0
    ias_kts:               float = 0.0
    palt_ft:               float = 0.0
    turnrate_dps:          float = 0.0
    lateral_g:             float = 0.0
    vertical_g:            float = 1.0
    percent_lift:          int   = 0
    vsi_fpm:               float = 0.0
    oat_c:                 int   = 15
    flightpath_deg:        float = 0.0
    flap_deg:              int   = 0
    tones_on_pct_lift:     int   = 0
    onspeed_fast_pct_lift: int   = 0
    onspeed_slow_pct_lift: int   = 0
    stall_warn_pct_lift:   int   = 0
    flaps_min_deg:         int   = 0
    flaps_max_deg:         int   = 0
    g_onset_rate:          float = 0.0
    spin_cue:              int   = 0
    data_mark:             int   = 0
    pip_pct_lift:          int   = 0   # v4.22+, visual L/Dmax pip

    def to_bytes(self) -> bytes:
        """Serialize to the 76-byte wire frame (payload + CRC + CRLF, v4.22)."""
        payload = (
            "#1"
            f"{_clamp_int(self.pitch_deg * 10, -999, 999):+04d}"
            f"{_clamp_int(self.roll_deg * 10, -9999, 9999):+05d}"
            f"{_clamp_uint(self.ias_kts * 10, 0, 9999):04d}"
            f"{_clamp_int(self.palt_ft, -99999, 99999):+06d}"
            f"{_clamp_int(self.turnrate_dps * 10, -9999, 9999):+05d}"
            f"{_clamp_int(self.lateral_g * 100, -99, 99):+03d}"
            f"{_clamp_int(self.vertical_g * 10, -99, 99):+03d}"
            f"{_clamp_uint(self.percent_lift, 0, 99):02d}"
            f"{_clamp_int(self.vsi_fpm / 10, -999, 999):+04d}"
            f"{_clamp_int(self.oat_c, -99, 99):+03d}"
            f"{_clamp_int(self.flightpath_deg * 10, -999, 999):+04d}"
            f"{_clamp_int(self.flap_deg, -99, 99):+03d}"
            f"{_clamp_uint(self.tones_on_pct_lift, 0, 99):02d}"
            f"{_clamp_uint(self.onspeed_fast_pct_lift, 0, 99):02d}"
            f"{_clamp_uint(self.onspeed_slow_pct_lift, 0, 99):02d}"
            f"{_clamp_uint(self.stall_warn_pct_lift, 0, 99):02d}"
            f"{_clamp_int(self.flaps_min_deg, -99, 99):+03d}"
            f"{_clamp_int(self.flaps_max_deg, -99, 99):+03d}"
            f"{_clamp_int(self.g_onset_rate * 100, -999, 999):+04d}"
            f"{_clamp_int(self.spin_cue, -9, 9):+02d}"
            f"{_clamp_uint(self.data_mark, 0, 99):02d}"
            f"{_clamp_uint(self.pip_pct_lift, 0, 99):02d}"
        )
        if len(payload) != PAYLOAD_LEN:
            raise AssertionError(
                f"payload length {len(payload)} != {PAYLOAD_LEN}: {payload!r}"
            )
        crc = sum(payload.encode("ascii")) & 0xFF
        return f"{payload}{crc:02X}\r\n".encode("ascii")


@dataclass
class FlapSetpoints:
    degrees:          int
    pot_value:        int
    ldmax_aoa:        float
    onspeed_fast_aoa: float
    onspeed_slow_aoa: float
    stallwarn_aoa:    float
    alpha_0:          float
    alpha_stall:      float
    k_fit:            float = 0.0   # IAS-to-AOA fit constant (deg·kt²)


def load_flap_setpoints(cfg_path: Path) -> dict[int, FlapSetpoints]:
    """Parse OnSpeed .cfg XML for per-flap setpoints."""
    tree = ET.parse(cfg_path)
    root = tree.getroot()
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
    if not out:
        raise ValueError(f"No FLAP_POSITION entries found in {cfg_path}")
    return out


def setpoints_for_flap(
    flap_deg: int, table: dict[int, FlapSetpoints]
) -> FlapSetpoints:
    """Return exact match or nearest flap entry."""
    if flap_deg in table:
        return table[flap_deg]
    return table[min(table.keys(), key=lambda k: abs(k - flap_deg))]


def ias_from_aoa(aoa: float, fs: FlapSetpoints, n: float = 1.0) -> float:
    """Invert the AOA = K/IAS² + alpha_0 calibration fit to get IAS.

    Multi-G version: AOA - alpha_0 scales with n, so
        IAS = sqrt(n × K / (AOA - alpha_0))
    Returns 0 if the fit isn't useful (no calibration, or AOA below alpha_0).
    """
    if fs.k_fit <= 0:
        return 0.0
    margin = aoa - fs.alpha_0
    if margin <= 1e-3:
        return 999.0   # very small AOA above alpha_0 → very fast
    import math
    return math.sqrt(n * fs.k_fit / margin)


def compute_percent_lift(aoa: float, fs: FlapSetpoints) -> int:
    """Honest single-linear envelope fraction.

    Mirrors onspeed_core/aoa/PercentLift.cpp::ComputePercentLift.
    """
    alpha_stall = fs.alpha_stall
    if alpha_stall <= fs.stallwarn_aoa:
        alpha_stall = fs.stallwarn_aoa * 100.0 / 90.0
    span = alpha_stall - fs.alpha_0
    if span <= 0:
        return 0
    pct = int((aoa - fs.alpha_0) / span * 100.0)
    return max(0, min(99, pct))
