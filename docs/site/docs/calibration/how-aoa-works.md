# How OnSpeed Measures AOA

Understanding the measurement helps you produce better calibrations and troubleshoot problems.

## Two AOA Measurements

OnSpeed actually computes AOA two different ways:

### 1. Pressure-Based AOA

A differential pressure probe senses the **coefficient of pressure** ($C_p$), which varies with air angle. This raw $C_p$ is converted to an AOA value using a polynomial curve (the one the calibration wizard fits):

$$\text{AOA} = f(C_p) = a_3 C_p^3 + a_2 C_p^2 + a_1 C_p + a_0$$

This is the primary AOA measurement used for tone generation.

### 2. Derived AOA (from AHRS)

The AHRS (Attitude and Heading Reference System) provides pitch angle. Combined with flight path angle, this gives:

$$\text{DerivedAOA} = \text{SmoothedPitch} - \text{FlightPath}$$

where:

$$\text{FlightPath} = \arcsin\left(\frac{\text{VSI}}{\text{TAS}}\right)$$

This DerivedAOA measures the **fuselage-to-wind angle** — the angle between where the airplane is pointed and where it's actually going. It's not the same as wing AOA (because of wing incidence), but it's what the calibration wizard uses as the reference measurement.

## The Lift Equation Fit

The key physics insight: in steady, wings-level flight, the lift equation gives us:

$$\text{DerivedAOA} = \frac{K}{\text{IAS}^2} + \alpha_0$$

where:

- $K$ is a constant related to aircraft weight, wing area, and the lift curve slope
- $\alpha_0$ is the zero-lift fuselage angle — a negative number that represents the AOA offset between the fuselage reference and the zero-lift angle
- $\text{IAS}$ is indicated airspeed

This is a hyperbolic curve: as you slow down (IAS decreases), DerivedAOA increases. At the stall, the curve departs from this shape (lift stops increasing linearly with AOA).

The calibration wizard fits this curve to your deceleration data and extracts both $\alpha_0$ and $\alpha_\text{stall}$.

## Why alpha_0 Matters

The DerivedAOA has a **non-zero origin**. At infinite speed (if that were possible), the DerivedAOA would be $\alpha_0$ — typically a negative number around -4° to -6° depending on the aircraft.

This means you **cannot** simply express AOA setpoints as fractions of the stall AOA. A naive calculation like "on-speed = 75% of stall AOA" gives dangerously wrong answers because it ignores the $\alpha_0$ offset.

Instead, OnSpeed uses **normalized AOA** (NAOA):

$$\text{NAOA} = \frac{\text{AOA} - \alpha_0}{\alpha_\text{stall} - \alpha_0}$$

And setpoints are computed as:

$$\text{AOA}_\text{setpoint} = \text{NAOA}_\text{target} \times (\alpha_\text{stall} - \alpha_0) + \alpha_0$$

The NAOA targets come from the lift equation: an on-speed tone at $1.3 \times V_s$ corresponds to $\text{NAOA} = 1/1.3^2 = 0.592$.

## Per-Flap Calibration

Each flap setting has different:

- **$\alpha_0$** — zero-lift angle shifts with flap deflection
- **$\alpha_\text{stall}$** — stall angle changes with flaps
- **$K$** — the lift curve slope changes
- **AOA polynomial** — the $C_p$-to-AOA mapping changes

This is why you must calibrate each flap position separately.

## What the Calibration Wizard Fits

The wizard performs two fits:

1. **$C_p$ to AOA polynomial** — maps the raw pressure coefficient to an AOA angle (3rd-order polynomial)
2. **IAS to DerivedAOA hyperbola** — the $K/\text{IAS}^2 + \alpha_0$ fit that extracts $\alpha_0$, $\alpha_\text{stall}$, and the $K$ parameter

From these, it computes the six AOA setpoints (L/Dmax, OnSpeed-Fast, OnSpeed-Slow, Stall Warning, Stall, Maneuvering) using the normalized AOA fractions.
