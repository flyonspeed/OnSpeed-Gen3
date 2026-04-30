# PR 9 spec — Hybrid log-then-synthetic scenario composer

**Status:** SPEC ONLY — review before agent dispatch.
**Depends on:** PR 8 (`scenario_from_log` + log-window CLI).
**Estimated diff:** ~50 lines of glue + ~150 lines of `vac_spin_hybrid.py` scenario.

## What ships

A small composability primitive that lets a scenario be expressed as **a log window followed by a synthetic continuation**. The Vac spin hybrid is the worked example: real cruise-to-stall-break from Vac's flight, then synthetic spin development → cue → recovery.

## API

In `tools/onspeed_py/log_replay.py` (or a new `tools/onspeed_py/composer.py`):

```python
def chain_log_then_synthetic(
    log_states: Iterator[LiveSnapshot],
    synthetic: Iterator[LiveSnapshot],
    *,
    bridge_time_s: float = 0.5,
) -> Iterator[LiveSnapshot]:
    """Concatenate a log-derived stream with a synthetic continuation.

    The synthetic phase's first tick is interpolated from the log's
    last tick over `bridge_time_s` seconds so the handoff is smooth
    (no step discontinuity if synthesizer started at a different
    state than the log ended).

    Both streams must yield LiveSnapshots at 50 Hz.  Output stream's
    `t` field is restamped to be monotonic from 0.
    """
```

Implementation: yield all log_states, peek the first synthetic state, yield bridge ticks (smoothstep from last_log to first_synth field-by-field over `n_bridge` ticks), then yield the rest of synthetic.

The bridge handles:
- AOA crossover (synthetic might start at a different AOA than log ended)
- Ball position (lateral_g handoff smooths the inertia)
- Roll/pitch (mid-roll handoff stays continuous)

Without the bridge, the synthetic phase's first tick would step-discontinuously from log's last state, which the M5 sees as a "spike" in the EMA filters.

## The hybrid scenario

`tools/synth-record/scenarios/vac_spin_hybrid.py`:

```python
from pathlib import Path
from onspeed_py.log_replay import scenario_from_log, chain_log_then_synthetic
from onspeed_py.live_snapshot import LiveSnapshot
from scenarios._envelopes import smooth, hold_state, n_ticks
import onspeed_py.config as cfg_mod

VAC_LOG = Path.home() / "Downloads/vac_log.csv"
VAC_CFG = Path.home() / "Downloads/vac_config.cfg"
LOG_T_START   = 2857.0
LOG_T_HANDOFF = 2877.58   # Vac's last real tick — wing dropping, yaw building

def scenario():
    cfg = cfg_mod.load_flap_setpoints(VAC_CFG)
    fs0 = cfg[0]

    # --- Phase 1: real-flight log ---
    log_states = list(scenario_from_log(
        log_path=VAC_LOG, cfg_path=VAC_CFG,
        t_start_s=LOG_T_START, t_end_s=LOG_T_HANDOFF,
    ))
    last = log_states[-1]

    # --- Phase 2: synthetic spin development ---
    # ... define spin_onset, spin_developing, spin_hold, recover, pullout
    # using `last` for handoff continuity ...

    # All sign conventions per the LiveSnapshot docstring (ball-frame
    # lateral_g, yaw_rate body-frame).  See PR-8 commit notes for the
    # full lecture; mistakes here flip the cue or mirror the ball.

    synthetic = env.chain(spin_onset, spin_developing, spin_hold, recover, pullout)
    synthetic = env.add_realistic_jitter(synthetic, ...)

    yield from chain_log_then_synthetic(iter(log_states), synthetic)
```

The synthetic phase is essentially the synthesized-spin block from the spike's `vac_spin_hybrid.py` (with the lateral_g sign fix already applied). Lift it as-is.

## Sign convention reminder

This is the scenario where the cue + ball + yaw + lateral_g all need to agree. **Document this in the scenario file's docstring**:

| State | yaw | lateral_g (ball-frame) | ball position | cue value | rudder press |
|---|---|---|---|---|---|
| Right spin | +ve | -ve | left | -1 | left |
| Left spin | -ve | +ve | right | +1 | right |

Vac was breaking left → synthetic phase extends with yaw_rate negative, lateral_g positive, cue +1 (right rudder).

The bug we caught during the spike (lateral_g sign wrong → ball mirrored relative to cue) is exactly the failure mode this docstring guards against.

## Acceptance criteria

- [ ] `python3 record.py scenarios/vac_spin_hybrid.py --cfg ~/Downloads/vac_config.cfg --out /tmp/hybrid.mp4` produces a valid ~25s MP4.
- [ ] Spin cue active for ≥ 100 ticks (≥ 2 seconds) during the synthetic phase.
- [ ] At a sample frame mid-spin: ball on the right, arrow pointing right, RUDDER text red, right pedal lit.  (All four signals consistent for a left spin → press right rudder.)
- [ ] Audio L/R ratio shows pan walking right during the spin (sample rate of L/R peak per second; R/L should rise above 5 during developed spin).
- [ ] PR description shows: a representative frame from each phase (cruise / stall break / mid-spin with cue / recovery), the audio pan analysis, and a one-paragraph "what this demonstrates."

## Risks

1. **Bridge artifacts.** The 0.5s smoothstep bridge can produce mid-render visible glitches on the indexer if log's last state and synthetic's first state differ wildly. Mitigation: synthetic's first state is constructed FROM `last` (the log's last tick), so the bridge is mostly a no-op. Test by setting bridge_time_s=0 and verifying the render still looks smooth.

2. **Audio mid-handoff.** Wire frames at the bridge boundary mix log-derived and synthetic AOA values. Audio path's tone state machine should handle this without artifacts (the EMA on lateral_g smooths the transition; the tone-region boundaries are sharp by design).

3. **Sign convention regression.** Easy to slip when authoring synthetic continuations. Mitigation: PR adds an automated test (or just a runtime assertion in the hybrid scenario) that the cue's sign matches `-sign(yaw_rate)` and that ball-frame lateral_g and yaw_rate have opposite signs.

## Open questions

1. Should `chain_log_then_synthetic` live in `onspeed_py` or in `tools/synth-record/scenarios/`?  My vote: `onspeed_py` — it's reusable.
2. Should the bridge be a smoothstep, or just instant?  Spike used "synthetic starts from log's last state" as the interpolation seed; that gives an instant handoff that still looks smooth because the synthetic is *anchored* to the log's last value.  Maybe drop the bridge entirely if synthetic always anchors.

## Estimated agent dispatch effort

- ~2 hours.
