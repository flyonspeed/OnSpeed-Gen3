---
name: perf-testing
description: Use when generating a performance report for a release, capturing a perf baseline, comparing perf across firmware versions, investigating a suspected perf regression, or running the standardized bench-PERF capture procedure. Run at every release tag, and any time someone says "perf regression", "perf baseline", "is this slower than X", or asks for CPU-budget numbers.
---

# OnSpeed Performance Testing

## Overview

PR #605 added live PERF telemetry to V4P firmware. This skill turns those live numbers into a **stable artifact** — a Markdown report comparable across firmware versions via plain `diff`.

The point: when someone asks "did v4.24 get slower than v4.23?", you should be able to answer with two `.md` files and `diff`, not by re-flying and squinting at noisy single-snapshot output.

The tool: `tools/perf-report/capture_perf_report.py`. The output: `docs/perf-reports/<label>.md`.

## When to Use

- **Every release tag.** Capture a baseline named after the tag.
- "Is feature X a perf regression?" — capture before/after, diff.
- "How much CPU does EKFQ take?" — full snapshot is the answer.
- "Will 833 Hz IMU fit?" — capture at 208, look at task headroom, extrapolate.

**Don't use for** one-off curiosity numbers from a single snapshot — `perf dump` on the device console is faster. Use this when you want a comparable, durable artifact.

## When NOT to Use

- Unit-test perf changes — those go through native tests + benchmarks
- Quick "is the box working" checks — `perf dump` (one shot) is faster
- Without PERF telemetry firmware (the `esp32s3-v4p-perf` PIO env) — the script will fail to parse and exit 2

## The Capture Procedure

The numbers are only comparable if the **bench conditions** match. Always:

1. **Flash the perf firmware:** `pio run -e esp32s3-v4p-perf -t upload`. Production V4P does NOT include PERF.
2. **SD card inserted.** Without it, Log task and SD-write subsystem numbers are wrong by ~25% of one core.
3. **Decide WiFi state.** AP up with 0 clients vs. AP up with 1 client connected are different captures. Pick one and record it (`--wifi-clients N`).
4. **No M5 attached** unless you want UART-TX overhead included (default: no).
5. **At least 60 seconds.** One bad SD-pause cycle in a 15-second capture poisons the median. 60 seconds = 60 snapshots, the median is stable.
6. **Same physical hardware.** V4P vs V4B has different SPI pin maps; cross-board comparisons are nonsense.

## Quick Reference

```bash
# Capture a release baseline:
cd tools/perf-report
uv run ./capture_perf_report.py --label v4.24-baseline

# With explicit conditions when defaults aren't right:
uv run ./capture_perf_report.py \
    --label v4.24-with-wifi-client \
    --duration 120 \
    --wifi-clients 1 \
    --port /dev/cu.usbserial-310

# Re-parse a previously-captured raw serial dump:
uv run ./capture_perf_report.py --label re-analyze --from-file /tmp/perf_capture.txt

# Compare two reports:
diff -u docs/perf-reports/v4.23-baseline.md docs/perf-reports/v4.24-baseline.md
```

## What the Report Contains

A stable Markdown schema with six sections (per the smoke-test in `docs/perf-reports/`):

1. **Capture conditions** — git sha, build env, hardware, duration, SD/WiFi/M5/IMU/log state, free-form notes
2. **Per-task CPU** — `Loops/s | Avg | p50 | p95 | p99 | Max | Stack | Drops` across all instrumented FreeRTOS tasks. Values are `median (min..max)`.
3. **Per-subsystem timing** — EKFQ predict/correct/alpha, log_write, log_sync, efis_read, boom_read, etc.
4. **SPI bus** — bytes/sec, xfers/sec, max single-transfer µs by chip-select.
5. **System health** — heap free, heap min since boot, largest free block.

The schema is *append-only*. Adding new tasks/subsystems shows up as new rows in the table. **We never rename or reorder columns** — that would break `diff` against historical reports.

## Filename Convention

`docs/perf-reports/<label>.md`. Suggested labels:

- `v4.23-baseline` — captured at a release tag
- `v4.23-with-wifi-client` — same firmware, different bench conditions
- `pr-NNN-before` and `pr-NNN-after` — before/after a perf-sensitive PR
- `eskfq-sparse-h-2026-05-21` — capture for a specific experiment

## How to Diff

```bash
diff -u docs/perf-reports/before.md docs/perf-reports/after.md
```

Look for:

