---
name: release-notes
description: Use when the user asks to release a new OnSpeed firmware version, draft release notes for vX.Y, tag a new version, prepare or publish a GitHub release for OnSpeed-Gen3, or summarize what changed since the last release tag.
---

# OnSpeed Release Notes

## Overview

OnSpeed release notes are **an internal team update**. The audience is:

- The pilots flying OnSpeed.
- The test pilots.
- The project developer.

They already know what OnSpeed is. They already fly with it. **Nobody reading the release notes is evaluating the product for the first time, and nobody needs to be sold on it again.** The goal is not to make them slap their leg going "wow, what an update" — it is to tell them, plainly, what changed.

The pilots on this team do not want marketing voice from their own project. They want the state of things.

What the notes need to communicate, in order:

1. **Pilot notes.** Lead with this. Bulleted list, grouped by surface (Audio / M5 display / LiveView / etc.) when there's enough variety to group. Each bullet is one concrete behavior change a pilot or test pilot can verify on the next flight, in plain language. The PR cite goes at the end in parentheses; the bullet itself does not lead with the PR number or the firmware-internal mechanism.

   **Don't label the audience.** Do not name the section `## What pilots will notice` or `## For pilots` or `## Pilot-facing changes`. The whole release is for pilots — labeling the audience is condescending. Call this section `## Pilot notes`, or `## Release notes`, or just the version (`## v4.22`); pick whatever reads cleanest and use it consistently across releases. Same rule for sub-headers: `**Audio**`, not `**What pilots hear**`. Same rule for the watch-outs section: `## Things to watch out for` or `## Before you fly v4.22`, not `## What pilots need to do`.
