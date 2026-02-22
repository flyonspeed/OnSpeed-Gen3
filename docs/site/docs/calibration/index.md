# Calibration

Calibration is what makes OnSpeed work for **your specific aircraft**. Without calibration, the system doesn't know the relationship between its sensor readings and your aircraft's actual angle of attack.

## What Calibration Does

The calibration wizard has you fly a controlled deceleration from cruise to near-stall. During this sweep, the system records pressure and attitude data, then fits a mathematical curve that maps sensor readings to AOA. It also extracts the key aerodynamic parameters:

- **alpha_0** — the zero-lift angle of attack (the AOA baseline)
- **alpha_stall** — the stall angle of attack
- **AOA setpoints** — the specific AOA values where tone transitions happen

Each flap setting needs separate calibration because extending flaps changes the wing's lift characteristics.

## Calibration Pages

- **[How OnSpeed Measures AOA](how-aoa-works.md)** — The physics behind the measurement
- **[Calibration Wizard](wizard.md)** — Step-by-step wizard walkthrough
- **[Verifying Calibration](verification.md)** — How to confirm your calibration is correct
