# INDEX — Post-Overlay Plans (2026-05-14)

**Date:** 2026-05-14
**Owner:** Sam
**Status:** Plan set, three documents — none merged yet. This branch is the don't-merge artifact (mirrors PR #504's pattern).

> **Context for a fresh agent**: Project C (the video overlay replay tool) shipped in May 2026 via PRs #512, #524, #526, #527, #528/#529/#532, #545, #548. The original plan PR #504 was closed 2026-05-14 with a comprehensive shipped-state summary. This branch adds three new plans for work that emerged AFTER the overlay project completed.

## Reading order for a fresh agent

If you're picking up this work cold, read these documents in this order:

| # | Doc | Status | Purpose |
|---|---|---|---|
| 0 | `2026-05-08-replay-INDEX.md` | Historical reference | The pre-existing INDEX from Project C. Lists the architectural invariant ("`onspeed_core` at the heart, every consumer through canonical paths, no hand-ports"). Read for the one-sentence story. |
| 1 | `2026-05-14-post-overlay-consolidation.md` | **Active plan** | Bulldog architecture audit (2026-05-14) found three remaining hand-ports + one missing drift test. Plan kills them all in four small PRs. Includes 9 rider tech-debt items folded into the same PRs. |
| 2 | `2026-05-14-replay-sidecar-persistence.md` | **Active plan** | Sidecar JSON persistence for the replay tool. Flight test engineer's notes + clips + sync points move from IndexedDB to a `.replay.json` file next to the log. One sidecar per log, **triple-keyed debrief records** for (video, log, config) combinations. Includes deep research on FS Access API + cross-browser story + sidecar prior art (XMP, Lightroom, etc.). |
| 3 | `2026-05-14-ekf-tuning-log-fidelity.md` | **Active plan — TIME-SENSITIVE** | Vac's next flights are for EKF6 tuning. Audit found **six concrete gaps** in the SD log that prevent byte-faithful offline replay (no per-row dt, no `compFadeIn`, no cfg snapshot, etc.). Five gaps require firmware changes before Vac flies. ~1.5 days of firmware work. |

## Priority

- **Plan 3 (EKF tuning log fidelity)** is time-sensitive — gates Vac's flights. Read first if executing.
- **Plan 1 (consolidation)** is the natural next step after Project C closed. Most-recently-merged work touched these files; cleanup rides cheaply.
- **Plan 2 (sidecar persistence)** is anxiety-driven — pilot work shouldn't live only in browser cache. Independent of Plans 1 and 3.

## What's NOT in here

- Madgwick centripetal-gate firmware fix. Bench-tested on `feature/bundler-esbuild` branch (uncommitted) but VSI symptom is dominated by comp-factor residual, not pitch leak. The gate works exactly as designed for pitch but doesn't fix VSI alone. Decision deferred pending more work.
- VSI fix proper. Open. Will likely require comp-factor pipeline revision, which is more invasive.
- X-Plane plugin work, M5 firmware work, huVVer-AVI updates — these consumers are all clean per the consolidation audit; no plans needed.

## How to use these

**For a dispatched agent**: agent prompt should reference the specific plan file:

```
Read docs/superpowers/plans/2026-05-14-ekf-tuning-log-fidelity.md
as the spec. The agent should branch off master and ship its PR
against master; this plan branch stays here as a reference.
```

**For Sam**: these plans are first-draft and ready to evolve. Each one explicitly calls out its acceptance criteria. When a plan ships (its code merges to master), the plan's status moves to "shipped" here, but the plan file stays as historical context.

## Branch disposition

This branch (`sam/plans-postoverlay`) is the don't-merge artifact, anchored on master so the PR diff shows "what's in plans that isn't yet in master." Same convention as the now-closed PR #504. Don't merge; review via the PR UI; close when all three plans have shipped.
