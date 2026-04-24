---
name: docs-update
description: Use when the user asks to audit or update the OnSpeed docs site against current code, fix docs drift, check docs before a release, or verify that config parameters / console commands / log columns / tone mapping / wizard UI in the docs still match the firmware. Run this BEFORE every release.
---

# OnSpeed Docs Update

## Overview

The OnSpeed docs site (`docs/site/`) is not hand-maintained prose — it is a derived document. Every concrete claim on the site (default config values, console command names, CSV column order, tone thresholds, wizard field labels, pin numbers, test counts) is sourced from specific locations in the firmware codebase. When the firmware changes and the docs don't, the site drifts, and drift hurts pilots: someone reads "default volume is 80%" but the code initializes to 100%, or they look for the `FOO` console command that was renamed to `BAR` two releases ago.

This skill captures the repeatable process for auditing the site against current code, fixing what drifted, and matching the site's existing voice when writing new prose. It runs before every release — a release with stale docs is a release that generates support tickets.

## When to Use

- User says "audit the docs site", "does the docs site need updating", "check for docs drift", "fix docs for v4.X"
- Immediately before cutting a release (mandatory — see cross-reference in `release-notes/SKILL.md`)
- After a batch of config, console, log-column, tone-calc, EFIS, or wizard PRs merge
- User points out a specific page that "looks wrong" or "doesn't match what I see in the app"

## When NOT to Use

- User wants to write entirely new pages for a new feature — this skill is about drift, not greenfield authoring (though the content-sourcing and tone rules still apply)
- User wants to edit `CLAUDE.md`, `README.md`, `docs/ROADMAP.md`, or other in-repo design docs that are not part of the MkDocs site
- User wants release notes — use the `release-notes` skill

## The Iron Rules

1. **Every factual claim must have a code source.** Defaults come from `onspeed_core/config/OnSpeedConfig.cpp::LoadDefaults()` (reached via the sketch-side `FOSConfig::LoadDefaultConfiguration()`). Console commands come from `ConsoleSerial.cpp`. Log columns come from `LogSensor.cpp`. Tone thresholds come from `ToneCalc.cpp` / `ToneCalc.h`. Wizard fields come from `ConfigWebServer.cpp::HandleCalWizard()`. Pin numbers come from `Globals.h`. Do not guess, do not invent, do not copy from the previous version of the page.
2. **Read the actual function, not a header that looks authoritative.** Config defaults live in `OnSpeedConfig::LoadDefaults()` in the core library — the old dead `ConfigDefaults.h` string-literal blob has been removed.
3. **Match the existing page's voice.** Dense, minimal prose. Tables over paragraphs. One sentence of context, then the data. No marketing voice, no "this ensures optimal performance", no "simply configure". If a neighboring page says "Configure your aircraft's flap positions. For each flap setting you use:" followed by a numbered list, match that register — don't write a three-paragraph essay.
4. **Never omit a safety-relevant caveat.** If a feature has a known bug, a flight-envelope restriction, or a "don't touch this yet" status, the docs say so explicitly. Tone matters: use `!!! warning` or `!!! danger` admonitions to match the severity.
5. **Strict build before PR.** `mkdocs build --strict` must pass. Broken links, missing anchors, and malformed admonitions fail the build and block the PR.
6. **One PR per audit pass.** Don't mix docs drift fixes with feature work, content reorganizations, or new pages. A clean "fix docs drift from vX.Y work" PR is easy to review and easy to revert.

Violating the letter of these rules is violating the spirit.

## Canonical terminology

The docs site has a standardized vocabulary. Use it verbatim; do not invent synonyms; do not mix conventions within or across pages.

### Aerodynamic states (the Fast / On-Speed / Slow framework)

These terms name aerodynamic conditions of the wing — not tone behaviors, not cockpit sounds, not product features.

