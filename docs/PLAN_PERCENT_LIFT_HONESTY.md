# Honest Percent Lift: Wire Redesign + Function Fix

**Status:** Draft, not committed. Captures a 2026-04-26 design conversation.
**Tracking issues:** supersedes #321, supersedes #322 (partial), closes #323.
**Affects:** PR #320 (must rewrite), the m5-replay tool (must update),
the docs site, the Gen2 user manual (out of scope here, but flag for
the team — the manual teaches the bug).

---

## The bug

`onspeed_core/aoa/PercentLift.cpp::ComputePercentLift` produces a
piecewise-linear curve that maps:

- α₀ → 0%
- LDmax → **always 50%**
- OnSpeedFast → **always 55%**
- OnSpeedSlow → **always 66%**
- StallWarn → **always 90%**
- α_stall (or fallback ceiling) → 100%

Each segment between those anchors is linear in body angle, but the
break-points are *firmware constants*, not properties of the airframe.

This is a verbatim port from Gen2 (`OnSpeedTeensy_AHRS/DisplaySerial.ino`
line 31–39).  Both Gen2's user manual and Gen3's docs site
(`docs/site/docs/installation/external-display.md`,
`docs/site/docs/reference/glossary.md`) document the segmented behavior
as if 50/55/66/90 are aerodynamic facts.  They aren't.

**Why it's wrong:**

L/Dmax body angle is a *measurable, flyable property* of each flap
configuration.  The pilot finds it in flight by trimming for max range
or best glide.  Once you've measured it, its position on the lift
envelope is whatever it is — typically not 50%, and *typically
different between flap settings on the same airframe*.

The honest computation is a single linear normalization between the
two measurable aerodynamic anchors:

```
percentLift = (AOA − α₀) / (α_stall − α₀) × 100
```

Plug L/Dmax body angle into this and you get the actual lift fraction
at L/Dmax for that flap.  For a typical RV-class clean configuration
that lands somewhere around 51–55%; for full flaps it's typically
higher (different sub-band geometry).  The number varies per flap
because the *aerodynamics* vary per flap.  That's what the indicator
should show.

The segmented function pretends each flap has L/Dmax pinned to 50% by
*defining* the percent scale to wrap around it.  That makes the
displayed number a categorical "which band am I in" signal rather
than an envelope-fraction reading.  It's an indirection layer that
hides the aerodynamic truth.

Documenting it is documenting the bug.  Fix the function, fix the
wire format, fix the docs.

---

## The redesign

### One function, two quantities

The fix has two parts:

1. **`ComputePercentLift` becomes the honest formula** (≈6 lines).
   Same signature, different body. Same API for the firmware caller.

2. **The wire format ships rendered percents instead of body-angle
   primitives.**  Everything the M5 needs to render the indexer
   becomes a percent.  Body angles never leave the firmware.

### What the wire carries (after)

The display-serial `#1` frame today (post-PR-320) carries 8
AOA-related fields totaling 30 bytes:

| Wire field today | Width | Contents |
| --- | --- | --- |
| `aoaDeg` | 4 | Current AOA, body angle |
| `stallWarnAoaDeg` | 4 | Per-flap StallWarn body angle |
| `onSpeedSlowAoaDeg` | 4 | Per-flap OnSpeedSlow body angle |
| `onSpeedFastAoaDeg` | 4 | Per-flap OnSpeedFast body angle |
| `tonesOnAoaDeg` | 4 | Per-flap LDmax body angle |
| `alpha0Deg` | 4 | Per-flap α₀ |
| `alphaStallDeg` | 4 | Per-flap α_stall |
| `percentLift` | 2 | Precomputed (segmented) percent |

Replace with 5 percent fields totaling 16 bytes:

