#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "pyserial>=3.5",
# ]
# ///
"""
capture_perf_report.py — capture a standardized PERF report from V4P.

What this does
==============

Reads the PERF telemetry stream off USB serial (firmware built with
`pio run -e esp32s3-v4p-perf`, then `perf on` sent over the console),
accumulates N seconds of `==== PERF ====` snapshots, and writes a
Markdown report with a stable schema. The report is comparable across
firmware versions / releases via plain `diff`.

The output schema is stable — we add columns at the bottom, never
reorder, never rename. Old reports diff cleanly against new ones.

Why this exists
===============

Single-snapshot PERF output is noisy: one bad SD-write-pause cycle in
a 15-second capture looks like 'EKFQ correct got 3x slower'. Aggregating
across N>=30 snapshots and reporting median + p95 + range pins down
typical behavior vs tail events.

The capture has to be reproducible too — bench conditions matter.
This script enforces a known procedure (SD card in, WiFi state known,
no M5 unless flagged) and embeds the conditions in the report so two
reports from different days are diff-comparable.

Usage
=====

  uv run ./capture_perf_report.py --label v4.23-baseline

  # With explicit conditions:
  uv run ./capture_perf_report.py \\
      --label v4.23-baseline \\
      --duration 60 \\
      --wifi-clients 0 \\
      --sd-card true \\
      --m5-attached false \\
      --port /dev/cu.usbserial-310

  # Diff two reports:
  diff -u docs/perf-reports/v4.23-baseline.md \\
          docs/perf-reports/v4.24-baseline.md

Output: docs/perf-reports/<label>.md
"""

from __future__ import annotations

import argparse
import dataclasses
import io
import os
import re
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path
from typing import Optional

import serial


# ---------------------------------------------------------------------------
# Snapshot parsing
# ---------------------------------------------------------------------------


SNAPSHOT_HEADER = "==== PERF ===="
# Regex pieces — the firmware emits these in a fixed format.
TASK_RE = re.compile(
    r"task=(?P<name>\S+)\s+loops=\s*(?P<loops>\d+)\s+"
    r"p50=\s*(?P<p50>\d+)us\s+p95=\s*(?P<p95>\d+)us\s+p99=\s*(?P<p99>\d+)us\s+"
    r"max=\s*(?P<max>\d+)us\s+avg=\s*(?P<avg>\d+)us\s+"
    r"stack_free=\s*(?P<stack>\d+)w\s+drops=(?P<drops>\d+)"
)
SUBSYS_RE = re.compile(
    r"subsys\.(?P<name>\S+)\s+n=\s*(?P<n>\d+)\s+total=\s*(?P<total>\d+)us\s+"
    r"p50=\s*(?P<p50>\d+)us\s+p95=\s*(?P<p95>\d+)us\s+p99=\s*(?P<p99>\d+)us\s+"
    r"max=\s*(?P<max>\d+)us"
)
SPI_RE = re.compile(
    r"(?P<name>spi\.\S+)\s+bytes=\s*(?P<bytes>\d+)\s+xfers=\s*(?P<xfers>\d+)\s+"
    r"max_xfer=\s*(?P<max_xfer>\d+)us"
)
HEAP_RE = re.compile(
    r"heap free=(?P<free>\d+)\s+min=(?P<min>\d+)\s+largest_block=(?P<largest>\d+)"
)


@dataclasses.dataclass
class TaskSnapshot:
    loops: int
    p50: int
    p95: int
    p99: int
    max: int
    avg: int
    stack: int
    drops: int


@dataclasses.dataclass
class SubsysSnapshot:
    n: int
    total: int
    p50: int
    p95: int
    p99: int
    max: int


@dataclasses.dataclass
class SpiSnapshot:
    bytes: int
    xfers: int
    max_xfer: int


