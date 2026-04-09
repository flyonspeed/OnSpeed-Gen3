---
name: release-notes
description: Use when the user asks to release a new OnSpeed firmware version, draft release notes for vX.Y, tag a new version, prepare or publish a GitHub release for OnSpeed-Gen3, or summarize what changed since the last release tag.
---

# OnSpeed Release Notes

## Overview

OnSpeed firmware ships to pilots flying real aircraft. Release notes are not changelogs — they are a safety document, a marketing document, and a learning aid all at once. A returning pilot needs to know in 10 seconds whether their calibration is still valid; a new pilot needs to know what changed and why; a developer needs to know what's been refactored.

This skill captures the OnSpeed-specific process for getting from "release v4.X" to a high-quality draft GitHub release. It does **not** publish — publishing is always a manual step the user takes after reviewing the draft.

## When to Use

- User says "release v4.X", "let's release version X", "draft release notes for vX", "tag a new version", "prepare a release"
- A version bump is being prepared and notes need to be written
- The previous release left a draft that needs revision (re-run the skill to regenerate from a fresh research pass)

## When NOT to Use

- The user just wants a changelog or commit summary (use `git log` directly)
- The user wants to update CLAUDE.md, docs, or any in-repo file based on recent changes (this skill writes to the GitHub release, nothing in the repo)
- The user wants you to **publish** an existing draft — that is a manual step they perform, not an action this skill takes

## The Iron Rules

1. **Never publish.** This skill creates drafts only. Never run `gh release edit --draft=false`, never push tags, never flip a draft live. Show the URL and stop.
2. **Never tag from a feature branch without explicit confirmation.** Default target is `origin/master` HEAD. If the user is on a feature branch, ask them to confirm the target before proceeding.
3. **Never summarize a PR from its commit subject alone.** Always read the full PR body via `gh pr view <NNN>`.
4. **Never describe a safety-critical change without reading the diff.** See the safety-critical file list below.
5. **Always include a recalibration callout.** Every release. Even if the answer is "no recalibration needed."
6. **Always calibrate voice against the previous release.** Read it before drafting. The previous release is the canonical example of project tone and structure.
7. **Always audit the docs site for drift before drafting.** Pilots read the docs to understand what a release means — stale docs turn a clean release into a support problem. See Phase 0 below.

Violating the letter of these rules is violating the spirit. Don't rationalize.

## Workflow

Create a TodoWrite todo for each phase. Mark complete as you go.

### Phase 0 — Docs audit (mandatory)

Before any scope work on release notes, run the `docs-update` skill against the same release window. The docs site is not hand-maintained prose — it is a derived document whose defaults, console commands, CSV columns, tone thresholds, and wizard fields all come from specific source files in the firmware. If those sources changed since the last release and the docs didn't, the release will ship with documentation that contradicts the code the pilot just flashed.

Invoke the `docs-update` skill with the same version window you intend to release. It will:

1. Walk the firmware-to-docs mapping for every file that changed since the previous tag.
2. Verify each concrete claim on the docs site against the live code (not `ConfigDefaults.h` — always the `LoadDefaultConfiguration()` function in `Config.cpp`).
3. Open a separate `docs/drift-fixes-v<version>` PR with a strict mkdocs build.

Do not proceed to Phase 1 of the release workflow until the docs audit PR is open (or the user explicitly says "no drift, continue"). A release with stale docs is a release that generates support tickets the day it ships.

### Phase 1 — Discover scope

```bash
# Find the previous release tag
git tag -l "v*" | sort -V | tail -1

# Default target ref is the tip of master
git fetch origin
git log v<prev>..origin/master --oneline
```

**Decision points:**
- Confirm with the user which version number they want (`v4.16`, `v4.17`, etc.)
- Confirm the target ref. If the user is checked out on a feature branch, **ask explicitly** whether to release from `origin/master` (almost always yes) or from the branch
- Note the exact commit SHA of the target — you will pass this to `gh release create --target <sha>`. Never use a branch name as the target; branch HEAD can move while you work

### Phase 2 — Calibrate voice against the previous release

```bash
gh release view v<prev>
```

Read the entire body. This is the canonical example of:
- How the release is framed (one-paragraph theme statement at the top)
- Section vocabulary and ordering
- Level of technical detail in bullets (deeply technical, "the bug was X, the consequence was Y, the fix is Z")
- How PRs are linked (`(PR #NNN)` inline, often multiple PRs per bullet)
- Tone (direct, no marketing fluff, explains *why* a change matters to a pilot)

