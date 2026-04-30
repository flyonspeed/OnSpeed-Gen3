# PR 10 spec — Skill registration (`.claude/skills/synth-record.md`)

**Status:** SPEC ONLY — review before agent dispatch.
**Depends on:** PR 7 (`tools/synth-record/`) at minimum; ideally also PRs 8 + 9 for complete coverage.
**Estimated diff:** ~120 lines new skill file.

## Why this skill

The synth-record tool ships in PR 7+8+9. The skill makes it trigger-discoverable for Claude — when the user asks "make a video of the spin cue" or "show me what the box did at that moment in this log," the relevant skill content loads and Claude knows the recipe.

The audience for the skill is **Claude (me-future)**, not contributors. Contributor docs live in `tools/synth-record/README.md`.

## Trigger patterns

The skill should activate on any of:

- "record a video of [feature]" / "make a [demo / clip] of X"
- "show me what the box [did / would show / sounded like]"
- "render a [10-second / clip / demo / video] of [scenario / log / log window]"
- "make a demo for [Vac / Lenny / Zach / a pilot]"
- "generate audio for [scenario / log window]"
- "replay [log file] at [time / window]"
- "what did the audio sound like during [event] in [log]"
- "synthetic data video"
- "log replay video"

## Skill content shape

Follow the project's existing `.claude/skills/` style (see `.claude/skills/liveview-edit/SKILL.md`, `.claude/skills/release-notes-feedback/SKILL.md` for reference).

```markdown
---
name: synth-record
description: Render OnSpeed demo videos with synced audio, from synthetic
  scenarios or real flight logs.  Use when the user asks for a demo /
  clip / video of any OnSpeed feature, or wants to replay a window of
  a real SD log with the audio + indexer the box would have produced.
---

# synth-record

Render audio+video clips of OnSpeed features.  Audio is byte-identical
to what the firmware would produce; visuals come from the actual M5
firmware code running headlessly through SDL2.

## When to use this skill

[trigger patterns from above, paraphrased]

## Quickstart

The tool lives at `tools/synth-record/`.  See its README for in-depth
docs; this skill is for the common patterns.

## Pattern: synthetic scenario

[copy-paste example: spin_recovery.py invocation]

## Pattern: real-log replay

[copy-paste example: --log-csv invocation, fixture-log path notes]

## Pattern: log + synthetic hybrid

[copy-paste example: vac_spin_hybrid.py invocation]

## Authoring scenarios

[short DSL reference: `chain`, `hold`, `smooth_to`, `aoa_ramp`,
`add_realistic_jitter`; pointer to scenarios/_envelopes.py for full set]

## Where outputs land

`tools/synth-record/out/`.  Default upscale is 1280x960; pass
`--scale-to <N>` for other sizes.

## Sign conventions (CRITICAL)

[the LiveSnapshot docstring's lateral_g + yaw_rate + cue table — copy
verbatim with a "spend the time to read this before authoring a spin
scenario" note]

## When audio sounds wrong

1. Check that `audio_harness.cpp` is built fresh (`build_harnesses.sh`).
2. Check `--firmware-pan` flag — without it, demo-tool's punchier pan
   override is active.
3. Check that PR 5 / PR 6 are at the expected versions (the tool
   inherits their behavior; if the firmware tone path changed, the
   tool tracks).

## When the ball renders mirrored

The log column `LateralG` is body-frame; the wire and `LiveSnapshot`
expect ball-frame.  `from_log.py` handles the negation.  If you're
authoring a synthetic scenario and the ball renders on the wrong
side, your sign is wrong.  See live_snapshot.py docstring for the
full lecture.

## Issues to know about

- #371 (3D-pan curve) — fixed in PR 1.  Pre-fix master has the bug;
  if you're rendering against an old firmware, audio pan will look
  weak in spins.
- #372 (flapsRawADC) — fixed in PR 3.  Old logs without the column
  use the synthetic lever-sweep workaround.
- #374 (lateralG sign convention split) — open.  Document in scenario
  comments when authoring.
```

## Acceptance criteria

- [ ] `.claude/skills/synth-record/SKILL.md` exists with the shape above.
- [ ] Trigger phrases listed in the description are exhaustive enough that the typical user request matches.
- [ ] All commands shown in the skill body have been verified to actually run against current master.
- [ ] Cross-references to `tools/synth-record/README.md` (contributor docs) are accurate.
- [ ] Cross-references to issues #371, #372, #374 reflect their merge status at the time of skill PR.

## Risks

1. **Trigger over-firing.** "Make a demo" is broad; the user might want a marketing video, not a tool render. Mitigate by listing the trigger phrases narrowly enough that only OnSpeed-feature demo requests match.

2. **Trigger under-firing.** Better than over-firing; the user can always invoke explicitly.

## Open questions

1. Should the skill body include the full DSL reference, or just point at `_envelopes.py`?  My vote: short reference inline (~10 lines), pointer for the rest.
2. Should the skill register as a Claude skill via `Skill` tool, or as a project plugin?  Per project convention (the existing skills like liveview-edit), it's `.claude/skills/<name>/SKILL.md`.

## Estimated agent dispatch effort

- ~30 minutes once PRs 7-9 are in.
