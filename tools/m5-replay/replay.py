#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "pyserial>=3.5",
# ]
# ///
"""Stream OnSpeed #1 display-serial frames to an M5Stack secondary display.

Reads an OnSpeed SD-card CSV log (or generates synthetic data), formats the
70-byte ASCII payload + 2-byte CRC + CRLF (74 bytes total) exactly as the
Gen3 firmware does, and writes it at 20 Hz to a serial port (typically a
USB-to-TTL dongle).

Wire format reference:
    docs/site/docs/reference/serial-protocol.md
Canonical builder/parser:
    software/Libraries/onspeed_core/src/proto/DisplaySerial.{h,cpp}
Producer in firmware:
    software/sketch_common/src/io/DisplaySerial.cpp

Usage:
    uv run replay.py --port /dev/cu.usbserial-XXXX --input LOG.csv
    uv run replay.py --port /dev/cu.usbserial-XXXX --synthetic
    uv run replay.py --port /dev/cu.usbserial-XXXX --input LOG.csv --skip 120 --loop

Wiring (one-way):
    dongle TX  ->  M5 GPIO16 (Port C RX)
    dongle GND ->  M5 GND
    M5 powered via its own USB-C.
"""
from __future__ import annotations

import argparse
import csv
import math
import signal
import sys
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Optional

import serial

FRAME_PERIOD_S = 0.050  # 20 Hz, matches kDisplaySerialPeriodMs in HardwareMap.h
BAUD = 115200


# ---------------------------------------------------------------------------
# Frame construction
# ---------------------------------------------------------------------------


# Wire-format constants. Mirror onspeed_core/proto/DisplaySerial.h.
PAYLOAD_LEN = 70    # bytes 0..69 — ASCII fields up to and including dataMark
FRAME_LEN   = 74    # PAYLOAD_LEN + 2 hex CRC + CRLF


@dataclass
class Frame:
    """All fields transmitted in one #1 payload.

    Units / convention:
      - `*_aoa` body angles are NOT on the wire; the percent-lift fields
        (`tones_on_pct_lift`, `onspeed_fast_pct_lift`, etc.) are what go
        out.  Computed via the honest single-linear formula
        `(body_angle - alpha_0) / (alpha_stall - alpha_0) * 100` clamped
        to [0, 99].  See onspeed_core/aoa/PercentLift.h.
    """

    pitch_deg: float = 0.0
    roll_deg: float = 0.0
    ias_kts: float = 0.0
    palt_ft: float = 0.0
    turnrate_dps: float = 0.0
    lateral_g: float = 0.0
    vertical_g: float = 1.0
    percent_lift: int = 0
    vsi_fpm: float = 0.0
    oat_c: int = 15
    flightpath_deg: float = 0.0
    flap_deg: int = 0
    tones_on_pct_lift: int = 0
    onspeed_fast_pct_lift: int = 0
    onspeed_slow_pct_lift: int = 0
    stall_warn_pct_lift: int = 0
    flaps_min_deg: int = 0
    flaps_max_deg: int = 0
    g_onset_rate: float = 0.0
    spin_cue: int = 0
    data_mark: int = 0

    def to_bytes(self) -> bytes:
        """Serialize to the 74-byte wire frame (payload + CRC + CRLF).

        Matches the printf format in onspeed_core/proto/DisplaySerial.cpp.
        """
        payload = (
            f"#1"
            f"{_clamp_int(self.pitch_deg * 10, -999, 999):+04d}"
            f"{_clamp_int(self.roll_deg * 10, -9999, 9999):+05d}"
            f"{_clamp_uint(self.ias_kts * 10, 0, 9999):04d}"
            f"{_clamp_int(self.palt_ft, -99999, 99999):+06d}"
            f"{_clamp_int(self.turnrate_dps * 10, -9999, 9999):+05d}"
            f"{_clamp_int(self.lateral_g * 100, -99, 99):+03d}"
            f"{_clamp_int(self.vertical_g * 10, -99, 99):+03d}"
            f"{_clamp_uint(self.percent_lift, 0, 99):02d}"
            f"{_clamp_int(self.vsi_fpm / 10, -999, 999):+04d}"
            f"{_clamp_int(self.oat_c, -99, 99):+03d}"
            f"{_clamp_int(self.flightpath_deg * 10, -999, 999):+04d}"
            f"{_clamp_int(self.flap_deg, -99, 99):+03d}"
            f"{_clamp_uint(self.tones_on_pct_lift, 0, 99):02d}"
            f"{_clamp_uint(self.onspeed_fast_pct_lift, 0, 99):02d}"
            f"{_clamp_uint(self.onspeed_slow_pct_lift, 0, 99):02d}"
            f"{_clamp_uint(self.stall_warn_pct_lift, 0, 99):02d}"
            f"{_clamp_int(self.flaps_min_deg, -99, 99):+03d}"
            f"{_clamp_int(self.flaps_max_deg, -99, 99):+03d}"
            f"{_clamp_int(self.g_onset_rate * 100, -999, 999):+04d}"
            f"{_clamp_int(self.spin_cue, -9, 9):+02d}"
            f"{_clamp_uint(self.data_mark, 0, 99):02d}"
        )
        if len(payload) != PAYLOAD_LEN:
            raise AssertionError(
                f"payload length {len(payload)} != {PAYLOAD_LEN}: {payload!r}"
            )
        crc = sum(payload.encode("ascii")) & 0xFF
        return f"{payload}{crc:02X}\r\n".encode("ascii")