Do not skip this step. Do not assume you remember the voice. Read it fresh every time.

### Phase 3 — Per-PR research

For every commit between previous tag and target:

```bash
# Extract PR number from squash subject "(#NNN)" and fetch the PR
gh pr view <NNN> --json number,title,body --jq '{n:.number,t:.title,b:.body}'
```

**Parallelize aggressively.** Issue many `gh pr view` calls in a single message — they're independent and finish in seconds.

For commits **without** a `(#NNN)` PR reference (rare — direct master pushes, unsquashed merge commits):

```bash
git show <sha>
```

Treat the commit message as the source of truth.

#### Safety-critical tripwire

If the PR touches **any** of these files or directories, you MUST run `gh pr diff <NNN>` (or `git show <sha>`) and read the diff yourself before writing any bullet about it:

- `software/OnSpeed-Gen3-ESP32/Audio.cpp` / `.h`
- `software/OnSpeed-Gen3-ESP32/AHRS.cpp` / `.h`
- `software/OnSpeed-Gen3-ESP32/IMU330.cpp` / `.h`
- `software/OnSpeed-Gen3-ESP32/HscPressureSensor.cpp` / `.h`
- `software/OnSpeed-Gen3-ESP32/SensorIO.cpp` / `.h`
- `software/OnSpeed-Gen3-ESP32/EfisSerial.cpp` / `.h`
- `software/OnSpeed-Gen3-ESP32/Config.cpp` / `.h` / `ConfigDefaults.h`
- `software/OnSpeed-Gen3-ESP32/Flaps.cpp` / `.h`
- `software/OnSpeed-Gen3-ESP32/gLimit.cpp` / `.h`
- `software/OnSpeed-Gen3-ESP32/LogSensor.cpp` / `.h`
- `software/OnSpeed-Gen3-ESP32/3DAudio.cpp` / `.h`
- `software/OnSpeed-Gen3-ESP32/VnoChime.cpp` / `.h`
- Anything in `software/Libraries/onspeed_core/` (EKF6, Madgwick, Kalman, AOACalculator, ToneCalc, OnSpeedTypes, CurveCalc, EMAFilter, SavGolDerivative)
- Anything matching `Calibration*`, `EKF6*`, `Madgwick*`, `Kalman*`

The PR body alone is **not** enough for safety-critical code. A developer can describe what they changed without realizing the implication. You owe it to the pilot to read the diff.

#### Per-PR digest

Before moving on, write a one-line digest for each PR. Format:

```
PR #NNN | <theme tag> | safety-critical: yes/no | <one-sentence summary>
```

Themes you'll typically see (loose vocabulary — pick what fits, group related PRs):
`safety` `data-quality` `display` `web-ui` `bug-fix` `code-quality` `performance` `infrastructure` `docs` `delivery` `calibration` `ahrs` `efis` `audio`

These digests become the raw material for grouping in Phase 4. Don't write release-note prose yet.

### Phase 4 — Synthesize the framing

#### Find the theme

Write one sentence that explains why this release exists. Then compare it against the previous release's theme so the two read as a sequence:

> "v4.15 was the safety + calibration release. v4.16 makes that easy to get and easy to learn — docs site and automated firmware downloads."

If you can't write this sentence in one try, you don't understand the release yet. Go back to Phase 3.

#### Mandatory recalibration callout

Based on the safety-critical PRs you reviewed in Phase 3, decide which of these to write:

**If recalibration IS required**, write a section explaining:
- Which specific changes invalidate old calibration data
- What the pilot must do (re-run wizard, re-flash defaults, etc.)
- Why — link to the underlying physics or sensor change

Example (v4.15): "The pressure sensors now read at full 14-bit resolution (was incorrectly reading only 12 bits), so old bias values are wrong for the new scale."

**If recalibration is NOT required**, write a one-paragraph section explicitly listing what you checked and confirmed unchanged:

Example (v4.16): "Calibration math, AHRS algorithms, sensor reads, and tone setpoints are all unchanged from v4.15. Flash v4.16 over v4.15 and your existing calibration carries over."

This section is mandatory. Even when the answer is "no", the pilot needs to see it explicitly stated. Place it immediately after the intro paragraph, before "Highlights".

