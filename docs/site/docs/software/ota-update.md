# OTA Updates via WiFi

You can update the OnSpeed firmware wirelessly through the web interface — no USB cable needed.

## Procedure

1. Download the new firmware `.bin` file to your phone, tablet, or laptop
2. Power on the OnSpeed controller
3. Connect to the OnSpeed WiFi (`OnSpeed` / `angleofattack`)
4. Navigate to: **`http://192.168.0.1/upgrade`**
5. Click **Choose File** and select the `.bin` firmware file
6. Click **Upload**
7. Wait for the upload and flash to complete (this may take 30–60 seconds)
8. The controller will reboot automatically with the new firmware

!!! warning "Don't interrupt the update"
    Do not power off the controller or disconnect WiFi during the firmware update. An interrupted update could leave the firmware in a corrupted state, requiring USB recovery.

## After the Update

- The web interface may show a brief loading delay as your browser fetches updated assets (ETag cache invalidation)
- Check the version number on the home page to confirm the update
- Your configuration is preserved across firmware updates (it's stored separately on SD card and flash)
- Review the changelog for any new configuration options or changes

## If Something Goes Wrong

If the controller won't boot after an OTA update:

1. Connect via USB and flash a known-good firmware using `esptool` (see [Flashing Firmware](flashing.md))
2. Your configuration file on the SD card should still be intact