def _clamp_int(v: float, lo: int, hi: int) -> int:
    """C-style truncation-toward-zero + clamp, matching SafeScaledInt."""
    if not math.isfinite(v):
        return 0
    # int() in Python truncates toward zero, matching C's (int)cast.
    i = int(v)
    if i < lo:
        return lo
    if i > hi:
        return hi
    return i


def _clamp_uint(v: float, lo: int, hi: int) -> int:
    if not math.isfinite(v):
        return lo
    i = int(v)
    if i < lo:
        return lo
    if i > hi:
        return hi
    return i


# ---------------------------------------------------------------------------
# Config parsing — reads per-flap setpoints from OnSpeed .cfg XML
# ---------------------------------------------------------------------------


@dataclass
class FlapSetpoints:
    degrees: int
    ldmax_aoa: float
    onspeed_fast_aoa: float
    onspeed_slow_aoa: float
    stallwarn_aoa: float
    alpha_0: float
    alpha_stall: float


def load_flap_setpoints(cfg_path: Path) -> dict[int, FlapSetpoints]:
    """Parse OnSpeed .cfg XML for per-flap setpoints."""
    tree = ET.parse(cfg_path)
    root = tree.getroot()

    out: dict[int, FlapSetpoints] = {}
    for fp in root.findall("FLAP_POSITION"):
        deg = int(fp.findtext("DEGREES", "0"))
        out[deg] = FlapSetpoints(
            degrees=deg,
            ldmax_aoa=float(fp.findtext("LDMAXAOA", "0")),
            onspeed_fast_aoa=float(fp.findtext("ONSPEEDFASTAOA", "0")),
            onspeed_slow_aoa=float(fp.findtext("ONSPEEDSLOWAOA", "0")),
            stallwarn_aoa=float(fp.findtext("STALLWARNAOA", "0")),
            alpha_0=float(fp.findtext("ALPHA0", "0")),
            alpha_stall=float(fp.findtext("ALPHASTALL", "0")),
        )
    if not out:
        raise ValueError(f"No FLAP_POSITION entries found in {cfg_path}")
    return out


def setpoints_for_flap(
    flap_deg: int, table: dict[int, FlapSetpoints]
) -> FlapSetpoints:
    """Return exact match or nearest flap entry."""
    if flap_deg in table:
        return table[flap_deg]
    return table[min(table.keys(), key=lambda k: abs(k - flap_deg))]


# ---------------------------------------------------------------------------
# PercentLift — replicates DisplaySerial.cpp:155-174 exactly
# ---------------------------------------------------------------------------