def parse_snapshot_block(lines: list[str]) -> dict:
    """Parse a single ==== PERF ==== block into a dict."""
    snapshot = {
        "tasks": {},
        "subsys": {},
        "spi": {},
        "heap": None,
    }
    for line in lines:
        if m := TASK_RE.search(line):
            snapshot["tasks"][m["name"]] = TaskSnapshot(
                loops=int(m["loops"]), p50=int(m["p50"]), p95=int(m["p95"]),
                p99=int(m["p99"]), max=int(m["max"]), avg=int(m["avg"]),
                stack=int(m["stack"]), drops=int(m["drops"]),
            )
        elif m := SUBSYS_RE.search(line):
            snapshot["subsys"][m["name"]] = SubsysSnapshot(
                n=int(m["n"]), total=int(m["total"]),
                p50=int(m["p50"]), p95=int(m["p95"]),
                p99=int(m["p99"]), max=int(m["max"]),
            )
        elif m := SPI_RE.search(line):
            snapshot["spi"][m["name"]] = SpiSnapshot(
                bytes=int(m["bytes"]), xfers=int(m["xfers"]),
                max_xfer=int(m["max_xfer"]),
            )
        elif m := HEAP_RE.search(line):
            snapshot["heap"] = {
                "free": int(m["free"]), "min": int(m["min"]),
                "largest": int(m["largest"]),
            }
    return snapshot


def split_snapshots(raw_text: str) -> list[list[str]]:
    """Split raw serial capture into per-snapshot line lists."""
    blocks = []
    current: Optional[list[str]] = None
    for line in raw_text.splitlines():
        if SNAPSHOT_HEADER in line:
            if current is not None:
                blocks.append(current)
            current = []
        elif current is not None:
            current.append(line)
    if current is not None:
        blocks.append(current)
    return blocks


# ---------------------------------------------------------------------------
# Aggregation across snapshots
# ---------------------------------------------------------------------------


def aggregate_int_field(values: list[int]) -> dict:
    """Return median/min/max/p95 for a list of ints. Missing => None."""
    if not values:
        return {"median": None, "min": None, "max": None, "p95": None}
    sorted_v = sorted(values)
    n = len(sorted_v)
    p95_idx = min(n - 1, int(n * 0.95))
    return {
        "median": statistics.median(sorted_v),
        "min": min(sorted_v),
        "max": max(sorted_v),
        "p95": sorted_v[p95_idx],
    }


def aggregate_snapshots(snapshots: list[dict]) -> dict:
    """Aggregate across N snapshots. Returns the structure the report uses."""
    if not snapshots:
        return {"tasks": {}, "subsys": {}, "spi": {}, "heap": None,
                "snapshot_count": 0}

    task_names = sorted({n for s in snapshots for n in s["tasks"]})
    subsys_names = sorted({n for s in snapshots for n in s["subsys"]})
    spi_names = sorted({n for s in snapshots for n in s["spi"]})

    agg = {
        "tasks": {},
        "subsys": {},
        "spi": {},
        "snapshot_count": len(snapshots),
    }

    for name in task_names:
        per_field = defaultdict(list)
        for s in snapshots:
            if t := s["tasks"].get(name):
                per_field["loops"].append(t.loops)
                per_field["p50"].append(t.p50)
                per_field["p95"].append(t.p95)
                per_field["p99"].append(t.p99)
                per_field["max"].append(t.max)
                per_field["avg"].append(t.avg)
                per_field["stack"].append(t.stack)
                per_field["drops"].append(t.drops)
        agg["tasks"][name] = {k: aggregate_int_field(v) for k, v in per_field.items()}

    for name in subsys_names:
        per_field = defaultdict(list)
        for s in snapshots:
            if t := s["subsys"].get(name):
                per_field["n"].append(t.n)
                per_field["total"].append(t.total)
                per_field["p50"].append(t.p50)
                per_field["p95"].append(t.p95)
                per_field["p99"].append(t.p99)
                per_field["max"].append(t.max)
        agg["subsys"][name] = {k: aggregate_int_field(v) for k, v in per_field.items()}

    for name in spi_names:
        per_field = defaultdict(list)
        for s in snapshots:
            if t := s["spi"].get(name):
                per_field["bytes"].append(t.bytes)
                per_field["xfers"].append(t.xfers)
                per_field["max_xfer"].append(t.max_xfer)
        agg["spi"][name] = {k: aggregate_int_field(v) for k, v in per_field.items()}

    heap_values = [s["heap"] for s in snapshots if s["heap"]]
    if heap_values:
        agg["heap"] = {
            "free":    aggregate_int_field([h["free"] for h in heap_values]),
            "min":     aggregate_int_field([h["min"] for h in heap_values]),
            "largest": aggregate_int_field([h["largest"] for h in heap_values]),
        }

    return agg


