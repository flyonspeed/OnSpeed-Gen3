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
- IAS, altitude
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

Run a 20-second audio test sequence.

```
AUDIOTEST
```

Plays test tones through left and right channels. Runs as a background task (doesn't block the console). Returns "busy" if already running.

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

### REBOOT

Restart the controller.

```
REBOOT
```

Equivalent to power cycling. Configuration is reloaded from storage.

## Tips

- **SENSORS is your best friend** — run it whenever something seems wrong. It shows you exactly what every sensor is reading.
- **MSG for debugging** — set specific modules to `debug` level to see detailed logging. Set back to `warning` or `off` when done to reduce console noise.
- **CONFIG for verification** — dump the full config after making changes to confirm they took effect.
- **FLAPS during setup** — run this while moving the flap handle to record pot values for each position.
