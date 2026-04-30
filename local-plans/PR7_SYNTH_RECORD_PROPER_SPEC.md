# PR 7 spec — `tools/synth-record/` proper

**Status:** SPEC ONLY — review before agent dispatch.
**Depends on:** PR 5 (audio orchestration in `onspeed_core::audio`) and PR 6 (`tools/onspeed_py/` shared module). Don't dispatch until both are merged to master.
**Estimated diff:** ~700 lines added (orchestrator + harnesses + scenarios + sim recording mode), ~150 lines removed (audio orchestration duplicates absorbed into onspeed_core, Frame-builder absorbed into onspeed_py).

## What ships

The spike at `synth-record-spike` branch (in worktree `onspeed-worktrees/synth-record`) has the working tool. PR 7 lifts that into master, with the audio and Python duplications already replaced by `onspeed_core::audio::AudioOrchestrator` (from PR 5) and `tools/onspeed_py/` (from PR 6).

## File-by-file what to land

### NEW

| Path | Source | Notes |
|---|---|---|
| `tools/synth-record/README.md` | NEW | Quickstart, scenario authoring, log replay, where outputs land |
| `tools/synth-record/record.py` | from spike | Orchestrator. Drop the in-spike `wire_frame_builder.py` import; use `from onspeed_py.frame import Frame` instead. |
| `tools/synth-record/build_harnesses.sh` | from spike | Builds the 3 C++ harnesses against `onspeed_core` (no copy of `MakePulseSpec` etc. — uses PR 5's `AudioOrchestrator::DecideAndArm`). |
| `tools/synth-record/audio_harness.cpp` | from spike (modified) | **Drop the local `MakePulseSpec` / `MakeSolidSpec` / SetTone-mimic block (~75 lines).** Replace with `onspeed::audio::DecideAndArm(toneResult, envelope, kCfg)`. The 3D-pan code stays — see "What about the pan tuning?" below. |
| `tools/synth-record/spin_detector_harness.cpp` | from spike, unchanged | Already calls `onspeed::SpinDetector` directly. No changes needed. |
| `tools/synth-record/display_anchors_harness.cpp` | from spike, unchanged | Already calls `onspeed::aoa::ComputeDisplayPctAnchors` directly. No changes needed. |
| `tools/synth-record/scenarios/__init__.py` | from spike, empty | |
| `tools/synth-record/scenarios/_envelopes.py` | from spike | Helpers: `chain`, `hold`, `smooth_to`, `aoa_ramp`, `add_realistic_jitter`, `add_t_offsets`. |
| `tools/synth-record/scenarios/spin_recovery.py` | from spike | Synthetic spin demo. |
| `tools/synth-record/scenarios/ldmax_pip.py` | from spike | Synthetic L/Dmax-pip-slide demo using N720AK config. |
| `tools/synth-record/scenarios/tone_sweep.py` | from spike | Synthetic tone-region tour. |
| `software/OnSpeed-M5-Display/sim/SimRecord.cpp` | from spike | Headless recording-mode entry point. |
| `software/OnSpeed-M5-Display/platformio.ini` | EDIT | Add `[env:native_record]` block from spike. |
| `software/OnSpeed-M5-Display/src/main.cpp` | EDIT | Skip splash screen under `SIM_RECORD` (one `#ifdef`). Already in spike. |

### NOT INCLUDED (saved for later PRs)

- `scenarios/from_log.py` — saved for PR 8.
- `scenarios/vac_decel_run.py`, `vac_spin_hybrid.py`, `sam_approach_flaps.py` — saved for PR 8 / PR 9.
- `live_snapshot.py` — moves to `tools/onspeed_py/live_snapshot.py` per PR 6 spec.
- `wire_frame_builder.py` — entirely replaced by `onspeed_py.frame` per PR 6.
- The local `_fake_lever_sweep` workaround — saved for PR 8.

### TESTS

- `tools/synth-record/tests/test_record.py` — new file.
  - Smoke test: `python3 record.py scenarios/tone_sweep.py --out /tmp/test.mp4` produces a valid MP4. Verify with `ffprobe`.
  - Determinism test: run twice, hash the output bytes, assert identical.
  - Audio bytes regression: render a fixture scenario, hash the audio.pcm, compare against committed golden hash. (Lets us catch unintended audio path changes early.)

## What about the demo-tool pan tuning override?

The synth-record spike has a local 3D-pan override in `audio_harness.cpp` (α = 0.5 instead of 0.1, curve `min(1, 12·|x|)` instead of the firmware's `min(1, 8·|x|)`). This was tuned for demo-video punch — it makes the pan walk to the affected ear within ~one ball-width.

**Decision required:** does PR 7 ship that override, or strictly mirror firmware?

Recommendation: **ship the override**, but make it explicit + opt-out-able. Add a `--firmware-pan` flag to `record.py` that disables the override. Default behavior is the punchy demo-tuned pan; for "what would the box actually sound like" rendering, pass `--firmware-pan`.

The override is then a feature of the *demo tool*, documented as "the tool deliberately overrides the firmware's pan params for demo punchy-ness; pass --firmware-pan to match the box exactly." That's honest and gives both behaviors.

(A secondary path: file a separate firmware PR to actually retune the firmware's α and curve based on what we learned in flight-test. That's a Vac decision, not a synth-record decision.)

## Module-level imports to settle

Per PR 6 spec, the open question of where `LiveSnapshot` lives. I'm specifying for PR 7: **`LiveSnapshot` lives in `onspeed_py/live_snapshot.py`** (lifted from spike) and `tools/synth-record/record.py` does `from onspeed_py.live_snapshot import LiveSnapshot`. No `live_snapshot.py` in `tools/synth-record/`.

The `sys.path.insert` glue in `tools/synth-record/scenarios/*.py` from the spike (which prepends the parent directory) is replaced by `from onspeed_py.live_snapshot import LiveSnapshot` and `from onspeed_py.frame import Frame, FlapSetpoints, ...`. The scenarios become cleaner.

For the path resolution itself, follow PR 6's recommendation (option a — `sys.path.insert(0, str(Path(__file__).parent.parent.parent))` in `record.py`) until a `pyproject.toml` follow-up lands.

## CLI surface

Final CLI for `record.py`:

```bash
record.py <scenario_path> --out <mp4_path>
                          [--cfg <path>]                # default ~/Dropbox/...
                          [--keep-intermediate]         # debug: keep frames.bin/.rgb/.pcm
                          [--firmware-pan]              # use firmware pan params, not demo override
```

PR 8 will add `--log-csv <path> --window <t_start>-<t_end>` for log replay (it'll bypass the `<scenario_path>` argument).

## Acceptance criteria

- [ ] `python3 tools/synth-record/record.py tools/synth-record/scenarios/tone_sweep.py --out /tmp/test.mp4` works.
- [ ] `python3 tools/synth-record/record.py tools/synth-record/scenarios/spin_recovery.py --out /tmp/test.mp4` works.
- [ ] `python3 tools/synth-record/record.py tools/synth-record/scenarios/ldmax_pip.py --out /tmp/test.mp4` works.
- [ ] `tools/synth-record/build_harnesses.sh` builds clean against current `onspeed_core` master.
- [ ] `cd software/OnSpeed-M5-Display && pio run -e native_record` builds clean.
- [ ] `audio_harness.cpp` does NOT contain `MakePulseSpec` / `MakeSolidSpec` symbols — verify by grep. (Confirms PR 5 was actually used.)
- [ ] `wire_frame_builder.py` does NOT exist — verify directory listing. (Confirms PR 6 was actually used.)
- [ ] PR description shows: the diff stats (lines added by `tools/synth-record/`, lines removed because of `onspeed_py` adoption), three render commands and their PASS results, and a `ffprobe` output for one of the produced MP4s.

## Risks

1. **PR 5/6 land in different shapes than the spec calls for.** Mitigation: the implementer reads the merged 5/6 PRs first, adapts as needed, calls out any spec drift in their PR description.

2. **PR 5's `OrchestratorConfig` API doesn't match the harness's needs.** If 5 ships with `kCfg` constants in the header instead of as a struct, the harness will need to adapt. Acceptable.

3. **Sim recording mode breaks under master's M5 changes.** PR #354 (Preact indexer) and #348 (PALT label) and #350 (G rounding) all touched M5 code post-spike. The `SIM_RECORD` flag in `main.cpp` and the `SimRecord.cpp` itself should still work but verify carefully.

## Open questions

1. The pan-override decision above — agreed to ship with `--firmware-pan` opt-out?
2. Should the three demo MP4s (tone_sweep, spin_recovery, ldmax_pips) be committed as fixtures, or just regenerable on demand? My vote: regenerable; commit only ffmpeg-mux command snippets and let CI optionally render them as part of a docs build.
3. Is `tools/synth-record/` the right path, or `tools/synth-record/` → just `tools/record/`? (Naming bikeshed; `synth-record` is more specific to the tool's nature.)

## Estimated agent dispatch effort

- Read PR 5 + PR 6 once they're in master (~30 min).
- Cherry-pick or re-implement spike's record.py + scenarios + harnesses + SimRecord.cpp (~90 min).
- Wire up the new imports (~30 min).
- Write tests (~45 min).
- Verify all three demos render + golden audio match (~30 min).
- PR (~15 min).
- **Total: ~4 hours.**
