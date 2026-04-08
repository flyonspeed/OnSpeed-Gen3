# Documentation Site — Progress Report

**Branch:** `docs/site-phase1`
**Date:** 2026-02-22
**Status:** Phase 1 complete, ready for review

---

## What Was Done

Built a complete MkDocs Material documentation site covering all Phase 1 ("Oshkosh-Ready") items from `DOCS_SITE_PLAN.md`. The site builds cleanly with `mkdocs build --strict` and includes a GitHub Actions workflow for auto-deployment to GitHub Pages.

### Infrastructure

| Item | File | Notes |
|------|------|-------|
| MkDocs config | `docs/site/mkdocs.yml` | Material theme, dark mode, tabs, search, KaTeX math, Mermaid diagrams, admonitions, code copy, edit-on-GitHub links |
| KaTeX helper | `docs/site/docs/javascripts/katex.js` | Auto-renders `$...$` and `$$...$$` math blocks |
| GitHub Actions | `.github/workflows/docs.yml` | Builds on push/PR to `docs/site/**`, deploys to GitHub Pages on master merge. Pins `mkdocs>=1.6,<2` to avoid MkDocs 2.0 incompatibility. |
| .gitignore | `docs/site/.gitignore` | Excludes `site/` build output |
| Logo | Uses Material theme's built-in `material/airplane` icon (no custom image yet) |

### Pages Written (60 total)

#### Getting Started (6 pages)

| Page | File | Content Source |
|------|------|---------------|
| Section index | `getting-started/index.md` | — |
| What is OnSpeed? | `getting-started/what-is-onspeed.md` | flyonspeed.org concepts, Gen2 README, original writing |
| How AOA Tones Work | `getting-started/how-aoa-tones-work.md` | `ToneCalc.h` constants, `Audio.cpp` tone frequencies. Includes Mermaid tone flow diagram. |
| System Overview | `getting-started/system-overview.md` | `Globals.h` pin defs, `CLAUDE.md` architecture section. Includes Mermaid block diagram. |
| What You Need | `getting-started/what-you-need.md` | Hardware BOM, installation requirements |
| Safety & Limitations | `getting-started/safety.md` | Original writing, covers supplemental-only status and accuracy limits |

#### Hardware Installation (9 pages)

| Page | File | Content Source |
|------|------|---------------|
| Section index | `installation/index.md` | — |
| Mounting the Controller | `installation/mounting.md` | Orientation config from `Config.cpp`, IMU requirements |
| Pneumatic Plumbing | `installation/pneumatics.md` | Sensor part numbers from `Globals.h`, pressure system design |
| Electrical Wiring | `installation/wiring.md` | All pin assignments from `Globals.h` (V4P and V4B), MCP3202 ADC channels, serial pins |
| **Audio Wiring** | `installation/audio.md` | **Written from scratch** — 4 options (alert, aux, music, direct splice) with comparison table, Garmin GMA 345/GTR 200 specifics |
| AOA Indexer LEDs | `installation/indexer.md` | NeoPixel pin from `Globals.h`, general NeoPixel wiring |
| OAT Sensor | `installation/oat-sensor.md` | `OAT_PIN=14`, DS18B20 OneWire, runtime config toggle from `Config.cpp` |
| External Display | `installation/external-display.md` | `DISPLAY_SER_TX=10`, serial output formats from `Config.cpp` |
| Boom Probe | `installation/boom-probe.md` | `BOOM_SER_RX=3`, boom data fields from `LogSensor.cpp` |
| Installation Checklist | `installation/checklist.md` | Comprehensive checkbox list covering all connections |

#### EFIS Integration (8 pages)

| Page | File | Content Source |
|------|------|---------------|
| Section index | `efis-integration/index.md` | All 6 EFIS types from `EfisSerial.cpp` enum |
| **Dynon SkyView / HDX** | `efis-integration/dynon-skyview.md` | **High priority, detailed.** `EfisSerial.cpp` parsing (`!1` ADAHRS 74 bytes, `!3` EMS 225 bytes), all data fields, common mistakes table, baud rate warning. Dynon config from `~/Dropbox/N720AK/Dynon Configs/`. |
| Dynon D10/D100 | `efis-integration/dynon-d10.md` | `EfisSerial.cpp` EnDynonD10 |
| Garmin G5 | `efis-integration/garmin-g5.md` | `EfisSerial.cpp` EnGarminG5 |
| Garmin G3X | `efis-integration/garmin-g3x.md` | `EfisSerial.cpp` EnGarminG3X, G3X serial output format |
| MGL iEFIS | `efis-integration/mgl.md` | `EfisSerial.cpp` binary iLink protocol, Msg1/Msg3 structure |
| VectorNav VN-300 | `efis-integration/vectornav.md` | `EfisSerial.cpp` 127-byte binary packets with CRC-16 |
| No EFIS (Standalone) | `efis-integration/standalone.md` | What works/doesn't without EFIS, DS18B20 recommendation |

