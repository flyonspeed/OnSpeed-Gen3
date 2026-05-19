# airpods_to_m5

Feed AirPods head-tracking pitch/roll into the M5 / huVVer secondary
display over USB. Wear AirPods Pro / 3 / Max / 4, put the M5 in
Attitude mode (mode 1), and tilt your head — the synthetic horizon
follows.

This is a reference example for the `macos-coremotion-cli` and
`m5-usb-cdc` skills. Both are in `.claude/skills/`.

## Setup, once

1. Plug the M5 into the Mac via USB.
2. On the M5, hold the center button (BtnB) for ~600 ms to enter the
   Settings menu. Navigate to **Data Source** and cycle it to `USB`.
   Exit the menu. Power-cycle once so the M5 boots with USB selected
   (otherwise it waits ~17 s during AUTO probe).
3. Wear the AirPods.

## Build

```bash
./build.sh
```

Produces `airpods_to_m5.app` — an ad-hoc codesigned macOS bundle. See
the `macos-coremotion-cli` skill for why all the ceremony is needed
to read `CMHeadphoneMotionManager` from a CLI tool.

## Run

```bash
# Find the M5's port:
ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial* 2>/dev/null

# Launch:
./run.sh /dev/cu.usbserial-XXXXXXXX

# In another terminal, watch the live readout:
tail -F /tmp/airpods_to_m5.stderr
```

On first launch macOS prompts for Motion permission — click Allow.
The grant persists across runs.

The first AirPods sample becomes the zero point. Hold your head where
you want "level horizon" to be when you launch, OR re-zero anytime:

```bash
./recenter.sh
```

Stop:

```bash
pkill -f airpods_to_m5
```

## What the M5 shows

- **Mode 1 (Attitude)** — synthetic horizon, follows your head.
- **Mode 0 (Energy Display)** — also fine, just no airspeed numbers
  (the program emits the "air data not valid" wire sentinel, so the
  M5 renders `--` for IAS / percentLift).
- **Modes 2 / 3 / 4** — work, but degenerate without air data.

## Wire format

77-byte v4.23 `#1` frame at 20 Hz, exactly what the M5's
`SerialRead.cpp` already speaks. The frame builder mirrors
`onspeed_core/proto/DisplaySerial.h::BuildDisplayFrame`. See the
`m5-usb-cdc` skill for the full protocol reference.

## Smoke test the frame builder

```bash
swift frame_smoke.swift
```

Confirms 77-byte length, magic `#1`, CRLF, and self-consistent
checksum. Run after any change to `airpods_to_m5.swift` that touches
the frame builder.

## Files

| File | What it does |
|---|---|
| `airpods_to_m5.swift` | Reads CoreMotion, opens serial, emits `#1` frames at 20 Hz. |
| `frame_smoke.swift` | Builds one frame and self-checks length / magic / CRLF / checksum. |
| `Info.plist` | Embeds `NSMotionUsageDescription` so TCC will grant Motion access. |
| `entitlements.plist` | `com.apple.security.device.bluetooth` so AirPods motion stream is unblocked. |
| `build.sh` | Builds the `.app`, codesigns ad-hoc with the entitlements. |
| `run.sh` | Launches via `open` with stdout/stderr redirected so output stays visible. |
| `recenter.sh` | `kill -USR1` to the running program — captures a new zero on the next sample. |
