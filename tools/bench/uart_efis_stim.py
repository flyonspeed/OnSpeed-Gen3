#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "pyserial>=3.5",
# ]
# ///
"""
UART EFIS stim — pump VN-300 138-byte frames into the V4P's EFIS UART RX
at 400 Hz / 921600 baud, from a USB-TTL dongle.

Pairs with tools/bench/stress_web_handlers.py. The web stress hammers
Core 0 from the WiFi side; this hammers Core 0 from the UART/IDF-driver
side. Together they reproduce the production workload that the synth
firmware build doesn't exercise (the IDF UART driver / RX FIFO / event
queue path — synth bypasses all three).

What this is:
    A pacing layer on top of efis-stim/efis_stim_vn300 (the C++ helper
    that emits canonical VN-300 frames on stdout). The helper produces
    bit-identical bytes to onspeed::test_frames::Vn300Frame(), so the
    wire is exactly what unit tests validate. This driver just sends
    those bytes out a serial port at 400 Hz.

Wiring (V4P):
    Dongle TX  → V4P GPIO 9 (EFIS RX, DB-15 pin 2)
    Dongle GND → V4P GND
    Dongle voltage selector → 3.3 V (NOT 5 V — the ESP32-S3 RX pin is 3.3 V)
    Dongle RX, 3.3V, CTS/RTS → leave disconnected
    Any real EFIS already on that line → DISCONNECT first

Quick start:
    cd tools/bench/efis-stim && make
    cd ..  # back to tools/bench/
    uv run uart_efis_stim.py                      # auto-detect dongle
    uv run uart_efis_stim.py --port /dev/cu.usbserial-XXXX

Run it alongside the web stress in another terminal:
    uv run stress_web_handlers.py --aggressive --no-saves --no-downloads --duration 45

Watch the V4P console (separate terminal) for:
    'EFIS UART RX buffer full'   ← IDF software buffer overflowed
    'EFIS UART FIFO overflow'    ← HW FIFO overflowed (worse)
    'EFIS UART frame error'      ← wire glitch (rare, can ignore at first)

And in PERF heartbeats:
    EfisRead loops/s should be ~400; significantly less means the task
    is being starved on Core 0.

Stop with Ctrl-C. Prints a summary on exit.
"""

from __future__ import annotations

import argparse
import glob
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial  # type: ignore
except ImportError:
    sys.stderr.write(
        "missing pyserial — run via `uv run uart_efis_stim.py` (PEP 723 deps "
        "are resolved automatically) or `pip install pyserial>=3.5`\n"
    )
    sys.exit(1)

# ---------------------------------------------------------------------------
# Configuration constants. Match Vn300.h kPacketSize and the VN-300 wire
# format from PR #642. If those change, change these.
# ---------------------------------------------------------------------------

FRAME_BYTES   = 138
BAUD          = 921600
RATE_HZ       = 400
PERIOD_S      = 1.0 / RATE_HZ           # 2.5 ms per frame
# Burst N frames per serial.write() — pyserial overhead per call is ~50 µs,
# so writing one frame at a time at 400 Hz spends 20% of wall time in
# syscall. Bursting 4 frames cuts that to 5% without making the on-wire
# pacing noticeably bursty (4 frames = 1380 bytes = ~15 ms at 921600 baud).
BURST_FRAMES  = 4

# Bench V4P enumerates as /dev/cu.usbserial-510 per CLAUDE.md — that's
# the V4P's CP2102N console, NOT the dongle. Exclude it when auto-detecting.
V4P_CONSOLE_HINTS = ("usbserial-510", "usbserial-410", "usbserial-310")

SERIAL_GLOBS = (
    "/dev/cu.usbserial-*",
    "/dev/cu.SLAB_USBtoUART*",
    "/dev/cu.wchusbserial*",
    "/dev/cu.usbmodem*",
)


