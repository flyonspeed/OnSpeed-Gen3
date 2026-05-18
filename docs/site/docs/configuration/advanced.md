# Advanced Settings

These settings are for fine-tuning. The defaults work well for most installations.

## AHRS Algorithm

OnSpeed supports two attitude estimation algorithms:

| Algorithm | Setting | Characteristics |
|-----------|---------|-----------------|
| **Madgwick** | `0` (default) | Complementary filter, quaternion-based. Well-proven, low CPU cost. |
| **EKF6** | `1` | 6-state Extended Kalman Filter that estimates pitch, roll, AOA, and three gyro biases. See the note below before selecting. |

!!! warning "EKF6 still flight-unverified"
    Issue [#128](https://github.com/flyonspeed/OnSpeed-Gen3/issues/128) tracks the EKF6 divergence root-cause analysis. v4.21 lands the gyro-bias convergence fix ([PR #319](https://github.com/flyonspeed/OnSpeed-Gen3/pull/319), `q_bias` raised so a real 0.3 dps bias converges in ~30 s instead of ~290 s), the dt-scaled process-noise injection ([PR #318](https://github.com/flyonspeed/OnSpeed-Gen3/pull/318)), the `+1g` reaction-force convention realignment ([PR #312](https://github.com/flyonspeed/OnSpeed-Gen3/pull/312)), and the EKF6-adapter sign fixes that previously sent a static 10° tilt to ~170° ([PR #310](https://github.com/flyonspeed/OnSpeed-Gen3/pull/310)). The remaining concerns flagged by issue #128 — the alpha-measurement tautology and Euler-angle singularity — are not addressed in v4.21. Leave `AHRS_ALGORITHM` at `0` (Madgwick) for routine flight; pilots experimenting with EKF6 should fly a verification approach against existing calibration before relying on the tones.

!!! note "No recalibration when switching"
    Both algorithms compute Derived AOA slightly differently, but switching between them does not require recalibration. Your existing calibration curves work with either algorithm.

## AOA Smoothing

Controls the Exponential Moving Average (EMA) filter applied to the AOA signal.

- **Default**: 20 (samples)
- **Lower values**: More responsive but noisier — the tone will fluctuate more in turbulence
- **Higher values**: Smoother but slower response — the tone may lag behind rapid AOA changes

For most aircraft, the default of 20 provides a good balance.

### Adaptive AOA Filter (experimental)

OnSpeed v4.24 and later expose an opt-in adaptive smoother (issue
[#566](https://github.com/flyonspeed/OnSpeed-Gen3/issues/566)) that
widens its effective alpha when per-frame AOA change is large
(pull-ups, aerobatic maneuvers) and tightens on steady cruise. The
goal: track fast AOA changes without flutter at hold.

When the **Enable adaptive filter** checkbox under "Adaptive AOA
Filter" is on, the legacy AOA Smoothing field is ignored. Three
tunables:

- **α<sub>min</sub>** (default 0.05) — steady-state alpha. Smaller =
  smoother at hold. Matches the legacy default at 1/20.
- **α<sub>max</sub>** (default 0.60) — responsive alpha. Larger =
  faster response on transients. The filter clamps to this ceiling.
- **k** (default 0.30/deg) — rate boost. Effective alpha is
  `clamp(α_min + k * |Δ_AOA|, α_min, α_max)`. Higher k means α<sub>max</sub>
  engages sooner.

A reboot is required after changing any of these values for the
filter to pick them up. The legacy fixed-alpha mode is the default
so existing pilots see no behavior change on upgrade.

## Pressure Smoothing

Controls EMA filtering on the pitot, AOA, and static pressure readings.

- **Default**: 15 (samples)
- **Lower values**: Faster response, more noise
- **Higher values**: Smoother, more lag

## CAS Curve (Calibrated Airspeed)

If your aircraft has a known airspeed calibration curve (correcting IAS to CAS), you can enter polynomial coefficients here:

- **Type**: `1` (polynomial)
- **Coefficients**: X3, X2, X1, X0 (cubic polynomial)
- **Enabled**: `false` by default

Most aircraft don't need this — the default is a pass-through (X1=1.0, all others=0.0).

Only enable this if you have a measured position error correction curve for your pitot/static system. An incorrect CAS curve is worse than no curve at all.

## Serial Output Format

Controls the format of data sent on the display serial output (GPIO 10):

- **ONSPEED** — OnSpeed native format
- **G3X** — Garmin G3X-compatible format

Use `G3X` if you're feeding data to a Garmin display. Otherwise, leave as `ONSPEED`.

## Data Source

Controls where the AOA data comes from:

| Source | Use |
|--------|-----|
| **SENSORS** | Normal operation — live sensor data |
| **TESTPOT** | Test mode — AOA from a test potentiometer |
| **RANGESWEEP** | Test mode — automatic AOA sweep 0–20° |
| **REPLAYLOGFILE** | Replay a recorded log file through the system |

For normal flight operations, this should always be set to **SENSORS**.

## Calibration Source

Controls which IAS source the calibration wizard uses:

- **ONSPEED** — use OnSpeed's own pitot/IAS
- **EFIS** — use the EFIS-provided IAS

If you have an EFIS connected with accurate IAS, using `EFIS` may give better calibration results.
