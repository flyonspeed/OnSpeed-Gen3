# OnSpeed-Gen3

<div align="center">

[![Build](https://github.com/flyonspeed/OnSpeed-Gen3/actions/workflows/ci.yml/badge.svg)](https://github.com/flyonspeed/OnSpeed-Gen3/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

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

Upload to a connected device:

```bash
pio run -t upload
```

Monitor serial output (Ctrl+C to exit):

```bash
pio device monitor
```

Or build, upload, and monitor in one command:

```bash
pio run -t upload && pio device monitor
```