| Site term | What it names |
|---|---|
| **Fast** | Wing operating at lower AOA than the reference condition. Aerodynamic margin is large; excess energy available. |
| **ONSPEED** | The reference AOA condition. A narrow AOA band (≈ ±1° in GA aircraft), not a single value. Corresponds to ~60% of maximum lift (≈ 1/(1.3)²). Typically coincides with V~REF~ at 1g. |
| **Slow** | Wing operating at higher AOA than the reference. Lift demand high; induced drag rises sharply; margin from stall reduced. |
| **Stall Warning** | The AOA range just below the critical angle where stall is imminent. Red end of the indexer. |
| **L/D~MAX~** | Maximum lift-to-drag AOA. Configuration-dependent; **not interchangeable with ONSPEED**. In clean configurations L/D~MAX~ is typically at a lower lift fraction; with flaps deployed it may approach ONSPEED. Always render in KaTeX (`L/D~MAX~`). |
| **Critical AOA** / α~crit~ | The AOA at which the wing stalls. Essentially constant for a given flap configuration regardless of weight or load factor. |

**Spelling notes:**

- The current site spelling of the reference condition is **`ONSPEED`** (all-caps). This is a site convention that predates the Fast–On-Speed–Slow canonical paper (Vaccaro et al., unpublished). When that paper is published, a site-wide rename from `ONSPEED` to `On-Speed` (hyphenated, capitalized) becomes a queued docs-drift follow-up. Until then, **keep writing `ONSPEED`** — internal consistency matters more than alignment with an unpublished external standard. Do not introduce mixed spellings.
- **`OnSpeed`** (camelCase) is the product/brand name. Always "OnSpeed Gen3", "OnSpeed system", "OnSpeed firmware". Never render the product name in all-caps.
- **`On-Speed`** (hyphenated) does not currently appear in the site. Reserve it for the future framework-alignment pass.

### Tone regions (OnSpeed product-specific audio cues)

These names describe the audio behavior — distinct from the aerodynamic states above, though they correlate. Standardized by PR #240 and consistent across the M5 display, web liveview, V-n diagram, tone simulator, and audio settings docs.

| Site term | What it names |
|---|---|
| **Silent** / **Silence** | No tone below L/D~MAX~ AOA. |
| **Fast Tone** | Low-pitch pulsing (400 Hz), 1.5 → 8.2 pps. Between L/D~MAX~ and ONSPEED-Fast. "Fast" here refers to the aerodynamic state (fast side of ONSPEED), not the pulse rate. |
| **ONSPEED solid tone** | Low-pitch (400 Hz) continuous tone inside the ONSPEED band. Pulse rate 0 (solid). |
| **Slow Tone** | High-pitch pulsing (1600 Hz), 1.5 → 6.2 pps. Between ONSPEED-Slow and Stall Warning. |
| **Stall warning buzz** | High-pitch (1600 Hz) at 20 pps above the stall-warn threshold. |

Within the pulsing bands, the docs further use **"low-pitch slow tone"** and **"low-pitch fast tone"** (and the high-pitch analogues) to describe the two ends of the pulse-rate interpolation. Do not invent alternatives — these are the canonical phrasings.

### Indexer colors (FAA green / yellow / red progression)

The M5 display, web liveview, V-n diagram fill regions, and future LED indexer all use the same color palette:

| Color | Region |
|---|---|
| Green | Fast Tone band + ONSPEED band (the whole safe side) |
| Yellow | Slow Tone band |
| Red / flashing red | Stall Warning |

Older pages sometimes describe the Fast-side chevron as "blue" or "orange" — both were standardized to green in PR #240. If you find residual blue/orange references on a fast-side docs surface, that's drift to fix.

### Operational framing

- **Push / Hold / Pull / Unload** are the four pilot actions the tone family cues. Match these verbs verbatim on pages that prescribe pilot response to tones.
- **Unload for control** is the italicized operational principle for restoring aerodynamic margin when margins shrink. Glossary entry `Unload for Control`. Use italicized `*unload for control*` in running prose.
- **Directive Information / Descriptive Information** — OnSpeed-specific distinction for why audio cueing is different from visual instruments. Glossary-level; use sparingly.
- **Lift demand** (the aerodynamic quantity AOA measures) and **aerodynamic margin** / **margin from stall** are the preferred framings for *why* AOA matters; prefer these to "how close to stall you are".

### Physical quantities and units

