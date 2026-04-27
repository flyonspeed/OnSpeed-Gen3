# Separate the L/Dmax pip from the tones-on threshold on the wire

**Status:** Spec — pending review
**Author:** Sam Ritchie (with Vac, design-rule source: `~/Downloads/ld_max.pdf`)
**Date:** 2026-04-27
**Branch:** `sritchie/ldmax-rebase-on-onspeed-band`

## Problem

After PR #327 ("Interpolate display percent anchors between flap detents"),
the wire field `tonesOnPctLift` is interpolated between adjacent flap detents
to make the L/Dmax pip slide smoothly during flap deployment. That interpolation
broke a separate consumer of the same field: the M5 indexer's bottom green
chevron, which uses `tonesOnPctLift` as the lower edge of the "low tone is
playing" band. The audio low tone fires at the *active detent's* calibrated
`fLDMAXAOA` (snapped); the chevron now follows an interpolated value that
does not match the audio threshold mid-deployment.

Per Vac's design rule (`ld_max.pdf` §8): **L/Dmax pips are aerodynamic
references. Fast tone is an operational limit cue. They must remain
independent.** PR #327 collapsed two things that the design says must be
separate. This spec restores the separation.

## Goals

1. Restore the chevron–audio alignment: bottom chevron lights green exactly
   when the low tone is playing, on every flap setting. Both fire from the
   active detent's calibrated L/Dmax, so they snap together at `iIndex`
   transitions and never disagree.
2. Keep the smooth-pip-slide visual behavior PR #327 introduced — a pilot
   watching the indexer during flap deployment sees the pip slide
   continuously toward the OnSpeed band, not jump.
3. Decouple the pip's position from the audio threshold so future changes
   to either one cannot affect the other.
4. Document the convention so future contributors understand which cue is
   which and why they are independent.

## Non-goals

- Changing the audio path. `ToneCalc::calculateTone` reads `g_Sensors.AOA`
  against `g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA` directly. That stays
  unchanged.
- Changing `fAcVfe` semantics or wiring it to the audio gate. `fAcVfe` is
  a calibration-default seed; it is not a runtime audio threshold.
- Changing the OnSpeed band edges (`onSpeedFastPctLift`,
  `onSpeedSlowPctLift`, `stallWarnPctLift`) — those already snap correctly
  per PR #320 + #327.