#### Group PRs into sections

Use the section vocabulary below. Skip sections that have no items. Section order is approximately as listed (Highlights first, Testing and Getting last).

**Section vocabulary** (use when applicable):

| Section | Use for |
|---------|---------|
| **Highlights** | Major user-facing features. Each gets an H3 subheading and a paragraph or two. Always present. 2–4 highlights is typical. |
| **Safety & Reliability** | Bugs that could cause incorrect tones, NaN propagation, missed stall warnings, crashes, audio failures, EFIS dropouts. |
| **Display & Data Quality** | What pilots see (LiveView, AOA needle, percent-lift) and what gets logged (CSV columns, display serial output). |
| **Bug fixes & UX** | Non-safety functional bugs and UX polish. Use when there's no clearer home for a fix. |
| **Configuration & Web UI** | Config page, validation, persistence, web UI changes, console commands. |
| **Data quality** | Log columns, CSV parsing, data integrity. Alias for "Display & Data Quality" when there's no display side. |
| **Performance & Infrastructure** | Speed, memory, CI, build system, code refactors, test additions, `onspeed_core` extractions. |
| **Code quality & infrastructure** | Alias for "Performance & Infrastructure" when there's no measurable perf change. |

Pick at most one of each alias pair. Don't worry if a release uses sections the previous release didn't — that's expected. The vocabulary is consistent, the selection is adaptive.

### Phase 5 — Draft the release notes

Write to `/tmp/v<version>-release-notes.md`.

#### Document structure

```markdown
**Release date:** <Month YYYY>

<One-paragraph theme statement. Reference the previous release for continuity if it makes sense. End with the PR count: "N PRs since v<prev>.">

## <Recalibration required | No recalibration required>

<Mandatory recalibration callout — see Phase 4.>

---

## Highlights

### <Headline 1> (PR #NNN)

<1–3 paragraphs. Explain WHY this matters to a pilot or developer. Lead with the user impact, not the technical change. Cite related PRs if any.>

### <Headline 2> (PR #NNN, #NNN)

<...>

---

## <Section from vocabulary>

- **<Bold short title>** (PR #NNN): <Single dense paragraph. Describe the bug, the consequence, and the fix. For safety bugs, lead with what could happen to the pilot. For features, lead with what the pilot can now do.>
- **<...>** (PR #NNN): <...>

## <Next section>

- ...

---

## Testing

v<version> ships with **N unit tests** (up from <prev count> in v<prev>) covering all `onspeed_core` modules, with CI building both V4P and V4B variants and running tests on every push. All tests pass; both firmware variants compile clean with `-Werror`.

\`\`\`bash
pio run -e esp32s3-v4p   # Build V4P firmware
pio run -e esp32s3-v4b   # Build V4B firmware
pio test -e native       # Run all N tests
\`\`\`

## Getting v<version>

**Recommended**: Download `onspeed-<version>-v4p-firmware.bin` (or `-v4b-firmware.bin`) from the assets below and flash via the OTA update page in the OnSpeed web UI. See [Flashing Firmware](https://dev.flyonspeed.org/software/flashing/) and [OTA Updates](https://dev.flyonspeed.org/software/ota-update/) in the docs for full instructions, including how to identify which hardware variant you have.

For a fresh-install or recovery flash via USB, the assets also include `onspeed-<version>-bootloader.bin` and `onspeed-<version>-partitions.bin`, which are shared across both variants (identical bytes); flash addresses are documented in `README.md` and the flashing guide.
```

#### Writing the bullets

- **Lead with bold short title.** "Vno chime test button returning 400 Bad Request" not "Fixed a bug where".
- **State the consequence.** Why does this matter? Who notices? "Stall warning could go silent at low TAS" is a stronger lead than "asin() can return NaN".
- **Explain the mechanism in one sentence.** "asin() returned NaN because Kalman VSI transients pushed inputs past ±1.0; NaN propagated through FlightPath → DerivedAOA → tone comparisons all returned false → silence."
- **Cite the fix in one phrase.** "Added safeAsin() that clamps inputs to the valid range."
- **Link multiple PRs when they're one logical change.** "(PRs #46, #54)" is fine.
- **Get the test count right.** Run `pio test -e native --list` or check `test/` directories — don't make it up.

#### Tone checks before saving

