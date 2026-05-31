#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# ///
"""
check-atomic-publish.py — detect torn reads in OnSpeed CSV logs captured
under the --epoch-encode UART stim.

Background
==========

The stim's --epoch-encode mode writes per-frame VN-300 fields that all
encode the same per-frame counter N (where N = TimeStartupNs / 2.5ms):

    Yaw      = (N % 36000) / 100.0          # degrees, [0, 360)
    Pitch    = ((N * 7)  % 6000) / 100 - 30 # degrees, [-30, +30)
    Roll     = ((N * 13) % 6000) / 100 - 30 # degrees, [-30, +30)
    GnssLat  = 40.0 + (N % 100_000) * 1e-6
    GnssLon  = -105.0 - (N % 100_000) * 1e-6
    TimeStartupNs = N * 2_500_000

A CSV row written under an ATOMIC publish (producer's struct write was
all-or-nothing from the consumer's view) yields the same N from every
column. A CSV row written during a TORN read shows fields from two
adjacent frames — the recovered N from at least one column will be off
by ±1 (or more) from the others.

Usage:
    uv run tools/bench/check-atomic-publish.py path/to/log_NNN.csv

Exit status:
    0 = no tears detected (atomic publish is working)
    1 = N >= 1 tears detected (publish is broken)
    2 = the CSV doesn't look like it was logged under --epoch-encode
"""

from __future__ import annotations

import csv
import sys
from dataclasses import dataclass


KS_NS_PER_FRAME = 2_500_000
KS_YAW_MOD = 36_000
KS_PITCH_MUL = 7
KS_ROLL_MUL = 13
KS_PR_MOD = 6_000
KS_LATLON_MOD = 100_000
KS_LATLON_BASE_LAT = 40.0
KS_LATLON_BASE_LON = -105.0
KS_LATLON_STEP = 1e-6

# Tolerances for float quantization noise.  Yaw at 0.01° step quantizes
# to ~1.2e-5 relative for values up to 360; we allow ±1 unit of the
# encoding step before flagging a mismatch.  TimeStartupNs is integer so
# its tolerance is 0.
KS_YAW_TOL_UNITS = 1   # ±1 in (N % 36000)
KS_PR_TOL_UNITS  = 1   # ±1 in (N*k % 6000)
KS_LATLON_TOL_UNITS = 2  # ±2 in (N % 1_000_000), generous for double rounding


@dataclass
class TearReport:
    row: int
    canonical_N: int
    field: str
    decoded_N: int


def decode_yaw_to_N(yaw_deg: float) -> int:
    return round(yaw_deg * 100) % KS_YAW_MOD


def decode_pitch_to_N_residue(pitch_deg: float) -> int:
    """Returns (N*7) % 6000; analyzer cross-checks against canonical N*7%6000."""
    return round((pitch_deg + 30.0) * 100) % KS_PR_MOD


def decode_roll_to_N_residue(roll_deg: float) -> int:
    """Returns (N*13) % 6000."""
    return round((roll_deg + 30.0) * 100) % KS_PR_MOD


def decode_lat_to_N(lat_deg: float) -> int:
    return round((lat_deg - KS_LATLON_BASE_LAT) / KS_LATLON_STEP) % KS_LATLON_MOD


def decode_lon_to_N(lon_deg: float) -> int:
    return round((KS_LATLON_BASE_LON - lon_deg) / KS_LATLON_STEP) % KS_LATLON_MOD


def within(a: int, b: int, tol: int, mod: int) -> bool:
    """Modular distance abs((a-b) mod m) ≤ tol."""
    d = (a - b) % mod
    return d <= tol or d >= mod - tol