| Wire field after | Width | Contents |
| --- | --- | --- |
| `percentLift` | 4 | Current AOA expressed as `(AOA − α₀)/(α_stall − α₀) × 100`, ×10 fixed-point, signed (`%+04d`) |
| `tonesOnPctLift` | 3 | LDmax body angle through the same formula, 0–99 (`%02u`) — wait, see sizing notes below |
| `onSpeedFastPctLift` | 3 | OnSpeedFast body angle through the same formula |
| `onSpeedSlowPctLift` | 3 | OnSpeedSlow body angle through the same formula |
| `stallWarnPctLift` | 3 | StallWarn body angle through the same formula |

Net wire savings: 14 bytes.  Frame shrinks from 94 → 80 bytes — happens
to be the *original* pre-PR-320 frame size, coincidentally.

#### Sizing notes

- **`percentLift` keeps its current name and grows from 2 bytes to 4
  bytes signed.**  Width 2 (range 0..99) was sufficient for the
  segmented function, which clamped to 99 in any segment.  The honest
  formula can produce values slightly below 0 (aircraft below α₀, e.g.
  steep negative-G push) or slightly above 100 (above α_stall).  We
  still clamp display values to 0..99 (per the existing convention),
  but width 4 with sign and 0.1% resolution gives:
   - finer granularity for the indexer's index bar (a body angle
     sweeping 1° in the lower band crosses several percent-lift
     points; 0.1% steps give smooth bar motion)
   - room to evolve toward sending the unclamped value if a future
     consumer wants it

  A pragmatic alternative: keep `%02u` width 2 (0..99 only).  Saves
  2 bytes.  Loses sub-percent indexer-bar resolution.  Recommend the
  4-byte fixed-point version unless wire bytes prove tight.

- **`tonesOnPctLift` and the band-edge percents are 0..99 unsigned,
  width 3 (`%02u` would also work but 3 leaves headroom).**  These are
  per-flap quasi-constants computed from the active flap snapshot's
  body-angle setpoints.  They change when the user re-calibrates a
  flap or when the active flap index changes; otherwise they're stable
  across frames.  Keep the same precision (1%) as the wire's existing
  `percentLift` — fractional-percent precision on band edges has no
  consumer.

#### Field naming

Keep `percentLift` as the name. It's:
- the existing wire field
- the existing M5 global (`PercentLift` in main.cpp)
- the term used in Gen2 user manual + Gen3 docs
- the term pilots already know

The redesign changes the *function* (segmented → honest), not the
*field name*. Renaming would create unnecessary churn — the bytes on
the wire still encode "lift fraction percent," they just compute it
correctly now.

