# How OnSpeed Measures AOA

Understanding the measurement helps you produce better calibrations and troubleshoot problems.

!!! note "OnSpeed measures body angle, not wing AOA"
    Throughout the firmware, configuration, and these docs, the quantity called "AOA" is **body angle** — the fuselage-to-wind angle, in degrees. It is not wing angle of attack.

    Most aircraft mount the wing at a positive incidence relative to the fuselage longitudinal axis, so the fuselage points slightly nose-down when the wing is at zero AOA. The body angle at zero wing lift is $\alpha_0$, and on most aircraft it is **negative** (typically −3° to −6°, varying with flap setting).

    This is why fractional lift is computed as $(\alpha - \alpha_0) / (\alpha_\text{stall} - \alpha_0)$ — the floor is $\alpha_0$, not zero. A body angle of −3° is genuinely producing positive lift if $\alpha_0$ is −4°, and the math has to reflect that.

## Angle of Attack and Fractional Lift

Every wing has an angle of attack at which it produces no lift (the **zero-lift angle**, $\alpha_0$). As AOA increases from this point, lift increases until the wing reaches its maximum at the **critical angle of attack** ($\alpha_\text{stall}$), just prior to stall.

Angle of attack can be thought of as a scale from zero to one. Zero represents no lift. One represents maximum lift. **Fractional lift** (sometimes called "percent lift") is where the wing is operating on that scale at any given moment:

$$\text{Fractional Lift} = \frac{\alpha - \alpha_0}{\alpha_\text{stall} - \alpha_0}$$

This is the same quantity that OnSpeed calls **Normalized AOA (NAOA)**. Approximate reference points across most straight-wing GA airplanes:

| Condition | Fractional Lift (typical) | What It Means |
|-----------|---------------------------|---------------|
| Zero lift | 0% | No lift being generated |
| Maneuvering speed (V~A~, 3.8G limit) | ~26% | Below this, full deflection causes stall before exceeding G-limit |
| L/D~MAX~ | ~50% | Best glide, maximum range |
| **ONSPEED** | **~60%** | Balanced effective power, V~REF~, max sustained turn |
| Stall warning | ~90% | Near aerodynamic limit |
| Stall | 100% | Maximum lift — critical AOA |

For most straight-wing GA airplanes, critical AOA is approximately 15–20°. Typical ONSPEED approach conditions correspond to roughly 60% of the wing's maximum lift capability. **The exact percent at which each band falls is set by your aircraft's calibration and may differ between flap settings** — it's the audio cue (or the indicator's chevrons) the pilot follows, not a specific number. The audio L/D~MAX~ for a clean RV-class airplane might land at 33% on the indicator's number, and at 56% with full flaps, depending on how the lift envelope shifts with flap deflection.

## Two AOA Measurements

OnSpeed computes AOA two different ways:

### 1. Pressure-Based AOA

A differential pressure probe senses the **coefficient of pressure** ($C_p$), which varies with air angle. This raw $C_p$ is converted to an AOA value using a polynomial curve (the one the calibration wizard fits):

$$\text{AOA} = f(C_p) = a_3 C_p^3 + a_2 C_p^2 + a_1 C_p + a_0$$

This is the primary AOA measurement used for tone generation.

### 2. Derived AOA (from AHRS)

The AHRS (Attitude and Heading Reference System) provides pitch angle. Combined with flight path angle, this gives:

$$\text{DerivedAOA} = \text{SmoothedPitch} - \text{FlightPath}$$

where:

$$\text{FlightPath} = \arcsin\left(\frac{\text{VSI}}{\text{TAS}}\right)$$

This DerivedAOA measures the **fuselage-to-wind angle** — the angle between where the airplane is pointed and where it's actually going. From a pilot's perspective, body angle corresponds directly to angle of attack. However, it is affected by flap configuration, which is why each flap setting requires separate calibration.

In straight-and-level, trimmed, unaccelerated flight, pitch attitude and AOA coincide (because flight path angle is zero). This is the specific condition used during calibration — slow, wings-level decelerations where pitch attitude is a reliable proxy for AOA.

## The Lift Equation Fit

The key physics insight: in steady, wings-level flight, the lift equation gives us:

$$\text{DerivedAOA} = \frac{K}{\text{IAS}^2} + \alpha_0$$

where:

- $K$ is a constant related to aircraft weight, wing area, and the lift curve slope
- $\alpha_0$ is the zero-lift fuselage angle — a negative number that represents the AOA at which the wing produces no lift
- $\text{IAS}$ is indicated airspeed

This is a hyperbolic curve: as you slow down (IAS decreases), DerivedAOA increases. The relationship between speed and AOA is not linear — **AOA increases more rapidly as the airplane decelerates**. Approximately half of the wing's lift-producing capability is used in the lower third of the airplane's speed range. This is why maneuvering near approach speeds is unforgiving: angle of attack rises rapidly with even small increases in G.

At the stall, the curve departs from this shape (lift stops increasing linearly with AOA). The calibration wizard fits this curve to your deceleration data and extracts both $\alpha_0$ and $\alpha_\text{stall}$.

## Why alpha_0 Matters