2. **Things to watch out for.** Bulleted list of pilot-action items: required reflashes of paired hardware (M5, huVVer-AVI), behavior changes that need verification before relying on tones, deprecations. Empty section is fine if there's nothing — but if there's a wire-format break, an algorithm-default change, or a known-unverified feature, it goes here, not buried below.
3. **Recalibration status.** Does the pilot need to recalibrate after flashing? Mandatory callout every time, even when the answer is no.
4. **Context and theme.** One paragraph at the top of "What changed". (In a longer release this can also serve as the intro under the title — but if the pilot-first list above already conveys the theme, don't restate.)
5. **What changed in the code.** Organizing theme, subsystem-level. Exactly one person cares about the detail; summarize, don't geek out. PR numbers are the backreference for anyone who does care.
6. **One-liners at the bottom** grouping the rest of the technical updates and fixes by subsystem.

**The pilot-first lead is non-negotiable.** A reader who scrolls only the first screen of the release page should know what they will hear, see, and need to do — not what cadence the audio engine ports from or which file the AHRS sign convention realigns. The engineering depth lives below the fold for the developer who cares.

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
8. **Lead with the pilot notes, not the theme paragraph.** Bulleted, plain-language, grouped by surface (Audio / M5 display / LiveView / etc.) when there's variety. The theme paragraph and engineering depth live below the fold. The lead section is named `## Pilot notes` (or just the version, e.g. `## v4.22`) — never `## What pilots will notice` or `## For pilots`. See "What the notes need to communicate, in order" above and the v4.21 reference release.
9. **Render-test the GitHub markdown.** Release notes render under GitHub Flavored Markdown, not MkDocs Material. `~text~` is strikethrough not subscript; `!!! warning` admonitions don't exist; `$math$` doesn't render. After pushing the draft, grep the rendered body for `~[A-Za-z]` and visually scan the page. See "GitHub markdown — different from the docs site" below.

Violating the letter of these rules is violating the spirit. Don't rationalize.

## Workflow

Create a TodoWrite todo for each phase. Mark complete as you go.

### Phase 0 — Docs audit (mandatory)

Before any scope work on release notes, run the `docs-update` skill against the same release window. The docs site is not hand-maintained prose — it is a derived document whose defaults, console commands, CSV columns, tone thresholds, and wizard fields all come from specific source files in the firmware. If those sources changed since the last release and the docs didn't, the release will ship with documentation that contradicts the code the pilot just flashed.

Invoke the `docs-update` skill with the same version window you intend to release. It will:

1. Walk the firmware-to-docs mapping for every file that changed since the previous tag.
2. Verify each concrete claim on the docs site against the live code — the defaults path is `FOSConfig::LoadDefaultConfiguration()` in `Config.cpp`, which delegates to `OnSpeedConfig::LoadDefaults()` in `onspeed_core`.
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

- `software/sketch_common/src/audio_io/Audio.cpp` / `.h`
- `software/sketch_common/src/tasks/AHRS.cpp` / `.h`
- `software/sketch_common/src/drivers/IMU330.cpp` / `.h`
- `software/sketch_common/src/drivers/HscPressureSensor.cpp` / `.h`
- `software/sketch_common/src/drivers/SensorIO.cpp` / `.h`
- `software/sketch_common/src/io/EfisSerialPort.cpp` / `.h` (plus `software/Libraries/onspeed_core/src/efis/`)
- `software/sketch_common/src/config/Config.cpp` / `.h`
- `software/sketch_common/src/tasks/Flaps.cpp` / `.h`
- `software/sketch_common/src/tasks/Housekeeping.cpp` / `.h` (G-limit, 3D audio, Vno chime logic)
- `software/sketch_common/src/tasks/LogSensor.cpp` / `.h`
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

## Pilot notes

<Group by surface when there's variety: Audio / M5 display / LiveView / EFIS / Logs. If there are only 2-3 bullets, skip the grouping. Sub-headers are surface names — `**Audio**`, `**M5 display**`, `**X-Plane plugin**` — not audience labels like `**What pilots hear**`.>

**<Surface 1>**

- **<Bold one-line plain-language change>** <One sentence of context if it isn't self-evident from the bold lead. Plain language. The PR cite goes at the end> (PR #NNN).
- ...

**<Surface 2>**

- ...

## Things to watch out for

- **<Bold action item>.** <Why the pilot needs to do this, in one or two sentences. Examples: paired-hardware reflash, algorithm-default verification before relying on tones, known-unverified preview firmware.>

(Skip this section entirely only when there really is nothing to flag — no wire-format break, no algorithm change a pilot might trip over, no preview hardware. When in doubt, include it.)

## <Recalibration required | No recalibration required>

<Mandatory recalibration callout — see Phase 4. Pilots have already seen the headline by this point, so the callout can be terse: one short paragraph reiterating the verdict and naming the safety-critical changes you weighed.>

---

## What changed

<Optional one-paragraph theme statement here, IF the pilot-first lead above hasn't already conveyed the theme. End with "N PRs since v<prev>." Skip the paragraph if it would just restate the bullets above.>

### <Headline 1> (PR #NNN)

<1–3 paragraphs. Lead with the user impact, then the mechanism. Cite related PRs.>

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

#### GitHub markdown — different from the docs site

Release notes render on github.com under GitHub Flavored Markdown. The docs site renders under MkDocs Material with KaTeX, custom admonitions, and a pile of MkDocs extensions. **Do not paste docs-site syntax into release notes.** Specific traps:

- **`~text~` is strikethrough on GitHub.** It is *not* subscript. The docs site uses `L/D~MAX~`, `V~REF~`, `α~stall~` to render KaTeX-style subscripts; on GitHub those render as ~~MAX~~, ~~REF~~, ~~stall~~ with the text struck through. Write `L/Dmax`, `Vref`, `alpha_stall` (or `α_stall` if the underscore reads cleanly) in release notes. Subscripts with `<sub>...</sub>` HTML render but look fussy — prefer flat text.
- **`$math$` and `$$math$$` do not render KaTeX on GitHub releases.** They render as literal text with dollar signs. (In some GitHub UI surfaces they render via MathJax, but release-notes pages historically do not.) Write the math inline as a code-formatted expression: `(BodyAngle − Alpha0) / (AlphaStall − Alpha0)`. If you need superscript-style notation, use `^` (e.g. `K/IAS^2`) and accept that it reads as a caret.
- **`!!! warning`, `!!! note`, `!!! danger` admonitions do not render.** They appear as literal text. Use `> **Warning:**` blockquotes or just bold-led paragraphs.
- **Fenced code blocks render fine.** Triple-backtick with a language tag (`` ```bash ``) gets syntax-highlighted; same for `cpp`, `python`, `markdown`.
- **Bold and italic render fine.** `**bold**`, `*italic*`, `~~strikethrough~~` (double-tilde, intentional) all work.
- **Em-dashes and en-dashes render fine.** Use them. `—` for em, `–` for en.
- **Inline code with backticks renders fine.** `` `g_Sensors.AOA` `` works for filenames, function names, and config keys.
- **Lists need a blank line before them.** GitHub is stricter than MkDocs — a list immediately after a paragraph without a blank line will render as a continuation of the paragraph.

**Verification step:** after pushing the draft, run `gh release view v<version> --json body --jq '.body'` and grep for `~[A-Za-z]` to find leftover docs-site subscripts. Do this every release.

#### Voice — internal team update, not marketing

**Every reader can detect AI marketing speak.** If the prose sounds like it's *selling* OnSpeed, rewrite it. OnSpeed has already been bought, installed, and flown by everyone reading the release notes. They want the state of things.

The voice is **the team telling the rest of the team what landed this release.** Not the dev lecturing pilots, not the pilots reporting to the dev — just the people who build and fly this thing comparing notes. Statements of fact, not claims of improvement. "The M5 display now responds at the same rate the audio fires." Not "The M5 display finally responds quickly!" and not "Experience the improved M5 display responsiveness."

**Rewrite any sentence that matches these patterns:**

| Marketing tone | Engineering-update tone |
|---|---|
| "no longer lies on first load" | "initialized hidden; first render shows N/A until first WebSocket message" |
| "finally stays in sync with the audio" | "double EMA pass removed; M5 AOA now arrives at audio cadence" |
| "brand-new X-Plane simulator support" | "X-Plane 12 plugin. Reads the sim's AOA dataref, plays the same tones." |
| "safer defaults across the board" | "Fresh install boots silent instead of with one specific RV-4's calibration baked in." |
| "train at home with the same audio cues" | (cut — pilots know what a sim is for) |
| "the shared-codebase release" | "onspeed_core extraction. Logic shared across Gen3, M5, X-Plane plugin." |
| "exciting new feature" | (cut — the feature's description is the excitement) |

#### Pilot-facing voice — no engineering vocabulary above the fold

The "Pilot notes" and "Things to watch out for" sections are read by pilots. Pilots do not know — and do not need to know — what a "wire bump" is, what "PROGMEM" is, what "subpixel smoothness" or "anti-aliasing" or "EMA" or "lerp" or "dataref" or "OpenAL streaming pipeline" is. They know what they will hear, what they will see, and what they have to do. Anything else above the fold is a tone-deaf engineer talking past them.

**The translation rule:** describe the pilot-visible effect in plain words. The mechanism, if it goes anywhere at all, lives in "What changed" further down. If you find yourself naming a function, a struct, a wire offset, a flag, a build define, or a math operator in a pilot-facing bullet, rewrite the bullet.

| Engineer voice (don't, in pilot sections) | Pilot voice (do) |
|---|---|
| "Wire format bumps from 76 → 77 bytes" | "You have to flash both the M5 and the Gen3 box together — old M5 firmware will not work with the new Gen3 firmware" |
| "Index bar renders at sub-pixel temporal smoothness off the 20 Hz frame cadence" | "The index bar moves smoother than before and feels more responsive" |
| "`pipPctLift` lerps clean → fullflap by the flap-handle ratio" | "The L/Dmax pip slides as you move the flap handle" |
| "`gOnsetRate` is computed from the verticalG dataref through `GOnsetFilter` with a pre-smoother" | "The G-onset bar actually responds when you pull" |
| "Streaming OpenAL pipeline fed by `Envelope` / `AudioMixer` / `AudioOrchestrator`; pulse cadence is sample-accurate" | "Audio sounds like the box" |
| "Wire emit gates on `sim/time/paused` so the M5's gHistory buffer stops scrolling" | "The G-load history page freezes when the sim pauses" |
| "M5GFX's bundled CJK font tables and unused panel-driver classes were exported from the dylib; `-fvisibility=hidden` plus dead-strip flags trims it 80×" | "The plugin file is 660 KB now, not 54 MB. Same indexer, same audio." |
| "Preact application served from gzipped PROGMEM bundles" | "The web pages have been rewritten" |
| "The cal wizard no longer applies a client-side EMA. The on-screen readouts update at WebSocket rate instead of through a 250 ms low-pass" | "The cal wizard's on-screen readouts update faster — the values you watch during a stall pull no longer go through a quarter-second visual smoothing pass" |
| "Loaded-ear gain rises from 1.000 to 1.000 (saturation in both) but the unloaded-ear gain drops from 0.131 to 0.429" | "Expect the directional cue to feel sharper in coordinated turns" |
| "`SUPPORT_WIFI_CLIENT` and `SUPPORT_CONFIG_V1` build flags are gone" | (cut from pilot sections — this is developer-facing housekeeping; goes in the post-fold "Cleanup" section if anywhere) |

**Pilot-facing words that don't belong**, in approximate order of how often the LLM reaches for them:

- wire / wire format / frame / payload / offset / byte / packet
- subpixel / sub-pixel / anti-aliased / fractional pixels
- EMA / low-pass / filter / decimation / interpolation / lerp / dataref
- struct / field / flag / build flag / `#define` / namespace / handler / endpoint / route
- PROGMEM / heap / DMA / mutex / SDK / bundle / blob
- saturation / saturating / monotonic / continuous / discontinuity
- "emit" / "consume" / "consumer" / "transport" / "pipeline"

**Test before publishing the pilot section:** read each bullet aloud. Imagine the audience is a pilot at the airport reading on their phone. If you'd be embarrassed by a bullet, rewrite it. If a phrase would force the pilot to ask "what's a wire?", rewrite it. The engineering reader gets their detail in the post-fold sections — they're not deprived by your translating up top, they're served by it.

**Specific words to avoid**, because the LLM produces them by default and they set off marketing-detection:

- finally, now at last, at long last
- no longer, no more, never again
- brand-new, all-new, truly new
- exciting, amazing, revolutionary, seamless
- train at home, get started, take advantage of, leverage
- "what pilots will love" / "pilots can now experience"
- "empowers the pilot to…"
- "under the hood"
- "ensures that…", "in order to…"
- "whether you're a […] or a […]"

**Structural rules:**

- **No bullet starts with "We" or "You".** Third person or imperative. "Replaced the…", "The fix…", "Flash the new firmware and…".
- **No "various improvements" or "minor fixes"** — every bullet names something specific.
- **No emoji** unless the previous release used them.
- **Pilot-relevant detail first, developer-relevant detail second**, within any bullet or section.
- **Recalibration callout is present and unambiguous.** Every release.

**Leave sales to the website.** The release notes are a log of what changed. If the state of the system is genuinely better, that speaks for itself in the plain description. "The AOA bar is hidden until the first WebSocket message arrives" is enough — reading that, anyone can work out that the previous behavior was worse.

#### Voice — no dunking on the past

Marketing fluff is one failure mode. **Dunking on the past is the other**, and it's the one this team is most likely to slip into when describing a fix. The team reading the notes wrote the prior code. They don't need to be told their old code was bad — they wrote the fix; that's what changed. Describe the new behavior, name the prior behavior only when the contrast is needed for clarity, and skip the editorializing.

**Banned phrasings — these read as the author dunking on the past:**

- "for as long as anyone's been watching", "for years", "long-standing", "always lied"
- "silently skipped", "silently broken", "silent failure" *(when describing past behavior — fine when describing actual silent-fault modes the new code guards against)*
- "smoking gun", "forensic trail", "dead end"
- "we used to…", "the old code…" *(if the contrast is genuinely needed for the reader to understand the fix, fine; otherwise just say what the code does now)*
- "finally", "at last", "no longer" *(also banned in the marketing-tone list above — listed here too because it's the most common dunking phrase)*

**Replace with present-tense statements of current behavior:**

| Don't | Do |
|---|---|
| "Pitch has drifted ±0.5–1° for years; finally fixed." | "Pitch holds steady at rest. Cause: pitot noise feeding the comp factors. Fix: gate comp on `iasAlive`." |
| "The parser silently skipped this field." | "The parser now reads bytes 3–10 as `HHMMSSFF`." |
| "Long-standing bug where the AOA bar lied on first load." | "AOA bar initializes hidden; first render is `N/A` until the WebSocket delivers a value." |
| "The first field failure used to be a dead end." | "Records reset reason and prior-boot uptime to NVS + SD on every power-up." |

The reader can infer "this used to be wrong" from "the new code does X." That inference doesn't need help. State the current behavior; let the diff speak for the rest.

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
| Writing in "selling OnSpeed to pilots" voice | The audience is pilots who already own and fly OnSpeed. They are the team, not the customer. Marketing voice reads as condescending. | Plain statements of what changed. See "Voice — internal team update, not marketing". |
| "No longer…", "finally…", "brand-new…" in bullets | These phrases imply a customer-journey frame ("you were suffering, now you're saved"). Team members do not need to be reminded the old version was worse. | State the current behavior. The previous state is implicit. |
| Pitching the pilot-facing features at the top as "what you can do now" | Pilots already know what OnSpeed does. Adding a feature is not a pitch event. | Describe the feature in the same plain voice as the rest of the release. |
| Trusting a PR body for a safety-critical file | The PR author may not have realized the implication of their change. | Read the diff yourself for any file in the safety-critical list. |
| Omitting the recalibration callout because "nothing changed" | Pilots need to see it explicitly stated, every release. | Always include it. The "no recalibration needed" version is one paragraph. |
| Using "various bug fixes" or "minor improvements" | Every bullet must name something specific. | Re-read the PR; find the specific thing. |
| Letting the title sprawl | Long titles look unprofessional and don't render well in GitHub's release list. | 1–2 headlines, mirror v4.15/v4.16. |
| Publishing the draft because it looks good | Publishing is the user's call, every time. | Stop at the draft. Show the URL. |
| Generating notes from `git log --oneline` alone | Commit subjects describe what, not why or how it matters. | Read the PR body for every commit. |
| Forgetting to confirm release count and target with the user | A surprise scope or wrong target wastes work. | Confirm both before Phase 3. |
| Leading the release with a theme paragraph full of internal mechanism (`compFadeIn_`, `q_bias`, `safeAsin`) | The first thing a pilot reads should be what they will hear, see, or need to do — not the firmware-internal terms. The engineering depth lives below the fold. | "Pilot notes" bullets first, grouped by surface (Audio / M5 / LiveView). "Things to watch out for" second. Theme paragraph optional, kept terse, lower in the page. |
| Naming the lead section `## What pilots will notice` or `## For pilots` | The whole release is for pilots. Labeling the audience is condescending — pilots reading their own project's notes don't need to be told the notes are for them. | `## Pilot notes`, `## Release notes`, or just the version (`## v4.22`). Stay consistent across releases. |
| Using docs-site KaTeX subscript syntax (`L/D~MAX~`, `V~REF~`) in release notes | GitHub renders `~text~` as strikethrough, not subscript. The pilot sees ~~MAX~~ struck through. | Use flat text in release notes — `L/Dmax`, `Vref`, `alpha_stall`. The KaTeX syntax is an MkDocs Material extension that does not exist on github.com release pages. |
| Pasting `!!! warning` / `!!! note` admonitions from docs-site prose | They render as literal text on GitHub releases. | Use `> **Warning:**` blockquotes or bold-led paragraphs. |

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
- "This bullet sounds a little promotional but it's fine" → it isn't. Rewrite in plain-statement voice. Readers detect marketing tone immediately in their own project's release notes, and it reads as condescending from their own team.
- "Pilots will be excited about X" → cut the excitement framing. Describe X. Let X speak for itself.
- "I'll lead with the theme paragraph, the bullets can come later" → no. Lead with the "Pilot notes" bullets. The pocket-protector engineering details live below the fold.
- "I'll name the lead section `## What pilots will notice` so the audience is clear" → no. The audience is obvious; labeling it is condescending. `## Pilot notes` or just the version.
- "`L/D~MAX~` is what the docs site uses, it'll be fine here" → no. GitHub renders that as strikethrough. Use `L/Dmax` flat.
- "I'll paste this `!!! warning` block from the docs and it'll work" → no. GitHub does not render MkDocs admonitions. Use `> **Warning:**`.

All of these mean: stop, follow the workflow, do not cut corners.

## Reference: example releases

Read these in order — each one shows the structure the next was built on, with the pilot-first lead introduced at v4.21.

- **v4.15** — `gh release view v4.15` — Major safety/calibration release with mandatory recalibration. Use as the template for "recalibration required" framing.
- **v4.16** — `gh release view v4.16` — Delivery-focused release with no recalibration needed. Use as the template for "no recalibration required" framing and for releases dominated by docs/infrastructure work.
- **v4.21** — `gh release view v4.21` — Audio engine port + percent-lift wire change + EKF6 correctness, with M5 reflash required. **Use as the canonical structure** (note: v4.21 still uses the older `## What pilots will notice` header — v4.22 onward use `## Pilot notes` per the audience-labeling fix; structure underneath is the same): pilot-notes bullets grouped by surface, then "Things to watch out for" with paired-hardware reflash + EKF6 verification, then "No recalibration required", then "What changed" with Highlights, then per-section one-liners. This is the structure introduced in v4.21 in response to the v4.21-draft feedback that the engineering-deep theme paragraph was burying the pilot-relevant changes.

Read all three. They show the full range of section vocabulary in action.
