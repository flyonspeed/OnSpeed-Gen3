# Pneumatic Plumbing

The OnSpeed controller has three pneumatic connections for pressure measurement. These connect to your aircraft's pitot/static system and an AOA pressure probe.

## Three Pressure Ports

| Port | Sensor | Measures | Connects To |
|------|--------|----------|-------------|
| **Pitot** | HSCDRNN1.6BASA3 (differential) | Dynamic pressure (ram air) | Your pitot system (tee fitting) |
| **AOA** | HSCDRNN1.6BASA3 (differential) | Angle-dependent differential pressure | AOA probe on wing/fuselage |
| **Static** | HSCDRRN100MDSA3 (absolute) | Barometric/static pressure | Your static system (tee fitting) |

## Plumbing Requirements

- **Use aviation-grade tubing** — typically 1/4" or 3/16" OD flexible tubing rated for pneumatic instrument systems
- **Keep runs short** — shorter tubing means faster pressure response. Avoid unnecessary length.
- **Avoid kinks and low spots** — kinks restrict flow; low spots can trap moisture
- **Secure tubing** — use adel clamps or zip ties to prevent tubing from chafing or pulling loose
- **Seal connections** — ensure all fittings are snug. A leak in the pitot or static line affects both OnSpeed and your primary instruments.

## Connecting to Your Pitot/Static System

### Pitot Connection

Install a **tee fitting** in your pitot line between the pitot tube and your airspeed indicator (or EFIS pitot input). One branch goes to your existing instrument, the other goes to the OnSpeed pitot port.

!!! warning "Pitot system integrity"
    Adding a tee to your pitot system means a leak at the OnSpeed connection will affect your primary airspeed indicator. Ensure all connections are secure and leak-tested. Use thread sealant appropriate for instrument plumbing.

### Static Connection

Install a **tee fitting** in your static line. One branch continues to your existing instruments, the other goes to the OnSpeed static port.

If your aircraft has an alternate static source, ensure the OnSpeed static connection is on the primary static line.

### AOA Probe Connection

The AOA pressure port connects to a differential pressure probe that senses the angle of the airflow. This is a **separate probe** — not part of your existing pitot/static system.

The AOA probe is typically:

- A small tube or port mounted on the wing or fuselage
- Oriented to sense differential pressure that varies with angle of attack
- Connected to the OnSpeed AOA port via tubing

!!! note "AOA probe installation is aircraft-specific"
    The location and type of AOA probe depends on your aircraft. Common options include wing-mounted probes, fuselage-mounted differential ports, or a boom probe. Consult the OnSpeed community for recommendations for your aircraft type.

## Leak Testing

After connecting all pneumatic lines:

1. **Block the pitot port** and gently pressurize the line. The airspeed reading should increase and hold steady. If it drops, you have a leak.
2. **Block the static port** and gently apply suction. The altimeter should show an increase in altitude and hold. If it drops, you have a leak.
3. **Check the AOA line** — with the probe blocked, the AOA pressure reading on the OnSpeed sensor page should be stable and near zero.

## Moisture Management

Moisture in pneumatic lines can cause erratic pressure readings, especially in cold weather.

- Route tubing to avoid low spots where water can collect
- If your aircraft has a pitot drain, ensure it's downstream of the OnSpeed tee
- In cold/humid climates, consider moisture traps in the lines
- If you suspect moisture contamination, disconnect the tubing at the controller and blow it out with dry air
