# Glossary

| Term | Definition |
|------|-----------|
| **AHRS** | Attitude and Heading Reference System — fuses accelerometer and gyroscope data to estimate pitch, roll, and heading |
| **AOA** | Angle of Attack — the angle between the wing chord line and the relative wind. Determines lift and proximity to stall. |
| **alpha_0** ($\alpha_0$) | Zero-lift angle of attack — the DerivedAOA value at which the aircraft produces zero lift. Typically negative. |
| **alpha_stall** ($\alpha_\text{stall}$) | The angle of attack at which the wing stalls (maximum lift coefficient exceeded) |
| **CAS** | Calibrated Airspeed — IAS corrected for position error in the pitot/static system |
| **Cp** | Coefficient of Pressure — the ratio of AOA probe differential pressure to pitot (dynamic) pressure. Varies with angle of attack. |
| **DerivedAOA** | SmoothedPitch - FlightPath. The fuselage-to-wind angle computed from the AHRS and vertical speed. Used as the reference AOA for calibration. |
| **EFIS** | Electronic Flight Instrument System — glass panel avionics (Dynon, Garmin, MGL, etc.) |
| **EKF6** | 6-state Extended Kalman Filter — an AHRS algorithm that estimates pitch, roll, AOA, and 3 gyro biases |
| **EMA** | Exponential Moving Average — a smoothing filter that weights recent samples more heavily |
| **FlightPath** | The angle between the aircraft's velocity vector and the horizontal. Computed as arcsin(VSI/TAS). |
| **FreeRTOS** | Real-Time Operating System used by the ESP32 firmware for multitasking |
| **I2S** | Inter-IC Sound — digital audio interface protocol used for the stereo audio output |
| **IAS** | Indicated Airspeed — airspeed as measured by the pitot system, uncorrected for position error or density |
| **IMU** | Inertial Measurement Unit — a sensor package containing accelerometers and gyroscopes |
| **K parameter** | The lift sensitivity constant in the equation DerivedAOA = K/IAS² + alpha_0. Related to weight, wing area, and lift curve slope. |
| **L/Dmax** | Maximum Lift-to-Drag ratio — the AOA at which the aircraft achieves the best glide ratio. Corresponds to best-glide speed. |
| **LittleFS** | Little File System — flash-based filesystem used by the ESP32 for configuration storage |
| **Madgwick** | A complementary filter algorithm for AHRS using quaternion mathematics. The default OnSpeed AHRS. |
| **NAOA** | Normalized AOA — AOA expressed as a fraction of the usable range: (AOA - alpha_0) / (alpha_stall - alpha_0). 0.0 = zero lift, 1.0 = stall. |
| **NeoPixel** | WS2812B addressable RGB LED — used for the optional visual AOA indexer |
| **OAT** | Outside Air Temperature — measured by EFIS or DS18B20 sensor. Used for TAS and density altitude corrections. |
| **OneWire** | A serial protocol used by the DS18B20 temperature sensor |
| **OTA** | Over-The-Air — firmware updates delivered wirelessly via WiFi |
| **PPS** | Pulses Per Second — the rate at which the audio tone pulses on and off |
| **PSRAM** | Pseudo Static RAM — 8MB of additional RAM on the ESP32-S3 module |
| **SPI** | Serial Peripheral Interface — the bus protocol used for sensors and SD card |
| **TAS** | True Airspeed — IAS corrected for air density (altitude and temperature) |
| **Vfe** | Maximum Flap Extended speed — the maximum speed at which flaps may be deployed |
| **Vno** | Maximum Structural Cruising Speed — the speed above which flight should only occur in smooth air |
| **Vs** | Stall Speed — the minimum speed at which the aircraft can maintain level flight at a given weight and configuration |
| **VSI** | Vertical Speed Indicator — rate of climb or descent, typically in feet per minute |
| **WebSocket** | A protocol for real-time bidirectional communication. Used by the live view page (10 Hz updates). |
| **×Vs** | "Times stall speed" — a way to express approach speed as a multiple of stall speed (e.g., 1.3×Vs = 1.3 times the stall speed) |
