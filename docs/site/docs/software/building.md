# Building from Source

For developers who want to modify the firmware or contribute to the project.

## PlatformIO (Recommended)

PlatformIO provides reproducible builds with pinned dependencies.

### Install

```bash
pip install platformio
```

### Build

```bash
cd OnSpeed-Gen3
pio run
```

### Build and Upload

```bash
pio run -t upload
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
├── platformio.ini                   # Build configuration
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