def compute_percent_lift(aoa: float, fs: FlapSetpoints) -> int:
    """Honest single-linear envelope fraction, mirroring
    onspeed_core/aoa/PercentLift.cpp::ComputePercentLift.

    percentLift = (aoa - alpha_0) / (alpha_stall - alpha_0) * 100,
    clamped to [0, 99].  When alpha_stall is uncalibrated (<= stallwarn)
    a synthetic ceiling of stallwarn * 100/90 is used instead.
    """
    alpha_stall = fs.alpha_stall
    if alpha_stall <= fs.stallwarn_aoa:
        alpha_stall = fs.stallwarn_aoa * 100.0 / 90.0
    span = alpha_stall - fs.alpha_0
    if span <= 0:
        return 0
    pct = int((aoa - fs.alpha_0) / span * 100.0)
    return max(0, min(99, pct))


# ---------------------------------------------------------------------------
# CSV reader — yields `Frame` objects at a time-aligned 20 Hz
# ---------------------------------------------------------------------------


def csv_frame_stream(
    csv_path: Path,
    setpoints: dict[int, FlapSetpoints],
    skip_seconds: float = 0.0,
) -> Iterator[Frame]:
    """Read the OnSpeed SD log CSV and yield one Frame per logical 50 ms
    step, using nearest-sample-by-timestamp downsampling.

    The CSV timestamps are in milliseconds (from `millis()`); the first row
    is the epoch. Sample rate is ~50 Hz (20 ms cadence) so we take every
    other row for 20 Hz output.
    """
    with csv_path.open("r", newline="") as f:
        reader = csv.DictReader(f)
        rows = iter(reader)

        try:
            first = next(rows)
        except StopIteration:
            return

        t0_ms = int(first["timeStamp"])
        skip_until_ms = t0_ms + int(skip_seconds * 1000)
        target_ms = max(t0_ms, skip_until_ms)

        flaps_min = min(setpoints.keys())
        flaps_max = max(setpoints.keys())

        def row_to_frame(r: dict[str, str]) -> Frame:
            flap = _int_or(r.get("flapsPos"), 0)
            fs = setpoints_for_flap(flap, setpoints)
            aoa = _float_or(r.get("AngleofAttack"), 0.0)
            return Frame(
                pitch_deg=_float_or(r.get("Pitch"), 0.0),
                roll_deg=_float_or(r.get("Roll"), 0.0),
                ias_kts=_float_or(r.get("IAS"), 0.0),
                palt_ft=_float_or(r.get("Palt"), 0.0),
                turnrate_dps=_float_or(r.get("YawRate"), 0.0),
                lateral_g=_float_or(r.get("LateralG"), 0.0),
                vertical_g=_float_or(r.get("VerticalG"), 1.0),
                percent_lift=compute_percent_lift(aoa, fs),
                vsi_fpm=_float_or(r.get("VSI"), 0.0),
                oat_c=int(_float_or(r.get("OAT"), 15.0)),
                flightpath_deg=_float_or(r.get("FlightPath"), 0.0),
                flap_deg=flap,
                # Per-flap band-edge percents — each is the per-flap
                # setpoint's body angle put through compute_percent_lift.
                tones_on_pct_lift=compute_percent_lift(fs.ldmax_aoa, fs),
                onspeed_fast_pct_lift=compute_percent_lift(fs.onspeed_fast_aoa, fs),
                onspeed_slow_pct_lift=compute_percent_lift(fs.onspeed_slow_aoa, fs),
                stall_warn_pct_lift=compute_percent_lift(fs.stallwarn_aoa, fs),
                flaps_min_deg=flaps_min,
                flaps_max_deg=flaps_max,
                g_onset_rate=0.0,  # not in log; leave at 0
                spin_cue=0,
                data_mark=_int_or(r.get("DataMark"), 0) % 100,
            )

        if target_ms > t0_ms:
            # fast-forward without yielding
            for row in rows:
                if int(row["timeStamp"]) >= target_ms:
                    first = row
                    break

        prev_row = first
        yield row_to_frame(first)
        next_target_ms = int(first["timeStamp"]) + int(FRAME_PERIOD_S * 1000)

        for row in rows:
            t_ms = int(row["timeStamp"])
            # Advance until we pass next_target_ms, then emit prev (nearest)
            while t_ms >= next_target_ms:
                # pick whichever of prev_row / row is closer
                prev_t = int(prev_row["timeStamp"])
                pick = prev_row if (next_target_ms - prev_t) <= (t_ms - next_target_ms) else row
                yield row_to_frame(pick)
                next_target_ms += int(FRAME_PERIOD_S * 1000)
            prev_row = row


