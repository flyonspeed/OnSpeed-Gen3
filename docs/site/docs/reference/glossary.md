# Glossary

| Term | Definition |
|------|-----------|
| **AHRS** | Attitude and Heading Reference System — fuses accelerometer and gyroscope data to estimate pitch, roll, and heading |
| **AOA** | Angle of Attack — the angle between the wing chord line and the relative wind. Also referred to by the Greek letter alpha (α). Determines lift and proximity to stall. |
| **alpha_0** ($\alpha_0$) | Zero-lift angle of attack — the DerivedAOA value at which the aircraft produces zero lift. Typically negative for cambered airfoils. |
| **alpha_stall** ($\alpha_\text{stall}$) | Critical angle of attack — the AOA at which the wing stalls (maximum lift coefficient exceeded). Essentially constant for a given configuration, regardless of weight or load factor. |
| **Body Angle** | The difference between pitch attitude and flight-path angle. From a pilot's perspective, body angle corresponds to angle of attack. Equivalent to DerivedAOA. |
| **CAS** | Calibrated Airspeed — IAS corrected for position error in the pitot/static system |
| **C~L~** | Coefficient of Lift — a dimensionless parameter describing how much lift an airfoil produces relative to the surrounding airflow. Directly proportional to AOA over the normal operating range. |
| **Cp** | Coefficient of Pressure — the ratio of AOA probe differential pressure to pitot (dynamic) pressure. Varies with angle of attack. |
| **Critical AOA** | The angle of attack at which the wing stalls. Essentially constant for a given flap configuration, regardless of gross weight or load factor. An airplane can stall at any airspeed and in any attitude, but critical AOA remains the same. |
| **DerivedAOA** | SmoothedPitch - FlightPath. The fuselage-to-wind angle computed from the AHRS and vertical speed. Used as the reference AOA for calibration. |
| **Directive Information** | Information that directly calls for pilot action, requiring no interpretation. An AOA tone is directive — it tells you to push or pull. Contrast with *descriptive information*. |
| **Descriptive Information** | Information that must be perceived, interpreted, and then acted upon. A conventional flight instrument is descriptive — the pilot must see it, understand it, then respond. |
| **Effective Power** | The difference between thrust and drag, normalized by effective weight. When positive, the airplane can climb, accelerate, or sustain maneuvering. When negative, the airplane must descend or decelerate. |
| **Effective Weight** | Actual weight multiplied by load factor. In a 2G turn, a 1500 lb airplane has an effective weight of 3000 lbs. |
| **EFIS** | Electronic Flight Instrument System — glass panel avionics (Dynon, Garmin, MGL, etc.) |
| **EKF6** | 6-state Extended Kalman Filter — an AHRS algorithm that estimates pitch, roll, AOA, and 3 gyro biases |
| **EMA** | Exponential Moving Average — a smoothing filter that weights recent samples more heavily |
| **FlightPath** | The angle between the aircraft's velocity vector and the horizontal. Computed as arcsin(VSI/TAS). |
| **Fractional Lift** | How hard the wing is working relative to its maximum capability, expressed as a fraction from 0 (zero lift) to 1 (stall). Mathematically identical to NAOA. ~60% = ONSPEED, ~50% = L/D~MAX~, ~90% = stall warning. |
| **FreeRTOS** | Real-Time Operating System used by the ESP32 firmware for multitasking |
| **I2S** | Inter-IC Sound — digital audio interface protocol used for the stereo audio output |
| **IAS** | Indicated Airspeed — airspeed as measured by the pitot system, uncorrected for position error or density |
| **IMU** | Inertial Measurement Unit — a sensor package containing accelerometers and gyroscopes |
| **Instantaneous Turn** | A turn where the pilot pulls beyond ONSPEED, borrowing energy. Turn rate increases briefly but the condition is unsustainable — airspeed decays and AOA continues to increase. |
| **K parameter** | The lift sensitivity constant in the equation DerivedAOA = K/IAS² + alpha_0. Related to weight, wing area, and lift curve slope. |
| **L/D~MAX~** | Maximum Lift-to-Drag ratio — the AOA at which the aircraft achieves the best glide ratio (~50% fractional lift). Corresponds to best-glide speed and maximum range. |
| **LittleFS** | Little File System — flash-based filesystem used by the ESP32 for configuration storage |
| **Load Factor** | The ratio of lift to weight, commonly expressed in G units. Denoted by engineers as *n*. In level flight, load factor is 1G. In a 60° bank turn, approximately 2G. |
| **Madgwick** | A complementary filter algorithm for AHRS using quaternion mathematics. The default OnSpeed AHRS. |
| **NAOA** | Normalized AOA — AOA expressed as a fraction of the usable range: (AOA - alpha_0) / (alpha_stall - alpha_0). 0.0 = zero lift, 1.0 = stall. Mathematically identical to fractional lift. |
| **NeoPixel** | WS2812B addressable RGB LED — used for the optional visual AOA indexer |
| **OAT** | Outside Air Temperature — measured by EFIS or DS18B20 sensor. Used for TAS and density altitude corrections. |
| **ONSPEED** | A specific angle-of-attack condition, not an airspeed. Corresponds to ~60% fractional lift, balanced effective power, maximum sustained turn rate, V~REF~, best angle of climb, and maximum endurance. The airspeed associated with ONSPEED varies with weight, configuration, and load factor. |
| **OneWire** | A serial protocol used by the DS18B20 temperature sensor |
| **OTA** | Over-The-Air — firmware updates delivered wirelessly via WiFi |
| **PPS** | Pulses Per Second — the rate at which the audio tone pulses on and off |
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
| **WebSocket** | A protocol for real-time bidirectional communication. Used by the live view page (10 Hz updates). |
| **×V~S~** | "Times stall speed" — a way to express approach speed as a multiple of stall speed (e.g., 1.3×V~S~ = 1.3 times the stall speed) |
