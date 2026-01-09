# OnSpeed-Gen3

<div align="center">

[![Build](https://github.com/flyonspeed/OnSpeed-Gen3/actions/workflows/ci.yml/badge.svg)](https://github.com/flyonspeed/OnSpeed-Gen3/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Codecov](https://img.shields.io/codecov/c/github/flyonspeed/OnSpeed-Gen3/master.svg?maxAge=3600)](https://codecov.io/gh/flyonspeed/OnSpeed-Gen3)

</div>

Gen3 - OnSpeed open source hardware and software

This is a device that provides simple and intuitive Onspeed tone cues that allow the pilot to 
hear key performance AOA's as well outstanding progressive stall warning. ONSPEED AOA is 
used for approach and landing, assists with maximum performance takeoff and provides optimum 
turn performance.  The aural logic simplifies energy management and helps the pilot maintain 
positive aircraft control.

Gen3 builds on and extends the capabilities of Gen2 with new hardware and software. The 
current Gen3 device is avionics independent. The current features include:

- Measures differential pressures from an AOA pitot tube
- Measures static pressure
- Receives, parses and logs several types of EFIS data for testing purposes
- Receives, parses and logs flight test boom data for testing purposes
- Calculates a differential pressure based calibrated Angle of Attack and uses it to generate the OnSpeed tones
- Can be wired directly into the aircraft's audio panel
- Logs all data at 50Hz to the onboard SD card and provides an experimental AOA display via its Wifi interface
- Logs can also be downloaded via Wifi

We are happy to collaborate and answer any questions.  We will work with any experimenter 
or manufacturer that is interested in adapting the logic and always welcome qualified 
volunteers that want to join the project. Email us at team@flyonspeed.org to get in touch.

More details and videos of the OnSpeed system at http://flyOnspeed.org

## Software Development

FlyOnSpeed (FOS) Gen 3 software is targeted for the ESP32-S3-WROOM-2 system on a chip made by
Espressif. Different FOS platforms may have slightly different versions of this device
but for use in FOS all should be compatible.

The ESP32-S3-WROOM-2 comes with varying amounts of Flash RAM and external PSRAM accessed via
a dedicated SPI interface. All FOS hardware use the 32 MB Flash variant. Currently PSRAM is
not used and so the size of PSRAM available is not significant.

### Pre-built Firmware

Each commit to `master` builds firmware automatically via GitHub Actions. To flash without building:

1. Go to the [Actions tab](../../actions) and click the latest successful workflow run
2. Download the `firmware` artifact (contains `.bin` files)
3. Flash using [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32/):
   ```bash
   pip install esptool
   esptool.py --chip esp32s3 write_flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
   ```
   Or use the [ESP Web Flasher](https://esp.huhn.me/) for a browser-based option.

**Versioning:** The firmware version is derived from git tags at build time. Tag a release with `git tag v4.0.1` and the version string is automatically embedded in the firmware. Between tags, the version includes the commit hash (e.g., `v4.0.0-5-gabc1234` means 5 commits after v4.0.0).

### Building with PlatformIO

Install PlatformIO and build:

```bash
pip install platformio
pio run
```

Upload to a connected device (auto-detect port):

```bash
pio run -t upload
```

If auto-detection fails, find your port and specify it explicitly:

```bash
# List available ports
ls /dev/cu.usb*

# Upload to specific port
pio run -t upload --upload-port /dev/cu.usbserial-110
```

Monitor serial output (Ctrl+C to exit):

```bash
pio device monitor --port /dev/cu.usbserial-110 --baud 921600
```

Or build, upload, and monitor in one command:

```bash
pio run -t upload --upload-port /dev/cu.usbserial-110 && pio device monitor --port /dev/cu.usbserial-110 --baud 921600
```

### Building with Arduino IDE

#### Setup

1. Install ESP32 board support:
   - Arduino IDE → Settings → Additional boards manager URLs, add:
     `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   - Tools → Board → Boards Manager → search "esp32"
   - Install "esp32 by Espressif Systems" version 3.3.5

2. Set Sketchbook location to the `software` folder in this repository:
   - Arduino IDE → Settings → Sketchbook location: `/path/to/OnSpeed-Gen3/software`
   - This allows Arduino IDE to find the libraries in `software/Libraries/`

3. Initialize git submodules (if not already done):
   ```bash
   git submodule update --init --recursive
   ```

#### Build Settings

| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module Octal (WROOM2) |
| USB CDC On Boot | Disabled |
| CPU Frequency | 240MHz (WiFi) |
| Core Debug Level | None |
| USB DFU On Boot | Disabled |
| Erase All Flash Before Sketch Upload | Disabled |
| Events Run On | Core 0 |
| Flash Mode | OPI 80MHz |
| Flash Size | 32MB (256Mb) |
| JTAG Adapter | Disabled |
| Arduino Runs On | Core 1 |
| USB Firmware MSC On Boot | Disabled |
| Partition Scheme | 32M Flash (4.8MB APP/22MB LittleFS) |
| PSRAM | OPI PSRAM |
| Upload Mode | UART0 / Hardware CDC |
| Upload Speed | 921600 |
| USB Mode | Hardware CDC and JTAG |

### Libraries

Libraries are provided via git submodules in `software/Libraries/`:

- Adafruit_NeoPixel
- Arduino-Temperature-Control-Library (DallasTemperature)
- ArduinoJson
- BasicLinearAlgebra
- EspSoftwareSerial
- ghostl (required by EspSoftwareSerial 8.2.0)
- OneButton
- OneWire
- RunningAverage
- RunningMedian
- SdFat
- arduinoWebSockets
- csv-parser (vendored)
- tinyxml2 (vendored)