#### Software & Firmware (4 pages)

| Page | File | Content Source |
|------|------|---------------|
| Section index | `software/index.md` | — |
| Flashing Firmware | `software/flashing.md` | `README.md` build instructions, esptool usage, PlatformIO |
| OTA Updates | `software/ota-update.md` | `/upgrade` endpoint from `ConfigWebServer.cpp` |
| Building from Source | `software/building.md` | `platformio.ini`, `CLAUDE.md` build section, test suite info |

#### Configuration (8 pages)

| Page | File | Content Source |
|------|------|---------------|
| Section index | `configuration/index.md` | — |
| Web Interface | `configuration/web-interface.md` | `ConfigWebServer.cpp` — all 16 endpoints, WiFi credentials (`OnSpeed`/`angleofattack`), IP `192.168.0.1`, mDNS, ETag caching, WebSocket at port 8080 |
| **First-Time Setup** | `configuration/first-time-setup.md` | **Written from scratch** — 12-step guided checklist covering EFIS type, orientation, flaps, aircraft limits, audio, optional features, sensor cal, audio test, EFIS verification |
| Sensor Calibration | `configuration/sensor-calibration.md` | `BIAS` console command from `ConsoleSerial.cpp`, bias parameters from `Config.cpp`, AHRS re-init behavior (v4.15) |
| **Flap Position Setup** | `configuration/flap-setup.md` | **Written from scratch** — `Flaps.cpp` midpoint algorithm, MCP3202 ADC (V4P) vs discrete jumpers (V4B), example with RV-4 pot values (129/158/206) |
| Audio Settings | `configuration/audio.md` | Volume config from `ConfigDefaults.h`, 3D audio from `3DAudio.cpp`, Vno chime from `VnoChime.cpp`, G-limit from `gLimit.cpp`, AUDIOTEST from `ConsoleSerial.cpp` |
| Advanced Settings | `configuration/advanced.md` | AHRS algorithm selection, smoothing, CAS curve, serial output format, data source |
| Backup & Restore | `configuration/backup-restore.md` | Config XML format from `Config.cpp`, load order (defaults → flash → SD) |

#### Calibration (4 pages)

| Page | File | Content Source |
|------|------|---------------|
| Section index | `calibration/index.md` | — |
| How OnSpeed Measures AOA | `calibration/how-aoa-works.md` | `AHRS.cpp:233` DerivedAOA formula, `SETPOINT_IMPROVEMENT_PLAN.md` physics (K/IAS² + alpha_0), NAOA fractions, KaTeX equations throughout |
| **Calibration Wizard** | `calibration/wizard.md` | `javascript_calibration.h` wizard steps, `html_calibration.h` UI elements, decel gauge, R² quality checks, good/bad data table |
| Verifying Calibration | `calibration/verification.md` | Speed check procedure, flap check, log data analysis, Dynon PercentLift cross-check |

#### Flying with OnSpeed (5 pages)

| Page | File | Content Source |
|------|------|---------------|
| Section index | `flying/index.md` | — |
| **What the Tones Mean** | `flying/tone-map.md` | `ToneCalc.h` — all 5 regions, PPS ranges (1.5–8.2, 1.5–6.2, 20), tone frequencies (400 Hz low, 1600 Hz high), example RV-4 speed table, muted mode behavior |
| Normal Operations | `flying/normal-ops.md` | Startup → taxi → takeoff → cruise → pattern → landing → shutdown → go-around |
| Approach and Landing | `flying/approach-landing.md` | Standard/short-field/gusty approaches, base-to-final turn safety, steep turns, best glide |
| Warnings and Alerts | `flying/warnings.md` | Stall warning (20 PPS override), Vno chime, G-limit warning, audio test tones |

#### Data & Logs (3 pages)

