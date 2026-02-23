# Flashing Firmware

How to flash the OnSpeed firmware to a new or existing controller board.

## Download Pre-Built Firmware

Pre-built firmware binaries are available from the GitHub repository:

1. Go to the [OnSpeed-Gen3 GitHub releases](https://github.com/flyonspeed/OnSpeed-Gen3/releases)
2. Download the latest `.bin` firmware file
3. If no release is available, CI builds produce artifacts on each push — check the [Actions tab](https://github.com/flyonspeed/OnSpeed-Gen3/actions)

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

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 921600 write_flash 0x0 firmware.bin
```

Replace `/dev/cu.usbmodem1101` with your actual serial port and `firmware.bin` with the downloaded filename.

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
pio run -t upload
```

This builds the firmware from source and uploads it in one step.

## Troubleshooting Flash Issues

| Problem | Fix |
|---------|-----|
| "Failed to connect" | Check USB cable, try a different port, hold BOOT button on ESP32 while connecting |
| Wrong serial port | List available ports and try each one |
| "Timed out" | Try lower baud rate: `--baud 115200` |
| Board doesn't reboot after flash | Manually power cycle the controller |
