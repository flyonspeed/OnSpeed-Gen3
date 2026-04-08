# Troubleshooting: No EFIS Data

OnSpeed is configured for an EFIS but isn't receiving data.

## Check 1: Is the EFIS Type Correct?

The most common cause. Open the OnSpeed web interface and verify the **EFIS Type** matches your hardware:

| Your EFIS | Setting |
|-----------|---------|
| Dynon SkyView / HDX | **ADVANCED** |
| Dynon D10 / D100 | **DYNOND10** |
| Garmin G5 | **GARMING5** |
| Garmin G3X / G3X Touch | **GARMING3X** |
| MGL iEFIS / Odyssey | **MGL** |
| VectorNav VN-300 | **VN-300** |

If the EFIS type is wrong, OnSpeed will try to parse the serial data in the wrong format and get nothing useful.

## Check 2: Baud Rate

All EFIS types require **115200 baud** on the OnSpeed side. The EFIS must also be configured to transmit at 115200.

**Most common mistake**: Dynon SkyView defaults to 9600 baud. You must change it to 115200 in the Dynon serial port setup menu.

## Check 3: TX/RX Polarity

Serial connections are **crossed**: the EFIS **transmit** (TX) pin connects to the OnSpeed **receive** (RX) pin.

- OnSpeed RX: GPIO 11 (through ADM3202 RS-232 level shifter)
- If you wired TX to TX, no data will flow
- Swap the wire at one end

## Check 4: Serial Output Enabled on EFIS

Make sure your EFIS is configured to output data on the serial port you wired to:

- **Dynon SkyView**: Must enable ADAHRS + EMS output (not just ADAHRS alone) on the specific serial port
- **Garmin G5**: Must enable serial output in the G5 configuration
- **Garmin G3X**: Must configure a serial port for data output
- **MGL**: Must enable iLink output

## Check 5: Use SENSORS Command

Connect to the USB serial console and type:

```
SENSORS
```

Look at the EFIS data fields:

- If all EFIS fields are zero, data isn't arriving
- If `efisAge` or `vnDataAge` is very large (> 5000 ms), data was received at some point but has gone stale
- If fields have non-zero values and small age, EFIS is working

## Check 6: Serial EFIS Data Enabled

Verify that the `SERIALEFISDATA` configuration option is set to `true`. If it's `false`, OnSpeed won't even try to read EFIS serial data.

## Check 7: Physical Wiring

- Verify the wire has continuity from EFIS TX to OnSpeed RX
- Check for broken wires, cold solder joints, or loose connectors
- Verify ground is connected between the two devices
- Check that the RS-232 level shifter (ADM3202) on the OnSpeed board has power and is functioning

## Still No Data?

If all wiring and configuration looks correct:

1. Try a different serial port on the EFIS (if available)
2. Use a USB-to-serial adapter on a laptop to verify the EFIS is actually outputting data
3. Check the EFIS firmware version — some older firmware versions may have serial output bugs
4. Try power-cycling both the EFIS and OnSpeed
