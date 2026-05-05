# Indexer Spec

This is the developer-facing reference for the OnSpeed indexer — the visual gauge the M5 secondary display (and the in-cockpit tablet via the `/indexer` web tab) draws. It defines what every visual element shows, what gates each color change, and how each anchor on the gauge moves with the lever.

It is **not** a pilot-facing document. For "what do the lights mean in flight," see [What the Tones Mean](../flying/tone-map.md). For the audio side that the chevron + audio low tone share, see [Audio Tone Spec](audio-tone-spec.md).

The canonical implementations are:

- `software/OnSpeed-M5-Display/src/main.cpp::drawAOA` — the renderer
- `software/Libraries/onspeed_core/src/aoa/DisplayPctAnchors.{h,cpp}` — the anchor producer
- `software/Libraries/onspeed_core/src/proto/DisplaySerial.h` — the wire format

## 0. Design rule (Vac)

> **L/Dmax pips are aerodynamic references.**
>
> **Fast tone is an operational limit cue.**
>
> **They must remain independent.**
>
> — Vac, *L/Dmax Cue and Fast-Tone Logic*, §8

!!! note "Vac's 'fast tone' = OnSpeed's 'low tone'"
    Vac's term **fast tone** is the cue that fires when you're flying *fast for this configuration* — i.e., AOA is below the OnSpeed band. In OnSpeed firmware terminology this is the **low tone** (the 400 Hz pulse, gated by `fLDMAXAOA`). The OnSpeed *high tone* (1600 Hz, gated by `fONSPEEDSLOWAOA`) is the slow-side warning, not the fast-side cue. So Vac's "fast tone is an operational limit cue" applies to the L/Dmax / low-tone gate documented throughout this spec.

This rule is load-bearing. The indexer carries two cues that are *visually* near each other but *semantically* unrelated:

- **Operational cue**: the bottom green chevron, gated on the audio low-tone threshold. Snaps per detent. If a tone is on, the chevron is green.
- **Aerodynamic cue**: the L/Dmax pip dots on the index bar's edges. Slides smoothly with the lever. Reflects the aerodynamic intuition that L/Dmax body angle slides toward the OnSpeed band as flaps deploy.

The two coincide visually only at the cleanest detent. They diverge during deployment, by design.

## 1. Visual elements

The indexer occupies a vertical strip on the M5 panel (and the tablet canvas). Five elements:

| Element | Position | Purpose |
|---|---|---|
| **Top chevrons** | Top of strip | Stall warning. Yellow approaching stall, red imminent, flashing red past stall warn. |
| **Bottom chevron** | Bottom of strip | Operational cue. Green when the audio low tone is playing. |
| **Donut top arc** | Just above midline | OnSpeed cue (top half). Green when AOA is in the upper half of the OnSpeed band. |
| **Donut bottom arc** | Just below midline | OnSpeed cue (bottom half). Green when AOA is in the lower half of the OnSpeed band. |
| **Donut center dot** | Center of strip | OnSpeed cue (centered). Green when AOA is in the middle 50% of the OnSpeed band. |
| **White index bar** | Slides on strip | Current AOA in percent-lift space. |
| **L/Dmax pip dots** | Two small dots on strip edges | Aerodynamic cue. Slides smoothly clean → fullflap. |

The strip's vertical extent maps to percent-lift via a piecewise-linear function (see §4). All gates are in percent-lift units.

## 2. Wire fields that drive the indexer

The Gen3 firmware emits one `#1` frame every 50 ms. Each visual element reads one or more percent fields:

| Wire field | Snap or slide | Drives |
|---|---|---|
| `percentLift` | live AOA reading | white index bar position |
| `tonesOnPctLift` | snap per active detent | bottom chevron lower gate |
| `onSpeedFastPctLift` | snap per active detent | donut bottom arc lower edge, donut center bounds, screen Y-mapping |
| `onSpeedSlowPctLift` | snap per active detent | donut top arc upper edge, screen Y-mapping, top chevron color thresholds |
| `stallWarnPctLift` | snap per active detent | top chevron flash threshold, screen Y-mapping |
| `pipPctLift` | slide clean → fullflap | L/Dmax pip dot positions |
| `flapsDeg` | per-bracket lerp | flap-position widget angle (separate from indexer) |

`percentLift` is the live AOA reading as a `float` in whole-percent units (0.0..99.9), computed from `g_Sensors.AOA` and the active detent's calibration via `ComputePercentLift`. The four anchor fields (`tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`, `stallWarnPctLift`, `pipPctLift`) stay integer-percent in `[0, 99]` — they only move on detent or config-save events, so sub-percent buys nothing on those. Computed by `ComputeDisplayPctAnchors` from the configured flap vector and the raw lever-pot ADC. Comparisons between `percentLift` (float) and an anchor (int) promote the int to float; behavior is exact for our range since integer values up to 2²⁴ are representable in float32 without rounding.

