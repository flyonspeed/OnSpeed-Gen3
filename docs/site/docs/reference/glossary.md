# Glossary

| Term | Definition |
|------|-----------|
| **AHRS** | Attitude and Heading Reference System — fuses accelerometer and gyroscope data to estimate pitch, roll, and heading |
| **AOA** | Angle of Attack — the angle between the wing chord line and the relative wind. Also referred to by the Greek letter alpha (α). Determines lift and proximity to stall. **In OnSpeed firmware and config, the variable named `AOA` is body angle (fuselage-to-wind), not wing AOA — see Body Angle below.** |
| **Attitude (display mode)** | Indexer page (Mode 1) showing a synthetic horizon driven by OnSpeed's AHRS — pitch ladder, roll, magenta flight-path marker, slip ball, VSI tape, and IAS / PALT / G / percent-lift readouts. Used as a backup AI cross-check or for verifying IMU operation. Distinct from the *AHRS algorithm* itself. |
| **alpha_0** ($\alpha_0$) | Zero-lift body angle — the body angle (DerivedAOA) at which the wing produces zero lift. Typically **negative** because most aircraft have wing incidence: the fuselage points nose-down when the wing is at zero AOA. Varies with flap setting. |
| **alpha_stall** ($\alpha_\text{stall}$) | Critical body angle — the body angle at which the wing stalls (maximum lift coefficient exceeded). Essentially constant for a given configuration, regardless of weight or load factor. |
| **Body Angle** | The difference between pitch attitude and flight-path angle ($\text{Pitch} - \text{FlightPath}$). The angle the fuselage makes with the relative wind. **What OnSpeed actually measures and calibrates against** — wing AOA is a different quantity that OnSpeed never computes directly. The two are linearly related (body angle = wing AOA + wing incidence + small terms), so body angle is a faithful proxy for the pilot. Equivalent to DerivedAOA. |
| **CAS** | Calibrated Airspeed — IAS corrected for position error in the pitot/static system |
| **C~L~** | Coefficient of Lift — a dimensionless parameter describing how much lift an airfoil produces relative to the surrounding airflow. Directly proportional to AOA over the normal operating range. |
| **Cp** | Coefficient of Pressure — the ratio of AOA probe differential pressure to pitot (dynamic) pressure. Varies with angle of attack. |
| **Critical AOA** | The angle of attack at which the wing stalls. Essentially constant for a given flap configuration, regardless of gross weight or load factor. An airplane can stall at any airspeed and in any attitude, but critical AOA remains the same. |
| **Decel Display** | Indexer page showing instantaneous airspeed deceleration in knots per second on a vertical "energy tape" gauge. Mode 3 of five. Used for tuning approach energy and gauging flare authority. |
| **DerivedAOA** | SmoothedPitch - FlightPath. The fuselage-to-wind angle computed from the AHRS and vertical speed. Used as the reference AOA for calibration. |
| **Directive Information** | Information that directly calls for pilot action, requiring no interpretation. An AOA tone is directive — it tells you to push or pull. Contrast with *descriptive information*. |
| **Descriptive Information** | Information that must be perceived, interpreted, and then acted upon. A conventional flight instrument is descriptive — the pilot must see it, understand it, then respond. |
| **Effective Power** | The difference between thrust and drag, normalized by effective weight. When positive, the airplane can climb, accelerate, or sustain maneuvering. When negative, the airplane must descend or decelerate. |
| **Effective Weight** | Actual weight multiplied by load factor. In a 2G turn, a 1500 lb airplane has an effective weight of 3000 lbs. |
| **EFIS** | Electronic Flight Instrument System — glass panel avionics (Dynon, Garmin, MGL, etc.) |
| **EKF6** | 6-state Extended Kalman Filter — an AHRS algorithm that estimates pitch, roll, AOA, and 3 gyro biases |
| **EMA** | Exponential Moving Average — a smoothing filter that weights recent samples more heavily |
| **Energy Display** | The default (Mode 0) indexer page. Combines the AOA indexer widget with IAS, vertical-G, flap position, slip ball, percent-lift number, and a G-onset rate tape. The page Vac calls the "energy management" view; what older code labelled "Primary" or "AOA + Numbers." |
| **FlightPath** | The angle between the aircraft's velocity vector and the horizontal. Computed as arcsin(VSI/TAS). |
| **Fractional Lift** | How hard the wing is working relative to its maximum capability, expressed as a fraction from 0 (zero lift) to 1 (stall). Mathematically identical to NAOA. The percent at which each tone region transitions varies per flap by calibration — there are no fixed-percent band edges. |
| **FreeRTOS** | Real-Time Operating System used by the ESP32 firmware for multitasking |
| **Historic G** | Indexer page showing a 60-second scrolling vertical-G strip chart. Mode 4 of five. Used during flight test to review G-load excursions after the fact (e.g., wind-up turns). |
| **I2S** | Inter-IC Sound — digital audio interface protocol used for the stereo audio output |
| **IAS** | Indicated Airspeed — airspeed as measured by the pitot system, uncorrected for position error or density |
| **IMU** | Inertial Measurement Unit — a sensor package containing accelerometers and gyroscopes |
| **Indexer** | Three meanings, all in active use: (1) the chevron-and-donut **AOA-display widget** at the center of every indexer page (military terminology — what Vac calls the "AOA indexer"); (2) the **page collection** as a whole (the `/indexer` web route, the M5 firmware); (3) **Mode 2** specifically — the AOA-only page that shows the indexer widget without surrounding numeric readouts. Context disambiguates. |
| **Instantaneous Turn** | A turn where the pilot pulls beyond ONSPEED, borrowing energy. Turn rate increases briefly but the condition is unsustainable — airspeed decays and AOA continues to increase. |
| **K parameter** | The lift sensitivity constant in the equation DerivedAOA = K/IAS² + alpha_0. Related to weight, wing area, and lift curve slope. |
| **L/D~MAX~** | Maximum Lift-to-Drag ratio — the AOA at which the aircraft achieves the best glide ratio. Where this lands on the fractional-lift scale varies per flap by calibration; the audio cue (start of fast tone) is what the pilot follows, not a fixed percent. Corresponds to best-glide speed and maximum range. |
| **L/D~MAX~ pip** | The two small white dots on the indexer's index-bar edges. *Aerodynamic reference cue.* Slides smoothly from the cleanest detent's L/D~MAX~ percent to the most-deployed detent's OnSpeed-band center as the lever moves; intermediate detents are intentionally ignored. The pip lines up with the bottom chevron's edge only at the cleanest detent — at deployed flap settings the pip sits inside the donut while the audio threshold (chevron edge) stays at the active detent's calibrated L/D~MAX~ percent. Per Vac's design rule (`ld_max.pdf` §8): "L/Dmax pips are aerodynamic references." See [Indexer Spec](indexer-spec.md). |
| **Lateral G** | Sideways acceleration on the airframe in the body's left-right axis. Positive = airframe accelerating rightward (e.g., right-hand turn coordinated). Carried on the display-serial wire, the WebSocket JSON, and the SD log's `imuLateralG` column under the same body-frame sign convention. The slip-skid ball moves in the opposite direction, like every slip-skid ball ever made. |
| **LittleFS** | Little File System — flash-based filesystem used by the ESP32 for configuration storage |
| **Low-tone threshold** | The body angle (or its percent-lift representation) at which the audio low tone begins playing — also the lower edge of the bottom green chevron on the indexer. *Operational cue.* Snaps to the active detent's calibrated L/D~MAX~ body angle at every detent transition, in lockstep with the audio path. The chevron and the audio fire from the same source of truth; if a tone is on, the chevron is green. Per Vac's design rule (`ld_max.pdf` §8): "Fast tone is an operational limit cue." See [Indexer Spec](indexer-spec.md). |
| **Load Factor** | The ratio of lift to weight, commonly expressed in G units. Denoted by engineers as *n*. In level flight, load factor is 1G. In a 60° bank turn, approximately 2G. |
| **Madgwick** | A complementary filter algorithm for AHRS using quaternion mathematics. The default OnSpeed AHRS. |
| **NAOA** | Normalized AOA — AOA expressed as a fraction of the usable range: (AOA - alpha_0) / (alpha_stall - alpha_0). 0.0 = zero lift, 1.0 = stall. Mathematically identical to fractional lift. |
| **OAT** | Outside Air Temperature — measured by EFIS or DS18B20 sensor. Used for TAS and density altitude corrections. |
| **ONSPEED** | A specific angle-of-attack condition, not an airspeed. Corresponds to ~60% fractional lift, balanced effective power, maximum sustained turn rate, V~REF~, best angle of climb, and maximum endurance. The airspeed associated with ONSPEED varies with weight, configuration, and load factor. |
| **OneWire** | A serial protocol used by the DS18B20 temperature sensor |
| **OTA** | Over-The-Air — firmware updates delivered wirelessly via WiFi |
| **PPS** | Pulses Per Second — the rate at which the audio tone pulses on and off |
| **Pressure Altitude (PALT, `Palt`, `PAlt`)** | Altitude derived from the static pressure sensor against the **ISA standard atmosphere** (1013.25 hPa / 29.92 inHg sea-level reference). **Every altitude OnSpeed reports — on the M5 display, the live view, the SD log columns (`Palt`, `Altitude`), and the display-serial wire — is pressure altitude.** No Kollsman / QNH / baro-set correction is applied, so the value matches the panel altimeter only when the altimeter is set to 29.92. The CSV's `Palt` column is the raw 50 Hz reading; the `Altitude` column is the same quantity smoothed by a 3-state Kalman filter. |
| **PSRAM** | Pseudo Static RAM — 8MB of additional RAM on the ESP32-S3 module |
| **SPI** | Serial Peripheral Interface — the bus protocol used for sensors and SD card |
| **Sustained Turn** | A turn flown at ONSPEED where available power balances drag. AOA and airspeed are stable, and the turn can be maintained without energy loss. Maximum sustained turn rate occurs at ONSPEED. |
| **TAS** | True Airspeed — IAS corrected for air density (altitude and temperature) |
| **Unload for Control** | The fundamental principle for maintaining positive aircraft control near the aerodynamic limits. Reduce AOA and load factor to regain aerodynamic margin. An airplane cannot stall at zero G. |
| **V~A~** | Maneuvering Speed — the speed below which the airplane will reach the aerodynamic limit (stall) before exceeding the structural G-limit. Changes with weight. |
| **Velocity Vector** | Where the airplane is going and how fast. Not necessarily aligned with where the airplane is pointing. AOA governs wing workload but does not determine flight path. |
| **V~FE~** | Maximum Flap Extended speed — the maximum speed at which flaps may be deployed |
| **V~NO~** | Maximum Structural Cruising Speed — the speed above which flight should only occur in smooth air |
| **V~REF~** | Reference approach speed — typically 1.3×V~S~ at maximum gross weight. Corresponds to the ONSPEED AOA condition. |
| **V~S~** | Stall Speed — the minimum speed at which the aircraft can maintain level flight at a given weight and configuration. Varies with the square root of load factor. |
| **VSI** | Vertical Speed Indicator — rate of climb or descent, typically in feet per minute |
| **V--n Diagram** | A plot of load factor (n) vs. airspeed (V) showing the airplane's flight envelope. The curved left boundary represents the aerodynamic limit (stall). |
| **WebSocket** | A protocol for real-time bidirectional communication. Used by the live view page (20 Hz updates). |
| **×V~S~** | "Times stall speed" — a way to express approach speed as a multiple of stall speed (e.g., 1.3×V~S~ = 1.3 times the stall speed) |