The DerivedAOA has a **non-zero origin**. At infinite speed (if that were possible), the DerivedAOA would be $\alpha_0$ — typically a negative number around -4° to -6°, depending on the aircraft and flap setting.

This means you **cannot** express AOA setpoints as simple fractions of the stall AOA. A naive calculation like "ONSPEED = 75% of stall AOA" gives dangerously wrong answers because it ignores the $\alpha_0$ offset.

Instead, OnSpeed uses **normalized AOA** (NAOA), which is identical to fractional lift:

$$\text{NAOA} = \frac{\text{AOA} - \alpha_0}{\alpha_\text{stall} - \alpha_0}$$

And setpoints are computed as:

$$\text{AOA}_\text{setpoint} = \text{NAOA}_\text{target} \times (\alpha_\text{stall} - \alpha_0) + \alpha_0$$

The NAOA targets come directly from the lift equation. For example, an ONSPEED tone at $1.3 \times V_s$ corresponds to:

$$\text{NAOA}_\text{ONSPEED} = \frac{1}{1.3^2} \approx 0.592 \quad (\text{~60\% fractional lift})$$

This is not coincidental — it reflects the physics of lift. At 1.3 × stall speed, the wing is using approximately 60% of its maximum lift capability.

## Per-Flap Calibration

Each flap setting has different:

- **$\alpha_0$** — zero-lift angle shifts with flap deflection
- **$\alpha_\text{stall}$** — stall angle changes with flaps
- **$K$** — the lift sensitivity constant changes
- **AOA polynomial** — the $C_p$-to-AOA mapping changes

This is why you must calibrate each flap position separately. Different flap settings produce different aerodynamic behavior, and the tone setpoints must reflect the characteristics of each configuration.

## What the Calibration Wizard Fits

The wizard performs two fits:

1. **$C_p$ to AOA polynomial** — maps the raw pressure coefficient to an AOA angle (3rd-order polynomial)
2. **IAS to DerivedAOA hyperbola** — the $K/\text{IAS}^2 + \alpha_0$ fit that extracts $\alpha_0$, $\alpha_\text{stall}$, and the $K$ parameter

From these, it computes the six AOA setpoints (L/D~MAX~, ONSPEED-Fast, ONSPEED-Slow, Stall Warning, Stall, Maneuvering) using the normalized AOA fractions.

## Why the calibration sweep works

The wizard's procedure — a single deceleration sweep at idle power, wings level, ball centered, from above approach speed down to the stall — is not arbitrary. Each constraint exists to make the recorded data match the assumptions baked into the lift-equation fit.

**Idle power, wings level, coordinated.** The hyperbolic fit $\alpha = K/\text{IAS}^2 + \alpha_0$ assumes steady, unaccelerated flight at 1 G. Power applied to the propeller adds thrust that the fit doesn't model. Bank or sideslip changes the load factor and the pressure field at the AOA probe. A sweep flown clean of these confounders produces a curve that fits the model; a sweep flown with power, bank, or yaw produces noise the wizard tries to fit anyway.

**Stop trimming at 80–90 KIAS.** Trimming all the way down to the stall buries control-position changes inside the data. The recorded pitch attitude at the slow end of the sweep is no longer a clean reflection of AOA; it's pitch attitude plus whatever the trim and elevator are doing. Stopping the trim adjustment at 80–90 KIAS keeps the slow end of the sweep clean.

**Recover with AOA, not power.** When you reach the stall warning, ease the stick forward to break the AOA, then add power. Pulling power back to recover flattens the recovery curve and hides the AOA peak the wizard needs to identify $\alpha_\text{stall}$.

**Peak-AOA extraction is per-flap.** Each flap setting moves $\alpha_0$, $\alpha_\text{stall}$, and the lift-curve slope. There is no shortcut to calibrating one flap setting and scaling — the fit is genuinely different per flap, which is why the wizard runs once per flap detent.

