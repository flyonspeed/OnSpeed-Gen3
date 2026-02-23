# How OnSpeed Measures AOA

Understanding the measurement helps you produce better calibrations and troubleshoot problems.

## Angle of Attack and Fractional Lift

Every wing has an angle of attack at which it produces no lift (the **zero-lift angle**, $\alpha_0$). As AOA increases from this point, lift increases until the wing reaches its maximum at the **critical angle of attack** ($\alpha_\text{stall}$), just prior to stall.

Angle of attack can be thought of as a scale from zero to one. Zero represents no lift. One represents maximum lift. **Fractional lift** (sometimes called "percent lift") is where the wing is operating on that scale at any given moment:

$$\text{Fractional Lift} = \frac{\alpha - \alpha_0}{\alpha_\text{stall} - \alpha_0}$$

This is the same quantity that OnSpeed calls **Normalized AOA (NAOA)**. Key reference points:

| Condition | Fractional Lift | What It Means |
|-----------|----------------|---------------|
| Zero lift | 0% | No lift being generated |
| Maneuvering speed (V~A~, 3.8G limit) | ~26% | Below this, full deflection causes stall before exceeding G-limit |
| L/D~MAX~ | ~50% | Best glide, maximum range |
| **ONSPEED** | **~60%** | Balanced effective power, V~REF~, max sustained turn |
| Stall warning | ~90% | Near aerodynamic limit |
| Stall | 100% | Maximum lift — critical AOA |

For most straight-wing GA airplanes, critical AOA is approximately 15–20°. Typical ONSPEED approach conditions correspond to roughly 60% of the wing's maximum lift capability. This relationship is remarkably consistent across different airplane types because it's tied to basic aerodynamic behavior, not to a specific airspeed.

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
