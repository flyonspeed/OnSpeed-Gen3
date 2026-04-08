# Backup & Restore

Your OnSpeed configuration is stored as an XML file (`config.cfg`) on the SD card and in flash memory. You can download, upload, and manually edit this file.

## Downloading a Backup

1. Connect to OnSpeed WiFi
2. Navigate to the configuration page
3. Click the **Download Config** button
4. Save the XML file to your computer

!!! tip "Save a backup after initial setup"
    Always download a backup after completing first-time setup and after a successful calibration. This lets you restore if anything goes wrong.

## Uploading a Configuration

1. Navigate to the configuration page
2. Click the **Upload Config** button
3. Select a previously saved config XML file
4. The system will load the new configuration
5. Reboot for all settings to take effect

## Config File Format

The configuration is stored as XML with the root element `CONFIG2`. Example structure:

```xml
<CONFIG2>
  <AOA_SMOOTHING>20</AOA_SMOOTHING>
  <PRESSURE_SMOOTHING>15</PRESSURE_SMOOTHING>
  <DATASOURCE>SENSORS</DATASOURCE>
  <EFISTYPE>ADVANCED</EFISTYPE>
  <ORIENTATION>
    <PORTS>FORWARD</PORTS>
    <BOX_TOP>UP</BOX_TOP>
  </ORIENTATION>
  <FLAP_POSITION>
    <DEGREES>0</DEGREES>
    <POT_VALUE>129</POT_VALUE>
    <LDMAXAOA>8.03</LDMAXAOA>
    <!-- ... more setpoints and curves ... -->
  </FLAP_POSITION>
  <!-- ... more flap positions ... -->
  <VOLUME>
    <DEFAULT>100</DEFAULT>
    <MUTE_UNDER_IAS>25</MUTE_UNDER_IAS>
    <!-- ... -->
  </VOLUME>
  <BIAS>
    <PFWD>8192</PFWD>
    <P45>8192</P45>
    <!-- ... -->
  </BIAS>
</CONFIG2>
```

## Manual Editing

You can edit the config file with any text editor. This is useful for:

- Copying calibration data between identically-equipped aircraft
- Bulk-editing setpoints
- Debugging configuration issues

!!! warning "Be careful with manual edits"
    An invalid XML file or out-of-range values can cause the system to behave incorrectly. Always keep a known-good backup before making manual changes.

## Config Load Order

On startup, the firmware loads configuration in this order:

1. **Compiled defaults** (from `ConfigDefaults.h`)
2. **Flash memory** (LittleFS) — overwrites defaults
3. **SD card** (`config.cfg`) — overwrites flash

This means the SD card config takes priority. If you need to factory-reset, you can either:

- Use the **Load Defaults** button on the web interface (resets to compiled defaults)
- Delete the `config.cfg` file from the SD card