def _float_or(s: Optional[str], default: float) -> float:
    if s is None or s == "":
        return default
    try:
        v = float(s)
    except ValueError:
        return default
    if not math.isfinite(v):
        return default
    return v


def _int_or(s: Optional[str], default: int) -> int:
    if s is None or s == "":
        return default
    try:
        return int(float(s))
    except ValueError:
        return default


# ---------------------------------------------------------------------------
# Synthetic scenario — deterministic 5-minute flight that exercises every
# display mode (cruise, approach, slow flight, stall, g-turns)
# ---------------------------------------------------------------------------


def synthetic_stream(
    setpoints: dict[int, FlapSetpoints],
) -> Iterator[Frame]:
    dt = FRAME_PERIOD_S
    t = 0.0
    while True:
        if t < 60:
            # Cruise
            phase = "cruise"
            ias = 140.0
            aoa_target = 2.0
            flap = 0
            pitch = 2.0
            roll = 0.0
            vg = 1.0
            lat = 0.0
            vsi = 0.0
        elif t < 120:
            # Slow flight sweep: AOA 2 -> 8, IAS 140 -> 90
            phase = "slow"
            u = (t - 60) / 60
            ias = 140.0 - 50.0 * u
            aoa_target = 2.0 + 6.0 * u
            flap = 0
            pitch = 2.0 + 4.0 * u
            roll = 0.0
            vg = 1.0
            lat = 0.0
            vsi = -100.0 * u
        elif t < 180:
            # Approach config, flaps 16, on-speed
            phase = "approach"
            ias = 80.0
            flap = 16
            fs = setpoints_for_flap(flap, setpoints)
            aoa_target = fs.onspeed_slow_aoa
            pitch = 4.0
            roll = math.sin((t - 120) * 0.3) * 15.0
            vg = 1.0
            lat = math.sin((t - 120) * 0.3) * 0.1
            vsi = -500.0
        elif t < 210:
            # Approach-to-stall at approach flaps
            phase = "stall"
            flap = 16
            fs = setpoints_for_flap(flap, setpoints)
            u = (t - 180) / 30
            ias = 80.0 - 25.0 * u
            aoa_target = fs.onspeed_slow_aoa + (
                fs.alpha_stall - fs.onspeed_slow_aoa
            ) * u
            pitch = 6.0 + 4.0 * u
            roll = 0.0
            vg = 1.0
            lat = 0.0
            vsi = -200.0
        elif t < 240:
            # Stall recovery
            phase = "recovery"
            u = (t - 210) / 30
            ias = 55.0 + 40.0 * u
            flap = 16
            fs = setpoints_for_flap(flap, setpoints)
            aoa_target = fs.alpha_stall - (fs.alpha_stall - 2.0) * u
            pitch = 10.0 - 8.0 * u
            roll = 0.0
            vg = 1.0 - 0.3 * math.sin(u * math.pi)
            lat = 0.0
            vsi = -1500.0 + 2000.0 * u
        elif t < 300:
            # G-turns for history mode
            phase = "g-turns"
            u = (t - 240) / 60
            ias = 130.0
            flap = 0
            aoa_target = 3.0 + 2.0 * math.sin(u * 4 * math.pi)
            pitch = 5.0 * math.sin(u * 4 * math.pi)
            roll = 45.0 * math.sin(u * 2 * math.pi)
            vg = 1.0 + 0.8 * abs(math.sin(u * 2 * math.pi))
            lat = 0.1 * math.sin(u * 2 * math.pi)
            vsi = 200.0 * math.cos(u * 4 * math.pi)
        else:
            # Loop back to cruise
            t = 0.0
            continue

        fs = setpoints_for_flap(flap, setpoints)
        pct = compute_percent_lift(aoa_target, fs)

        yield Frame(
            pitch_deg=pitch,
            roll_deg=roll,
            ias_kts=ias,
            palt_ft=2500.0,
            turnrate_dps=roll * 0.3,
            lateral_g=lat,
            vertical_g=vg,
            percent_lift=pct,
            vsi_fpm=vsi,
            oat_c=15,
            flightpath_deg=pitch - 2.0,
            flap_deg=flap,
            tones_on_pct_lift=compute_percent_lift(fs.ldmax_aoa, fs),
            onspeed_fast_pct_lift=compute_percent_lift(fs.onspeed_fast_aoa, fs),
            onspeed_slow_pct_lift=compute_percent_lift(fs.onspeed_slow_aoa, fs),
            stall_warn_pct_lift=compute_percent_lift(fs.stallwarn_aoa, fs),
            flaps_min_deg=min(setpoints.keys()),
            flaps_max_deg=max(setpoints.keys()),
            g_onset_rate=0.0,
            spin_cue=0,
            data_mark=int(t) % 100,
        )
        t += dt