## 3. Gates and color logic

### 3.1 Top chevrons

Two stylized triangles forming an up-arrow near the top.

```
    chevMid = onSpeedSlowPctLift + (stallWarnPctLift − onSpeedSlowPctLift) / 2

    if percentLift > onSpeedSlowPctLift  and  percentLift ≤ chevMid          → YELLOW
    if percentLift > chevMid             and  percentLift ≤ stallWarnPctLift → RED
    if percentLift > stallWarnPctLift    and  not flashing                   → RED
    if percentLift > stallWarnPctLift    and  flashing                       → DARKGREY (flashing red blink)
    otherwise                                                                 → DARKGREY
```

Source: `main.cpp::drawAOA`, lines ~870–920.

### 3.2 Bottom chevron — *operational cue*

Two stylized triangles forming a down-arrow near the bottom.

```
    if percentLift ≥ tonesOnPctLift  and  percentLift < onSpeedSlowPctLift → GREEN
    otherwise                                                              → DARKGREY
```

Source: `main.cpp::drawAOA`, lines ~921–970.

**This gate matches the audio low-tone gate exactly.** When the audio is pulsing the low tone (a "you're flying fast for this flap configuration" cue), the bottom chevron is green. When the audio goes silent or transitions to solid (in the donut band), the chevron goes dark or stays in the prior color.

The audio path computes the same condition independently, against `g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA` and `fONSPEEDFASTAOA`. `tonesOnPctLift` on the wire is `ComputePercentLift(active.fLDMAXAOA, active, true)` — the percent representation of the audio threshold for the active detent. Both fire from the same source of truth; they snap together at every detent transition.

### 3.3 Donut

Three concentric elements.

```
    OnspeedRange = onSpeedSlowPctLift − onSpeedFastPctLift

    bottom arc   = GREEN  if  percentLift ≥ onSpeedFastPctLift                  and  percentLift ≤ onSpeedSlowPctLift − 0.25 × OnspeedRange
    top arc      = GREEN  if  percentLift ≥ onSpeedFastPctLift + 0.25 × OnspeedRange  and  percentLift ≤ onSpeedSlowPctLift
    center dot   = GREEN  if  percentLift ≥ onSpeedFastPctLift + 0.25 × OnspeedRange  and  percentLift ≤ onSpeedSlowPctLift − 0.25 × OnspeedRange
```

The three regions overlap: at the geometric center of the band, all three are lit simultaneously. The center dot lights across the inner 50% of the band; the arcs light across an extra 25% on each side.

Source: `main.cpp::drawAOA`, lines ~975–1000.

### 3.4 White index bar

Horizontal bar across the full strip width.

```
    Y = mapPct2Display(percentLift, anchors)
```

Where `mapPct2Display` (see §4) maps percent-lift to a pixel Y coordinate using the snapped band edges as anchor points.

Source: `main.cpp::drawAOA`, lines ~1002–1004.

### 3.5 L/Dmax pip dots — *aerodynamic cue*

Two small white dots, one on each side of the strip.

```
    Y = mapPct2Display(pipPctLift, anchors)
```

`pipPctLift` slides smoothly across the entire pot range (see §5). At the cleanest detent, `pipPctLift == tonesOnPctLift` and the pip lines up with the bottom chevron's edge. At the most-deployed detent, `pipPctLift` equals the geometric center of that detent's OnSpeed band and the pip sits inside the donut.

Source: `main.cpp::drawAOA`, lines ~1010–1014.

## 4. Screen-Y mapping (`mapPct2Display`)

The strip's pixel Y coordinate is a piecewise-linear function of percent-lift, anchored on the snapped band edges:

| Percent-lift range | Pixel Y range |
|---|---|
| `≤ 0` | `192` (display bottom) |
| `(0, onSpeedFastPctLift]` | `192 → 115` (linear) |
| `(onSpeedFastPctLift, onSpeedSlowPctLift]` | `115 → 78` (donut band, fixed screen-Y) |
| `(onSpeedSlowPctLift, 99]` | `78 → 1` (linear) |
| `> 99` | `1` (display top) |

The upper ramp tops out at percent_lift = 99 — the lift-envelope ceiling — independent of the active detent's stall-warn percent. Stall-warn drives the chevron flash-red color logic in `drawAOA` (§3.1); it does not gate Y here. Floats in (99.0, 99.9] (the float clamp range above 99) saturate at y=1 just like the integer 99 — the bar visibly pinning at "off the chart" is the documented saturation cue.

