# PR 8 spec — `scenarios/from_log.py` + log-window CLI

**Status:** SPEC ONLY — review before agent dispatch.
**Depends on:** PR 6 (`onspeed_py.log_replay::scenario_from_log`) and PR 7 (`tools/synth-record/`).
**Estimated diff:** ~150 lines new (+ 3 fixture-using scenario files), ~50 lines deleted from PR 6's `log_replay.py` if shape needs to change.

## What ships

The synth-record spike's `from_log.py` adapter and three concrete scenarios that use it:

- `scenarios/from_log.py` (or import from `onspeed_py.log_replay`) — the adapter
- `scenarios/vac_decel_run.py` — Vac's t=2857-2885 decel-to-stall-to-recovery
- `scenarios/sam_approach_flaps.py` — Sam's t=836-852 flap deployment
- `--log-csv <path> --window <t_start>-<t_end>` CLI flag for one-off renders without writing a scenario file

## API decision

Per PR 6 spec the adapter lives in `onspeed_py.log_replay`. PR 8 just adds the CLI flag to `record.py` and the three scenario files (which are 5 lines each, calling `scenario_from_log`).

If PR 6's `scenario_from_log` shape ends up different from the spike's, this PR adapts. Most likely:
- Function signature is the same (log_path, cfg_path, t_start_s, t_end_s, kwargs).
- Return type is `Iterator[LiveSnapshot]`.
- `flap_overrides`, `smooth_accels`, `fake_lever_sweep` kwargs match.

## CLI extension

```bash
record.py --log-csv <path> --cfg <path> --window <t_start>-<t_end> --out <mp4>
                          [--no-smooth-accels]      # disable α=0.0609 EMA on accels
                          [--no-fake-lever-sweep]   # disable sweep workaround for #372
```

When `--log-csv` is passed, the positional `<scenario_path>` is omitted. They're mutually exclusive; argparse enforces it.

## The three scenario files

Each is ~5-10 lines:

```python
# scenarios/vac_decel_run.py
from onspeed_py.log_replay import scenario_from_log
from pathlib import Path

VAC_LOG = Path.home() / "Downloads/vac_log.csv"
VAC_CFG = Path.home() / "Downloads/vac_config.cfg"

def scenario():
    yield from scenario_from_log(
        log_path=VAC_LOG, cfg_path=VAC_CFG,
        t_start_s=2857.0, t_end_s=2885.0,
    )
```

Same shape for `sam_approach_flaps.py` (different paths, t=836-852).

For `vac_spin_hybrid.py` (the log-then-synthetic composer) — defer to PR 9.

## Fixtures

The fixtures committed for PR 6 (`tools/onspeed_py/tests/fixtures/vac_decel_run.csv`, `vac_config.cfg`, `n720ak_config.cfg`) are reused. The scenarios point at the user's `~/Downloads/` path because the full Vac and Sam logs are 25–80 MB each (not committable). Add a doc note in `tools/synth-record/README.md` explaining: scenarios pointing at `~/Downloads/` paths assume the user has the log; if you don't, change the path or use the committed fixture for a smaller demo.

## Acceptance criteria

- [ ] `python3 record.py --log-csv ~/Downloads/vac_log.csv --cfg ~/Downloads/vac_config.cfg --window 2857-2885 --out /tmp/vac.mp4` produces a valid 28s MP4.
- [ ] `python3 record.py scenarios/vac_decel_run.py --out /tmp/vac.mp4` does the same thing (the named-scenario form).
- [ ] `python3 record.py scenarios/sam_approach_flaps.py --out /tmp/sam.mp4` works.
- [ ] All renders complete without errors against both V1 (Vac) and new-format (Sam) configs.
- [ ] Renderered audio for the Vac decel matches what the box would have emitted, modulo the spike-era pan tuning if `--firmware-pan` not passed. Verified by ear OR by hashing the audio.pcm against a committed golden.

## Risks

1. **Fixture log path absolutism.** Vac's full log is 25 MB, too big to commit. Sam's is 80 MB. The 30s slice fixture (committed in PR 6) covers Vac's decel for tests but doesn't help users who want to render their own approach. Document the workflow: copy log to `~/Downloads/`, render against it; or use the slice fixture for committed-test renders.

2. **`--window` argparse parsing.** Format is "2857-2885" (dash-separated seconds). Edge case: negative seconds (no, log timestamps are always positive). Edge case: window spans a whole second boundary (just float-arithmetic, fine).

## Open questions

1. Does the `--log-csv` CLI flag form replace the named-scenario form, or coexist? My vote: coexist. Quick one-off renders use the flag form; reusable demos get a scenario file.
2. Do we want `--from-log <log> --window ...` syntax instead of `--log-csv`? Bikeshed.

## Estimated agent dispatch effort

- ~1.5 hours.