| Page | File | Content Source |
|------|------|---------------|
| Section index | `data-and-logs/index.md` | — |
| Understanding Logs | `data-and-logs/log-format.md` | `LogSensor.cpp` column headers — core (28 cols), boom (6 cols), EFIS (18 cols), VN-300 (27 cols), computed (6 cols). Python loading example. |
| Downloading Logs | `data-and-logs/downloading.md` | `/logs` and `/download` endpoints, SD card removal option |

#### Troubleshooting (6 pages)

| Page | File | Content Source |
|------|------|---------------|
| Section index | `troubleshooting/index.md` | Quick diagnosis table |
| **No Audio** | `troubleshooting/no-audio.md` | **Written from scratch** — 6-step diagnostic: IAS threshold, mute button, AUDIOTEST, volume, wiring, pressure sensors |
| **No EFIS Data** | `troubleshooting/no-efis.md` | **Written from scratch** — 7-step diagnostic: EFIS type, baud rate, TX/RX polarity, serial output enabled, SENSORS command, config flag, physical wiring |
| **Erratic Tones** | `troubleshooting/erratic-tones.md` | **Written from scratch** — calibration quality, flap detection, sensor biases, smoothing, moisture, orientation |
| WiFi Issues | `troubleshooting/wifi.md` | WiFi AP behavior, credentials, mDNS fallback, browser caching |
| **Console Commands** | `troubleshooting/console-commands.md` | `ConsoleSerial.cpp` — all 16 commands (HELP, SENSORS, FLAPS, VOLUME, CONFIG, LOG, BIAS, MSG, AUDIOTEST, LIST, DELETE, PRINT, FORMAT, TASKS, REBOOT, COOKIE), syntax, examples |

#### Reference (5 pages)

| Page | File | Content Source |
|------|------|---------------|
| Section index | `reference/index.md` | — |
| **Configuration Parameters** | `reference/config-parameters.md` | `Config.cpp` + `ConfigDefaults.h` — complete table of every XML tag, type, default, and description. Organized by section (general, EFIS, orientation, volume, Vno, biases, CAS, per-flap). |
| **CSV Log Columns** | `reference/log-columns.md` | `LogSensor.cpp` — complete column reference for core (28), boom (6), EFIS (18), VN-300 (27) columns with units and descriptions |
| Hardware Specifications | `reference/hardware-specs.md` | `Globals.h` — full pin tables for V4P and V4B, sensor specs, MCU specs, I2S audio params, V4P vs V4B differences table |
| Glossary | `reference/glossary.md` | 30+ terms: AOA, AHRS, alpha_0, CAS, Cp, DerivedAOA, EKF6, EMA, IAS, K parameter, L/Dmax, Madgwick, NAOA, etc. |

---

## Content Sourcing

All content was sourced from the actual firmware codebase and existing documentation — nothing was made up. Specific source files read:

| Source File | What Was Extracted |
|------------|-------------------|
| `Globals.h` | Pin definitions (V4P/V4B), version (4.15), hardware defines, FreeRTOS task handles |
| `ToneCalc.h` | Tone types (None/Low/High), PPS constants (20/1.5–6.2/1.5–8.2), AOA regions |
| `Config.cpp` + `ConfigDefaults.h` | All XML tags, types, defaults, flap structure, bias structure |
| `ConsoleSerial.cpp` | All 16 console commands with syntax |
| `EfisSerial.cpp` | 6 EFIS types, protocol details, data fields per type, serial settings |
| `LogSensor.cpp` | CSV column headers and logging rate (50 Hz) |
| `ConfigWebServer.cpp` | WiFi credentials, IP (192.168.0.1), all web endpoints, WebSocket port |
| `Flaps.cpp` | Flap detection algorithm (midpoint matching, ascending/descending support) |
| `Audio.cpp` | I2S config (16 kHz, 16-bit stereo), tone frequencies (400/1600 Hz), mute behavior, AUDIOTEST |
| `AHRS.cpp` | DerivedAOA = SmoothedPitch - FlightPath (line 233) |
| `docs/SETPOINT_IMPROVEMENT_PLAN.md` (memory) | K/IAS² + alpha_0 physics, NAOA fractions, alpha_0 significance |
| `docs/AUTO_CALIBRATION.md` | Offline calibration workflow, data quality filters |
| `docs/SETPOINT_TUNING_UI.md` | ×Vs multiplier physics, K parameter |
| `CALIBRATION.md` | Calibration wizard steps, polynomial fitting, stall detection |