- **AOA** (initialism, uppercase). Spell out as "angle of attack" on first use per page, then AOA.
- **α** (Greek alpha) in math contexts; KaTeX-styled in running prose: `$\alpha_0$`, `$\alpha_\text{stall}$`, `$\alpha_\text{crit}$`.
- **V~X~ / V~Y~ / V~S~ / V~NO~ / V~NE~ / V~REF~ / V~FE~ / V~A~** — always KaTeX-styled with subscript tildes. Lowercase `v` is wrong.
- **IAS, CAS, TAS** — initialisms, all uppercase.
- **Fractional Lift** and **NAOA** — mathematically identical. NAOA in code/engineering contexts; Fractional Lift in pilot-facing prose. Glossary links them.
- **Knots**, **feet**, **degrees**, **°C**, **mbar**, **PSI** — lowercase `knots`, lowercase `feet`, always `°C` (not `degrees C`), `mbar` not `millibars` in tables.
- **Lift fraction** and **NAOA** are interchangeable; pick one per page and stick with it. The glossary's `Fractional Lift` entry says as much.

### Glossary is authoritative

`reference/glossary.md` is the authoritative list. When a page introduces a term that appears in the glossary, either link to it or match its wording. Do not re-define a glossary term with different wording elsewhere on the site.

### Forward-looking: Fast–On-Speed–Slow framework

There is an in-progress standardization paper ("Standardizing Angle of Attack in General Aviation: The Fast–On-Speed–Slow Framework for Aircraft Control", Vaccaro et al.) that would align the site's vocabulary with broader GA conventions:

- The framework name **Fast–On-Speed–Slow** (em-dash).
- Rename `ONSPEED` → **On-Speed** (hyphenated).
- Rename `ONSPEED-Fast` / `ONSPEED-Slow` setpoints → **On-Speed-Fast** / **On-Speed-Slow**.
- Introductory page explicitly naming the framework OnSpeed implements.

**Do not start this migration until the paper is published.** The site is currently internally consistent on `ONSPEED`; introducing mixed spellings mid-stream would be worse than either option alone. When the paper publishes, the migration is a single focused docs-drift PR.

## Workflow

Create a TodoWrite todo for each phase. Mark complete as you go.

### Phase 1 — Scope the audit

Start by identifying which areas of the firmware changed since the last docs pass. If the user says "audit before v4.X", the natural window is "since the previous docs-update PR" or "since the previous release tag".

```bash
# Previous release tag
git tag -l "v*" | sort -V | tail -1

# Commits touching firmware source since that tag
git log v<prev>..origin/master --oneline -- software/OnSpeed-Gen3-ESP32/ software/Libraries/onspeed_core/

# Commits touching the docs site since that tag
git log v<prev>..origin/master --oneline -- docs/site/
```

From the firmware-side commit list, note which files changed. The mapping below tells you which docs pages those files drive.

### Phase 2 — Walk the code-to-docs mapping

This is the authoritative mapping from "firmware file changed" to "docs pages to check". Walk every entry where the firmware side shows commits in the window.

