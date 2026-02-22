# Mounting the Controller Box

The OnSpeed controller needs a secure mounting location in your aircraft. The box contains sensitive pressure sensors and an IMU, so mounting quality directly affects measurement accuracy.

## Location Requirements

Choose a location that provides:

- **Rigid mounting** — the IMU measures accelerations. Any vibration or flex in the mount introduces noise. Mount to structure, not fabric or thin sheet metal.
- **Access to pneumatic lines** — the controller connects to your pitot, AOA, and static systems. Shorter lines mean faster response.
- **Access to the SD card** — you'll need to reach the microSD slot for card removal (or use WiFi download).
- **Access to USB port** — for firmware updates and serial console debugging.
- **Reasonable temperature** — avoid mounting near the engine, exhaust, or heating ducts. The electronics are rated for typical cockpit/cabin temperatures.
- **Wire routing** — power, audio, EFIS serial, and flap sensor wires all need to reach the controller.

### Common Locations

- **Behind the instrument panel** — convenient for wire routing, but can be hard to access
- **Under the seat** — easy access, but longer pneumatic and audio runs
- **In a side console** — good balance of access and routing distance
- **On the baggage shelf** — easy access, but may need longer lines

## Physical Mounting

Secure the enclosure using screws, nutplates, or Velcro straps depending on your aircraft's structure. The enclosure has mounting holes/tabs for this purpose.

!!! warning "Avoid vibration-coupled mounting"
    Do not hang the controller by its wires or tubing. It must be rigidly attached to aircraft structure. Vibration degrades IMU performance and can cause intermittent connections.

## Orientation Configuration

The controller can be mounted in any orientation — upright, inverted, on its side, etc. The firmware compensates for mounting orientation using two configuration settings:

- **PORTS_ORIENTATION** — Which direction do the pressure ports face? (FORWARD, AFT, LEFT, RIGHT)
- **BOX_TOP_ORIENTATION** — Which direction does the top of the box point? (UP, DOWN, LEFT, RIGHT)

These must be set correctly during [First-Time Setup](../configuration/first-time-setup.md). If they're wrong, pitch and roll readings will be incorrect, and your AOA tones won't work.

### How to Determine Orientation

1. Mount the box in its final position
2. Stand behind the aircraft looking forward
3. Note which direction the pressure port fittings face — that's your PORTS_ORIENTATION
4. Note which direction the top of the PCB/enclosure faces — that's your BOX_TOP_ORIENTATION

### Verifying Orientation

After configuration, use the `SENSORS` console command or the web interface sensor page to verify:

- With the aircraft level on the ground, you should see approximately 1G on the Z-axis (vertical)
- Pitch should read near 0° (or your aircraft's typical ground attitude)
- Roll should read near 0°

If the readings are wrong, your orientation settings need adjustment. See [Troubleshooting](../troubleshooting/index.md).