The new fields (`tonesOnPctLift`, etc.) follow the same convention:
`tonesOn` is the existing semantic name for the LDmax field on the
wire (because it's the AOA at which the audio "fast tone" turns on).

### What the M5 does after

`mapAOA2Display` becomes `mapPct2Display`.  Single function, no
piecewise-body-angle tracking.  The screen-y constants (192, 115, 78,
1) stay; the inputs change from body angles to percents:

```c
int mapPct2Display(int aoaPct, const struct PctAnchors& a) {
    if      (aoaPct <= 0)                   return 192;
    else if (aoaPct <= a.onSpeedFastPct)    return map2int(aoaPct, 0, a.onSpeedFastPct, 192, 115);  // lower ramp
    else if (aoaPct <= a.onSpeedSlowPct)    return map2int(aoaPct, a.onSpeedFastPct, a.onSpeedSlowPct, 115, 78);  // donut band
    else if (aoaPct <= a.stallWarnPct)      return map2int(aoaPct, a.onSpeedSlowPct, a.stallWarnPct, 78, 1);  // upper ramp
    else                                     return 1;
}
```

L/Dmax pip y: `mapPct2Display(tonesOnPctLift, anchors)`.  Slides per
flap because `tonesOnPctLift` differs per flap.  ✓

Index bar y: `mapPct2Display(percentLift, anchors)`.  Slides smoothly
through the calibrated envelope.  ✓

Donut top/bottom edges: `mapPct2Display(onSpeedFastPctLift, anchors)`
and `mapPct2Display(onSpeedSlowPctLift, anchors)`.  These slide too
when calibration changes the OnSpeed band's position relative to
α₀..α_stall.  Visually the donut still fills the screen-y range
115..78 (it's where it's drawn) — what shifts is the *percent values
that map to those screen positions*.

Numeric `%` readout: `displayPercentLift` shows `percentLift` (clamped
0..99) directly. Same as today, just the underlying value is now
honest.

The piecewise structure of `mapPct2Display` is the visual compression
the user described:

> "We have one ramp from zero to whatever percent AoA fast is pegged at
> in the config. That's the sub-doughnut part. The part above the
> doughnut is the AoA slow up to stall."

That's exactly what the function above does.  The donut is the
horizontal middle band of the screen (y = 115..78).  Below it: one
linear ramp from 0% → onSpeedFastPct mapped to bottom → donut top.
Above it: one linear ramp from onSpeedSlowPct → 100%
(or stallWarnPct, see below) mapped to donut bottom → top.

#### Open question: top of upper ramp — `stallWarnPct` or 100%?

Two options:

a. Upper ramp goes from `onSpeedSlowPct` → 100%, with `stallWarnPct`
   marked as the "flash threshold" but not a band anchor.
b. Upper ramp goes from `onSpeedSlowPct` → `stallWarnPct`, with
   anything past `stallWarnPct` clamped to top-of-bar (current
   `mapAOA2Display` behavior).

Option (b) matches the current visual.  Option (a) would let the bar
slide a bit further toward the top when AOA is between StallWarn and
α_stall, which is actually a region the pilot is in for a bit
post-stall-warning.  Recommend (b) to match current behavior; can
revisit.

### What the audio path does after

**Unchanged.**  `ToneCalc::calculateTone` reads body angles and per-flap
body-angle setpoints directly from `g_Sensors.AOA` and
`g_Config.aFlaps[idx]`.  Audio cadence and frequency don't go through
`ComputePercentLift` and don't go on the display-serial wire.  Audio
path keeps doing exactly what it does today.

This is correct: audio cues fire at calibrated body angles per flap.
That's the OnSpeed contract — "when you hear the fast tone, you're at
your aircraft's calibrated L/Dmax for the active flap."  The audio is
the *truth* the pilot trains against.  The display is a visualization
of where AOA is in the envelope.

### What LiveView does after

LiveView gets data from a separate WebSocket (10–20 Hz JSON), not from
the display-serial wire.  The WebSocket carries `Alpha0`, `LDmax`,
`OnspeedFast`, `OnspeedSlow`, `OnspeedWarn` — body angles plus the
α₀ added in PR #320.  The WebSocket is a soft contract: clients ignore
unknown fields, so we can add new fields without breaking anything.

LiveView's onboard JS has the same bug structure as the M5: it
linear-interpolates body angle to screen-y per band.  The fix: change
the JS to compute the same honest percent from the same body angles
it already has, and use one `mapPct2DisplayJs` function with the four
percent anchors.  Or — easier — just have firmware send the percent
anchors over the WebSocket too (additive change to JSON, not breaking)
and let the JS use them directly.

Recommend: add `Alpha0`/`AlphaStall` to the WebSocket if they aren't
already, plus the four band-edge percents, and switch the JS to render
in percent space.  Mirrors the M5 redesign exactly.

### Defensive behavior at the boundaries

Handles the existing edge cases the segmented function already had:

1. **Below α₀ (negative percent):** clamp to 0.  Existing behavior.
2. **Above α_stall:** clamp to 99.  Existing behavior.
3. **α_stall ≤ StallWarn (uncalibrated):** the segmented function uses
   `stallWarn × 100/90` as a synthetic ceiling.  Honest formula needs
   an analogous fallback because uncalibrated `α_stall` would make the
   denominator collapse.  Use the same synthetic: if
   `α_stall ≤ StallWarn`, set effective `α_stall = StallWarn × 100/90`.
   Behavior in the calibrated case is unchanged; uncalibrated case
   reads approximately the same as today.
4. **`!iasValid`:** return 0.  Existing behavior.

---

## What this implies for PR #320

PR #320 needs a substantive rewrite, not a minor tweak:

- **Wire format changes again.**  PR #320 added the body-angle fields
  (`alpha0Deg`, `alphaStallDeg`, `tonesOnAoaDeg`, four threshold body
  angles, plus `flapsMin`/`Max`).  This redesign drops most of those
  and adds percent fields.  The 94-byte frame becomes 80 bytes.
  `aoaDeg` also drops (never displayed by M5 in degrees; LiveView gets
  it from the WebSocket).
- **`flapsMin`/`flapsMax` stay.**  These are needed by the flap-position
  widget (issue #322), not by the AOA bar.  Independent question.
- **The M5-side geometry change** (PR #320's `mapAOA2Display` lower-band
  collapse from `alpha_0 → LDmax → OnSpeedFast` piecewise to a single
  ramp, plus the L/Dmax-pip-floats fix) survives in spirit but is
  reimplemented as `mapPct2Display`.  The screen-y constants stay; the
  inputs become percents.
- **The framing accumulator + native-test infrastructure** survive
  unchanged — those are wire-agnostic.
- **The firmware-parser interop test** for m5-replay survives unchanged
  in shape; the test's expected fields just shift from body angles to
  percents.

Bottom line: the *infrastructure* PR #320 added (DisplayFrameAccumulator,
the bench-replay tool, the firmware-parser interop test, the protocol
reference doc) is all keepable.  The *fields it added to the wire* are
mostly the wrong fields.  Easier to rewrite the field set than to
revert and start over.

### How to land this

Two reasonable paths:

**Option A:** Force-push a redesigned PR #320 onto the same branch.
Mechanically simpler — already have the worktree, the test
infrastructure, the docs commit.  Replaces the body-angle wire fields
with percent wire fields; rewrites `ComputePercentLift`; rewrites the
M5 geometry; updates the protocol-reference doc; updates m5-replay's
Python builder to match.

**Option B:** Leave PR #320 as-is (body-angle fields on the wire) and
ship this redesign as a separate PR-after-#320 that supersedes those
fields.  Cleaner git history but requires double protocol bumps in
field — flash all units to PR #320 first, then to the redesign
shortly after.  Confusing for users.

**Recommend Option A.**  PR #320 hasn't merged.  It's a draft state
that this redesign supersedes.  Better to ship the right thing once.

---

## Implementation steps

1. **`ComputePercentLift` body** (`onspeed_core/aoa/PercentLift.cpp`):
   replace segmented function with honest formula.  Keep clamps and
   defensive ceiling.  ≈10 lines, signature unchanged.

2. **`test_percent_lift.cpp`**: rewrite tests.  Today's tests
   *encode the bug as a contract* (assert L/Dmax → 50, etc.).  New
   tests should:
   - Verify the formula at α₀ → 0, α_stall → 99 (clamp).
   - Verify the formula's linearity between those endpoints.
   - Verify the synthetic ceiling fallback for uncalibrated `α_stall`.
   - Verify L/Dmax body angle → its actual computed percent (will be
     ≈51% for a typical RV-class clean calibration; specific number
     depends on the test fixture).
   - **Not** assert any constant percent for any band edge.

3. **Wire format** (`onspeed_core/proto/DisplaySerial.{h,cpp}`):
   - Drop `aoaDeg`, `alpha0Deg`, `alphaStallDeg`, `tonesOnAoaDeg`,
     `stallWarnAoaDeg`, `onSpeedSlowAoaDeg`, `onSpeedFastAoaDeg`.
   - Change `percentLift` width 2 → width 4 signed, ×10 fixed-point.
   - Add `tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`,
     `stallWarnPctLift` as width-3 unsigned 0..99.
   - Update `BuildDisplayFrame` and `ParseDisplayFrame` accordingly.
   - Update `kDisplayFrameSizeBytes` from 94 to 80.
   - Bump format version comment.

4. **`test_display_serial.cpp`**: update for new field set.  Round-trip
   tests for new percent fields, drop tests for removed body-angle
   fields.

5. **Firmware producer** (`software/sketch_common/src/io/DisplaySerial.cpp`):
   - Compute `percentLift` via `ComputePercentLift` (already does — but
     calls the new honest function).
   - Compute the four band-edge percents the same way:
     `tonesOnPctLift = ComputePercentLift(flapSnapshot.fLDMAXAOA,
     flapSnapshot, true)`, and similarly for the other three.
   - Populate the new `DisplayBuildInputs` fields.

6. **M5 consumer** (`software/OnSpeed-M5-Display/src/main.cpp` and
   `SerialRead.cpp`):
   - Drop globals `Alpha0`, `AlphaStall`, `OnSpeedStallWarnAOA`,
     `OnSpeedSlowAOA`, `OnSpeedFastAOA`, `OnSpeedTonesOnAOA`, `AOA`.
   - Replace with `PercentLift`, `TonesOnPctLift`, `OnSpeedFastPctLift`,
     `OnSpeedSlowPctLift`, `StallWarnPctLift`.
   - Replace `mapAOA2Display` with `mapPct2Display`.
   - Replace AOAThresholds[] body-angle population with percent-anchor
     population (or just inline the four percent values into the
     `mapPct2Display` calls).
   - Update `displayAOA()` callers — pass percent-mode anchors instead
     of body-angle thresholds.

7. **LiveView** (`software/OnSpeed-Gen3-ESP32/Web/html_liveview.h`):
   - Add `Alpha0`, `AlphaStall` (already added by PR #320 for `Alpha0`),
     plus four band-edge percents to the WebSocket JSON.
   - Switch JS rendering to percent-based `mapPct2DisplayJs` with the
     same shape as M5's new function.

8. **m5-replay** (`tools/m5-replay/replay.py`,
   `tools/m5-replay/test_replay.py`,
   `tools/m5-replay/parse_frame.cpp`):
   - Update Python `Frame` dataclass: remove body-angle fields, add
     percent fields.
   - Update Python `compute_percent_lift` to honest formula.
   - Update `to_bytes` for new wire layout.
   - Update CSV-stream + synthetic-stream factories to populate the
     percent fields (compute via `compute_percent_lift` from the
     calibration's body angles).
   - Update `parse_frame.cpp` to print the new field set.
   - Update tests.

9. **Docs**:
   - `docs/site/docs/installation/external-display.md`: rewrite "50% =
     LDmax. 66% ≈ middle... 90% = stall warn." paragraph.  Replace
     with: "the percent-lift number is your AOA expressed as a fraction
     of the lift envelope. L/Dmax, OnSpeed band edges, and stall warn
     all sit at the percent your calibration places them at — measure
     the L/Dmax body angle in flight and find what percent your
     airframe shows when you're there."
   - `docs/site/docs/reference/glossary.md`: rewrite the "Fractional
     Lift" entry similarly.  Drop the "~60% = ONSPEED, ~50% = L/D~MAX~,
     ~90% = stall warning" line — those are training shorthand from
     the old design, not aerodynamic facts.
   - `docs/site/docs/reference/serial-protocol.md`: rewrite the wire
     spec.  Update the change log to note the v4.20 redesign.
   - `docs/site/docs/calibration/how-aoa-works.md`: this page already
     describes the honest formula correctly (the `(α − α₀)/(α_stall − α₀)`
     framing).  Add a sentence noting that the displayed percent
     readout matches this formula directly — the indicator is now an
     honest visualization of what this page describes.

10. **Gen2 user manual** (out of scope for this PR, but worth flagging
    to the team): the manual at
    `flyonspeed/OnSpeed-Gen2/Documents/The ONSPEED System Abbreviated User.pdf`
    explicitly states "L/D~MAX~ occurs at 50% lift and ONSPEED occurs at
    60% lift."  After the redesign, the Gen3 software contradicts this.
    Worth deciding: is the Gen2 manual canonical (in which case the
    Gen3 design is broken — but it isn't, you flagged it as the bug),
    or is the Gen2 manual itself a documentation-of-the-bug?  Almost
    certainly the latter.  Flag for Vac.

---

## Issue cleanup

After this lands:

- **#321 (`SnapshotForDisplay()` interpolation)** closes as obsolete.
  The interpolation happened in body-angle space; now everything is
  in percent space, and the L/Dmax pip's percent value is just
  `tonesOnPctLift` of the active flap snapshot.  When the firmware
  detects between-detents flap position, it can interpolate between
  adjacent flaps' `tonesOnPctLift` values instead — but that's a
  smaller question than #321 originally was.

- **#322 (flap widget consumes flapsMin/Max)** unchanged.  Still
  applies — the flap-position widget is a separate concern from the
  AOA bar.

- **#323 (PercentLift wire-field audit)** closes as fixed.  This
  redesign IS the audit's recommended endpoint.

- **#324 (gOnsetRate)** unchanged.  Independent of the percent-lift
  question.

---

## Open questions to confirm before code

1. **Wire size of `percentLift`:** width 4 signed `%+04d` ×10
   (gives 0.1% resolution + below-α₀/above-α_stall headroom)
   vs. width 2 `%02u` (saves 2 bytes, matches today).  Recommend
   width 4; needs your call.

2. **Upper-ramp top:** stop at `stallWarnPct` (today's behavior) or
   extend to 100% (`α_stall`).  Recommend stop at `stallWarnPct`.

3. **Synthetic α_stall fallback:** keep the `stallWarn × 100/90`
   fallback for uncalibrated configs?  Recommend yes.

4. **Vac sign-off:** the design intent shift (from "L/Dmax pinned to
   50% as a categorical training cue" to "L/Dmax position is whatever
   the calibration gives it") is a pilot-facing behavior change.  Vac
   is the right person to confirm the new framing is consistent with
   how he thinks about and teaches OnSpeed.  This is a training /
   pilot-mental-model question, not just a software question.

5. **Gen2 manual:** flag for Vac as part of the same conversation.

---

## Why this is the right fix

- **The number becomes truthful.**  Pilots can reason about percent-lift
  as an aerodynamic property of the wing, comparable across flap
  settings.
- **The wire becomes simpler.**  Five fields, all in the same units.
  The whole "alpha_0 carries the negative-floor information" complexity
  PR #320 introduced disappears.
- **The consumer becomes simpler.**  One mapping function, no body-angle
  knowledge, no per-flap math at the consumer.
- **The audio path is unchanged.**  Audio fires at calibrated body
  angles per flap, which is the OnSpeed training contract.  The
  display visualization is now honest about where those audio cues
  fall in the lift envelope, but the audio cues themselves don't move.
- **The next consumer is trivial to add.**  HuVVer panel, X-Plane
  bridge, future TronView HUD, all consume "percent at the pip,
  percent at the bar, four percent anchors" — no aerodynamic curve
  fitting required.

---

## What this DOES NOT change

- **Audio behavior.**  Same tones at same body angles per flap.
- **Calibration workflow.**  Same wizard, same `.cfg` parameters
  (`fLDMAXAOA`, `fONSPEEDFASTAOA`, `fONSPEEDSLOWAOA`, `fSTALLWARNAOA`,
  `fAlpha0`, `fAlphaStall`).  The firmware just composes them
  differently for display.
- **Log CSV format.**  `efisPercentLift` is the EFIS's own number,
  unrelated.  OnSpeed's own AOA log column is `AngleofAttack` (body
  angle, unchanged).  No log columns affected.
- **Audio tone simulator on the docs site.**  Body-angle direct,
  unaffected.
- **X-Plane plugin.**  Reads X-Plane datarefs, unaffected.

---

## File staging

Both this plan (`docs/PLAN_PERCENT_LIFT_HONESTY.md`) and the spin-cue
plan (`docs/PLAN_SPIN_RECOVERY_CUE.md`) are on disk in the worktree,
untracked.  Memory persists at
`~/.claude/projects/-Users-sritchie-code-onspeed/memory/`.  Neither is
committed pending your read-through.