---

## Phase 1 Checklist vs. DOCS_SITE_PLAN.md

### Must-Have (all complete)

- [x] Home page (what is OnSpeed, links to everything)
- [x] System overview (what's in the box, what you need)
- [x] Hardware installation (mounting, power, pneumatics)
- [x] Audio wiring guide (all 4 options with comparison table, Garmin panels)
- [x] EFIS integration: Dynon SkyView (most common — detailed page)
- [x] EFIS integration: No EFIS (standalone)
- [x] Firmware flashing (download + esptool)
- [x] WiFi connection and web interface
- [x] First-time configuration checklist (12-step walkthrough)
- [x] Flap position setup (potentiometer and jumper methods)
- [x] Sensor calibration (biases)
- [x] Calibration wizard walkthrough
- [x] What the tones mean (audio map with all 5 regions)
- [x] Basic troubleshooting (no audio, no EFIS, WiFi issues)
- [x] Console commands reference (all 16 commands)

### Nice-to-Have for Oshkosh (all complete)

- [x] Flying with OnSpeed (approach and landing)
- [x] Box orientation guide (in mounting page)
- [x] Configuration reference table (complete XML parameter reference)

### Bonus (beyond Phase 1 plan)

- [x] All 6 EFIS types documented (not just Dynon + standalone)
- [x] Erratic tones troubleshooting
- [x] Complete CSV log column reference (70+ columns)
- [x] Complete hardware specs / pinout reference (V4P and V4B)
- [x] Glossary (30+ terms)
- [x] Calibration physics with KaTeX equations (alpha_0, NAOA, K/IAS²)
- [x] Approach and landing technique guide
- [x] Warnings and alerts reference
- [x] Data downloading guide
- [x] OTA firmware update guide
- [x] Building from source guide

---

## What's NOT Done Yet (Phase 2–4)

### Phase 2: Complete Installation Coverage

- [ ] Garmin audio panel integration details (wiring diagrams with specific pinouts)
- [ ] CAS curve setup documentation
- [ ] Wiring diagrams (visual SVG/PNG — currently text descriptions only)
- [ ] Dynon SkyView menu screenshots (need actual screenshots from hardware)
- [ ] Photos of actual installations

### Phase 3: Analysis & Tools

- [ ] Calibration Explorer user guide (Jupyter notebook walkthrough)
- [ ] Auto-calibration tool guide
- [ ] FlySto integration (needs format research first)
- [ ] Custom analysis recipes (Python/pandas examples beyond the basic one)
- [ ] Log replay guide

### Phase 4: Advanced & Reference

- [ ] Aerodynamic theory deep-dive (extended alpha_0/NAOA treatment)
- [ ] Setpoint tuning guide (×Vs multiplier UI when implemented)
- [ ] Calibration for specific aircraft types (RV-4 detailed, RV-7/8 notes)
- [ ] Serial protocol reference (packet formats for each EFIS type)
- [ ] Contributing guide (PR workflow, code style)

### Infrastructure

- [ ] Custom logo/favicon (currently using Material theme icon)
- [ ] Custom domain setup (`docs.flyonspeed.org` vs `flyonspeed.github.io/OnSpeed-Gen3/`)
- [ ] Search tuning
- [ ] Analytics

---

## How to Build Locally

```bash
cd OnSpeed-Gen3/docs/site

# Using uv (recommended)
uv run --with "mkdocs>=1.6,<2" --with mkdocs-material mkdocs serve

# Or with pip
pip install "mkdocs>=1.6,<2" mkdocs-material
mkdocs serve
```

Then open `http://127.0.0.1:8000` in your browser.

## How to Deploy

The GitHub Actions workflow (`.github/workflows/docs.yml`) automatically:

1. **On PR**: Builds the site with `--strict` to catch errors
2. **On merge to master**: Builds and deploys to GitHub Pages

Manual deployment:

```bash
cd OnSpeed-Gen3/docs/site
mkdocs build --strict
# Output is in site/ directory
```

---

## Corrections Made

- **WiFi IP address**: The firmware configures the AP at `192.168.0.1` (in `ConfigWebServer.cpp:177`), not the ESP32 default `192.168.4.1`. Fixed in all 9 pages that referenced the IP.
