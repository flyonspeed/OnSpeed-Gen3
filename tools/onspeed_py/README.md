# `onspeed_py` — shared OnSpeed Python module

Single-source-of-truth Python implementation of the OnSpeed
flight-data pipeline:

- **`frame.py`** — `Frame` builder for the `#1` 76-byte (v4.22) display-
  serial wire format. Mirrors `onspeed_core/proto/DisplaySerial.{h,cpp}`
  byte for byte. Both `tools/m5-replay/replay.py` and
  `tools/synth-record/` consume this.
- **`config.py`** — `FlapSetpoints` dataclass and `load_flap_setpoints()`
  that auto-detects V1 list-style configs and the new
  per-`<FLAP_POSITION>`-block format.
- **`percent_lift.py`** — `compute_percent_lift()` (single-linear envelope
  fraction matching `onspeed_core/aoa/PercentLift.cpp`) and
  `ias_from_aoa()` (inverts `K/IAS² + alpha_0` for V2+ configs).
- **`log_replay.py`** — `scenario_from_log()` adapter that turns OnSpeed
  SD-log CSVs (old or new format) into `LiveSnapshot` ticks at a
  target rate, with body→ball lateral-G negation, EMA accel smoothing,
  and synthetic flap-lever sweep workarounds.
- **`live_snapshot.py`** — `LiveSnapshot` dataclass; the per-tick
  aircraft-state struct that orchestrators consume.

## Who calls this

| Caller | Imports |
|--------|---------|
| `tools/m5-replay/replay.py` | `frame.Frame`, `frame.FRAME_LEN`, `config.FlapSetpoints`, `config.load_flap_setpoints`, `config.setpoints_for_flap`, `percent_lift.compute_percent_lift` |
| `tools/synth-record/` (post-PR 7) | all of the above + `percent_lift.ias_from_aoa`, `live_snapshot.LiveSnapshot`, `log_replay.scenario_from_log` |

## Path resolution

`onspeed_py` is not installed as a package. Consumers add the parent
`tools/` directory to `sys.path` so `from onspeed_py.frame import …`
resolves. m5-replay does this with:

```python
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
```

This keeps the PEP-723 `# /// script` header in `replay.py` working
with `uv run replay.py`. Migrating to a real `pyproject.toml` package
is a future option; the current approach trades a tiny `sys.path`
hack for zero install setup.

## Wire-format alignment

The `Frame` builder emits the 76-byte (v4.22) ASCII frame defined in
`onspeed_core/proto/DisplaySerial.h` (`kDisplayFrameSizeBytes = 76`,
`kDisplayFrameChecksumLen = 72`). The `pip_pct_lift` field at offset
70 was added by PR #336 (separating the visual L/Dmax pip from the
tones-on threshold). Future wire changes update this module and the
firmware's `BuildDisplayFrame` / `ParseDisplayFrame` in lockstep.

## Tests

```bash
cd tools/onspeed_py/tests
python3 -m pytest -v
```

Fixtures live under `tests/fixtures/`:

- `vac_config.cfg` — V1 list-style (3 detents at 0/20/40°).
- `vac_decel_run.csv` — V1 SD-log slice (30 s decel-to-stall).
- `n720ak_config.cfg` — new per-`<FLAP_POSITION>` format with
  calibrated alpha_0/alpha_stall/k_fit (3 detents at 0/16/33°).

Layer 2 firmware-parser interop lives in `tools/m5-replay/` (build
the C++ `parse_frame` harness with `pio run -e native`, then run
`uv run test_replay.py`); the same `Frame` builder feeds both.
