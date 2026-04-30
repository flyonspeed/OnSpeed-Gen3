# PR 6 spec — Shared `tools/onspeed_py/` Python module

**Status:** SPEC ONLY — review before agent dispatch.
**Estimated diff:** +400 lines new module, -200 lines from m5-replay (factored out), -200 lines from synth-record (factored out, after PR 7).
**Independent of PRs 1, 2, 3, 5.** Can land in parallel with any of them.

## What the bug is

`tools/m5-replay/replay.py` and `tools/synth-record/wire_frame_builder.py` (in the synth-record-spike branch) both contain a near-identical `Frame` builder, `FlapSetpoints` dataclass, percent-lift formula, and config XML parser. They diverged because synth-record needed to skip the `pyserial` import that m5-replay does at module level, and added V1-config support that m5-replay doesn't have.

| Symbol | In m5-replay | In synth-record |
|---|---|---|
| `Frame` (74-byte builder) | ✓ (76 bytes per #336) | ✓ (76 bytes) |
| `_clamp_int / _clamp_uint` | ✓ | ✓ |
| `FlapSetpoints` dataclass | ✓ | ✓ + `pot_value`, `k_fit` |
| `load_flap_setpoints` (new XML) | ✓ | ✓ |
| `_load_flap_setpoints_v1` (V1) | ✗ | ✓ |
| `setpoints_for_flap` | ✓ | ✓ |
| `compute_percent_lift` | ✓ | ✓ |
| `ias_from_aoa` (K/IAS² inversion) | ✗ | ✓ |
| `csv_frame_stream` (log → frame) | ✓ | partial (in `from_log.py`) |
| `synthetic_stream` (built-in scenario) | ✓ | external scenarios |
| `stream_to_serial` (USB-TTL output) | ✓ | ✗ |

The first 7 rows are duplicates and should land in one place.

## What the consequences are

1. Wire-format changes have to land in two files (m5-replay's `Frame.to_bytes` and synth-record's). PR #336 already landed once and we had to chase it through the synth-record fork.

2. V1 support and `ias_from_aoa` are stuck in synth-record. Anyone using m5-replay against an old config from a customer can't.

3. The fixture-based regression tests in `test/fixtures/v1-config-and-log/` (added during the synth-record spike) only exercise the synth-record copy. The m5-replay copy bit-rots silently against V1 configs.

## Proposed module layout

```
tools/onspeed_py/
├── __init__.py
├── README.md                      ← what's here, what calls it, what doesn't
├── frame.py                       ← Frame builder + clamp helpers
├── config.py                      ← FlapSetpoints, load_flap_setpoints (V1+new)
├── percent_lift.py                ← compute_percent_lift, ias_from_aoa
├── log_replay.py                  ← csv_frame_stream-style adapter (column-name aliasing,
│                                    EMA smoothing, lever-sweep workaround for #372,
│                                    body→ball lateralG negation)
└── tests/
    ├── test_frame_roundtrip.py    ← byte-identical round-trip vs onspeed_core::ParseDisplayFrame
    ├── test_config_v1.py          ← V1 fixture parses correctly (digit-prefix, lists, defaults)
    ├── test_config_new.py         ← new-format fixture parses correctly
    ├── test_log_replay.py         ← Vac fixture log → expected LiveSnapshot tick stream
    └── fixtures/                  ← lifted from test/fixtures/v1-config-and-log/
        ├── vac_config.cfg
        ├── vac_decel_run.csv
        ├── n720ak_config.cfg          (current N720AK config — new format)
        └── README.md
```

## API surface

### `frame.py`

```python
PAYLOAD_LEN = 72   # v4.22; bumped to 74 if a future wire change lands
FRAME_LEN   = 76

@dataclass
class Frame:
    """All fields in one #1 wire payload.  Names match
    DisplaySerial.h::DisplayBuildInputs."""
    pitch_deg: float = 0.0
    # ... (one field per wire field, no aliases)
    pip_pct_lift: int = 0   # v4.22+

    def to_bytes(self) -> bytes:
        """76-byte wire frame, byte-identical to onspeed::proto::BuildDisplayFrame."""
```

(Single source of truth.  `m5-replay/replay.py` imports `from onspeed_py.frame import Frame, FRAME_LEN`.)

### `config.py`

```python
@dataclass
class FlapSetpoints:
    degrees: int
    pot_value: int                  # iPotPosition for lever ADC interpolation
    ldmax_aoa: float
    onspeed_fast_aoa: float
    onspeed_slow_aoa: float
    stallwarn_aoa: float
    alpha_0: float                  # 0.0 default for V1 configs
    alpha_stall: float              # stallwarn + 1.5 default for V1 configs
    k_fit: float = 0.0              # 0.0 if not stored

def load_flap_setpoints(cfg_path: Path) -> dict[int, FlapSetpoints]:
    """Auto-detect V1 list-style or new per-FLAP_POSITION-block format.
    Preprocesses digit-prefix XML tags (<3DAUDIO>) before parsing.
    Returns {degrees: FlapSetpoints}."""

def setpoints_for_flap(deg: int, table: dict[int, FlapSetpoints]) -> FlapSetpoints:
    """Exact match or nearest detent."""
```

### `percent_lift.py`

```python
def compute_percent_lift(aoa: float, fs: FlapSetpoints) -> int:
    """Honest single-linear envelope fraction, mirroring
    onspeed_core/aoa/PercentLift.cpp::ComputePercentLift."""

def ias_from_aoa(aoa: float, fs: FlapSetpoints, n: float = 1.0) -> float:
    """Invert AOA = K/IAS² + alpha_0 to get IAS for a given body angle.
    Returns 0 if k_fit is unset (V1 configs).  n = vertical G load
    (defaults to 1.0)."""
```

### `log_replay.py`

```python
def scenario_from_log(log_path: Path,
                      cfg_path: Path,
                      t_start_s: float,
                      t_end_s: float,
                      *,
                      target_rate_hz: float = 50.0,
                      smooth_accels: bool = True,
                      fake_lever_sweep: bool = True,
                      flap_overrides: dict | None = None) -> Iterator[LiveSnapshot]:
    """OnSpeed SD-log replay → LiveSnapshot tick stream.

    - Old-format columns (AngleofAttack) and new-format (DerivedAOA) both work.
    - Handles V1 configs and new-format configs.
    - Body-frame log lateralG is negated to ball-frame for LiveSnapshot
      (see _log_to_wire_lateral_g; bug #374 tracks the convention split).
    - α=0.0609 EMA smoothing applied to lateral_g, vertical_g, pitch,
      roll when smooth_accels=True (Gen2 logs are pre-smoothing).
    - Synthetic lever sweep across detent transitions when
      flapsRawADC isn't in the log (#372).  4-second sweep centered
      on the snap tick.
    - Optional flap_overrides: dict of {flap_deg: {alpha_0, alpha_stall, k_fit}}
      to substitute fit-derived values for V1 configs.
    """
```

The `LiveSnapshot` dataclass either lives in `onspeed_py` (since both m5-replay and synth-record need it for log replay) or gets its own module `live_snapshot.py`. Since m5-replay's existing `csv_frame_stream` returns `Frame` directly (not `LiveSnapshot`), this is a minor schema decision. Recommendation: put `LiveSnapshot` in `onspeed_py.live_snapshot`; `Frame` is for direct wire I/O, `LiveSnapshot` is the orchestrator's input shape. m5-replay can use either depending on what's natural for its serial-output path.

### Tests

Each test imports the fixtures from `tools/onspeed_py/tests/fixtures/`:

1. **`test_frame_roundtrip.py`** — build a `Frame` with a deterministic field set, call `.to_bytes()`, then parse with the C++ harness from `tools/m5-replay/parse_frame` and assert every field round-trips. Reuses m5-replay's existing Layer-2 test infrastructure.

2. **`test_config_v1.py`** — load `fixtures/vac_config.cfg`, assert: 3 detents at 0/20/40 deg, pot values 129/158/206, alpha_0 == 0.0 (V1 default), alpha_stall == stallwarn + 1.5, k_fit == 0.0. Pin one full row of values for flap 0 (`ldmax=8.03`, etc.) so any parser change is a deliberate edit.

3. **`test_config_new.py`** — load `fixtures/n720ak_config.cfg`, assert: 3 detents at 0/16/33 deg, pot values match the file, alpha_0/alpha_stall/k_fit populated from the file (not defaulted), specific values pinned.

4. **`test_log_replay.py`** — load `fixtures/vac_decel_run.csv` + `fixtures/vac_config.cfg`, request a 1-second window, assert: 50 LiveSnapshot ticks emitted, lat_g sign is negated relative to log values, flap_deg unchanged across the window, EMA-smoothed lateral_g converges to a fixed value over the first ~30 ticks (one tau).

5. **`test_log_replay_v4_22.py`** — load a fixture log with `flapsRawADC` column (we'll synthesize one if PR 3 hasn't landed; it's a small synthetic CSV), assert `lever_raw` comes from the column, not the synthesized sweep.

## Migration plan for callers

### `tools/m5-replay/replay.py`

Becomes:
```python
from onspeed_py.frame import Frame, FRAME_LEN
from onspeed_py.config import FlapSetpoints, load_flap_setpoints, setpoints_for_flap
from onspeed_py.percent_lift import compute_percent_lift

# replay.py-specific code stays:
def csv_frame_stream(...):  # log → Frame (its own loop, not the LiveSnapshot one)
def synthetic_stream(...):  # built-in scripted demo
def stream_to_serial(...):  # USB-TTL serial output
def main() -> int:          # CLI
```

Net: `replay.py` keeps ~250 lines of m5-replay-specific code (the synthetic stream + serial driver + CLI), drops ~200 lines of duplication.

### `tools/synth-record/` (after PR 7)

`wire_frame_builder.py` is deleted entirely. `from_log.py` becomes a thin wrapper that imports `scenario_from_log` from `onspeed_py.log_replay`.

`scenarios/*.py` import `Frame`, `FlapSetpoints`, `compute_percent_lift`, `ias_from_aoa` from `onspeed_py`.

## Acceptance criteria

- [ ] `tools/onspeed_py/` module exists with the 4 sub-modules + tests + fixtures listed above.
- [ ] `tools/m5-replay/test_replay.py` (the existing Layer 2 round-trip tests) **still pass** with `replay.py` rewritten to import from `onspeed_py`.
- [ ] New `tools/onspeed_py/tests/` pass: 5 test files, ~12 test cases.
- [ ] `tools/m5-replay/replay.py --synthetic --port /dev/null` runs without error and produces the same first 100 frame bytes as before the refactor (golden test).
- [ ] `docs/site/docs/data-and-logs/` updated to document `tools/onspeed_py/` as the canonical Python-side library; m5-replay README points there.

## Risks

1. **Import path cleanliness**. Currently m5-replay uses a PEP-723 inline-deps header in `replay.py` so `uv run replay.py` works without a project-level pyproject. Moving code into `tools/onspeed_py/` means we need *some* way for m5-replay to find that import path. Two options:
   - Add `sys.path.insert(0, str(Path(__file__).parent.parent))` to `replay.py`. Ugly but works for the script-style invocation.
   - Add a `pyproject.toml` to `tools/` and make `onspeed_py` a real installable package. Cleaner long-term but more setup.
   Recommendation: option (a) for this PR; option (b) as a follow-up.

2. **Pyserial dependency**. m5-replay's `replay.py` imports `pyserial` at module level for its serial-output path. `onspeed_py` shouldn't import pyserial (most consumers don't need it). The serial-output code stays in m5-replay's CLI. Verified: only `stream_to_serial` and the CLI use pyserial.

3. **Test fixture location**. Currently the V1 fixtures live at `test/fixtures/v1-config-and-log/` (alongside firmware fixtures). For Python tests it's tidier to mirror under `tools/onspeed_py/tests/fixtures/`. **Decision**: copy them, don't move them. Firmware tests may want them too eventually.

## Open questions for review

1. Does the module name `onspeed_py` work, or do you prefer something like `onspeed_pylib` / `onspeedlib`? Avoiding `onspeed_core` since that name is the firmware library.
2. Does `LiveSnapshot` belong in `onspeed_py` or stay in `tools/synth-record/`? (My vote: in `onspeed_py.live_snapshot`, since log-replay needs it.)
3. Should the `tools/m5-replay/parse_frame` (C++ Layer 2 firmware-parser harness) move under `tools/onspeed_py/` too, since `test_frame_roundtrip` will rely on it?

## Estimated agent dispatch effort

- Read m5-replay + synth-record Python in full (~30 min).
- Build `onspeed_py/` package + tests + fixtures (~90 min).
- Migrate m5-replay (~30 min).
- Run all tests, verify m5-replay's CLI still works against a local fixture log (~30 min).
- Write the PR (~15 min).
- **Total: ~3 hours.**