The mechanics above are the OnSpeed wizard's automated form of a technique Vac described pre-OnSpeed for pilots characterizing a third-party EFIS's AOA scaling, in [On Speed Angle of Attack Video](https://vansairforce.net/threads/on-speed-angle-of-attack-video.148638/) (post #25). The flying is the same; the wizard does the data extraction the EFIS-characterization workflow used to do by hand:

> "The simplest technique is to select idle power and maintain altitude precisely. Trim as the airplane decelerates, but stop trimming at about 80-90. After the stall, use AoA to recover (i.e., pitch down) and let the airplane recover at idle power and accelerate to about 100 MPH/90 KTS indicated… This isn't a speed drill — about 45-60 seconds per stall should work out about right."

## Calibration reality

A pressure-derived AOA system measures AOA through a stack of sensors, plumbing, and air data — and every layer adds error.

**Static-source position error.** The static port is calibrated for unaccelerated, level flight at one airspeed. Outside that band the indicated static pressure drifts from true static, IAS drifts from CAS, and dynamic pressure at the AOA probe drifts from the value the fit assumes. The drift is small — 1–2 knots is typical — and it's mostly absorbed into the calibrated curve, but it's why a sweep flown at one altitude may produce slightly different setpoints than a sweep flown at another.

**Yaw effects.** Sideslip changes the pressure field at the AOA probe. Conventional pitot/AOA probes (Dynon, Garmin) lose accuracy past about 6° of yaw with a blade probe and about 20° with a sphere probe. The system can still detect the stall through significant yaw — Vac demonstrates oscillating slipping stalls in the [AOA System – Calibration and Use](https://vansairforce.net/threads/aoa-system-calibration-and-use.222357/) thread — but the absolute AOA reading drifts during heavy slip.

**Icing.** Pitot ice plugs the dynamic-pressure inlet, which drives both IAS and pressure-derived AOA toward zero. Plumbing ice between the probe and the sensor can introduce slow drift before complete blockage. Pitot heat is the only mitigation; without it, an iced probe is a silent failure mode for both airspeed and AOA.

**Probe placement.** The probe lives in the wing's flow field, which is itself disturbed by the airframe. Move the probe more than a few inches and the calibration shifts. Vac's note from [Go down, speed up, stall out…](https://vansairforce.net/threads/go-down-speed-up-stall-out.228078/) (post #25):

> "Aerodynamics (thus AOA performance) occur when the tubes are in the same flow field. There is an art to sensor placement when the objective is accurate performance and good transient response."

A correct calibration is not "the AOA reading matches a reference instrument exactly." It is "the tone transitions arrive at the airspeeds and load factors the lift equation predicts, and the stall warning fires before the break." The wizard's fit captures that relationship; the [verification flight](../flying/flight-test-procedures.md) confirms it.

## Some Dynon AOA systems peak at 80% lift, not 100%

OnSpeed's wizard finds $\alpha_\text{stall}$ from the AOA peak in the deceleration sweep. Some EFIS-derived AOA systems display lift fraction relative to a reference that is not 100% at the stall — the Dynon DY-10/100 stack, in the example below, peaks at ~80% during a clean-configuration stall:

> "You can see the stall is occurring at about 80% total lift in this case, not 100. Now ONSPEED still occurs at 60% of peak (i.e., .6 × 80%); but I don't think the Dynon graphic display makes allowance for this."  
> — Vac, [What Energy Sounds Like](https://vansairforce.net/threads/what-energy-sounds-like.196593/) (post #6)

When the EFIS's "100%" doesn't actually correspond to the stall, you can't read setpoints off the graphic at face value. The fix is proportional: ONSPEED on a system that peaks at 80% sits at $0.6 \times 0.8 = 48\%$ of the displayed lift fraction, not at 60%. OnSpeed avoids this by computing fractional lift from $(\alpha - \alpha_0) / (\alpha_\text{stall} - \alpha_0)$, where $\alpha_\text{stall}$ is the value the wizard captured at the actual stall. The OnSpeed indexer's 99% reading corresponds to the wing at the calibrated stall AOA, regardless of what any other instrument shows.

This matters when comparing OnSpeed's ONSPEED tone arrival to a Dynon (or similar) percent-lift readout side-by-side. The numbers can differ by 10–20 percentage points without either system being wrong; they're computing a fraction against different references. Trust the tone, not the cross-display arithmetic.

## L/D~MAX~ pip vs. low-tone threshold

The indexer carries two cues that look related but are independent:

- **L/D~MAX~ pip** — the small white dots on the index bar's edges. *Aerodynamic reference.* Slides smoothly as the lever moves, from the cleanest detent's L/D~MAX~ percent up to the most-deployed detent's OnSpeed-band center.
- **Low-tone threshold** — the audio "you're flying too fast for this configuration" cue, and the bottom green chevron that mirrors it. *Operational cue.* Snaps to the active detent's calibrated L/D~MAX~ body angle.

Per Vac's design rule, "L/Dmax pips are aerodynamic references; fast tone is an operational limit cue; they must remain independent." The pip slides because aerodynamically L/D~MAX~ slides toward the OnSpeed band as flaps deploy. The audio threshold snaps because it must match a specific calibrated body angle for each flap setting.

The two coincide visually only at the cleanest detent, where L/D~MAX~ *is* both the audio threshold and the aerodynamic reference. As flaps deploy, the pip slides up through the donut band while the chevron edge stays at the active detent's calibrated L/D~MAX~ percent.

For the full layered specification — every gate, every color rule, every wire field — see [Indexer Spec](../software/indexer-spec.md).

### Worked example (RV-10, 0° / 16° / 33° calibration)

| Flap | L/D~MAX~ pct (audio threshold, chevron edge) | Pip pct (visual) |
|---|---|---|
| 0° (clean) | 49 | 49 (pip and chevron edge coincide) |
| 16° | 46 | 53 (lerp position; ignores 16° detent's calibrated 46) |
| 33° (full) | 33 | 59 (geometric center of OnSpeed band) |

(Percents come from the actual firmware computation, which truncates fractions toward zero per the saturation convention.)

At full flaps, the pip sits inside the donut band on screen — the visual "you'd better be near the donut on approach" cue. The audio low tone fires at 33% (a much lower AOA), giving the pilot the same operational "you're fast" cue the audio path always provided.
