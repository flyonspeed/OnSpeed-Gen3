# Flashing Firmware

How to flash the OnSpeed firmware to a new or existing controller board.

## Which Hardware Do I Have?

OnSpeed Gen3 has two hardware variants with different pin assignments. You must flash the correct firmware for your board.

| Variant | Description | How to identify |
|---------|-------------|-----------------|
| **V4P** | Phil's box — most common | Label on PCB, or if you received your board from Phil |
| **V4B** | Bob's box | Label on PCB, or if you received your board from Bob |

!!! tip "Not sure?"
    If you don't know which variant you have, ask the person who built your board, or check for a label on the PCB. V4P is far more common. If you flash the wrong variant, the sensors will read incorrectly — but it won't damage anything, and you can re-flash with the correct version.

## Download Firmware

Pre-built firmware binaries are available from GitHub:

1. Go to the [latest GitHub release](https://github.com/flyonspeed/OnSpeed-Gen3/releases/latest)
2. Download the files for your hardware variant:

| File | What it is | When you need it |
|------|-----------|-----------------|
| `onspeed-vX.Y.Z-v4p-firmware.bin` | Application firmware for V4P | OTA or USB flash |
| `onspeed-vX.Y.Z-v4b-firmware.bin` | Application firmware for V4B | OTA or USB flash |
| `onspeed-vX.Y.Z-bootloader.bin` | ESP32 bootloader (same for both variants) | USB flash only |
| `onspeed-vX.Y.Z-partitions.bin` | Flash partition table (same for both variants) | USB flash only |

## Which Update Method?

- **OTA (WiFi)**: Use for routine updates when the box is already running and the `OnSpeed` WiFi network appears. You only need the `firmware.bin` for your variant. See [OTA Updates](ota-update.md).
- **USB (esptool)**: Use for initial flash on a brand-new board, or to recover a bricked device that won't boot. You need all 3 files (bootloader + partitions + firmware). See below.

## Flash via USB with esptool

### Install esptool

```bash
pip install esptool
```

### Connect the Board

1. Connect a USB cable from your computer to the ESP32-S3 USB port on the OnSpeed controller
2. Identify the serial port:
    - **macOS**: `/dev/cu.usbmodem*` or `/dev/cu.SLAB_USBtoUART*`
    - **Linux**: `/dev/ttyACM0` or `/dev/ttyUSB0`
    - **Windows**: `COM3` (check Device Manager)

### Flash the Firmware

Flash all three files at the correct addresses:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 921600 \
  write_flash 0x0 onspeed-v4.15.0-bootloader.bin \
              0x8000 onspeed-v4.15.0-partitions.bin \
              0x10000 onspeed-v4.15.0-v4p-firmware.bin
```

Replace `/dev/cu.usbmodem1101` with your actual serial port. Replace `v4p` with `v4b` if you have Bob's hardware.

!!! warning "Three files at three addresses"
    The bootloader goes at `0x0`, partitions at `0x8000`, and firmware at `0x10000`. Writing firmware to address `0x0` will overwrite the bootloader and the board won't boot.

### Verify

After flashing:

1. The controller reboots automatically
2. The heartbeat LED should start blinking
3. The `OnSpeed` WiFi network should appear
4. Connect and verify the version on the web interface home page

## Flash via PlatformIO (for developers)

If you have the source code and PlatformIO installed:

```bash
cd OnSpeed-Gen3

# Flash V4P (default)
pio run -e esp32s3-v4p -t upload

# Flash V4B
pio run -e esp32s3-v4b -t upload
```

This builds the firmware from source and uploads it in one step.

## Troubleshooting Flash Issues

| Problem | Fix |
|---------|-----|
| "Failed to connect" | Check USB cable, try a different port, hold BOOT button on ESP32 while connecting |
| Wrong serial port | List available ports and try each one |
| "Timed out" | Try lower baud rate: `--baud 115200` |
| Board doesn't reboot after flash | Manually power cycle the controller |