- **Regressions in `Avg µs` for hot tasks** — Imu, Log, EKFQ. A 10%+ jump in any of these is significant.
- **New tail behavior in `p99` / `Max`** — Max can be one-shot noise (SD wear), p99 changing is structural.
- **`Drops` going non-zero** — a producer ring overflowed. Means the consumer drain rate isn't keeping up; investigate.
- **Heap min dropping** — something is allocating more (or a slow leak).
- **Stack free going down** — task is closer to OOM.

## Common Mistakes

| Mistake | Fix |
|---|---|
| Capturing without SD card | Reflash, insert SD, redo |
| 15-second capture | Bump to 60s — single bad cycle poisons short captures |
| Comparing V4P to V4B | Don't. They're different SPI maps. |
| Comparing perf-build to production-build | Don't. Production doesn't have PERF; nothing to capture. |
| Reading only `Max` | Look at p50/p95 first. Max is often a one-shot SD wear pause. |
| No conditions in the label | The label IS the conditions; future-you needs to know what was running. |

## When You Need More Detail

The script aggregates median across snapshots. If you need the raw per-snapshot data:

- The raw serial capture is whatever the python script read. Capture it independently first if you want it:
  ```bash
  python3 -c "
  import serial, time, sys
  s = serial.Serial('/dev/cu.usbserial-310', 921600, timeout=0.5)
  time.sleep(2); s.read(8192)
  s.write(b'perf on\\r\\n'); s.flush()
  end = time.time() + 60
  while time.time() < end:
      chunk = s.read(8192)
      if chunk: sys.stdout.buffer.write(chunk)
  s.write(b'perf off\\r\\n')
  " > /tmp/perf_raw.txt
  ```
- Then run the script with `--from-file /tmp/perf_raw.txt` to generate the aggregated report. You keep both the raw bytes and the report.

## Where Reports Live

`docs/perf-reports/` is checked in. Treat reports like release notes: one per release tag, with optional follow-ups for specific PRs or experiments. The directory will grow over time — that's fine, they're small.

## Synthetic Max-Load Capture (perf-synth env)

The default perf env (`esp32s3-v4p-perf`) is captured WITHOUT a real VN-300 or boom probe attached, so `subsys.efis_read` / `subsys.boom_read` read 0 and the log row stays narrow. The numbers understate the real "all sensors present" load.

A separate env, `esp32s3-v4p-perf-synth`, replaces those byte sources with synthetic Streams that emit fixed valid frames at the protocol's native rate. The parser, `applyVn300Data()` copies, log-row expansion, and WebSocket fanout all do their real work. See issue #607 for the rationale.

When to use it:

- Sizing headroom for "can we move IMU to 833 Hz with VN-300 attached?"
- Sanity-checking that a PR's perf impact doesn't differ at full sensor load.
- Establishing a stable "max load" baseline alongside each release's normal baseline.

When NOT to use it:

- For the normal release baseline. The published baseline reflects real-world install conditions; the synth is a companion capture for headroom planning.

Capture procedure (label `-synth-vn300` or `-synth-skyview`):

```bash
pio run -e esp32s3-v4p-perf-synth -t upload
# defaults to VN-300; for SkyView, add -DONSPEED_SYNTH_EFIS_SKYVIEW=1 to build_flags
cd tools/perf-report
uv run ./capture_perf_report.py --label v4.24-synth-vn300
```

Bench conditions: same as a normal perf capture (SD card in, no M5 unless intentional, WiFi state recorded). On the synth binary, `synth status` on the device console reports frames/bytes emitted since boot (sanity check: ~50 frames/sec for VN-300, ~50/sec for boom, ~20/sec for SkyView with the `!1`+`!3` alternation).

Diff against the real-sensor baseline:

```bash
diff -u docs/perf-reports/v4.24-baseline.md \
        docs/perf-reports/v4.24-synth-vn300.md
```

Expected deltas (from #607's prototype):

- `subsys.efis_read` and `subsys.boom_read` appear with non-zero `n`.
- `subsys.synth_build` shows a small overhead (≪1 ms/sec) — confirms the synth-emission cost is attributable and not silently inflating the parser numbers.
- `task=Log` grows substantially (`+54%` total/sec in the prototype).
- `task=Imu` grows modestly (`+28%` in the prototype).

## Refs

- PR #605 — PERF telemetry implementation (the runtime side this skill wraps)
- PR #606 — fast CSV formatters (first measurable perf win after PERF landed)
- Issue #607 — Synthetic VN-300 + boom inject (the perf-synth env is the answer)
- `software/Libraries/onspeed_core/src/util/Perf.h` — instrumentation API + idiom doc
- `software/Libraries/onspeed_core/src/test_frames/SynthFrames.h` — synth byte tables
- `local-plans/PLAN_IMU_HIGHRATE_VN300_PARITY.md` — why this matters for the IMU-rate push
