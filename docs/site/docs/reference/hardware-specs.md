# Hardware Specifications

## Microcontroller

| Parameter | Value |
|-----------|-------|
| **MCU** | ESP32-S3-WROOM-2 |
| **CPU** | Dual-core Xtensa LX7, 240 MHz |
| **Flash** | 32 MB (OPI 80 MHz) |
| **PSRAM** | 8 MB (OPI) |
| **WiFi** | 802.11 b/g/n, 2.4 GHz |
| **USB** | Native USB (CDC) |

## Sensors

| Sensor | Part Number | Interface | Key Specs |
|--------|-------------|-----------|-----------|
| **IMU** | IMU330 | SPI (CS: GPIO 4) | 6-axis accel/gyro, 208 Hz sample rate |
| **Pitot Pressure** | HSCDRNN1.6BASA3 | SPI (CS: GPIO 15 V4P) | Differential, 14-bit, ±1.6 PSI |
| **AOA Pressure** | HSCDRNN1.6BASA3 | SPI (CS: GPIO 6 V4P) | Differential, 14-bit, ±1.6 PSI |
| **Static Pressure** | HSCDRRN100MDSA3 | SPI (CS: GPIO 7) | Absolute, 14-bit, 0–100 mbar |
| **External ADC** | MCP3202 | SPI (CS: GPIO 5, V4P only) | 12-bit, 2-channel (flap + volume) |

## Power

| Parameter | Value |
|-----------|-------|
| **Input Voltage** | 12–28V DC |
| **Regulator** | L7805ABD2T-TR (5V output) |
| **Current Draw** | TBD (low — ESP32 + sensors) |

## Audio

| Parameter | Value |
|-----------|-------|
| **Interface** | I2S DAC |
| **Sample Rate** | 16 kHz |
| **Bit Depth** | 16-bit signed |
| **Channels** | Stereo (left + right) |
| **Output Level** | Line level |

## Storage

| Parameter | Value |
|-----------|-------|
| **Media** | microSD card (FAT32) |
| **Interface** | SPI |
| **Logging Rate** | 50 Hz |
| **Typical File Size** | ~50–100 MB per hour |

## Pin Assignments (V4P Hardware)

### Sensor SPI Bus

| Pin | GPIO | Function |
|-----|------|----------|
| MOSI | 17 | SPI Master Out |
| MISO | 18 | SPI Master In |
| SCLK | 16 | SPI Clock |
| CS_IMU | 4 | IMU330 chip select |
| CS_STATIC | 7 | Static pressure CS |
| CS_AOA | 6 | AOA pressure CS |
| CS_PITOT | 15 | Pitot pressure CS |
| CS_ADC | 5 | MCP3202 ADC CS |

### SD Card SPI Bus (V4P)

| Pin | GPIO | Function |
|-----|------|----------|
| SCLK | 41 | SPI Clock |
| MISO | 42 | SPI Master In |
| MOSI | 40 | SPI Master Out |
| CS | 39 | Chip Select |

### Audio I2S (V4P)

| Pin | GPIO | Function |
|-----|------|----------|
| BCK | 20 | Bit Clock |
| DOUT | 19 | Data Out |
| LRCK | 8 | Left/Right Clock |

### Serial Interfaces

| Interface | TX GPIO | RX GPIO | Baud | Notes |
|-----------|---------|---------|------|-------|
| USB Console | — | — | 921600 | Native USB CDC |
| EFIS | 46 (NC) | 11 | 115200 | RS-232 via ADM3202 |
| Boom | 8 (NC) | 3 | 115200 | TTL level |
| Display | 10 | 11 (shared) | varies | RS-232 |

### Control Pins

| Function | GPIO | Notes |
|----------|------|-------|
| Switch/Button | 12 | Internal pull-up, active low |
| Status LED | 13 | Heartbeat indicator |
| OAT Sensor | 14 | DS18B20 OneWire |

## V4B Hardware Differences

| Parameter | V4P | V4B |
|-----------|-----|-----|
| CS_AOA | GPIO 6 | GPIO 15 |
| CS_PITOT | GPIO 15 | GPIO 6 |
| SD_SCLK | GPIO 41 | GPIO 42 |
| SD_MISO | GPIO 42 | GPIO 41 |
| I2S_BCK | GPIO 20 | GPIO 45 |
| I2S_DOUT | GPIO 19 | GPIO 48 |
| I2S_LRCK | GPIO 8 | GPIO 47 |
| External ADC | MCP3202 on SPI | Direct analog (GPIO 1, 2) |