# ---------------------------------------------------------------------------
# Serial capture
# ---------------------------------------------------------------------------


def capture_serial(port: str, baud: int, duration_sec: int,
                   verbose: bool = False) -> str:
    """Open the port, send `perf on`, capture duration_sec of output."""
    ser = serial.Serial(port, baud, timeout=0.5)
    print(f"# connected: {port} @ {baud}", file=sys.stderr)
    # Drain boot banner briefly.
    deadline = time.time() + 2.0
    while time.time() < deadline:
        ser.read(8192)

    print("# sending: perf on", file=sys.stderr)
    ser.write(b"perf on\r\n")
    ser.flush()

    print(f"# capturing {duration_sec}s of PERF output", file=sys.stderr)
    start = time.time()
    buf = b""
    last_report = 0
    while time.time() - start < duration_sec:
        chunk = ser.read(8192)
        if chunk:
            buf += chunk
        if verbose:
            elapsed = int(time.time() - start)
            if elapsed != last_report and elapsed % 10 == 0:
                last_report = elapsed
                snaps = buf.decode(errors="replace").count(SNAPSHOT_HEADER)
                print(f"# t={elapsed}s captured {len(buf)} B, {snaps} snapshots",
                      file=sys.stderr)

    print("# sending: perf off", file=sys.stderr)
    ser.write(b"perf off\r\n")
    ser.flush()
    time.sleep(0.3)
    buf += ser.read(8192)
    ser.close()
    return buf.decode(errors="replace")


# ---------------------------------------------------------------------------
# Report writer — the stable schema
# ---------------------------------------------------------------------------


def fmt_int(v) -> str:
    if v is None:
        return "—"
    return f"{v:,}"


def fmt_range(field: dict) -> str:
    """Format median (min..max) for a field aggregation."""
    if field.get("median") is None:
        return "—"
    med = int(field["median"])
    lo = field["min"]
    hi = field["max"]
    if lo == hi:
        return f"{med:,}"
    return f"{med:,} ({lo:,}..{hi:,})"


def fmt_pct(us: float) -> str:
    """Format µs/sec as a CPU% of one core. 10000 µs = 1%."""
    if us is None:
        return "—"
    return f"{us / 10000.0:5.2f}%"


# Task → core mapping for OnSpeed Gen3. Derived from xTaskCreatePinnedToCore
# call sites in the firmware. Used to roll up per-core CPU% totals.
TASK_CORE = {
    "Imu":          1,
    "Sensors":      1,
    "Audio":        1,
    "Display":      1,
    "Switch":       1,
    "Housekeeping": 1,
    "LogReplay":    1,   # bench-mode replay variant
    "TestPot":      1,   # bench-mode variant
    "RangeSweep":   1,   # bench-mode variant
    "ArduinoLoop":  1,   # Arduino's loop() task default core
    "Log":          0,   # LogSensorCommit
    "WebServer":    0,
    "DataServer":   0,
    "EfisRead":     0,   # PR #622 — dedicated EFIS UART task on Core 0
    "BoomRead":     0,   # PR #622 — dedicated boom UART task on Core 0
}

# Tasks whose PerfLoop scope includes blocking / sleep time, not just
# CPU work. For these, the "loops × avg" CPU% is OVERSTATED — it
# reflects the wake-to-wake period, not actual CPU consumption.
#
# These tasks need their PerfLoop placement fixed (issue #611), or
# subsystem-level CPU% (the rows in the per-subsystem table) is the
# honest number to look at.
TASKS_WITH_BLOCKING_IN_SCOPE = {
    "Log",          # PerfLoop wraps xRingbufferReceive (100ms timeout)
                    # — see issue #611 finding 1
    "ArduinoLoop",  # loop() has no explicit sleep; period is dominated
                    # by preemption from higher-priority tasks (Audio @
                    # prio 6, IMU @ 5). Real work is the inner
                    # efis_read + boom_read subsystems (~0.2% total).
}