The donut band is anchored at fixed pixel Y so the donut never moves on screen, regardless of which detent is active. The L/Dmax pip and the white index bar both float according to their percent values — when those values fall within the donut band, they appear inside it.

Source: `main.cpp::mapPct2Display`, line ~1510.

## 5. Pip computation (`pipPctLift`)

The pip percent is a single linear interpolation between two endpoints:

$$
\text{pipPctLift} = \text{round}\left(\text{lerp}\left(\text{pipClean},\ \text{pipFullFlap},\ \lambda\right)\right)
$$

with

$$
\lambda = \mathrm{clamp}\!\left( \frac{\text{rawAdc} - \text{cleanest.iPotPosition}}{\text{mostDeployed.iPotPosition} - \text{cleanest.iPotPosition}},\ 0,\ 1 \right)
$$

and

$$
\text{pipClean} = \text{ComputePercentLift}(\text{cleanest.fLDMAXAOA},\ \text{cleanest})
$$

$$
\text{pipFullFlap} = \frac{1}{4}\left( 3 \cdot \text{ComputePercentLift}(\text{mostDeployed.fONSPEEDFASTAOA},\ \text{mostDeployed}) + \text{ComputePercentLift}(\text{mostDeployed.fONSPEEDSLOWAOA},\ \text{mostDeployed}) \right)
$$

That is, the full-flap pip target is the **bottom-half-of-donut** anchor — one quarter of the way from the fast (lower-percent) edge into the OnSpeed band. Lands the chevron in the lower donut at L/Dmax instead of climbing into the upper donut to meet a band-center pip (per PR #376).

`cleanest` is the lowest-degree configured flap entry (`g_Config.aFlaps[0]` after parse-time sort). `mostDeployed` is the highest-degree entry (`g_Config.aFlaps[entryCount-1]`). **Intermediate detents are ignored** for the pip — a typical 3-detent config (clean / 16° / 33°) lerps from clean to 33° as the lever moves, passing through the 16° detent's calibrated L/Dmax percent only by coincidence (the 16° detent's L/Dmax does not anchor the pip).

Source: `DisplayPctAnchors.cpp::ComputeDisplayPctAnchors`.

### Why ignore intermediate detents

Per Vac, this is intentional. The pip is a *visual aerodynamic reference*, not a per-detent calibration anchor. The aerodynamic intuition — "L/Dmax slides toward OnSpeed as flaps deploy" — is faithfully represented by a smooth two-endpoint lerp; making the pip visit each calibrated detent's L/Dmax mid-deployment introduces a subtle stutter the design specifically avoids.

The operational cue (the bottom chevron, gated on `tonesOnPctLift`) handles per-detent calibration accurately, because it must match the audio.

## 6. Snap-vs-slide table

