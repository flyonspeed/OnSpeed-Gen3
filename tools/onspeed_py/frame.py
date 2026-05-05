"""OnSpeed `#1` display-serial wire-frame builder.

Produces the 77-byte ASCII frame (v4.23) that the firmware emits at
20 Hz and that `onspeed_core::ParseDisplayFrame` decodes on the M5
side. Single source of truth for the Python side of the wire — both
`tools/m5-replay/replay.py` and `tools/synth-record/` import `Frame`
from here.

The byte-for-byte contract lives in
`software/Libraries/onspeed_core/src/proto/DisplaySerial.h`. Tests in
`tools/onspeed_py/tests/` and `tools/m5-replay/test_replay.py`
(Layer 2 round-trip) verify the firmware's `ParseDisplayFrame` accepts
what `Frame.to_bytes()` emits.
"""

from __future__ import annotations

import math
from dataclasses import dataclass


# Wire-format constants. Mirror onspeed_core/proto/DisplaySerial.h
# (kDisplayFrameSizeBytes / kDisplayFrameChecksumLen).
PAYLOAD_LEN = 73   # bytes 0..72 — ASCII fields up to and including pipPctLift
FRAME_LEN   = 77   # PAYLOAD_LEN + 2 hex CRC + CRLF


def _clamp_int(v: float, lo: int, hi: int) -> int:
    """C-style truncation-toward-zero + clamp, matching SafeScaledInt."""
    if not math.isfinite(v):
        return 0
    # int() in Python truncates toward zero, matching C's (int)cast.
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
    """All fields transmitted in one `#1` payload (v4.23 wire).

    Field names and units mirror `DisplayBuildInputs` in
    `onspeed_core/proto/DisplaySerial.h`.

    Lateral G uses the BALL-FRAME convention (positive = ball deflects
    rightward, i.e. airframe accelerating leftward). The firmware
    negates the body-frame `g_AHRS.AccelLatCorr` before encoding.

    `percent_lift` (v4.23 wire) carries tenths of a percent (0..999):
    `473` means 47.3% lift. The four band-edge percents
    (`tones_on_pct_lift`, `onspeed_fast_pct_lift`, etc.) stay at integer
    percent (0..99); they only move on detent or config-save events.

    `pip_pct_lift` (v4.22+) is the visual L/Dmax pip percent — separated
    from `tones_on_pct_lift` per PR #336.
    """

    pitch_deg:             float = 0.0
    roll_deg:              float = 0.0
    ias_kts:               float = 0.0
    palt_ft:               float = 0.0
    turnrate_dps:          float = 0.0
    lateral_g:             float = 0.0
    vertical_g:            float = 1.0
    percent_lift:          int   = 0   # tenths of a percent (0..999); v4.23+
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
        """Serialize to the 77-byte wire frame (payload + CRC + CRLF, v4.23).

        Matches the printf format in
        `onspeed_core/proto/DisplaySerial.cpp::BuildDisplayFrame`.
        """
        payload = (
            "#1"
            f"{_clamp_int(self.pitch_deg * 10, -999, 999):+04d}"
            f"{_clamp_int(self.roll_deg * 10, -9999, 9999):+05d}"
            f"{_clamp_uint(self.ias_kts * 10, 0, 9999):04d}"
            f"{_clamp_int(self.palt_ft, -99999, 99999):+06d}"
            f"{_clamp_int(self.turnrate_dps * 10, -9999, 9999):+05d}"
            f"{_clamp_int(self.lateral_g * 100, -99, 99):+03d}"
            f"{_clamp_int(self.vertical_g * 10, -99, 99):+03d}"
            f"{_clamp_uint(self.percent_lift, 0, 999):03d}"
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
