# Console Commands Reference

OnSpeed provides a serial console for diagnostics, configuration, and testing. Connect via USB at **921600 baud, 8N1**.

## Connecting

1. Connect a USB cable from your computer to the OnSpeed controller
2. Open a serial terminal (PlatformIO monitor, Arduino Serial Monitor, PuTTY, screen, etc.)
3. Set baud rate to **921600**
4. Type `HELP` to see available commands

All commands are **case-insensitive**.

## Command Reference

### HELP

Display all available commands.

```
HELP
```

### SENSORS

Display current sensor readings — the most useful diagnostic command.

```
SENSORS
```

Shows:

- Accelerometer readings (Forward/Lateral/Vertical G)
- Pitch and roll angles
- Pressure readings (Pfwd, P45, PStatic — raw counts and PSI)
- IAS, pressure altitude (`Palt`)
- EFIS data (if connected)
- OAT

### FLAPS

Show current flap position detection.

```
FLAPS
> Flap position: 0 degrees (raw value: 131, index: 0)
```

Shows the detected flap position, the raw ADC value, and the index into the flap configuration array. Useful for:

- Verifying flap detection is correct
- Recording pot values during [Flap Position Setup](../configuration/flap-setup.md)

### VOLUME

Show current volume setting.

```
VOLUME
```

Displays the raw ADC reading from the volume potentiometer and the mapped volume percentage.

### CONFIG

Dump the entire current configuration in XML format.

```
CONFIG
```

Outputs the complete configuration including all flap positions, setpoints, biases, and settings. Useful for:

- Verifying settings without using the web interface
- Copying configuration for backup
- Comparing against a known-good config

### LOG

Enable or disable SD card data logging.

```
LOG ENABLE    # Start logging
LOG DISABLE   # Stop logging
LOG           # Show current logging state
```

### BIAS

Calibrate pressure sensor and IMU biases.

```
BIAS
```

Collects 1000 samples of Pfwd and P45 pressure to establish zero-point readings. Also zeros the accelerometer and gyro biases. The aircraft must be **level and stationary in zero-wind conditions**.

After completion, new bias values are displayed and saved to config.

### MSG

View or set debug message levels by module.

```
MSG *                # Show all modules and their current levels
MSG AHRS debug       # Set AHRS module to debug level
MSG SensorIO warning # Set SensorIO to warning level
MSG Audio off        # Disable Audio module logging
```

Levels: `debug`, `warning`, `error`, `off`

Available modules include: AHRS, Audio, Config, Console, DataServer, Display, EFIS, Flaps, GLimit, HeartBeat, IMU, LogReplay, LogSensor, Pressure, SdFileSys, SensorIO, Switch, Volume, VnoChime, Web.

### AUDIOTEST

Run an audio test sequence.

```
AUDIOTEST
```

Plays the left/right speaker voice clips, the ONSPEED solid low tone, the G-limit voice, then a continuous AOA range sweep that walks linearly from just below L/D~MAX~ past the stall-warn threshold — covering every region of the tone map (silent → low-pitch pulsing → ONSPEED solid → high-pitch pulsing → stall warning buzz). Runs as a background task (doesn't block the console). Returns "busy" if already running.

### VNOCHIMETEST

Play the Vno overspeed chime once.

```
VNOCHIMETEST
```

Useful for verifying the chime audio without having to exceed Vno in flight. Equivalent to the "Test" button on the Vno chime config page.

### LIST

List all files on the SD card and flash filesystem.

```
LIST
```

Shows filenames and sizes for all stored files.

### DELETE

Delete a file from the SD card.

```
DELETE log_001.csv
```

### PRINT

Display the contents of a file to the console.

```
PRINT config.cfg
```

### FORMAT

Format the SD card.

```
FORMAT
```

!!! danger "This erases all data"
    FORMAT permanently deletes all log files and configuration on the SD card. Use with extreme caution.

### TASKS

Display FreeRTOS task information.

```
TASKS
```

Shows each running task's name and minimum stack watermark (bytes remaining). Useful for diagnosing stack overflow risks.

### BOOTLOG

Print the last 20 lines of `/boot_log.txt`.

```
BOOTLOG
```

Each line records one boot: monotonic boot counter, firmware version + git short SHA, ESP32 reset reason, how long the previous boot lived, and free heap at append time. Useful when chasing intermittent power-on failures — `BROWNOUT` and `PWRGLITCH` reset reasons point at supply-side problems, `PANIC` / `WDT` at firmware bugs. The same file is also accessible at `/logs` over WiFi or by pulling the SD card.

For panic-class reset reasons, the BootDiagnostics path also archives the ESP-IDF binary coredump from the dedicated coredump partition to `/coredumps/coredump_NNNN_<version>_<task>.bin` on the SD card. To send a crash report, attach the `boot_log.txt` line plus the matching `.bin` from `/coredumps/`.

### REBOOT

Restart the controller.

```
REBOOT
```

Equivalent to power cycling. Configuration is reloaded from storage.

### WIFIREFLASH

Listed in `HELP` for historical reasons. The handler currently prints `wifi reflash mode not supported` and does nothing — the on-board ESP32-S3's WiFi runs in the same firmware image as the rest of the system, so there's nothing separate to reflash.

```
WIFIREFLASH
```

### CRASHME

Force a deterministic StoreProhibited exception so the next boot exercises the BootDiagnostics coredump archival path. Used by developers to bench-test `/coredumps/` archival without provoking a real bug.

```
CRASHME
```

After the panic, the next boot writes a `coredump_NNNN_<version>_<task>.bin` to the SD card under `/coredumps/` and appends the panic reason to `/boot_log.txt`. View with `BOOTLOG`.

## Tips

- **SENSORS is your best friend** — run it whenever something seems wrong. It shows you exactly what every sensor is reading.
- **MSG for debugging** — set specific modules to `debug` level to see detailed logging. Set back to `warning` or `off` when done to reduce console noise.
- **CONFIG for verification** — dump the full config after making changes to confirm they took effect.
- **FLAPS during setup** — run this while moving the flap handle to record pot values for each position.
