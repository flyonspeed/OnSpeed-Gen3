# Downloading Logs

Log data is downloaded from the OnSpeed controller via WiFi.

## Via WiFi Web Interface

1. Connect to OnSpeed WiFi (`OnSpeed` / `angleofattack`)
2. Navigate to **`http://192.168.0.1/logs`**
3. The page lists each log with start time, duration, max IAS, max altitude, and size
4. Click a filename to download it

The metadata columns come from a small `.meta` sidecar file written next to each `log_NNN.csv` when the log closes. Older logs without a sidecar render with em-dashes for the metadata columns but still download and delete normally.

!!! note "Logging pauses during download"
    SD card logging is temporarily paused while a file is being downloaded to prevent conflicts. It resumes automatically after the download completes.

### Download Speed

WiFi transfers are not fast — the ESP32's WiFi bandwidth is limited. A large log file (50–100 MB) may take several minutes to download. Stay close to the controller for the best signal.

## Managing Logs

### Deleting Logs

Via the web interface:

- Navigate to `/logs`
- Click the trash icon next to a single log to delete it, or check several rows and click **Delete selected** to remove them in one pass. Either action shows a confirmation page before files are removed. The log currently being written is protected from deletion.
- Deleting a log also removes its `.meta` sidecar.

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
