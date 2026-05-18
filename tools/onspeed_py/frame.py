"""OnSpeed `#1` display-serial wire-frame builder.

Produces the 83-byte ASCII frame (v4.24) that the firmware emits at
20 Hz and that `onspeed_core::ParseDisplayFrame` decodes on the M5
side. Single source of truth for the Python side of the wire — both
`tools/m5-replay/replay.py` and `tools/synth-record/` import `Frame`
from here.

The byte-for-byte contract lives in
`software/Libraries/onspeed_core/src/proto/DisplaySerial.h`.  Drift
between this module and the C++ encoder is caught by the parity test
`tools/onspeed_py/tests/test_v424_byte_parity.py`, which compares
`Frame.to_bytes()` output against a C++-produced golden binary.
"""

from __future__ import annotations

import math
from dataclasses import dataclass


# Wire-format constants (v4.24).  Mirror
# onspeed_core/proto/DisplaySerial.h (kDisplayFrameSizeBytes,
# kDisplayFrameChecksumLen, kWireVersion).
PAYLOAD_LEN  = 79   # bytes 0..78 — ASCII payload up to and including validFlags
FRAME_LEN    = 83   # PAYLOAD_LEN + 2 hex CRC-8 + CRLF
WIRE_VERSION = 24

# IAS-invalid wire sentinel.  Mirrors `kIasInvalidWireSentinel` in
# onspeed_core/proto/DisplaySerial.h: when the producer marks air-data
# invalid, the iasKt %04u field carries 9999.  The M5 parser detects
# this exact value, sets `iasIsValid=false` on the parsed frame, and
# renders dashes for both IAS and percentLift.  Picked as the maximum
# of the field width — far above any operational airspeed — so a
# consumer that ignores `iasIsValid` still sees an obviously bogus
# rather than a plausibly-low IAS reading.
IAS_INVALID_WIRE_SENTINEL = 9999


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


# CRC-8 lookup table (poly 0x07, init 0x00, no XOR-out, no reflection;
# SMBus).  Computed once at import; runtime call is one xor + one
# table lookup per byte.  Mirrors onspeed_core/proto/Crc8.h.
def _build_crc8_table() -> tuple[int, ...]:
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if (c & 0x80) else (c << 1) & 0xFF
        table.append(c)
    return tuple(table)


_CRC8_TABLE = _build_crc8_table()


def _crc8(data: bytes) -> int:
    c = 0
    for b in data:
        c = _CRC8_TABLE[c ^ b]
    return c


@dataclass
class Frame:
    """All fields transmitted in one `#1` payload (v4.24 wire).

    Field names and units mirror `DisplayBuildInputs` in
    `onspeed_core/proto/DisplaySerial.h`.

    Lateral G uses the BODY-FRAME convention (positive = airframe
    accelerating rightward).  Matches the IMU's body-Y axis, the SD
    log's `imuLateralG` column, and the WebSocket JSON's
    `lateralGLoad` field.  Ball-frame renderers (M5 SerialRead,
    JS slipBall) negate locally at the rendering site.

    `percent_lift_pct` is in whole-percent units (0.0..99.9); the wire
    encoder scales ×10 and truncates to int for the `%03u` field
    (range 0..999) — the wire carries tenths-of-a-percent for
    sub-pixel temporal smoothness, but every consumer surfaces a
    float.  The four band-edge percents (`tones_on_pct_lift`,
    `onspeed_fast_pct_lift`, etc.) stay at integer percent (0..99);
    they only move on detent or config-save events.

    `pip_pct_lift` (v4.22+) is the visual L/Dmax pip percent —
    separated from `tones_on_pct_lift` per PR #336.

    `validity` (v4.24+) carries the per-channel `AirDataValid` bitmap.
    Low 16 bits encode as the wire's `validFlags` %04X field.
    """

    pitch_deg:             float = 0.0
    roll_deg:              float = 0.0
    ias_kts:               float = 0.0
    # Air-data validity flag.  Mirrors `DisplayBuildInputs::iasValid` in
    # onspeed_core/proto/DisplaySerial.h.  When False, `to_bytes()`
    # writes the IAS_INVALID_WIRE_SENTINEL (9999) into the iasKt field
    # regardless of the `ias_kts` value, which the M5 parser uses to
    # flip `iasIsValid=false` and render dashes for IAS and percentLift.
    # Default True keeps live-mode and v2-log producers (all-numeric
    # IAS) emitting the live value unchanged.
    ias_valid:             bool  = True
    palt_ft:               float = 0.0
    turnrate_dps:          float = 0.0
    lateral_g:             float = 0.0
    vertical_g:            float = 1.0
    percent_lift_pct:      float = 0.0  # whole percent (0.0..99.9); v4.24 wire encoder scales ×10 to tenths
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
    validity:              int   = 0   # low 16 bits become the wire's validFlags field (v4.24+)

    def to_bytes(self) -> bytes:
        """Serialize to the 83-byte wire frame (v4.24).

        Matches the printf format in
        `onspeed_core/proto/DisplaySerial.cpp::BuildDisplayFrame`.
        """
        # Wire contract: when `ias_valid` is False, write
        # IAS_INVALID_WIRE_SENTINEL (9999) into the iasKt %04u field.
        # The M5 parser detects this exact value and flips
        # `iasIsValid=false`, which the M5 firmware uses to render
        # dashes for IAS and percentLift.  See iasValid contract in
        # onspeed_core/proto/DisplaySerial.h.  Invalidity wins over
        # the live `ias_kts` value — a defensively-set NaN with
        # `ias_valid=True` still falls through `_clamp_uint`'s NaN
        # branch (returns the lower clamp 0), but the canonical caller
        # (live mode / v3 log replay) pairs NaN with `ias_valid=False`.
        ias10_field = (
            IAS_INVALID_WIRE_SENTINEL if not self.ias_valid
            else _clamp_uint(self.ias_kts * 10, 0, 9999)
        )
        payload = (
            "#1"
            f"{WIRE_VERSION:02d}"
            f"{_clamp_int(self.pitch_deg * 10, -999, 999):+04d}"
            f"{_clamp_int(self.roll_deg * 10, -9999, 9999):+05d}"
            f"{ias10_field:04d}"
            f"{_clamp_int(self.palt_ft, -99999, 99999):+06d}"
            f"{_clamp_int(self.turnrate_dps * 10, -9999, 9999):+05d}"
            f"{_clamp_int(self.lateral_g * 100, -99, 99):+03d}"
            f"{_clamp_int(self.vertical_g * 10, -99, 99):+03d}"
            f"{_clamp_uint(self.percent_lift_pct * 10.0, 0, 999):03d}"
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
            f"{(self.validity & 0xFFFF):04X}"
        )
        if len(payload) != PAYLOAD_LEN:
            raise AssertionError(
                f"payload length {len(payload)} != {PAYLOAD_LEN}: {payload!r}"
            )
        crc = _crc8(payload.encode("ascii"))
        return f"{payload}{crc:02X}\r\n".encode("ascii")
