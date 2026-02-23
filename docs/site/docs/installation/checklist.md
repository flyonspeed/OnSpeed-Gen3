# Installation Checklist

Use this checklist to verify your hardware installation is complete before proceeding to configuration.

## Mounting

- [ ] Controller box is securely mounted to aircraft structure
- [ ] Mounting location allows access to SD card slot
- [ ] Mounting location allows access to USB port
- [ ] You've noted the box orientation (which way ports face, which way top faces)

## Pneumatic

- [ ] Pitot line connected from aircraft pitot system to controller pitot port (tee fitting)
- [ ] AOA line connected from AOA probe to controller AOA port
- [ ] Static line connected from aircraft static system to controller static port (tee fitting)
- [ ] All tubing secured with clamps — no kinks, no unsupported runs
- [ ] All fittings tight and leak-tested
- [ ] No low spots in tubing where moisture could collect

## Electrical — Required

- [ ] Power wire connected to aircraft bus through a circuit breaker or fuse
- [ ] Ground wire connected to aircraft ground bus
- [ ] Circuit breaker labeled "OnSpeed" or similar

## Electrical — Audio

- [ ] Audio output connected to audio panel or headset (see [Audio Wiring](audio.md))
- [ ] Audio ground connected
- [ ] If using stereo: both left and right channels connected
- [ ] Audio panel input configured and enabled (if applicable)

## Electrical — Optional

- [ ] EFIS serial: TX from EFIS connected to OnSpeed RX (GPIO 11)
- [ ] Flap position sensor: potentiometer or jumpers wired and connected
- [ ] Volume potentiometer: wired to ADC input (if using hardware volume control)
- [ ] Button/switch: wired between GPIO 12 and ground
- [ ] Status LED: wired to GPIO 13 (if desired)
- [ ] OAT sensor: DS18B20 wired to GPIO 14 with pull-up resistor (if no EFIS OAT)
- [ ] External display: serial TX (GPIO 10) connected to display RX
- [ ] Boom probe: serial RX (GPIO 3) connected to boom TX (if applicable)

## SD Card

- [ ] microSD card inserted (FAT32 formatted)
- [ ] Card is seated fully in the slot

## Before First Power-On

- [ ] Double-check all wiring — especially power polarity
- [ ] Verify no exposed wire connections that could short
- [ ] Verify audio panel is configured to accept the OnSpeed input channel
- [ ] Have the OnSpeed WiFi credentials ready: SSID `OnSpeed`, password `angleofattack`

## Next Step

Power on and proceed to [First-Time Setup](../configuration/first-time-setup.md).
