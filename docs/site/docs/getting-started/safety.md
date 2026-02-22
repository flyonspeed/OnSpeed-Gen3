# Safety & Limitations

!!! danger "OnSpeed is a supplemental awareness tool"
    OnSpeed is **not a certified flight instrument**. It does not replace your airspeed indicator, stall warning horn, or any other aircraft system. It is a supplemental audio cue to improve situational awareness.

## Limitations

### Experimental Aircraft Only

OnSpeed is designed for **experimental/amateur-built aircraft** operating under FAA Part 91. It is not approved for installation in type-certificated aircraft.

### AOA Accuracy Limits

- **Very low airspeeds** (below ~25 knots IAS) — pressure-based AOA and IAS readings become unreliable. OnSpeed mutes audio below a configurable IAS threshold for this reason.
- **Sideslip** — large sideslip angles reduce AOA measurement accuracy. The AOA probe measures the air angle in the pitch plane; significant yaw introduces errors.
- **Turbulence** — in rough air, AOA readings fluctuate. The system applies smoothing, but strong turbulence will cause tone variations. Use the tone as trend information, not a moment-by-moment target.
- **Icing** — if the AOA probe or pitot port accumulates ice, readings will be incorrect. OnSpeed has no ice detection capability.

### Calibration Dependent

The accuracy of OnSpeed's tones depends entirely on the quality of your calibration. A poorly calibrated system may give misleading cues. Always verify calibration by comparing OnSpeed tone transitions against known approach speeds for your aircraft.

See [Verifying Calibration](../calibration/verification.md) for how to check.

### Not a Stall Prevention System

OnSpeed provides **awareness**, not prevention. It tells you how hard the wing is working and how that work is changing — it cannot prevent a stall. Proper pilot technique is always required.

### "Beating the Flight Controls"

No cueing system can overcome a "high-gain" pilot — one who moves the flight controls so rapidly that aerodynamic or load-factor limits are exceeded before corrective feedback can take effect. Even a well-calibrated AOA system cannot protect against abrupt, disproportionate control inputs. Effective flight control inputs must always be proportional to need: small corrections should be met with small inputs, and even large corrections should be applied smoothly.

In a typical light airplane, a sustained maneuvering input on the order of 2G per second represents the upper practical limit of controllability. In the traffic pattern, a one-second 2G input can be enough to eliminate aerodynamic margin entirely.

## Safe Use Guidelines

1. **Learn the tones on the ground first** — use the audio test function and log replay to become familiar with the tone progression before relying on it in flight.

2. **Verify against known speeds** — after calibration, confirm that the ONSPEED solid tone corresponds to your known approach speed for each flap configuration.

3. **Don't chase the tone in turbulence** — in rough air, the tones will fluctuate. Fly attitude and power settings; use the tone as trend information, not a moment-by-moment target.

4. **Keep your stall warning horn functional** — OnSpeed supplements but does not replace your aircraft's primary stall warning system.

5. **Recalibrate after modifications** — any change that affects your aircraft's aerodynamics (new prop, fairings, weight changes) may require recalibration.

6. **Monitor for sensor issues** — if tones seem wrong in flight (sounding at speeds where they shouldn't, or missing when they should be present), land and troubleshoot. See [Troubleshooting](../troubleshooting/index.md).

7. **Unload for control** — when you hear the stall warning, the correct response is always to reduce AOA (ease forward pressure). An airplane cannot stall at zero G. Reducing load factor is the most effective way to regain aerodynamic margin.
