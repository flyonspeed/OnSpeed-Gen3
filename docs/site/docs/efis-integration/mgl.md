# MGL iEFIS / Odyssey

MGL Avionics iEFIS and Odyssey systems use a binary serial protocol (iLink) to communicate with OnSpeed.

## Serial Setup

- **Baud rate**: 115200
- **Protocol**: Binary (iLink messages)
- **EFIS Type setting**: `MGL`

## Wiring

Connect the MGL serial **TX** output to OnSpeed **RX** (GPIO 11).

## Protocol Details

MGL uses a custom binary protocol with two message types:

| Message | Size | Contents |
|---------|------|----------|
| Message 1 (Flight Data) | 36 bytes | Altitude, speed, AOA, VSI, pressure, OAT, time/date, checksums |
| Message 3 (Attitude/Motion) | 36 bytes | Heading, pitch, bank, turn rate, G-forces, angular rates, flags |

Each message includes a CRC checksum for data integrity.

## Data Available

The MGL provides:

- IAS
- Pitch and bank angles
- Heading
- Pressure altitude, VSI
- G-loads and angular rates
- OAT
- Time/date

## Configuration

1. Configure your MGL EFIS to output iLink data on a serial port at 115200 baud
2. Set **EFIS Type** to `MGL` in the OnSpeed web interface
3. Save and reboot
4. Verify with the `SENSORS` console command