| Firmware source | Docs page(s) it drives | What to verify |
|---|---|---|
| `Config.cpp` (esp. `LoadDefaultConfiguration()`) / `onspeed_core/config/OnSpeedConfig.cpp` (`LoadDefaults()`) | `reference/config-parameters.md`, `configuration/first-time-setup.md`, `configuration/advanced.md` | Every default value, parameter name, unit, valid range, and the radio-button or dropdown choices shown on the web UI |
| `ConfigWebServer.cpp` (`HandleCalWizard`, `HandleAoaConfig`, etc.) | `configuration/first-time-setup.md`, `calibration/wizard.md`, `configuration/web-interface.md` | Exact field labels, order of fields, form sections, radio button groups, which fields persist to config vs. wizard-only |
| `ConsoleSerial.cpp` | `troubleshooting/console-commands.md` | Command names (case-insensitive but printed in uppercase), arguments, example output, order in HELP |
| `LogSensor.cpp` | `reference/log-columns.md`, `data-and-logs/log-format.md` | Column names, units, write order (base / optional Boom / optional EFIS / derived), enable conditions |
| `ToneCalc.cpp` / `ToneCalc.h` | `flying/tone-map.md`, `flying/tone-simulator.md`, `docs/site/docs/javascripts/tone-simulator.js` | Thresholds, tone type boundaries, guard conditions (e.g. `ldmax < onspeedFast` before mapping into the fast band), pulse rate ranges |
| `AHRS.cpp` / `MadgwickFusion.cpp` / `EKF6.cpp` | `configuration/advanced.md`, `calibration/how-aoa-works.md` | Algorithm selection semantics, default, known bugs (link to issue/PR) |
| `EfisSerial.cpp` | `efis-integration/*.md`, `reference/log-columns.md` (EFIS column block) | Supported EFIS types, protocol names, column additions |
| `Flaps.cpp` | `configuration/flap-setup.md` | Detection algorithm description, pot-value terminology |
| `Housekeeping.cpp` (g-limit task) + `ConfigWebServer.cpp` aircraft section | `configuration/first-time-setup.md` (Step 5), `reference/config-parameters.md` (Load Limits, AIRCRAFT section) | Distinction between airframe **structural** G-limit (maneuvering-speed computation) and over-G **warning** thresholds — they are different fields in different config sections |
| `Audio.cpp` (Vno chime) + `ConsoleSerial.cpp` | `troubleshooting/console-commands.md` (`VNOCHIMETEST`), `configuration/audio.md` | Command presence, config section, test button behavior |
| `Globals.h` | `reference/hardware-specs.md`, `installation/wiring.md` | Pin numbers, HW_V4B vs HW_V4P differences |
| `IMU330.cpp`, `HscPressureSensor.cpp`, `SensorIO.cpp` | `reference/hardware-specs.md`, `calibration/how-aoa-works.md` | Sensor part numbers, sample rates, resolution |
| `Audio.cpp` / `Audio.h`, `Volume.cpp` | `configuration/audio.md`, `installation/audio.md` | Audio routing, 3D panning (controlled by `bAudio3D` config flag), volume semantics |
| `test/` directory | `software/building.md` (test count) | Suite count and total test count — run `pio test -e native --list-tests` to get exact numbers |
| `platformio.ini` | `software/building.md` | Build environments, flags, toolchain version |

For each changed firmware file with a docs mapping, open the docs page(s) and the code side-by-side.

### Phase 3 — Per-page verification

For each docs page in scope, verify each concrete claim against the code source. Use the verification commands below.

#### Config defaults (`reference/config-parameters.md`)

The **only** authoritative source is the function called at boot. For OnSpeed the sketch-side wrapper is `FOSConfig::LoadDefaultConfiguration()` in `Config.cpp`, which delegates to the core `OnSpeedConfig::LoadDefaults()` in `software/Libraries/onspeed_core/src/config/OnSpeedConfig.cpp`. Open both and compare every row of the docs tables against the actual initializers — most of the defaults live in the core function.

```bash
# Find the entry points
grep -n "LoadDefaultConfiguration" software/sketch_common/src/config/Config.cpp
grep -n "LoadDefaults" software/Libraries/onspeed_core/src/config/OnSpeedConfig.cpp
```

**Note on per-flap calibration defaults:** `aFlaps[0]` ships with all setpoints and AOA-curve coefficients at 0.0, which is the explicit "uncalibrated" signal. The audio tone path stays silent at that state. Do not document RV-4-specific setpoints (8/11/14/16) or curve coefficients as the defaults — those were removed because shipping one airplane's calibration as the firmware default is a flight-safety hazard.

When you find a drift, fix the specific row. Do not rewrite the whole table if only three rows are wrong — a minimal diff is easier to review. Include the **Default** column note near the top of the page:

> The **Default** column shows the values set by `FOSConfig::LoadDefaultConfiguration()` in `Config.cpp` — what a fresh install or "Load Defaults" button press produces.

#### Console commands (`troubleshooting/console-commands.md`)

```bash
grep -n "Command\|sCommand\|handleCommand\|HELP" software/sketch_common/src/io/ConsoleSerial.cpp | head -60
```

Every command in the dispatcher must have an H3 heading on the page. Missing commands are a documentation gap; removed commands are stale docs. Order on the page should match the order in HELP where reasonable. For each command section: the name, a one-line summary, an example fenced block, and usage notes. Match the style of neighboring commands — don't add TLDR paragraphs or long backstories.

#### CSV log columns (`reference/log-columns.md`)

Column order is load-bearing — log files parsed by tooling that assumes fixed positions will break. The file is written in three groups in this order:

1. Base core columns (`timeStamp` through `Roll`)
2. Optional Boom columns (if `bBoom` enabled)
3. Optional EFIS columns (if EFIS type ≠ `None`)
4. Derived core columns (`EarthVerticalG` through `CoeffP`)

