# Flight Test Procedures

Procedures for verifying calibration and exploring the flight envelope at altitude. These are not normal-ops techniques — they are deliberate test maneuvers flown to gather data and confirm the system behaves as expected before relying on it in the pattern.

The external display's [Historic G mode](../installation/external-display.md#historic-g-scrolling-g-trace) (Mode 4) and the X-Plane plugin's [Historic G mode](../xplane/indexer.md) (Mode 4) exist for this purpose — they show vertical G over the last ~60 seconds, color-coded green/yellow/red, so the post-test debrief is on screen rather than reconstructed from the SD log.

!!! warning "Altitude, area, and recovery"
    Every procedure on this page is flown at altitude (3,000 ft AGL or higher), clear of traffic, in coordinated flight, and with a recovery plan rehearsed before the maneuver. Stop on the first cue you weren't expecting.

## Wind-up turn — sustained turn rate verification

A wind-up turn is a steady-bank, steady-G turn flown to verify that the ONSPEED solid tone tracks load factor correctly across a calibrated flap setting. Vac uses Historic G (Mode 4) as the primary readout for this maneuver.

> "[The] forth page is a historic G display (this is used for flight test purposes, e.g., wind-up turn)."  
> — Vac, [Go down, speed up, stall out…](https://vansairforce.net/threads/go-down-speed-up-stall-out.228078/) (post #32)

### What a wind-up turn shows

In a turn, effective weight rises with load factor: 1.41 G at 45° bank, 2 G at 60°. The wing has to work harder to support that effective weight, so AOA at any given airspeed is higher than wings-level. ONSPEED is an AOA condition, not an airspeed, so the ONSPEED solid tone should arrive at a higher airspeed in the turn than wings-level — and the relationship is set by the load factor.

Vac's framing in [Angle of Attack and Energy Maneuverability](https://vansairforce.net/threads/angle-of-attack-and-energy-maneuverability.225345/) (post #23):

> "The takeaway, is that at 1 G, we will run out of AOA on the backside of the power curve before we run out of power, but as G load increases, we eventually run out of power before we run out of G."

The wind-up turn is where you can hear that transition. As you tighten the turn, the ONSPEED solid tone arrives, then the high-pitch slow tone arrives, then — if you keep pulling — the stall warning buzz, all at progressively higher airspeeds. The G trace on Mode 4 makes the relationship visible.

### Setup

1. **Altitude**: 3,000 ft AGL minimum, with at least 1,000 ft of recovery margin below.
2. **Area**: clear of traffic, away from terrain and clouds.
3. **Configuration**: clean (flaps 0°), trim for cruise, power set for level flight.
4. **Display**: Mode 4 (Historic G) on the external display or X-Plane indexer. Note the trace baseline at 1 G.
5. **Calibration**: a recent flaps-0 calibration meeting the wizard's [good-data criteria](../calibration/wizard.md#what-good-data-looks-like) (R² > 0.95 on both fits, setpoints sensible for the aircraft).

### Procedure

1. **Establish level flight** at the test altitude, wings level, ball centered.
2. **Roll into a 30° bank** turn, holding altitude. Note the IAS and the tone — the ONSPEED solid tone, if it arrives, should arrive at a slightly higher airspeed than the 1 G ONSPEED airspeed.
3. **Tighten progressively**: roll to 45°, then 60°, holding altitude with back pressure. Pull is steady, not snatched.
4. **Listen for the tone progression**: low-pitch pulsing → ONSPEED solid tone → high-pitch slow tone. Each transition arrives at a higher IAS as G builds.
5. **Stop on the first stall warning buzz** or the first sign of buffet, whichever comes first. Relax back pressure, roll wings level, recover to level flight.
6. **Debrief on Mode 4**: the G trace shows the maximum sustained G you reached and how steady the pull was. A jagged trace says you were pumping the stick; a smooth ramp says you were flying the maneuver clean.

### What "good" looks like

- The ONSPEED solid tone arrives at a load-factor-scaled airspeed: roughly $V_\text{REF} \times \sqrt{n}$ where $n$ is the load factor. For an aircraft with 1 G ONSPEED at 70 KIAS, that's ~83 KIAS at 1.41 G (45° bank), ~99 KIAS at 2 G (60° bank).
- The stall warning buzz fires before any aerodynamic stall cue (buffet, wing drop). On a calibrated system the buzz precedes the break.
- The trace on Mode 4 is smooth — a steady ramp up to the peak G, a steady ramp back to 1 G during recovery.

### What "bad" looks like

- ONSPEED solid tone arrives at the same IAS as wings-level. AOA is not tracking load factor; the AHRS calibration or the AOA polynomial is wrong for this flap setting.
- Stall warning buzz fires after the break, not before. Stall warning AOA is set too high — re-fly the calibration sweep, or check that the buffet onset matches the captured stall AOA.
- Trace is jagged. The pull was uneven; the data is noisy, but the system behavior may still be fine. Re-fly with a steadier pull before drawing conclusions.

## Deceleration sweep verification

The [calibration wizard](../calibration/wizard.md) captures a deceleration sweep and fits the curve. After saving the calibration, fly a second, independent sweep at the same flap setting and confirm the ONSPEED solid tone arrives at the airspeed the wizard predicted.

The sweep is the same maneuver as the calibration sweep — idle power, level altitude, smooth deceleration, stop trimming at 80–90 KIAS, recover with AOA — but you don't record it. You listen and read the airspeed indicator at each tone transition.

| Transition | Expected IAS (clean, 1 G) | Verify against |
|---|---|---|
| Silence → low-pitch pulsing | L/D~MAX~ airspeed | The wizard's "Equivalent speeds" readout |
| Low-pitch pulsing → ONSPEED solid tone | V~REF~ at current weight | ~1.3 × V~S~ |
| ONSPEED solid tone → high-pitch pulsing | Just below V~REF~ | ~1.25 × V~S~ |
| High-pitch pulsing → stall warning buzz | ~5 kt above V~S~ | Aircraft POH V~S~ |

Drift of 1–2 knots is normal — weight, density altitude, and pitot static-source error all move the absolute airspeeds. Drift of 5 knots or more says the calibration captured the wrong curve; re-fly the wizard sweep in calmer air.

## Stall warning timing

The stall warning buzz must precede the aerodynamic stall, not coincide with it or follow it. To verify, fly a 1 G stall at safe altitude with the buzzer audible:

1. **Altitude**: 3,000 ft AGL.
2. **Configuration**: the flap setting you want to verify.
3. **Power**: idle.
4. **Procedure**: maintain altitude, allow the airplane to decelerate, listen for the stall warning buzz.
5. **Note the IAS** when the buzz arrives. Continue to the aerodynamic stall (buffet, wing drop, or stick force lightening) and note that IAS too.
6. **Recover**: forward stick, add power, allow the airplane to fly off.

The buzz should arrive 5–8 KIAS above the aerodynamic stall at 1 G. A smaller margin means stall warning AOA is set too close to $\alpha_\text{stall}$; a larger margin means the warning is firing too early and will be a nuisance in the pattern.

If the margin is wrong, re-run the calibration wizard for that flap setting. The wizard sets stall warning AOA from the captured $\alpha_\text{stall}$; if $\alpha_\text{stall}$ is wrong, the warning timing is wrong.

!!! danger "Stall, not approach to stall"
    A 1 G clean-configuration stall at altitude is a normal flight test maneuver. A full-flap stall is not — the airplane may drop a wing or pitch down sharply. Verify full-flap stall warning timing only with an instructor familiar with your aircraft, and only at altitudes that allow a full unaccelerated recovery.

## After the test flight

Pull the SD log and look at the AOA, IAS, and G traces around each maneuver. The wizard's calibration is fit against a single sweep; a second sweep that matches the prediction across a 30 KIAS range is stronger evidence the calibration is correct than the wizard's own R² number.

If something looks off in the log, the most common causes are:

- **Pitot static-source error** — the airspeed at the tone transitions doesn't match the predicted IAS, but the AOA traces are consistent. Fix the static source, not the calibration.
- **Yaw during the sweep** — the AOA polynomial fit was contaminated by sideslip. Re-fly with the ball centered.
- **Bank angle creep** — the sweep wasn't truly wings-level. Re-fly, paying closer attention to attitude.

For the underlying physics — why ONSPEED is at ~60% of maximum lift, why $\alpha_0$ is non-zero, why the curve is hyperbolic — see [How OnSpeed Measures AOA](../calibration/how-aoa-works.md).