- Per-detent-pair pip interpolation. The pip's path is a single line from
  the cleanest configured detent to the most-deployed configured detent;
  intermediate detents (e.g., the user's 16°) are intentionally ignored
  for the pip.
- `dataMark` semantics, producer, or offset. `dataMark` stays at offset
  68 with width 2; SD-log column unchanged. Downstream tools that
  time-align wire captures to SD logs continue to work without changes.
- All existing field offsets (0..69). The new `pipPctLift` is appended
  before the checksum. No existing parser field offset shifts. Producers
  and parsers that read individual fields by offset (the M5
  `DisplayFrameAccumulator`, `tools/m5-replay/parse_frame.cpp`) need to
  update only the frame size constant and the checksum-coverage range.

## Design rule (Vac, restated)

| Cue | Purpose | Behavior in lever-pot space |
|---|---|---|
| **Bottom green chevron** + **audio low tone** | Operational ("if a tone is playing, you are flying within a safe band for this flap configuration") | Snap to active detent's calibrated L/Dmax — chevron and audio fire together |
| **L/Dmax pip** | Aerodynamic reference ("L/Dmax slides toward OnSpeed as flaps deploy") | Smooth two-endpoint linear interpolation from the cleanest detent's L/Dmax percent to the most-deployed detent's OnSpeed center |

The two cues coincide visually only when the lever is at the cleanest detent.
That is the design intent: pip and chevron edge line up in clean cruise (where
L/Dmax is both the audio threshold and the aerodynamic reference), and they
diverge during approach (where the pip slides toward the donut to give a
smooth visual cue while the audio threshold snaps to the deployed detent's
calibration).

## Wire format change (v4.21 → v4.22)

The `#1` display-serial frame grows from 74 to 76 bytes. A new field is
inserted before the checksum.

```
Old (v4.21, current master):
   ...
   62      4     gOnsetRate
   66      2     spinRecoveryCue
   68      2     dataMark
   70      2     checksum
   72      2     terminator      (CR LF)
   = 74 bytes

New (v4.22):
   ...
   62      4     gOnsetRate
   66      2     spinRecoveryCue
   68      2     dataMark
   70      2     pipPctLift      %02u  ×1   unsigned 0–99
   72      2     checksum        (was 70)
   74      2     terminator      (was 72)
   = 76 bytes
```

`tonesOnPctLift` keeps its existing offset (48) and width (2). Its semantics
revert to the v4.21 (PR #320) behavior: snapped to the active detent's
calibrated L/Dmax percent. PR #327's interpolation moves to the new
`pipPctLift` field with **different** interpolation semantics (see below).

`kDisplayFrameSizeBytes` becomes 76. `kDisplayFrameChecksumLen` becomes 72.

This is a coordinated firmware-and-display upgrade, just like the v4.20 →
v4.21 transition for percent anchors (PR #320). Two mismatch scenarios:

1. **Older M5 + new Gen3** (M5 expects 74-byte frames, gets 76): old
   parser reads bytes 0–73 of a 76-byte frame, computes checksum over
   bytes 0–69 (the old `kDisplayFrameChecksumLen`), and finds the
   checksum slot at byte 70 contains `pipPctLift` instead of the actual
   checksum hex. Parse fails, accumulator resets, NO DATA overlay
   appears. Pin this with a test fixture: feed an old 74-byte parser a
   stream of new 76-byte frames; assert no successful parse over a
   sweep of frames.
2. **Newer M5 + old Gen3** (M5 expects 76-byte frames, gets 74): the M5
   parser's `kDisplayFrameSizeBytes` is 76. The old Gen3 emits 74 bytes
   ending in `\r\n`, then sends `#1` of the next frame. The M5
   accumulator collects 74 bytes, expects 76, sees `#1` two bytes early
   and resets. Result: NO DATA overlay, never a successful parse. Same
   recovery: reflash both to the same version.

In both directions, the failure is a clean NO DATA — no garbage frames,
no torn data on the indexer.

## Behavior contract

### `pipPctLift` — visual aerodynamic pip, interpolated (NEW)

Computed from raw lever-pot ADC and the configured flap-vector endpoints:

```
let detents     = g_Config.aFlaps sorted by iDegrees ascending
let cleanest    = detents.first
let mostDeployed = detents.last

let pipClean    = ComputePercentLift(cleanest.fLDMAXAOA, cleanest, true)
let pipFullFlap = (ComputePercentLift(mostDeployed.fONSPEEDFASTAOA, mostDeployed, true) +
                   ComputePercentLift(mostDeployed.fONSPEEDSLOWAOA, mostDeployed, true)) / 2

let t           = (rawAdc − cleanest.iPotPosition) / (mostDeployed.iPotPosition − cleanest.iPotPosition)
                  // signed; works for both ascending and descending pot wiring

let lambda      = clamp(t, 0, 1)

pipPctLift = round(lerp(pipClean, pipFullFlap, lambda)), clamped [0, 99]
```

**Worked example (descending wiring, user's config).** Pots: clean=1462,
16°=897, 33°=2. Numerator and denominator are both negative, so the
ratio is positive and runs 0→1 from clean to full:

| `rawAdc` | `t = (1462 − rawAdc) / (1462 − 2)` | `lambda` | meaning |
|---|---|---|---|
| 1462 | (1462 − 1462)/1460 = 0/1460 = 0 | 0.000 | clean detent — pip = pipClean |
| 1100 | (1462 − 1100)/1460 = 362/1460 | 0.248 | between clean and 16° |
| 897 | (1462 − 897)/1460 = 565/1460 | 0.387 | 16° detent — pip ignores it, lerps through |
| 450 | (1462 − 450)/1460 = 1012/1460 | 0.693 | between 16° and 33° |
| 2 | (1462 − 2)/1460 = 1460/1460 = 1 | 1.000 | full flap detent — pip = pipFullFlap |
| 2000 | above clean range | clamped 0 | pip = pipClean |
| −10 | below full range (impossible IRL) | clamped 1 | pip = pipFullFlap |

Note that the spec's "signed `t`" claim in the pseudocode is a slight
misuse of language — the divisions in this example are
positive-divided-by-positive (numerator and denominator are both
negative for descending wiring, so the ratio comes out positive). The
math just works; the "signed" remark refers to the *intermediate*
quantities, not the final ratio.

Edge cases:
- `entryCount == 0`: `pipPctLift = 0` (uncalibrated, consumer renders blank).
- `entryCount == 1`: only one detent configured (fixed-flap aircraft, or
  partial calibration). `pipPctLift = pipClean = tonesOnPctLift`. The
  pip is locked at that single detent's L/Dmax percent and never
  slides. Documented in the indexer-spec; pilot expectation is that a
  single-detent calibration produces no smooth-slide cue. This matches
  the single-detent fallback for every other anchor.
- `cleanest.iPotPosition == mostDeployed.iPotPosition`: degenerate wiring
  (two detents at same pot, or cleanest == mostDeployed). `lambda = 0`,
  pip locked at `pipClean`.
- `iDegrees` ordering: `aFlaps` is sorted ascending by `iDegrees` at
  parse time (`ConfigXmlParse.cpp:204`) **and** at every web config save
  (`ConfigWebServer.cpp:1955`). Both entry points produce a sorted
  vector, so `cleanest` is reliably `flapEntries[0]` and `mostDeployed`
  is `flapEntries[entryCount-1]` regardless of source. No cache or
  invalidation hook needed — the helper reads the live vector each
  call. Reflex flaps (negative `iDegrees` for cleanest) work — the math
  operates on pot ADC, not on degrees.
- IAS validity: pip uses `iasValid = true` always. `ComputePercentLift`
  branches on `iasValid` between the alpha_0-floor formula (above the
  audio mute threshold) and a zero-floor formula (below it). Without
  `iasValid = true`, the pip would jump position at the IAS-alive
  threshold — visible to the pilot as a discontinuity at gear-down or
  taxi-out. Same convention as the other anchors; calibration geometry
  must not gate on live-AOA validity.

### `tonesOnPctLift` — operational audio-on threshold, snapped (RESTORED)

Reverts to PR #320 / pre-#327 semantics:

```
tonesOnPctLift = ComputePercentLift(active.fLDMAXAOA, active, true)
```

where `active = g_Config.aFlaps[g_Flaps.iIndex]`. **Snaps** at every detent
transition. Matches the audio path's gate exactly.

The snap happens entirely on the Gen3 main firmware side. The M5 receives
`tonesOnPctLift` already-snapped on the wire — it has no `iIndex` field
and does not need one. The M5's bottom-chevron gate at `main.cpp:930`
reads `Array[2]` (the snapped wire field) and is correct as written.

### Other anchors — unchanged

- `onSpeedFastPctLift`, `onSpeedSlowPctLift`, `stallWarnPctLift` — snapped
  to active detent (unchanged from PR #327).
- `flapsDeg` — **per-bracket** linear interpolation between adjacent
  detents' `iDegrees` (unchanged from PR #327). This is the mechanical
  lever position; it must visit every detent's `iDegrees` exactly when
  the lever pot equals that detent's `iPotPosition`. **This is different
  from the pip's two-endpoint interpolation** — the pip skips intermediate
  detents intentionally; the mechanical lever angle does not.

## Continuity invariants (testable)

For the `pipPctLift` field:

1. **Continuous in `rawAdc` everywhere.** No discontinuities at detent
   boundaries (single lerp covers the entire pot range). Pinned by a
   sweep test that walks `rawAdc` across the configured pot span and
   asserts `|pipPctLift(rawAdc + 1) − pipPctLift(rawAdc)| ≤ 1`. The 1
   percent-per-ADC bound holds when `|pipFullFlap − pipClean| ≤
   |mostDeployed.iPotPosition − cleanest.iPotPosition|`, which holds
   for any sane calibration (percent range is 0–99, pot range is
   typically 1000+). The test asserts the bound directly; if a
   pathological config violates it the test fails clearly rather than
   silently rendering a jumpy pip.

2. **Endpoint values match calibration math.** At `rawAdc =
   cleanest.iPotPosition`, `pipPctLift == ComputePercentLift(cleanest.fLDMAXAOA, cleanest, true)`.
   At `rawAdc = mostDeployed.iPotPosition`,
   `pipPctLift == round((ComputePercentLift(mostDeployed.fONSPEEDFASTAOA, mostDeployed, true) +
   ComputePercentLift(mostDeployed.fONSPEEDSLOWAOA, mostDeployed, true)) / 2)`.
   Pinned with the math against `mostDeployed` directly — *not* against the
   `onSpeedFast/SlowPctLift` wire fields, which carry the *active* detent's
   values and only equal `mostDeployed`'s when the lever is fully extended.

3. **Independence from `iIndex`.** `pipPctLift` depends only on `rawAdc`
   and the configured flap vector — *not* on which detent is "active." A
   detent transition does not change the pip.

For `tonesOnPctLift`:

4. **Snaps at `iIndex` transitions.** Pinned by a test that calls
   `ComputeDisplayPctAnchors` with the same `rawAdc` but different
   `activeIndex` values: the result must change by the difference between
   the two detents' L/Dmax percents (i.e., the field reads from the
   active detent, not from the bracket).

5. **Matches the audio gate exactly.** For every flap entry,
   `tonesOnPctLift == ComputePercentLift(entry.fLDMAXAOA, entry, true)`.
   This is the chevron–audio alignment property the PR exists to restore.

For pip vs tones-on relationship:

6. **Pip and tones-on coincide at the clean detent.** When `rawAdc ==
   cleanest.iPotPosition` and `iIndex == 0`, `pipPctLift == tonesOnPctLift`.
   This is the visual "they line up in clean" property.

7. **Pip and tones-on diverge at full flaps.** When `rawAdc ==
   mostDeployed.iPotPosition` and `iIndex == entryCount - 1`,
   `pipPctLift > tonesOnPctLift` for any well-configured aircraft (because
   `(Fast + Slow) / 2 > L/Dmax_pct` for any sensible calibration where
   L/Dmax is below the OnSpeed band, which is the entire premise).

## Components to change

### `software/Libraries/onspeed_core/src/aoa/DisplayPctAnchors.{h,cpp}`

- Add `pipPctLift` to the `DisplayPctAnchors` struct.
- Rewrite `ComputeDisplayPctAnchors` to:
  - Compute `tonesOnPctLift` as `ComputePercentLift(active.fLDMAXAOA, active, iasValid)` — snapped.
  - Compute `pipPctLift` as the two-endpoint lerp described above.
  - Keep the existing per-bracket interpolation logic for `flapsDeg`
    only. (Fast/Slow/StallWarn snap; pip uses the new global lerp.)
- Update the docstring header to state the design rule and cite Vac's
  `ld_max.pdf` as the design source.

### `software/Libraries/onspeed_core/src/proto/DisplaySerial.{h,cpp}`

- Add `pipPctLift` to `DisplayBuildInputs` and `DisplayFrame`.
- Bump `kDisplayFrameSizeBytes` from 74 to 76 and `kDisplayFrameChecksumLen`
  from 70 to 72.
- Update the wire-format table at the top of the header.
- Insert the new field at offset 70 in `BuildDisplayFrame` and
  `ParseDisplayFrame`.
- The `DisplayFrameAccumulator` is unchanged — it consumes the new
  size automatically.

### `software/sketch_common/src/io/DisplaySerial.cpp`

- Pass `anchors.pipPctLift` into `inputs.pipPctLift` when building the
  `#1` frame.

### `software/sketch_common/src/web_server/DataServer.cpp`

- Add `"pipPctLift"` to the WebSocket JSON payload alongside the existing
  percent anchors.

### `software/OnSpeed-M5-Display/src/SerialRead.{h,cpp}`

- Add `extern int PipPctLift;` declaration.
- Define the global in `main.cpp` (alongside `TonesOnPctLift` etc.).
- Populate it in the parser (mirrors the existing pattern for the four
  band-edge percents).

### `software/OnSpeed-M5-Display/src/main.cpp`

The existing `PctAnchors[8]` array (declared at `main.cpp:111`) has
several unused slots (1, 5, 6) left over from a prior layout. Use the
existing length, no growth needed:

- `PctAnchors[2]` keeps its current meaning: `TonesOnPctLift` (snapped
  active-detent L/Dmax). The bottom chevron's gate at `main.cpp:930`
  already reads `Array[2]` and remains correct.
- `PctAnchors[6]` (currently `0`) is repurposed: `PipPctLift`.
- The pip rendering at `main.cpp:1010–1014` currently reads `Array[2]`;
  change to `Array[6]`.

Add named constants for the slots so future readers can grep instead of
counting indices:

```cpp
// In SerialRead.h or a new PctAnchors.h alongside it:
namespace pct_anchors {
    constexpr int kIdxTonesOn      = 2;   // active-detent L/Dmax (chevron + audio gate)
    constexpr int kIdxOnSpeedFast  = 3;
    constexpr int kIdxOnSpeedSlow  = 4;
    constexpr int kIdxPipPctLift   = 6;   // visual L/Dmax pip (lerp clean→fullflap)
    constexpr int kIdxStallWarn    = 7;
}
```

`drawAOA` reads `Array[kIdxTonesOn]` and `Array[kIdxPipPctLift]`
explicitly. Slot map also documented as a comment block at the
PctAnchors[] declaration in main.cpp.

### CSV log — out of scope

The SD-card CSV log (`LogCsv.cpp`) does not currently record any of the
percent anchors (`tonesOnPctLift` etc.). It records `flapsPos` and the
raw IMU/sensor data; the percent anchors can be recomputed offline from
the config. Adding `pipPctLift` alone would be inconsistent. If we want
to log the percent anchors, that's a separate PR.

### Tests

- `test/test_display_pct_anchors/`: add cases for the new `pipPctLift`
  field covering invariants 1–3, 6, 7. Update or remove the cases that
  pinned PR #327's *interpolated* `tonesOnPctLift` (those are now wrong);
  add the snap cases (invariants 4, 5).
- `test/test_display_serial/`: round-trip the new wire field. Pin the
  76-byte size constant and the new offset.
- `tools/m5-replay/parse_frame.cpp` and `tools/m5-replay/replay.py`:
  update both the C++ parser harness and the Python frame builder to
  consume the new 76-byte wire size.
- `tools/regression/host_main.cpp` + golden CSV: regenerate golden
  (the regression harness consumes wire bytes; the size change shifts
  the checksum offset).

### M5 sim WASM

- Rebuild the live and docs `sim*` targets after the wire format changes
  — the embedded WASM is a snapshot of the firmware code and must match
  the new contract.
- The Indexer-tab PROGMEM headers (PR #331) regenerate via the existing
  pre-build hook; nothing manual.

### Documentation

Files in `docs/site/docs/`:

- **NEW:** `software/indexer-spec.md` — sibling to the existing
  `audio-tone-spec.md`. The canonical, authoritative reference for what
  the indexer draws and why. Lists the five visual elements (top chevrons,
  bottom chevron, donut top arc / bottom arc / center dot, white index
  bar, L/Dmax pip dots), the percent-lift gate for each, the color
  logic, and which wire field drives it. Quotes Vac's `ld_max.pdf` §8
  ("L/Dmax pips are aerodynamic references. Fast tone is an operational
  limit cue. They must remain independent.") verbatim. Includes the
  pip's two-endpoint lerp formula and the snap-vs-slide table. Cross-
  references the audio spec for the audio-cue side. This document
  becomes the load-bearing reference future PRs cite when they touch
  the indexer.
- `calibration/how-aoa-works.md` — the existing body-angle/alpha_0
  explanation stays; add a short section after it titled "L/Dmax pip
  vs. low-tone threshold" that walks through the user's-config example
  numbers (clean 50%, full-flap pip at 59%, audio-on snapping per
  detent) and links to the new indexer-spec for full detail.
- `flying/tone-map.md` — the chevron description must match: "the bottom
  chevron lights green when the audio low tone is playing." Single
  paragraph; full color/threshold table lives in the indexer-spec.
- `reference/serial-protocol.md` — add the new wire field to the byte-offset
  table; bump frame size; add a v4.22 entry to the change-log.
- `reference/glossary.md` — separate glossary entries for "L/Dmax pip"
  (aerodynamic reference) and "Low-tone threshold" (operational cue).
  Each entry cites Vac's `ld_max.pdf` §8 as the design source.

## Rollout

1. Land the firmware change. Pre-merge gates:
   - Gen3 main builds clean: `pio run -e esp32s3-v4p` and `-v4b`.
   - All three M5 board variants build clean:
     `pio run -e m5stack-core-esp32 -e m5stack-core2 -e huvver-avi`.
   - Native unit tests pass: `pio test -e native`.
   - `scripts/check_core_purity.sh` and `scripts/check_board_flags.sh`
     pass.
   - **`tools/regression/host_main.cpp` and the golden CSV are
     regenerated and committed** (the wire-size change shifts the
     parser, which the snapshot regression catches).
   - **The Indexer-tab WASM PROGMEM headers are regenerated** by the
     pre-build hook on a clean checkout (delete
     `Web/sim_index_*.h`, run `pio run -e esp32s3-v4p`, confirm headers
     reappear with new wire-size constants).
2. Cut a v4.22 release. Release notes name the wire-format bump
   explicitly and mirror the v4.20 → v4.21 coordination guidance.
3. Reflash bench device and confirm chevron–audio alignment in all four
   regimes:
   - Clean, AOA below L/Dmax: silent, chevron grey.
   - Clean, AOA between L/Dmax and OnSpeedFast: low tone pulsing,
     chevron green.
   - Full flaps, AOA between active L/Dmax (33.5% per user config) and
     OnSpeedFast (54.8%): low tone pulsing, chevron green; pip sitting
     up at ~59% (above the chevron-lit band).
   - Mid-deployment (lever between detents): chevron snaps with iIndex,
     pip slides smoothly.
4. Replay a recorded approach log through the bench M5 to verify the
   pip slide and chevron snap behave correctly during flap-deployment
   sequences.

## Risks

- **Wire-format change requires coordinated reflash.** Same operational
  cost as the v4.20 → v4.21 transition; mitigated by clear release notes
  and the existing OTA path.
- **Behavior change for pilots familiar with the current display.** The
  pip's mid-deployment trajectory changes (it no longer visits intermediate
  detents' calibrated L/Dmax percents — it follows a single straight
  line). Documented; flagged in release notes.
- **`Array[]` index growth in M5 code.** Adding `Array[8]` (or whatever
  name) is a slight churn but follows the established pattern.

## Naming

The wire field stays `tonesOnPctLift` for compatibility with PR #320,
the existing parser, and the live regression infrastructure. C++ struct
member name and code comments use the same identifier. Pilot-facing
docs (indexer-spec, glossary, tone-map) refer to it as "the low-tone
threshold" — the audio low tone is what the chevron tracks. The wire
field name and the prose name describe the same quantity from different
audiences (engineering vs pilot). No internal rename.

## Open questions for review

1. **Defensive clamp on `pipPctLift`.** The math guarantees pip is
   bounded by `[0, 99]` because `ComputePercentLift` already clamps. A
   misconfigured aircraft where the clean detent's L/Dmax body angle is
   above the most-deployed detent's OnSpeed center (highly unusual but
   possible) would produce a *descending* pip during deployment — the
   slide goes the wrong direction. **Spec position:** do not add a
   defensive clamp narrower than `[0, 99]`. The pre-flight calibration
   check should catch this, and a defensive clamp would silently mask
   it. Let misconfiguration show.

2. **Symmetric pot wiring.** The lerp's `t = (rawAdc − cleanest.pot) /
   (mostDeployed.pot − cleanest.pot)` works for both ascending wiring
   (clean = low pot) and descending wiring (clean = high pot, like the
   user's config: 1462 → 2). Test this symmetry with two parametric
   fixtures inline in `test_display_pct_anchors.cpp` (one ascending,
   one descending), built from `SuFlaps` C++ literals — same pattern
   the existing tests use. No XML fixture round-trip needed; the
   property under test is the math, not the parser.

3. **Two-helper split for stronger goal-3 enforcement.** A reviewer
   suggested splitting `ComputeDisplayPctAnchors` into separate helpers
   for operational anchors (snapped) and pip anchors (lerp), to
   architecturally enforce "decoupled pip and audio threshold."
   **Spec position:** keep one helper, one struct, one function call
   from each consumer. Two helpers would force every consumer to call
   both and assemble the struct themselves; the single helper makes
   the contract clearer to read and harder to use incorrectly. The
   independence rule is a code-comment concern, not a function-boundary
   concern.
