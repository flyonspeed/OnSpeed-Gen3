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

The Arduino IDE does not use the PlatformIO variant environments. Instead, the hardware variant defaults to V4P via the guard block at the top of `HardwareMap.h`. To build for V4B, edit the guard block and change the default `HW_V4P` to `HW_V4B`.

1. Install Arduino IDE 2.x
2. Add the ESP32 board URL to **File → Preferences → Additional Boards Manager URLs**:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. Install the ESP32 board package version **3.3.5** via **Tools → Board Manager**
4. Set board to **ESP32S3 Dev Module Octal (WROOM2)**
5. Configure Tools menu:
    - PSRAM: **OPI PSRAM**
    - Flash Mode: **OPI 80 MHz**
    - Flash Size: **32MB (256 Mb)**
    - Partition Scheme: **32M Flash (4.8MB APP/22MB LittleFS)**
    - Upload Speed: **921600**
    - USB Mode: **Hardware CDC and JTAG**
    - USB CDC On Boot: **Disabled**
6. Open `software/OnSpeed-Gen3-ESP32/OnSpeed-Gen3-ESP32.ino`
7. Compile and upload

### Verifying an Arduino IDE build from the command line

Arduino IDE ships a bundled `arduino-cli` that uses the exact same compile pipeline as the GUI. This is the best way to sanity-check "does the firmware still build under Arduino IDE?" without clicking through the GUI — useful in CI or when reviewing a PR that touches includes or layout.

```bash
# The arduino-cli binary bundled inside Arduino IDE 2.x on macOS:
ARDUINO_CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"

# Set up an isolated test environment (one-time setup):
mkdir -p /tmp/arduino-test
"$ARDUINO_CLI" config init --dest-file /tmp/arduino-test/arduino-cli.yaml --overwrite
"$ARDUINO_CLI" --config-file /tmp/arduino-test/arduino-cli.yaml \
    config set board_manager.additional_urls \
    "https://espressif.github.io/arduino-esp32/package_esp32_index.json"
"$ARDUINO_CLI" --config-file /tmp/arduino-test/arduino-cli.yaml core update-index
"$ARDUINO_CLI" --config-file /tmp/arduino-test/arduino-cli.yaml core install esp32:esp32@3.3.5

# Point arduino-cli at this repo's bundled libraries (so we don't need to copy
# them into ~/Documents/Arduino/libraries/):
"$ARDUINO_CLI" --config-file /tmp/arduino-test/arduino-cli.yaml \
    config set directories.user $(pwd)/software

# Verify compile (no upload):
"$ARDUINO_CLI" --config-file /tmp/arduino-test/arduino-cli.yaml compile \
    --fqbn 'esp32:esp32:esp32s3-octal:PSRAM=opi,FlashSize=32M,FlashMode=opi,PartitionScheme=app5M_little24M_32MB,USBMode=hwcdc,CDCOnBoot=default' \
    software/OnSpeed-Gen3-ESP32
```

The FQBN options encode the Tools-menu settings from step 5 above — keep them in sync if you change any of the settings.

Expected output ends with something like:
```
Sketch uses 2176519 bytes (46%) of program storage space. Maximum is 4718592 bytes.
Global variables use 67784 bytes (20%) of dynamic memory, leaving 259896 bytes for local variables. Maximum is 327680 bytes.
```

To build for V4B, temporarily edit `HardwareMap.h`'s guard block to default to `HW_V4B`, re-run the compile, then revert.

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
│       ├── onspeed_core/            # Platform-independent algorithms
│       └── version/                 # Build version info (auto-generated + defaults)
├── test/                            # Native unit tests (12 suites, 136 tests)
└── scripts/                         # Build and analysis scripts
```

## Contributing

See the [GitHub repository](https://github.com/flyonspeed/OnSpeed-Gen3) for contribution guidelines, issue tracking, and pull request workflow.