def find_dongle_port() -> str | None:
    """First /dev/cu.usbserial-* that isn't the V4P console.

    Returns None if zero candidates remain. We deliberately don't try to
    sniff the device — if the user has two USB-serial devices plugged in
    and neither is the V4P console, picking one arbitrarily is no worse
    than asking and being wrong. They can override with --port.
    """
    seen: list[str] = []
    for pattern in SERIAL_GLOBS:
        seen.extend(glob.glob(pattern))
    seen = sorted(set(seen))
    candidates = [
        p for p in seen
        if not any(hint in p for hint in V4P_CONSOLE_HINTS)
    ]
    return candidates[0] if candidates else None


def helper_path() -> Path:
    """Locate the compiled efis_stim_vn300 binary next to this script."""
    here = Path(__file__).resolve().parent
    bin_ = here / "efis-stim" / "efis_stim_vn300"
    if not bin_.exists():
        sys.stderr.write(
            f"helper binary not found at {bin_}\n"
            f"  build it first: (cd {here}/efis-stim && make)\n"
        )
        sys.exit(1)
    if not os.access(bin_, os.X_OK):
        sys.stderr.write(f"helper at {bin_} is not executable\n")
        sys.exit(1)
    return bin_


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Pump VN-300 frames into the V4P EFIS UART at 400 Hz / "
                    "921600 baud via a USB-TTL dongle.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--port",
        help="USB-TTL dongle device path (default: auto-detect, excluding "
             f"V4P console patterns {V4P_CONSOLE_HINTS})",
    )
    parser.add_argument(
        "--duration", type=float, default=0.0,
        help="Run for N seconds then stop. 0 = forever (Ctrl-C to stop).",
    )
    parser.add_argument(
        "--baud", type=int, default=BAUD,
        help=f"Serial baud rate. Default {BAUD} (matches PR #642). Changing "
             f"this will desync the firmware unless EfisSerialPort::Init "
             f"agrees.",
    )
    parser.add_argument(
        "--probe", action="store_true",
        help="Open the port, write one frame, close, exit. Smoke test for "
             "wiring and permissions.",
    )
    args = parser.parse_args()

    # ---------------- Resolve port ----------------
    port = args.port or find_dongle_port()
    if not port:
        sys.stderr.write(
            "no USB-TTL dongle found.\n"
            f"  looked under {SERIAL_GLOBS}, excluding {V4P_CONSOLE_HINTS}.\n"
            "  is the dongle plugged in? if so, pass --port /dev/cu.XXX\n"
        )
        return 1

    helper = helper_path()

    print(f"uart_efis_stim:")
    print(f"  helper:    {helper}")
    print(f"  port:      {port}")
    print(f"  baud:      {args.baud}")
    print(f"  frame:     {FRAME_BYTES} bytes")
    print(f"  rate:      {RATE_HZ} Hz ({PERIOD_S*1000:.2f} ms/frame)")
    print(f"  on-wire:   {FRAME_BYTES * RATE_HZ / 1024:.1f} KB/s "
          f"({FRAME_BYTES * RATE_HZ * 10 / args.baud * 100:.0f}% of baud)")
    print()

    # ---------------- Open serial ----------------
    try:
        ser = serial.Serial(port, args.baud, timeout=1, write_timeout=1)
    except serial.SerialException as e:
        sys.stderr.write(f"failed to open {port}: {e}\n")
        return 1

    # ---------------- Probe path ----------------
    if args.probe:
        proc = subprocess.run(
            [str(helper), "--frames", "1"], capture_output=True, check=True,
        )
        frame = proc.stdout
        assert len(frame) == FRAME_BYTES, \
            f"helper produced {len(frame)} B, expected {FRAME_BYTES}"
        ser.write(frame)
        ser.flush()
        ser.close()
        print(f"probe: wrote 1 frame ({FRAME_BYTES} B) and exited.")
        print(f"check the V4P console for parse activity, no errors.")
        return 0

    # ---------------- Streaming path ----------------
    # Spawn the helper with no frame cap and pipe its stdout into us.
    # We pace at 400 Hz by sleeping between bursts; the helper produces
    # frames as fast as we drain it, which is rate-limited by our reads.
    proc = subprocess.Popen(
        [str(helper)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
    )
    assert proc.stdout is not None

    # SIGINT handling: let Ctrl-C break the loop cleanly so we can print a
    # summary and close the serial port. Otherwise pyserial leaves the
    # port held until process exit, which delays the next run.
    stop = {"flag": False}
    def on_sigint(signum: int, frame: object) -> None:
        stop["flag"] = True
    signal.signal(signal.SIGINT, on_sigint)

    print(f"streaming. Ctrl-C to stop.")
    print()

    t_start     = time.monotonic()
    deadline    = t_start + args.duration if args.duration > 0 else None
    next_burst  = t_start
    burst_bytes = FRAME_BYTES * BURST_FRAMES
    frames_sent = 0
    bursts_sent = 0
    last_report = t_start

    try:
        while not stop["flag"]:
            now = time.monotonic()
            if deadline is not None and now >= deadline:
                break

            # Read one burst worth of bytes from the helper.
            #
            # `read(N)` on a pipe is allowed to return any 1..N bytes — it's
            # not guaranteed to fill. A short read does NOT mean the helper
            # died. Loop until we have the full burst or the pipe closes
            # (EOF returns b""). Treating short reads as fatal was a bug
            # that fired at startup when the helper hadn't pre-filled the
            # pipe yet.
            buf = b""
            while len(buf) < burst_bytes:
                chunk = proc.stdout.read(burst_bytes - len(buf))
                if not chunk:
                    # EOF: helper actually exited
                    sys.stderr.write(
                        f"\nhelper exited (EOF) after {frames_sent} frames "
                        f"(got {len(buf)} of {burst_bytes} B in last burst)\n"
                    )
                    err = proc.stderr.read() if proc.stderr else b""
                    if err:
                        sys.stderr.write(
                            f"helper stderr: {err.decode(errors='replace')}\n")
                    return 1
                buf += chunk

            ser.write(buf)
            frames_sent += BURST_FRAMES
            bursts_sent += 1

            # Pace: next burst at start + bursts_sent * (BURST_FRAMES * PERIOD_S).
            next_burst = t_start + bursts_sent * BURST_FRAMES * PERIOD_S
            sleep_for = next_burst - time.monotonic()
            if sleep_for > 0:
                time.sleep(sleep_for)
            # If we fell behind (sleep_for < 0), don't try to catch up by
            # bursting — that just blasts the firmware harder than spec.
            # Just resume on the next tick; throughput report will surface
            # the lag.

            # Throughput report every 5 s.
            if now - last_report >= 5.0:
                elapsed = now - t_start
                rate = frames_sent / elapsed
                print(f"  t={elapsed:6.1f}s  frames={frames_sent:8d}  "
                      f"rate={rate:6.1f} Hz  "
                      f"(target {RATE_HZ} Hz, {'on-spec' if rate >= RATE_HZ * 0.98 else 'LAGGING'})")
                last_report = now

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            proc.kill()
        ser.close()

    elapsed = time.monotonic() - t_start
    print()
    print(f"done. {frames_sent} frames in {elapsed:.2f} s "
          f"({frames_sent / elapsed:.1f} Hz avg, target {RATE_HZ} Hz)")
    return 0


if __name__ == "__main__":
    # Line-buffer stdout so progress lines flush immediately when stdout is
    # a pipe (e.g. running under `uv run ... &` with stdout captured by a
    # parent). Without this, the 5-sec throughput reports sit in the Python
    # buffer until the process exits, which makes the stim look hung.
    sys.stdout.reconfigure(line_buffering=True)
    sys.exit(main())