def check_row(row_idx: int, row: dict) -> tuple[bool, list[TearReport]]:
    """Check one CSV row.  Returns (had_stim_data, tears_for_this_row).

    Pulled out of main() so unit tests can drive it against synthetic
    rows without going through a CSV file.
    """
    try:
        ts = int(row["vnTimeStartupNs"])
    except (KeyError, ValueError):
        return (False, [])
    if ts == 0:
        return (False, [])  # pre-stim row; firehose hadn't started yet

    N = ts // KS_NS_PER_FRAME
    tears: list[TearReport] = []

    # Yaw
    try:
        yaw = float(row["vnYaw"])
        N_from_yaw = decode_yaw_to_N(yaw)
        if not within(N_from_yaw, N % KS_YAW_MOD, KS_YAW_TOL_UNITS, KS_YAW_MOD):
            tears.append(TearReport(row_idx, N, "vnYaw", N_from_yaw))
    except (KeyError, ValueError):
        pass

    # Pitch
    try:
        pitch = float(row["vnPitch"])
        N_from_pitch = decode_pitch_to_N_residue(pitch)
        expected = (N * KS_PITCH_MUL) % KS_PR_MOD
        if not within(N_from_pitch, expected, KS_PR_TOL_UNITS, KS_PR_MOD):
            tears.append(TearReport(row_idx, N, "vnPitch", N_from_pitch))
    except (KeyError, ValueError):
        pass

    # Roll
    try:
        roll = float(row["vnRoll"])
        N_from_roll = decode_roll_to_N_residue(roll)
        expected = (N * KS_ROLL_MUL) % KS_PR_MOD
        if not within(N_from_roll, expected, KS_PR_TOL_UNITS, KS_PR_MOD):
            tears.append(TearReport(row_idx, N, "vnRoll", N_from_roll))
    except (KeyError, ValueError):
        pass

    # Lat / Lon — these are the doubles, the most likely to tear
    # because 8-byte writes are TWO store instructions on Xtensa LX7.
    try:
        lat = float(row["vnGnssLat"])
        N_from_lat = decode_lat_to_N(lat)
        if not within(N_from_lat, N % KS_LATLON_MOD,
                      KS_LATLON_TOL_UNITS, KS_LATLON_MOD):
            tears.append(TearReport(row_idx, N, "vnGnssLat", N_from_lat))
    except (KeyError, ValueError):
        pass

    try:
        lon = float(row["vnGnssLon"])
        N_from_lon = decode_lon_to_N(lon)
        if not within(N_from_lon, N % KS_LATLON_MOD,
                      KS_LATLON_TOL_UNITS, KS_LATLON_MOD):
            tears.append(TearReport(row_idx, N, "vnGnssLon", N_from_lon))
    except (KeyError, ValueError):
        pass

    # Cross-struct atomicity check.  EfisSerialPort::applyVn300Data
    # publishes suVN300_pub_ then mirrors Pitch/Roll/Heading into
    # suEfis_pub_ via a SECOND publish (PR #660: seqcount migration).
    # In the mutex-based version (PR #656), both updates happened in
    # one critical section — vnPitch and efisPitch always came from
    # the same frame.  With two seqcount publishes there's a ~1 µs
    # window between them where a reader can see frame N's vnPitch
    # but frame N-1's efisPitch (or vice versa).
    #
    # Detection: vnPitch and efisPitch should be byte-equal per frame
    # (both written from data.pitch).  Adjacent frames differ by the
    # step size (Pitch = ((N*7)%6000)/100 - 30 → 0.07°/frame step).
    # If they disagree by more than half the step (0.04°), one of
    # them came from a different frame.
    try:
        vn_pitch = float(row["vnPitch"])
        ef_pitch = float(row["efisPitch"])
        if abs(vn_pitch - ef_pitch) > 0.04:
            tears.append(TearReport(
                row_idx, N, "cross.vnPitch-vs-efisPitch",
                int(round((vn_pitch - ef_pitch) * 100))))
    except (KeyError, ValueError):
        pass

    try:
        vn_roll = float(row["vnRoll"])
        ef_roll = float(row["efisRoll"])
        if abs(vn_roll - ef_roll) > 0.04:
            tears.append(TearReport(
                row_idx, N, "cross.vnRoll-vs-efisRoll",
                int(round((vn_roll - ef_roll) * 100))))
    except (KeyError, ValueError):
        pass

    # Heading: efisMagHeading is stored as int (rounded from data.yaw).
    # vnYaw is float.  Within-frame: int(round(vnYaw)) == efisMagHeading.
    # Cross-frame: differ by step (Yaw step is 0.01°/frame, so a single
    # frame skew would round to the same int 99% of the time — the
    # cross-struct heading check is weak.  We skip it; the pitch/roll
    # check above is the load-bearing cross-struct test.

    return (True, tears)


def encode_row_for_N(N: int) -> dict:
    """Build a synthetic CSV row that should match canonical N.

    Mirror of the C++ stim's epoch-encode formulas (tools/bench/efis-stim/
    main.cpp).  A "clean" row (all fields from the same N) should produce
    zero tears.  Tests use this to construct both clean and torn fixtures.

    Note: efisPitch / efisRoll are the suEfis-mirror values that
    EfisSerialPort::applyVn300Data writes alongside the suVN300 publish.
    For a clean (untorn) frame they exactly match vnPitch / vnRoll.
    """
    pitch = ((N * 7) % 6000) / 100.0 - 30.0
    roll  = ((N * 13) % 6000) / 100.0 - 30.0
    return {
        "vnTimeStartupNs": str(N * KS_NS_PER_FRAME),
        "vnYaw":           f"{(N % 36000) / 100.0:.2f}",
        "vnPitch":         f"{pitch:.2f}",
        "vnRoll":          f"{roll:.2f}",
        "vnGnssLat":       f"{KS_LATLON_BASE_LAT + (N % KS_LATLON_MOD) * KS_LATLON_STEP:.6f}",
        "vnGnssLon":       f"{KS_LATLON_BASE_LON - (N % KS_LATLON_MOD) * KS_LATLON_STEP:.6f}",
        # suEfis-mirror fields (suEfis_pub_ second publish in applyVn300Data)
        "efisPitch":       f"{pitch:.2f}",
        "efisRoll":        f"{roll:.2f}",
    }


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write("usage: check-atomic-publish.py <log.csv>\n")
        return 2
    path = sys.argv[1]

    tears: list[TearReport] = []
    rows_checked = 0
    rows_with_data = 0

    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        if "vnTimeStartupNs" not in (reader.fieldnames or []):
            sys.stderr.write(
                "error: CSV has no vnTimeStartupNs column — not a VN-300 log\n")
            return 2

        for row_idx, row in enumerate(reader):
            rows_checked += 1
            had_data, row_tears = check_row(row_idx, row)
            if had_data:
                rows_with_data += 1
            tears.extend(row_tears)

    print(f"Rows scanned: {rows_checked}")
    print(f"Rows with stim data (vnTimeStartupNs != 0): {rows_with_data}")
    print(f"Tears detected: {len(tears)}")

    if rows_with_data == 0:
        sys.stderr.write(
            "\nerror: no rows had non-zero vnTimeStartupNs — was the firehose "
            "running with --epoch-encode during this log?\n")
        return 2

    if tears:
        # Show first 20 to keep output bounded.
        print("\nFirst tear reports (row, canonical N, mismatched field, decoded N):")
        for t in tears[:20]:
            print(f"  row {t.row:6d}  N={t.canonical_N:10d}  "
                  f"field={t.field:<14}  decoded_N={t.decoded_N}")
        if len(tears) > 20:
            print(f"  ... and {len(tears) - 20} more")
        return 1

    print("\nOK: no tears detected. Atomic publish is working.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
