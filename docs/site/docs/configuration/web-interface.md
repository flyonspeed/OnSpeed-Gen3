# Web Interface

The OnSpeed controller runs a WiFi access point and web server. You configure, calibrate, and manage logs through a web browser on your phone, tablet, or laptop.

## Connecting

1. Power on the OnSpeed controller
2. On your device, look for the WiFi network:
    - **SSID**: `OnSpeed`
    - **Password**: `angleofattack`
3. Connect to this network
4. Open a web browser and navigate to: **`http://192.168.0.1`**

You can also use the mDNS name: **`http://onspeed.local`** (may not work on all devices).

!!! note "No internet while connected"
    When your device connects to the OnSpeed WiFi, it disconnects from your normal WiFi/cellular network. You won't have internet access while configuring OnSpeed.

## Web Pages

| Page | URL | What It Does |
|------|-----|--------------|
| **Home** | `/` | Landing page with usage guidelines |
| **Live View** | `/live` | Real-time AOA gauge, attitude indicator, sensor data (WebSocket, 10 Hz) |
| **AOA Config** | `/aoaconfig` | Main configuration page — flap settings, audio, EFIS type, orientation |
| **Sensor Config** | `/sensorconfig` | Sensor bias calibration and orientation setup |
| **Calibration Wizard** | `/calwiz` | Interactive in-flight calibration wizard |
| **Logs** | `/logs` | List, download, and delete SD card log files |
| **Firmware Upgrade** | `/upgrade` | Upload new firmware binary (OTA update) |
| **Reboot** | `/reboot` | Restart the controller |
| **Format SD** | `/format` | Erase all data on the SD card |

## Live View

The live view page (`/live`) shows real-time sensor data updated at 10 Hz via WebSocket:

- AOA needle position on a visual scale
- IAS, altitude, G-loads
- Pitch and roll (attitude indicator)
- Derived AOA and pitch rate
- Current tone region indicators (L/Dmax, OnSpeed, Stall Warning zones)

!!! warning "Live view is for ground testing and debug"
    The live view is useful for verifying sensor operation on the ground and during calibration. Do not use it as a primary flight instrument — it requires a WiFi connection and a screen in the cockpit.

## Performance Notes

- Static assets (CSS, JavaScript) are cached using **ETags** based on the firmware version. After a firmware update, your browser will automatically fetch new assets.
- The web server runs on ESP32 Core 0 (the connectivity core), so it doesn't interfere with flight-critical tasks on Core 1.
- Large file downloads from the `/logs` page will pause SD card logging during the transfer to avoid conflicts.

## WiFi Range

The ESP32-S3's WiFi range is limited — typically 10–30 feet in open air, less through aircraft structure. For configuration and log download:

- Work in or near the cockpit
- Keep your device within a few feet of the controller
- If pages load slowly, move closer