| Field | Behavior | Why |
|---|---|---|
| `tonesOnPctLift` | snap per active detent | matches audio low-tone gate; chevron snaps with audio |
| `onSpeedFastPctLift` | snap per active detent | donut lower edge; matches audio's onspeed-fast threshold |
| `onSpeedSlowPctLift` | snap per active detent | donut upper edge; matches audio's onspeed-slow threshold |
| `stallWarnPctLift` | snap per active detent | top chevron flash; matches audio's stall-warn threshold |
| `pipPctLift` | slide clean → fullflap (single lerp, ignores intermediate detents) | aerodynamic reference; smooth visual |
| `flapsDeg` | slide per-bracket (lerp adjacent detents' iDegrees) | mechanical lever angle; visits every detent |

## 7. Continuity invariants

Pinned by `test/test_display_pct_anchors/`:

1. **`pipPctLift` is continuous in `rawAdc` everywhere.** No discontinuities at detent boundaries — single lerp covers the entire pot range. Adjacent ADC samples produce pip values within 1 percent of each other.
2. **`tonesOnPctLift` snaps at `iIndex` advances.** Same `rawAdc` with different `activeIndex` values produces different `tonesOnPctLift` values, by exactly the difference between the two detents' calibrated L/Dmax percents. The chevron edge snaps with the audio.
3. **`pipPctLift` is independent of `activeIndex`.** The pip depends only on `rawAdc` and the configured flap vector — not on which detent is "active." A detent transition does not change the pip.
4. **Pip and tones-on coincide at the cleanest detent.** When `rawAdc == cleanest.iPotPosition` and `iIndex == 0`, `pipPctLift == tonesOnPctLift`. This is the visual "they line up in clean" property.
5. **Pip and tones-on diverge at full flaps.** When `rawAdc == mostDeployed.iPotPosition` and `iIndex == entryCount-1`, `pipPctLift > tonesOnPctLift` for any sane calibration where L/Dmax is below the OnSpeed band.

## 8. Worked example (RV-10, sam@frogrocketai.com calibration)

Three detents from `~/Downloads/onspeed2_latest.cfg`:

| Flap | Pot ADC | `fLDMAXAOA` (°) | `fONSPEEDFASTAOA` (°) | `fONSPEEDSLOWAOA` (°) | `fAlpha0` (°) | `fAlphaStall` (°) |
|---|---|---|---|---|---|---|
| 0° (clean) | 1462 | 3.24 | 3.98 | 5.26 | −3.72 | 10.31 |
| 16° | 897 | 1.11 | 2.44 | 3.88 | −6.22 | 9.57 |
| 33° (full) | 2 | −2.24 | 2.19 | 4.09 | −9.21 | 11.57 |

Computed percent-lift values (from the actual `ComputePercentLift` /
`ComputeDisplayPctAnchors` implementation, which truncates fractions
toward zero per the saturation convention):

| Flap | L/Dmax pct | Fast pct | Slow pct | StallWarn pct |
|---|---|---|---|---|
| 0° (clean) | **49** | 54 | 64 | 85 |
| 16° | **46** | 54 | 63 | 84 |
| 33° (full) | **33** | 54 | 64 | 82 |

The pip's full-flap target is the round-half-away mean of 33°'s
Fast and Slow: `lround((54 + 64) / 2.0) = 59`.

Lever positions and what the indexer shows. Active-detent index
follows the standard midpoint rule (`Flaps::Update`):

| Lever pot | Active detent | `tonesOnPctLift` | `pipPctLift` | flapsDeg | Coincide? |
|---|---|---|---|---|---|
| 1462 (clean) | 0° | 49 | 49 | 0 | yes (pip and chevron edge line up) |
| 1100 (mid clean→16°) | 16° | 46 | 51 | 10 | no |
| 897 (16° detent) | 16° | 46 | 53 | 16 | no (chevron at 46, pip at 53) |
| 450 (mid 16°→33°) | 16° | 46 | 56 | 24 | no |
| 2 (33° full) | 33° | 33 | 59 | 33 | no (chevron at 33, pip at center of donut) |

Notice that at the 16° detent, the chevron edge sits at the
calibrated 46% (from the 16° detent's `fLDMAXAOA`), but the pip is at
53% (from the two-endpoint lerp). The pip ignores the 16° detent's
calibration.

The lerp's denominator is the full pot span (1462 − 2 = 1460); at the
1100 row, `t = (1462 − 1100) / 1460 = 0.248`, so
`pip = lround(49 + 0.248 × (59 − 49)) = lround(51.48) = 51`. Same
formula for every row.

## 9. Bench verification

Reflash bench device with v4.22 firmware on both Gen3 main and M5. Confirm:

- **Clean, AOA below L/Dmax:** silent, chevron grey, pip and chevron edge coincide on screen.
- **Clean, AOA between L/Dmax and OnSpeedFast:** low tone pulsing, chevron green, pip and chevron edge still coincide.
- **Mid-deployment (lever between 0° and 16° detents):** chevron snaps from the clean L/Dmax to the 16° detent's L/Dmax at the midpoint pot value (in lockstep with `iIndex` advancing in `Flaps::Update`); pip slides smoothly with no jump.
- **Full flaps (33°), AOA between active L/Dmax and OnSpeedFast:** low tone pulsing, chevron green; pip sits up at 59% (inside the donut band on screen), well above the chevron-lit band.

## 10. Wire-format change history

- **v4.21** (PR #320): introduced percent anchors; `tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`, `stallWarnPctLift` all snap per active detent.
- **v4.21+** (PR #327): made `tonesOnPctLift` interpolate across adjacent detent brackets so the L/Dmax pip slid smoothly. **This broke chevron–audio alignment mid-deployment** because the chevron is gated on the same field as the audio threshold — making the field interpolated meant the chevron lit at a percent the audio did not match.
- **v4.22** (PR #336): split the pip out into a new wire field, `pipPctLift`. `tonesOnPctLift` reverts to the v4.21 snap-per-detent semantics. Frame size grows from 74 to 76 bytes. Chevron and audio fire from the same threshold again.
- **v4.23** (PR #386): `percentLift` widens from `%02u` (integer percent, 0..99) to `%03u` (tenths of a percent, 0..999) so the index bar advances at sub-pixel temporal smoothness off the 20 Hz frame cadence. The four band-edge anchors stay at integer-percent. `lateralG` flips to body-frame (positive = right) to match the IMU, SD log, and WebSocket JSON; slip-ball renderers negate locally. Frame size grows from 76 to 77 bytes.

See [Display Serial Protocol](../reference/serial-protocol.md) for the byte-level wire format.
