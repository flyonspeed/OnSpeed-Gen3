#!/usr/bin/env python3
"""check_snapshot_sanity.py — torn-read / impossible-jump scan for the columns
fed by the AHRS / Sensor / IMU lock-free snapshots (PRs #669/#670/#671).

The existing check-atomic-publish.py validates the EFIS (VN-300) snapshot via
the --epoch-encode counter. This complements it for the NEW snapshots, which
carry physical flight quantities (no encoded counter to diff). It can't prove
byte-equivalence against pre-migration firmware; what it CAN catch is the
fingerprint of a torn snapshot read: a single column making a physically
impossible one-sample excursion (jump out and back within one row) while it's
logged at a fixed cadence.

Heuristic, not a proof: it flags one-sample spikes that exceed a generous
physical bound AND revert immediately (the torn-read shape), per column group:
  - IMU accel:  ForwardG/LateralG/VerticalG   (g_ImuSnapshot)
  - IMU gyro:   RollRate/PitchRate/YawRate     (g_ImuSnapshot)
  - sensor:     IAS, AngleofAttack, Palt       (g_SensorSnapshot)
  - ahrs:       Pitch, Roll, DerivedAOA, VSI   (g_AhrsSnapshot)

Header-driven: finds columns by name, tolerates reorder/absence.

Usage:  uv run tools/bench/check_snapshot_sanity.py path/to/log_NNN.csv
Exit:   0 = no suspicious one-sample reversions; 1 = N flagged; 2 = bad CSV.
"""
import sys, csv

# Column -> (max plausible |one-sample delta|, human label). Deltas are per
# single 50/208/416 Hz step; bounds are deliberately generous so only a torn
# read (not aggressive maneuvering) trips them.
GROUPS = {
    "IMU accel (g)":   {"ForwardG": 4.0, "LateralG": 4.0, "VerticalG": 6.0},
    "IMU gyro (dps)":  {"RollRate": 400.0, "PitchRate": 400.0, "YawRate": 400.0},
    "sensor":          {"IAS": 50.0, "AngleofAttack": 30.0, "Palt": 2000.0},
    "ahrs":            {"Pitch": 45.0, "Roll": 60.0, "DerivedAOA": 30.0},
}

def main(path):
    try:
        with open(path, newline="") as f:
            rows = list(csv.reader(f))
    except OSError as e:
        print(f"cannot read {path}: {e}"); return 2
    if len(rows) < 4:
        print("CSV too short"); return 2
    header = rows[0]
    idx = {name: i for i, name in enumerate(header)}

    def col(name):
        j = idx.get(name)
        if j is None:
            return None
        out = []
        for r in rows[1:]:
            if j < len(r) and r[j] not in ("", None):
                try: out.append(float(r[j]))
                except ValueError: out.append(None)
            else:
                out.append(None)
        return out

    total_flags = 0
    checked = []
    for group, cols in GROUPS.items():
        for name, bound in cols.items():
            series = col(name)
            if series is None:
                continue
            checked.append(name)
            flags = 0
            # torn-read shape: |x[i]-x[i-1]| > bound AND |x[i]-x[i+1]| > bound
            # AND |x[i-1]-x[i+1]| <= bound  (spikes out for exactly one sample,
            # neighbors agree). That's the snapshot-tear fingerprint, distinct
            # from a real fast transient (which doesn't revert in one sample).
            for i in range(1, len(series) - 1):
                a, b, c = series[i-1], series[i], series[i+1]
                if a is None or b is None or c is None:
                    continue
                if abs(b - a) > bound and abs(b - c) > bound and abs(a - c) <= bound:
                    flags += 1
                    if flags <= 3:
                        print(f"  [{group}] {name}: row {i+2} spike "
                              f"{a:.3f} -> {b:.3f} -> {c:.3f} (bound {bound})")
            if flags:
                print(f"  [{group}] {name}: {flags} one-sample reversions flagged")
                total_flags += flags

    if not checked:
        print("no recognized snapshot columns found in header"); return 2
    print(f"\nchecked columns: {', '.join(checked)}")
    print(f"total one-sample reversions flagged: {total_flags}")
    if total_flags == 0:
        print("OK — no torn-read fingerprints in snapshot-fed columns")
        return 0
    print("REVIEW — flagged spikes; inspect rows (could be torn read OR a real "
          "sensor glitch — correlate with imu_late/drops in the .dbg)")
    return 1

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__); sys.exit(2)
    sys.exit(main(sys.argv[1]))