`LogSensor.cpp` writes the header in this order. When columns change, update the page's explanatory note **and** the split between "Core Columns — Base" and "Core Columns — Derived". Always include the parse-by-name warning:

> Columns are written in three groups: (1) base core columns, (2) optional Boom and/or EFIS columns when those features are enabled, (3) derived core columns. Parse by column name — direct index positions shift when optional blocks are enabled.

Units must match what is written (e.g., smoothed pressure columns are still in ADC counts, not PSI; `PStatic` is in millibars, not PSI). A bad unit is a silent data-analysis bug downstream.

#### Tone thresholds and simulator (`flying/tone-map.md`, `docs/site/docs/javascripts/tone-simulator.js`)

```bash
grep -n "fLDMAX\|fONSPEEDFAST\|fONSPEEDSLOW\|fSTALLWARN" software/Libraries/onspeed_core/ToneCalc.cpp
```

The simulator in `tone-simulator.js` mirrors `ToneCalc::calculateTone()` in JavaScript. When the C++ logic changes — especially when a guard is added (e.g. `fAOA >= th.fLDMAXAOA && th.fLDMAXAOA < th.fONSPEEDFASTAOA`) — the JS must match exactly. Compare the cascade of `if` checks line for line. A subtly different guard causes the simulator to produce tones the real firmware won't.

#### Wizard fields (`calibration/wizard.md`, `configuration/first-time-setup.md`)

```bash
grep -n "HandleCalWizard\|name=\"acGrossWeight\"\|name=\"acVldmax\"" software/sketch_common/src/web_server/ConfigWebServer.cpp
```

Read `HandleCalWizard()` in `ConfigWebServer.cpp` and list every form field exactly as rendered, including:
- Label text (pilots will search the docs for these exact strings)
- Input name attribute (for cross-reference)
- Radio button groups, including the preset values (e.g., Normal +3.8G, Utility +4.4G, Aerobatic +6.0G, Custom)
- Which fields persist back to `g_Config.Save...()` vs. which are wizard-only (current weight is wizard-only, for example)

If a field doesn't exist in the code, remove it from the docs. If a field exists in code but isn't documented, add it. If the label changed, update it.

#### Test count (`software/building.md`)

```bash
cd OnSpeed-Gen3
pio test -e native --list-tests 2>&1 | tail -20
# or just count directories
ls test/ | grep -c "^test_"
```

Update the suite count and the total test count in any place they appear. The page mentions "N native unit tests" and the project-structure tree shows `test/ # Native unit tests (N suites, M tests)`. Both must match.

### Phase 4 — Collect findings before editing

Before touching any docs page, write a concise list of findings. Format:

```
D<N>: <short title> | <page path> | <severity>
  Evidence: <code file:line or function name>
  Fix: <one sentence>
```

