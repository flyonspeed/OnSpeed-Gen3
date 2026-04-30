"""Shared Python module for OnSpeed flight-data tooling.

Provides:

- `frame`        — `Frame` builder for the `#1` 74-byte display-serial wire
                   format; mirrors `onspeed_core/proto/DisplaySerial.{h,cpp}`.
- `config`       — `FlapSetpoints` dataclass + `load_flap_setpoints()` that
                   handles both the V1 list-style and the new per-flap-block
                   `.cfg` formats.
- `percent_lift` — `compute_percent_lift()` (single-linear envelope fraction
                   matching `onspeed_core/aoa/PercentLift.cpp`) and
                   `ias_from_aoa()` (inverts `K/IAS² + alpha_0` for V2+ configs).
- `log_replay`   — `scenario_from_log()` adapter that reads OnSpeed SD-log
                   CSVs (old or new format) and emits `LiveSnapshot` ticks
                   at a target rate.
- `live_snapshot` — `LiveSnapshot` dataclass; per-tick aircraft-state struct
                    consumed by display, audio, and log-replay code paths.

Consumers:

- `tools/m5-replay/replay.py` imports `Frame`, `FRAME_LEN`, `FlapSetpoints`,
  `load_flap_setpoints`, `setpoints_for_flap`, `compute_percent_lift`.
- `tools/synth-record/` (post-PR 7) imports the same plus `ias_from_aoa`,
  `LiveSnapshot`, and `scenario_from_log`.

The wire format here matches the firmware on `master` (76-byte v4.22
frame, `PAYLOAD_LEN=72`, with `pip_pct_lift`). Any future wire change
updates this module and the firmware's `BuildDisplayFrame` /
`ParseDisplayFrame` in lockstep.
"""