- No marketing fluff. ("Revolutionary new..." → no.)
- No emoji unless the previous release used them.
- No bullet starts with "We" — use third person or imperative ("Replaced the...", "The fix...").
- No "various improvements" — every bullet names something specific.
- Pilot-relevant detail goes first; developer-relevant detail goes second.
- The recalibration callout is present and unambiguous.

### Phase 6 — Create the draft (HARD STOP)

```bash
gh release create v<version> \
  --draft \
  --target <commit-sha> \
  --title "v<version>: <Headline 1>[, <Headline 2>]" \
  --notes-file /tmp/v<version>-release-notes.md
```

**Title format:** `v<version>: <Headline 1>[, <Headline 2>]`. Mirror v4.15 (`v4.15: Full-Resolution Sensors, Physics-Based Calibration, EKF`) and v4.16 (`v4.16: Documentation Site, Automated Firmware Releases`). Capture the 1–2 most important things. No more.

**The target MUST be a commit SHA, not a branch name.** Branches can move; the SHA pins the release to exactly what you researched.

**After running the command, verify:**

```bash
gh release view v<version> --json isDraft,tagName,targetCommitish,url
```

Confirm `isDraft: true` and `targetCommitish` matches the SHA you intended. Report both back to the user along with the URL.

### What this skill does NOT do

- **Does not publish.** No `gh release edit --draft=false`. No `git push --tags`. No `gh release create` without `--draft`.
- **Does not upload assets.** The repo's `release.yml` workflow attaches firmware binaries automatically when the user publishes.
- **Does not update files in the repo.** No CLAUDE.md edits, no CHANGELOG, no README. Release notes live on GitHub.
- **Does not create PRs or merge branches.** Pre-release code work is the user's responsibility.
- **Does not bump version constants in code.** Version comes from git tags via `BuildInfo::version`; no source files need updating.

## Common Mistakes

| Mistake | Why it's wrong | Fix |
|---------|----------------|-----|
| Using `--target master` | Branch HEAD can move while you're drafting. The release you tested could differ from what's tagged. | Always pass an explicit commit SHA. |
| Skipping the previous-release voice read | You will drift toward generic AI release-note voice. The OnSpeed voice is specific. | `gh release view v<prev>` every time. |
| Trusting a PR body for a safety-critical file | The PR author may not have realized the implication of their change. | Read the diff yourself for any file in the safety-critical list. |
| Omitting the recalibration callout because "nothing changed" | Pilots need to see it explicitly stated, every release. | Always include it. The "no recalibration needed" version is one paragraph. |
| Using "various bug fixes" or "minor improvements" | Every bullet must name something specific. | Re-read the PR; find the specific thing. |
| Letting the title sprawl | Long titles look unprofessional and don't render well in GitHub's release list. | 1–2 headlines, mirror v4.15/v4.16. |
| Publishing the draft because it looks good | Publishing is the user's call, every time. | Stop at the draft. Show the URL. |
| Generating notes from `git log --oneline` alone | Commit subjects describe what, not why or how it matters. | Read the PR body for every commit. |
| Forgetting to confirm release count and target with the user | A surprise scope or wrong target wastes work. | Confirm both before Phase 3. |

## Red Flags — STOP and reconsider

These thoughts mean you're rationalizing:

- "I remember the OnSpeed voice from earlier in the conversation" → re-read the previous release anyway
- "This PR looks too small to need its body read" → read it
- "This file changed but it's just a comment" → if it's in the safety-critical list, read the diff anyway
- "I'll just publish the draft, it's fine" → no. Hard stop at draft.
- "The branch HEAD won't move in the next 30 seconds" → use the SHA anyway
- "I'll skip the recalibration callout this time, nothing important changed" → write it anyway, even if it's "nothing changed"
- "I can paraphrase the PR description without reading it" → no
- "The target should obviously be `master`" → confirm with the user, especially if checked out on another branch
- "I'll batch-publish two releases at once" → no, draft only, one at a time, manual publish

All of these mean: stop, follow the workflow, do not cut corners.

## Reference: example releases

The two best examples to imitate are:

- **v4.15** — `gh release view v4.15` — Major safety/calibration release with mandatory recalibration. Use as the template for "recalibration required" framing.
- **v4.16** — `gh release view v4.16` (or the draft) — Delivery-focused release with no recalibration needed. Use as the template for "no recalibration required" framing and for releases dominated by docs/infrastructure work.

Read both. They show the full range of section vocabulary in action.