# ---------------------------------------------------------------------------
# 20 Hz scheduler + main loop
# ---------------------------------------------------------------------------


def stream_to_serial(
    port: str,
    frames: Iterator[Frame],
    speed: float = 1.0,
    loop: bool = False,
    source_factory=None,
) -> None:
    ser = serial.Serial(port, BAUD, timeout=0)
    print(f"Opened {port} @ {BAUD}. Streaming at {20 * speed:.1f} Hz...", file=sys.stderr)

    # signal handling for clean shutdown
    stopping = {"flag": False}

    def _stop(*_):
        stopping["flag"] = True

    signal.signal(signal.SIGINT, _stop)
    signal.signal(signal.SIGTERM, _stop)

    period = FRAME_PERIOD_S / max(speed, 0.01)
    next_tick = time.monotonic()
    sent = 0
    t_start = time.monotonic()

    while not stopping["flag"]:
        try:
            frame = next(frames)
        except StopIteration:
            if loop and source_factory is not None:
                frames = source_factory()
                continue
            break

        ser.write(frame.to_bytes())
        sent += 1

        if sent % 40 == 0:
            elapsed = time.monotonic() - t_start
            actual_hz = sent / elapsed if elapsed > 0 else 0
            print(
                f"sent={sent} elapsed={elapsed:6.1f}s actual={actual_hz:5.1f}Hz",
                file=sys.stderr,
                end="\r",
            )

        next_tick += period
        sleep = next_tick - time.monotonic()
        if sleep > 0:
            time.sleep(sleep)
        else:
            # fell behind; resync
            next_tick = time.monotonic()

    ser.close()
    elapsed = time.monotonic() - t_start
    print(
        f"\nSent {sent} frames in {elapsed:.1f}s "
        f"({sent/elapsed:.1f} Hz avg)",
        file=sys.stderr,
    )


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


DEFAULT_CONFIG = "~/Dropbox/N720AK/OnSpeed Cals/2_20_26_config.cfg"


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__.splitlines()[0] if __doc__ else "",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--port", required=True, help="Serial port (e.g. /dev/cu.usbserial-XXXX)")

    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--input", type=Path, help="OnSpeed SD-card CSV log to replay")
    src.add_argument("--synthetic", action="store_true", help="Generate deterministic demo scenario")

    p.add_argument(
        "--config",
        type=Path,
        default=Path(DEFAULT_CONFIG).expanduser(),
        help=f"OnSpeed .cfg file (for per-flap setpoints). Default: {DEFAULT_CONFIG}",
    )
    p.add_argument("--skip", type=float, default=0.0, help="Skip N seconds from CSV start")
    p.add_argument("--speed", type=float, default=1.0, help="Playback speed multiplier (1.0 = real-time)")
    p.add_argument("--loop", action="store_true", help="Loop input forever")
    args = p.parse_args()

    if not args.config.exists():
        print(f"ERROR: config file not found: {args.config}", file=sys.stderr)
        return 2
    setpoints = load_flap_setpoints(args.config)
    print(
        f"Loaded {len(setpoints)} flap setpoints from {args.config.name}: "
        f"{sorted(setpoints.keys())}",
        file=sys.stderr,
    )

    def make_stream() -> Iterator[Frame]:
        if args.synthetic:
            return synthetic_stream(setpoints)
        return csv_frame_stream(args.input, setpoints, skip_seconds=args.skip)

    stream_to_serial(
        port=args.port,
        frames=make_stream(),
        speed=args.speed,
        loop=args.loop,
        source_factory=make_stream,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
