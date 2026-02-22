# OnSpeed Gen3 — Open-Source AOA for General Aviation

**OnSpeed** is an open-source Angle of Attack (AOA) indicator that gives pilots **audio cues** to fly safely — avoiding stalls, nailing approach speed, and maximizing climb performance. No gauge to watch. Just listen.

The concept was developed by the military (first used in the F-4 Phantom in the 1960s) and has been proven to reduce loss-of-control accidents. OnSpeed brings that technology to experimental general aviation as open-source hardware and software.

---

## What Does It Do?

OnSpeed continuously measures your angle of attack and converts it to **audio tones piped through your headset**:

| What You Hear | What It Means |
|---|---|
| **Silence** | You're fast — everything is fine |
| **Slow pulsing tone** | Approaching best-glide speed — slowing down |
| **Steady tone** | You're in the "on speed" band — perfect approach speed |
| **Fast pulsing tone** | Getting slow — add power or lower the nose |
| **Continuous warning** | Stall imminent — take immediate action |

Because AOA is independent of weight and G-loading, the tones work correctly in turns, at any weight, and at any altitude. Your approach speed tone is always right.

[Learn how AOA tones work :material-arrow-right:](getting-started/how-aoa-tones-work.md)

---

## Quick Links

<div class="grid cards" markdown>

- :material-hammer-wrench: **[Hardware Installation](installation/index.md)**

    Mount the controller, run pneumatic lines, wire power and audio

- :material-connection: **[EFIS Integration](efis-integration/index.md)**

    Connect to Dynon, Garmin, MGL, or fly standalone

- :material-cog: **[Configuration](configuration/index.md)**

    First-time setup, sensor calibration, flap positions, audio settings

- :material-chart-line: **[Calibration](calibration/index.md)**

    Fly the calibration wizard and dial in your AOA curves

- :material-airplane-landing: **[Flying with OnSpeed](flying/index.md)**

    What the tones mean, approach technique, warnings

- :material-wrench: **[Troubleshooting](troubleshooting/index.md)**

    No audio? No EFIS data? WiFi problems? Start here

</div>

---

## Hardware at a Glance

The Gen3 system is built around an **ESP32-S3** microcontroller with:

- **Dual pressure sensors** — differential pitot and AOA ports (Honeywell HSC, 14-bit)
- **Static pressure sensor** — for altitude and density corrections
- **6-axis IMU** (IMU330) — accelerometer + gyroscope at 208 Hz for AHRS
- **I2S audio output** — high-quality stereo tones through your audio panel or headset
- **WiFi** — configuration, calibration, and log download via web browser
- **microSD logging** — 50 Hz flight data recording (40+ channels)
- **EFIS serial input** — Dynon, Garmin, MGL, or VectorNav for IAS, attitude, OAT

The controller fits in a small enclosure and connects to your aircraft's pitot/static system and audio panel.

---

## Getting Started

New to OnSpeed? Start here:

1. **[What is OnSpeed?](getting-started/what-is-onspeed.md)** — Understand the concept
2. **[System Overview](getting-started/system-overview.md)** — What's in the box
3. **[What You Need](getting-started/what-you-need.md)** — Prerequisites and tools
4. **[Hardware Installation](installation/index.md)** — Mount and wire it up
5. **[Configuration](configuration/first-time-setup.md)** — First-time setup checklist
6. **[Calibration](calibration/wizard.md)** — Fly the calibration wizard
7. **[Flying](flying/tone-map.md)** — Learn the tones and fly

---

## Open Source

OnSpeed Gen3 firmware is open source under GPLv3. Hardware designs are available for builders.

- **Firmware**: [github.com/flyonspeed/OnSpeed-Gen3](https://github.com/flyonspeed/OnSpeed-Gen3)
- **Website**: [flyonspeed.org](https://www.flyonspeed.org/)
- **Contact**: [team@flyonspeed.org](mailto:team@flyonspeed.org)
