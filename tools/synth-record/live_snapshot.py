"""LiveSnapshot — the per-tick aircraft-state struct produced by scenarios.

Aligned with `DisplayBuildInputs` (onspeed_core/proto/DisplaySerial.h) and
shaped to match the eventual `LiveDataFrame` struct from
docs/ARCHITECTURE_DECOUPLING.md §1. When that struct lands in
onspeed_core/proto/, this Python class becomes a thin wrapper around its
codec.
"""

from __future__ import annotations
from dataclasses import dataclass


@dataclass
class LiveSnapshot:
    t:           float = 0.0   # seconds since scenario start
    aoa:         float = 0.0   # body-angle AOA (deg)
    ias:         float = 0.0   # knots
    pitch:       float = 0.0   # deg
    roll:        float = 0.0   # deg
    yaw_rate:    float = 0.0   # deg/s, +nose-right
    vertical_g:  float = 1.0   # g
    # Lateral G — BALL-FRAME convention (positive = leftward acceleration).
    # This is the same convention as the `#1` wire's `lateralG` field
    # (see proto/DisplaySerial.h::DisplayBuildInputs::lateralG), which
    # the firmware produces by negating `g_AHRS.AccelLatCorr`.
    #
    # WARNING — this differs from the SD log's `LateralG` column AND from
    # the WebSocket's `lateralGLoad` field, both of which use the
    # body-frame convention (positive = airframe accelerating right).
    # When importing from a log row, negate the body-frame value.
    # `scenarios/from_log.py::_log_to_wire_lateral_g` does this.
    #
    # On screen: positive lateral_g (leftward airframe accel) → ball
    # drawn RIGHT of center (the ball lags rightward by inertia).
    # Anti-yaw rudder is on the side the ball moved → press right.
    lateral_g:   float = 0.0
    # Flap state — scenarios drive `lever_raw` (the raw pot ADC reading).
    # The orchestrator's display-anchors harness derives the active
    # detent index and the interpolated `flap_deg` for the wire.  When
    # `lever_raw` is None the orchestrator picks the pot value of the
    # closest detent to `flap_deg`, so old scenarios that set `flap_deg`
    # directly still work.
    lever_raw:   int   | None = None
    flap_deg:    int   = 0     # detected flap angle in degrees (0/16/33 typical)
    palt:        float = 0.0   # ft
    vsi:         float = 0.0   # fpm
    oat:         int   = 15    # °C
    flight_path: float = 0.0   # deg
    g_onset:     float = 0.0   # g/s
    data_mark:   int   = 0
    spin_cue:    int   = 0     # filled by orchestrator after SpinDetector tick

    def copy(self) -> "LiveSnapshot":
        return LiveSnapshot(**self.__dict__)