def task_cpu_us_per_sec(t: dict) -> float | None:
    """Per-task µs/sec consumed = loops × avg. None if either missing."""
    loops = t.get("loops", {}).get("median")
    avg   = t.get("avg",   {}).get("median")
    if loops is None or avg is None:
        return None
    return float(loops) * float(avg)


def get_git_sha(repo_dir: Path) -> str:
    try:
        out = subprocess.check_output(
            ["git", "-C", str(repo_dir), "rev-parse", "--short=8", "HEAD"],
            text=True, stderr=subprocess.DEVNULL,
        ).strip()
        dirty = subprocess.check_output(
            ["git", "-C", str(repo_dir), "status", "--porcelain"],
            text=True, stderr=subprocess.DEVNULL,
        ).strip()
        return f"{out}{'-dirty' if dirty else ''}"
    except Exception:
        return "unknown"


def get_git_branch(repo_dir: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(repo_dir), "branch", "--show-current"],
            text=True, stderr=subprocess.DEVNULL,
        ).strip() or "(detached)"
    except Exception:
        return "unknown"


def write_report(agg: dict, args: argparse.Namespace, out_path: Path,
                 repo_dir: Path) -> None:
    """Render the aggregated report to Markdown with the stable schema."""
    lines = []
    lines.append(f"# OnSpeed PERF Report — {args.label}")
    lines.append("")
    lines.append("> Generated by `tools/perf-report/capture_perf_report.py`. "
                 "Stable schema — diff this file against other PERF reports.")
    lines.append("")

    # --- Capture conditions ---------------------------------------------------
    lines.append("## Capture conditions")
    lines.append("")
    lines.append(f"- **Label:** {args.label}")
    lines.append(f"- **Captured:** {time.strftime('%Y-%m-%d %H:%M:%S %z')}")
    lines.append(f"- **Git commit:** {get_git_sha(repo_dir)} "
                 f"(branch {get_git_branch(repo_dir)})")
    lines.append(f"- **Build env:** {args.build_env}")
    lines.append(f"- **Hardware:** {args.hardware}")
    lines.append(f"- **Duration:** {args.duration}s "
                 f"({agg['snapshot_count']} snapshots aggregated)")
    lines.append(f"- **SD card:** {'in' if args.sd_card else 'not in'}")
    lines.append(f"- **WiFi clients:** {args.wifi_clients}")
    lines.append(f"- **M5 attached:** {'yes' if args.m5_attached else 'no'}")
    lines.append(f"- **IMU rate (configured):** {args.imu_rate_hz} Hz")
    lines.append(f"- **Log rate (configured):** {args.log_rate_hz} Hz")
    if args.notes:
        lines.append(f"- **Notes:** {args.notes}")
    lines.append("")

    # --- Core summary (CPU% rollup) ------------------------------------------
    # Computed from per-task `loops × avg` for every instrumented task,
    # mapped to its FreeRTOS-pinned core via TASK_CORE. This is the
    # number to watch when sizing future features / IMU rate pushes.
    # Two accumulators per core:
    #  - reliable: tasks whose PerfLoop scope honestly bounds CPU work
    #  - blocking_excluded: tasks where the scope includes blocking time
    #    (Log, ArduinoLoop) — these are reported separately because
    #    summing them into Core% would be misleading.
    core_us = {0: 0.0, 1: 0.0}
    blocking_us = {0: 0.0, 1: 0.0}
    unknown_tasks = []
    for name, t in agg["tasks"].items():
        us = task_cpu_us_per_sec(t)
        if us is None:
            continue
        core = TASK_CORE.get(name)
        if core is None:
            unknown_tasks.append(name)
            continue
        if name in TASKS_WITH_BLOCKING_IN_SCOPE:
            blocking_us[core] += us
        else:
            core_us[core] += us

    lines.append("## Core summary (CPU% rollup)")
    lines.append("")
    lines.append("One core = 1,000,000 µs/sec. **Used (reliable)** sums "
                 "CPU% of every instrumented task whose scope honestly "
                 "bounds work (excludes vTaskDelay / xRingbufferReceive "
                 "blocking time). **Headroom** is what's left for new "
                 "features at the current bench configuration.")
    lines.append("")
    lines.append("Tasks where the PerfLoop scope still includes blocking "
                 "time are reported separately as **Blocking-included** — "
                 "their `loops × avg` figure overstates real CPU consumption "
                 "and shouldn't be summed into Used. For honest CPU% of those "
                 "tasks, look at their subsystem-level scopes (e.g. "
                 "`log_write` + `log_sync` for the Log task). See issue #611 "
                 "for the planned scope reshape.")
    lines.append("")
    lines.append("| Core | Used (reliable) | Headroom | Blocking-included |")
    lines.append("|---|---:|---:|---:|")
    for core in (0, 1):
        used_pct = core_us[core] / 10000.0
        headroom_pct = 100.0 - used_pct
        blocking_pct = blocking_us[core] / 10000.0
        lines.append(
            f"| Core {core} | {used_pct:5.2f}% | {headroom_pct:5.2f}% "
            f"| {blocking_pct:5.2f}% |"
        )
    if unknown_tasks:
        lines.append("")
        lines.append(f"> ⚠ Tasks not mapped to a core (update TASK_CORE in "
                     f"capture_perf_report.py): {', '.join(unknown_tasks)}")
    lines.append("")

    # --- Per-task CPU --------------------------------------------------------
    lines.append("## Per-task CPU (work-only)")
    lines.append("")
    lines.append("Values are median across all snapshots, with (min..max) "
                 "shown in parens when the range is non-trivial. `loops/s` is "
                 "actual measured iterations per second (not the configured "
                 "rate). **`CPU%`** is `loops × avg ÷ 10,000` — fraction of "
                 "one core consumed by this task's instrumented work.")
    lines.append("")
    lines.append("| Task | Core | Loops/s | Avg µs | **CPU%** | p50 | p95 | p99 | Max | Stack free | Drops |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    # Sort by CPU% descending so the optimization-worthy ones come first.
    sorted_tasks = sorted(
        agg["tasks"].items(),
        key=lambda kv: -(task_cpu_us_per_sec(kv[1]) or 0)
    )
    for name, t in sorted_tasks:
        core = TASK_CORE.get(name, "?")
        cpu_us = task_cpu_us_per_sec(t)
        # Mark CPU% with † for tasks whose scope includes blocking time —
        # the number overstates real CPU consumption.
        suffix = "†" if name in TASKS_WITH_BLOCKING_IN_SCOPE else ""
        lines.append(
            f"| {name} "
            f"| {core} "
            f"| {fmt_range(t['loops'])} "
            f"| {fmt_range(t['avg'])} "
            f"| **{fmt_pct(cpu_us)}{suffix}** "
            f"| {fmt_range(t['p50'])} "
            f"| {fmt_range(t['p95'])} "
            f"| {fmt_range(t['p99'])} "
            f"| {fmt_range(t['max'])} "
            f"| {fmt_range(t['stack'])} "
            f"| {fmt_range(t['drops'])} |"
        )
    if any(n in TASKS_WITH_BLOCKING_IN_SCOPE for n in agg["tasks"]):
        lines.append("")
        lines.append("> † PerfLoop scope includes blocking time; CPU% is "
                     "overstated. See subsystem-level scopes for honest cost.")
    lines.append("")

    # --- Per-subsystem timing -----------------------------------------------
    lines.append("## Per-subsystem timing")
    lines.append("")
    lines.append("**`CPU%`** is `total ÷ 10,000` — fraction of one core "
                 "spent in this subsystem per second. Sorted high to low. "
                 "Subsystems nested inside a task contribute to that task's "
                 "CPU% above (not additive — these are slices of, not extras "
                 "on top of, the task totals).")
    lines.append("")
    lines.append("| Subsystem | Calls/s | Total/s µs | **CPU%** | p50 | p95 | p99 | Max |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    sorted_subsys = sorted(
        agg["subsys"].items(),
        key=lambda kv: -(kv[1].get("total", {}).get("median") or 0)
    )
    for name, s in sorted_subsys:
        total_us = s.get("total", {}).get("median")
        lines.append(
            f"| {name} "
            f"| {fmt_range(s['n'])} "
            f"| {fmt_range(s['total'])} "
            f"| **{fmt_pct(total_us)}** "
            f"| {fmt_range(s['p50'])} "
            f"| {fmt_range(s['p95'])} "
            f"| {fmt_range(s['p99'])} "
            f"| {fmt_range(s['max'])} |"
        )
    lines.append("")

    # --- SPI bus -------------------------------------------------------------
    if agg["spi"]:
        lines.append("## SPI bus")
        lines.append("")
        lines.append("| Bus | Bytes/s | Xfers/s | Max xfer µs |")
        lines.append("|---|---:|---:|---:|")
        for name in sorted(agg["spi"]):
            s = agg["spi"][name]
            lines.append(
                f"| {name} "
                f"| {fmt_range(s['bytes'])} "
                f"| {fmt_range(s['xfers'])} "
                f"| {fmt_range(s['max_xfer'])} |"
            )
        lines.append("")

    # --- System health -------------------------------------------------------
    if agg.get("heap"):
        lines.append("## System health")
        lines.append("")
        h = agg["heap"]
        lines.append(f"- **Heap free:** {fmt_range(h['free'])} bytes")
        lines.append(f"- **Heap min since boot:** {fmt_range(h['min'])} bytes")
        lines.append(f"- **Largest free block:** {fmt_range(h['largest'])} bytes")
        lines.append("")

    out_path.write_text("\n".join(lines))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    p = argparse.ArgumentParser(
        description="Capture a standardized PERF report from V4P.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--label", required=True,
                   help="Short identifier (e.g. 'v4.23-baseline'). "
                        "Used as the output filename.")
    p.add_argument("--port", default="/dev/cu.usbserial-310",
                   help="USB serial device path")
    p.add_argument("--baud", type=int, default=921600)
    p.add_argument("--duration", type=int, default=60,
                   help="Capture duration in seconds (default 60).")
    p.add_argument("--output-dir", default="docs/perf-reports",
                   help="Where the .md report lands.")
    p.add_argument("--from-file",
                   help="Skip capture; read serial output from this file. "
                        "Useful for re-parsing past captures.")

    # Capture conditions (recorded in the report header).
    p.add_argument("--build-env", default="esp32s3-v4p-perf")
    p.add_argument("--hardware", default="V4P")
    p.add_argument("--sd-card", type=lambda x: x.lower() in ("y", "yes", "true", "1"),
                   default=True, help="SD card in / not in")
    p.add_argument("--wifi-clients", type=int, default=0)
    p.add_argument("--m5-attached", type=lambda x: x.lower() in ("y", "yes", "true", "1"),
                   default=False)
    p.add_argument("--imu-rate-hz", type=int, default=208)
    p.add_argument("--log-rate-hz", type=int, default=208)
    p.add_argument("--notes", default=None)

    p.add_argument("--verbose", "-v", action="store_true")
    args = p.parse_args()

    # Locate the repo root from this script's location.
    here = Path(__file__).resolve().parent
    repo_dir = here.parent.parent  # tools/perf-report → ..  →  repo root
    out_dir = repo_dir / args.output_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{args.label}.md"

    # Capture or load.
    if args.from_file:
        raw = Path(args.from_file).read_text(errors="replace")
        print(f"# loaded {len(raw)} bytes from {args.from_file}", file=sys.stderr)
    else:
        raw = capture_serial(args.port, args.baud, args.duration,
                             verbose=args.verbose)

    # Parse + aggregate.
    snapshots = [parse_snapshot_block(b) for b in split_snapshots(raw)]
    if not snapshots:
        print("ERROR: no PERF snapshots parsed. Is the firmware perf-build?",
              file=sys.stderr)
        return 2
    print(f"# parsed {len(snapshots)} snapshots", file=sys.stderr)

    # Skip the first snapshot — it usually has partial counters from before
    # `perf on` had stabilized.
    if len(snapshots) > 2:
        snapshots = snapshots[1:]

    agg = aggregate_snapshots(snapshots)
    write_report(agg, args, out_path, repo_dir)

    print(f"# wrote {out_path}", file=sys.stderr)
    print(out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
