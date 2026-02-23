# Building from Source

For developers who want to modify the firmware or contribute to the project.

## PlatformIO (Recommended)

PlatformIO provides reproducible builds with pinned dependencies.

### Install

```bash
pip install platformio
```

### Build

The firmware has two hardware variant environments:

- **`esp32s3-v4p`** — V4P (Phil's box, most common)
- **`esp32s3-v4b`** — V4B (Bob's box)

```bash
cd OnSpeed-Gen3

# Build V4P firmware (default when no -e is specified)
pio run

# Build a specific variant
pio run -e esp32s3-v4p
pio run -e esp32s3-v4b

# Build both variants
pio run -e esp32s3-v4p -e esp32s3-v4b
```

The variant environments differ only in the hardware define (`-DHW_V4P` vs `-DHW_V4B`), which controls pin assignments for pressure sensor chip selects, SD card SPI, and the external ADC.

### Build and Upload

```bash
pio run -e esp32s3-v4p -t upload
```

### Serial Monitor

```bash
pio device monitor
```

### Run Unit Tests

Tests run on your development machine (no ESP32 needed):

```bash
pio test -e native
pio test -e native -v  # verbose output
```

## Arduino IDE

The Arduino IDE does not use the PlatformIO variant environments. Instead, the hardware variant defaults to V4P in `Globals.h`. To build for V4B, edit `Globals.h` and change the default inside the `#if !defined(HW_V4B) && !defined(HW_V4P)` block.

1. Install Arduino IDE 2.x
2. Add the ESP32 board URL to **File → Preferences → Additional Boards Manager URLs**
3. Install the ESP32 board package via **Tools → Board Manager**
4. Set board to **ESP32S3 Dev Module Octal (WROOM2)**
5. Configure Tools menu:
    - Flash Mode: **OPI 80 MHz**
    - Flash Size: **32MB (256 Mb)**
    - Partition Scheme: **32M Flash (4.8MB APP/22MB LittleFS)**
    - Upload Speed: **921600**
6. Open `software/OnSpeed-Gen3-ESP32/OnSpeed-Gen3-ESP32.ino`
7. Compile and upload

## Build Notes

- **Target**: ESP32-S3-WROOM-2 (32MB Flash, 8MB PSRAM)
- **Platform**: pioarduino 55.03.35 (Arduino Core 3.3.5)
- **Zero-warning policy**: The build enforces `-Werror` on project code. Any warnings will fail the build.
- **Build versioning**: `scripts/generate_buildinfo.py` runs as a pre-build hook, extracting version from git tags into `BuildInfo::version`, `BuildInfo::gitShortSha`, etc.

## Project Structure

```
OnSpeed-Gen3/
├── platformio.ini                   # Build configuration (V4P + V4B environments)
├── software/
│   ├── OnSpeed-Gen3-ESP32/          # Main firmware source
│   └── Libraries/
│       └── onspeed_core/            # Platform-independent algorithms
├── test/                            # Native unit tests (10 suites, 114 tests)
├── scripts/                         # Build and analysis scripts
└── lib/version/                     # Auto-generated build info
```

## Contributing

See the [GitHub repository](https://github.com/flyonspeed/OnSpeed-Gen3) for contribution guidelines, issue tracking, and pull request workflow.