Severity levels:
- **safety** — docs could mislead a pilot about a flight-envelope or safety-critical feature (e.g., EKF6 "better stability" claim when there's a known divergence bug)
- **correctness** — factual error that would confuse a user (wrong defaults, wrong units, missing command)
- **drift** — stale reference that has no safety impact but should be fresh (test count, build notes)
- **clarity** — prose that is correct but hard to read or doesn't match the site's voice

Sort by severity (safety first). Present the full list to the user before editing if the scope is more than ~3 findings, so they can approve or descope. For small audits you can just edit and describe what you did in the PR.

### Phase 5 — Edit, matching the existing voice

When writing fixes, mimic the register of the neighboring pages on the site. **Use the exact vocabulary from the [Canonical terminology](#canonical-terminology) section above** — ONSPEED (not On-Speed, not "the on-speed band"), Fast Tone / Slow Tone / ONSPEED solid tone, L/D~MAX~ (KaTeX-styled), Push / Hold / Pull / Unload for pilot actions. Do not synonymize.

The OnSpeed docs voice is:

- **Dense and minimal.** Short sentences. Tables over paragraphs where possible. Pilots scan, they do not read top-to-bottom.
- **No marketing voice.** Avoid "revolutionary", "seamlessly", "ensures optimal", "simply". These words get deleted on review.
- **Second person imperative for instructions, third person for reference.** "Set the EFIS type to match your wiring." / "The wizard writes gross weight, best-glide, Vfe, and G-limit back to the saved configuration."
- **Admonitions for severity.** `!!! warning` for "this has a known issue, be careful", `!!! danger` for "you will break your aircraft or calibration if you ignore this", `!!! note` for side information, `!!! tip` for optional optimizations. Don't escalate — a misused `danger` block desensitizes pilots to real dangers.
- **Math in KaTeX.** `$K/\text{IAS}^2 + \alpha_0$`, not Unicode symbols or ASCII.
- **Explicit cross-links.** `[Sensor Calibration](sensor-calibration.md)` — relative paths, always close the link.
- **Example values for one known aircraft.** The site uses RV-4 example values throughout (`Max gross weight: 2282 lbs`, `Best glide: 87.5 KIAS`). When you add or update a table with an example column, keep using RV-4 numbers for consistency.

**Tone check red flags** — if the prose you just wrote sounds like any of these, rewrite it:

| Wrote | Should be |
|---|---|
| "This important setting ensures that..." | "Sets..." |
| "You may want to consider..." | "Set..." or cut entirely |
| "OnSpeed's revolutionary new..." | "The..." |
| "It should be noted that..." | "Note:" or cut |
| "In order to configure..." | "To configure..." |
| Three-paragraph essay explaining context | One-sentence intro, then the table |
| Tables with one column of data and one column of 4-sentence prose | Two concise columns |

Edits should be minimal diffs. Do not refactor a page's structure unless the audit itself says "this page structure is wrong". A drift fix is one row in one table, not a rewrite.

### Phase 6 — Strict build

```bash
cd docs/site
uv run --with "mkdocs>=1.6,<2" --with mkdocs-material mkdocs build --strict
```

Strict mode catches:
- Broken internal links
- Missing anchors
- Malformed admonitions
- Orphaned pages (files not in `nav:`)
- YAML syntax errors in frontmatter

Fix every warning. Strict build must pass with zero warnings before opening the PR.

### Phase 7 — PR

Open a dedicated branch and PR. Do not mix docs drift with firmware work.

```bash
git checkout -b docs/drift-fixes-v<version>
git add -p docs/site/
git commit -m "Fix docs drift against v<version> work"
git push -u origin docs/drift-fixes-v<version>
gh pr create --head docs/drift-fixes-v<version> --title "Fix docs drift from v<version> work" --body "$(cat <<'EOF'
## Summary

Audit pass against the docs site for drift accumulated since v<prev>. Each finding is sourced from the actual firmware, not a previous docs revision.

## Findings

- **D1:** <one line> (<page>)
- **D2:** <one line> (<page>)
- ...

## Verification

- [x] `mkdocs build --strict` passes
- [x] Every changed claim traced to a specific file/line in firmware
- [x] Voice matched against neighboring pages
EOF
)"
```

**Important**: Use `--head <branch-name>` on `gh pr create`. The workspace root contains untracked files outside the git repo and `gh` will otherwise abort with "uncommitted changes".

## Common Mistakes

| Mistake | Why it's wrong | Fix |
|---|---|---|
| Documenting per-flap calibration setpoints as firmware defaults | `aFlaps[0]` ships zeroed — documenting 8/11/14/16 as defaults would bake one RV-4's calibration into the docs | The defaults section for per-flap values should say "all zero until the calibration wizard runs" |
| Updating docs from the previous docs revision instead of code | Drift compounds — if yesterday's docs were wrong, today's will be too | Always go back to the code |
| Mixing docs drift PR with feature work | Hard to review, hard to revert if something's wrong | One clean `docs/drift-fixes-*` PR |
| Rewriting a whole page to fix one row | Makes the diff unreadable and imports new voice issues | Minimal diff — one row, one sentence |
| Escalating admonitions (`danger` for a minor warning) | Desensitizes pilots to real danger blocks | Use `warning` for "known issue", `danger` only for "will break something" |
| Skipping strict build because "it's just a few edits" | Broken anchors and malformed admonitions ship otherwise | Always run `mkdocs build --strict` |
| Sourcing console commands from memory or from the docs | Commands get renamed, added, removed | Always grep `ConsoleSerial.cpp` |
| Forgetting the parse-by-name caveat on `log-columns.md` | Tooling that assumes fixed positions will silently break | Always include the three-group ordering note |
| Describing wizard fields that don't exist (e.g., "Stall Speed Vs") | Pilots search for these strings and get confused | Open `HandleCalWizard()` and list fields literally |
| Claiming an algorithm is "better" when it has a known bug | Safety issue — pilots may enable it | Link issue number and leave default unchanged |
| Using "On-Speed" (hyphenated) in new prose | Site convention is `ONSPEED` all-caps. Mixed spellings across pages is worse than either alone. | Use `ONSPEED`. See Canonical terminology. |
| Inventing tone names — "fast pulse", "slow pulse", "approach tone" | Confuses pilots who learned the standardized names. | `Fast Tone`, `Slow Tone`, `ONSPEED solid tone`, `stall warning buzz`. |
| Describing the fast-side chevron or band as "blue" or "orange" | Standardized to green in PR #240. Residual references are drift. | Green. Match the FAA green/yellow/red progression. |
| Treating L/D~MAX~ and ONSPEED as interchangeable | They are configuration-dependent and usually differ (L/D~MAX~ at lower lift fraction in clean, approaching ONSPEED with flaps). | Keep distinct. Link to the glossary entries. |
| Rendering `L/Dmax`, `L/D max`, `L/D-MAX` in prose | KaTeX subscript is the site convention | Always `L/D~MAX~`. |
| Lowercase `vref`, `vs`, etc. | Site convention is `V~REF~`, `V~S~` (KaTeX subscript) | Use the KaTeX form. |

## Red Flags — STOP and reconsider

- "I remember the default volume is 80%" → read `Config.cpp`
- "This console command is obvious, I don't need to check it" → grep `ConsoleSerial.cpp`
- "The units are probably PSI, they're pressure" → read the CSV writer in `LogSensor.cpp`
- "EKF6 has better stability" → check `AHRS.cpp` comments and issue tracker
- "I'll write one big docs PR that also restructures navigation" → no, one PR per concern
- "The simulator is close enough to the firmware" → diff the `if` cascade line by line
- "I don't need to run strict build, these are small edits" → run it anyway
- "These per-flap defaults (8/11/14/16) look like the firmware defaults" → those were removed; the compiled-in default is all zeros
- "The docs say X, the code says Y, the docs were probably right" → no, the code is authoritative
- "I'll start the ONSPEED → On-Speed rename now, it's the better spelling" → no, not until the canonical paper is published. Mixed spellings across the site is worse than either alone. See Canonical terminology.
- "I'll invent a more descriptive tone name for this context" → no. Fast Tone, Slow Tone, ONSPEED solid tone, stall warning buzz. The names are load-bearing.

All of these mean: stop, go back to the source file, fix from the code outward.

## Cross-Reference from Release Notes Skill

**Every release must have a docs-update pass run against it before the release notes are drafted.** This is enforced by a step in `release-notes/SKILL.md` Phase 1, which invokes this skill. Do not draft release notes against stale docs — pilots read the docs site to understand what a release means, and a drifted site turns a clean release into a support problem.

If you are running this skill as part of a release, your target version is the one being released. Audit against `origin/master` HEAD at the SHA the release will tag, not against whatever branch is currently checked out.

## Reference: known-good examples

The following edits from the v4.17 docs audit are canonical examples of the style to match:

- **`configuration/advanced.md`** — EKF6 section with `!!! warning` admonition linking issue #128 / PR #131. Example of how to document a known-bug algorithm without either hiding it or over-escalating.
- **`reference/config-parameters.md`** — Load Limits and AIRCRAFT > G_LIMIT rows explicitly distinguishing "airframe structural G-limit for maneuvering-speed computation" from "over-G warning thresholds". Example of how to resolve a previously conflated concept in minimal prose.
- **`configuration/first-time-setup.md`** — Step 5 split into two tables: aircraft parameters (structural G-limit radio buttons) and warning thresholds (positive/negative over-G warnings + Vno chime). Example of how to mirror the config page's section layout in docs prose.
- **`reference/log-columns.md`** — Three-group ordering note at the top, plus "Core Columns — Base" and "Core Columns — Derived" split. Example of how to make parse-order explicit without cluttering the tables.
- **`troubleshooting/console-commands.md`** — `VNOCHIMETEST` section matching the register of neighboring command sections (H3 heading, fenced example, one-line purpose, equivalence to GUI button). Example of the minimum required structure for a new command entry.

Read these diffs before your first audit to calibrate voice.
