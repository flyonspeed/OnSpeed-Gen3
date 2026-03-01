# OTA Updates via WiFi

You can update the OnSpeed firmware wirelessly through the web interface — no USB cable needed.

## What You Need

Download the `firmware.bin` for your hardware variant (V4P or V4B) from the [latest GitHub release](https://github.com/flyonspeed/OnSpeed-Gen3/releases/latest). You do **not** need the bootloader or partitions files — those are only for [USB flashing](flashing.md).

| Your hardware | Download this file |
|---------------|-------------------|
| V4P (Phil's box) | `onspeed-vX.Y.Z-v4p-firmware.bin` |
| V4B (Bob's box) | `onspeed-vX.Y.Z-v4b-firmware.bin` |

Not sure which variant you have? See [Which Hardware Do I Have?](flashing.md#which-hardware-do-i-have)

## When to Use OTA

Use OTA for routine firmware updates when your OnSpeed is already running and the `OnSpeed` WiFi network appears. OTA only updates the application firmware, not the bootloader or partition table.

## When NOT to Use OTA

If the device won't boot or the WiFi network doesn't appear, you need USB recovery instead. See [Flashing Firmware](flashing.md).

## Procedure

1. Download the firmware `.bin` file matching your hardware variant to your phone, tablet, or laptop
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
