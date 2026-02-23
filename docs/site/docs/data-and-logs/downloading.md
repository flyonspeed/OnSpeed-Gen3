# Downloading Logs

Two ways to get log data off the OnSpeed controller.

## Via WiFi Web Interface

1. Connect to OnSpeed WiFi (`OnSpeed` / `angleofattack`)
2. Navigate to **`http://192.168.0.1/logs`**
3. The page lists all log files on the SD card with sizes
4. Click a filename to download it

!!! note "Logging pauses during download"
    SD card logging is temporarily paused while a file is being downloaded to prevent conflicts. It resumes automatically after the download completes.

### Download Speed

WiFi transfers are not fast — the ESP32's WiFi bandwidth is limited. A large log file (50–100 MB) may take several minutes to download. Stay close to the controller for the best signal.

## Via SD Card Removal

For faster transfers, especially with large files:

1. Power off the OnSpeed controller
2. Remove the microSD card from its slot
3. Insert it into your computer (via SD card reader or adapter)
4. Copy the `log_*.csv` files to your computer
5. Re-insert the card into the OnSpeed controller

This is much faster than WiFi for large files.

## Managing Logs

### Deleting Logs

Via the web interface:

- Navigate to `/logs`
- Click the delete button next to a log file

Via console:

```
LIST            # See all files
DELETE log_001.csv  # Delete a specific file
```

### Formatting the SD Card

To erase all data and start fresh:

- Via web interface: `/format`
- Via console: `FORMAT`

!!! danger "FORMAT deletes everything"
    This permanently erases all log files and configuration on the SD card.
