---
name: bench-stress
description: Use when the user asks to run the bench stress test, hammer the SD writer / web handlers, verify a PR doesn't regress data-logging reliability before merge, or investigate suspected ring drops / paused_drops / mutex starvation under load. Pair with any PR that touches LogSensor, ApiHandlers, ConfigWebServer, DataServer, or the SD code paths.
---

# OnSpeed Bench Stress Test

## Overview

OnSpeed's hardest-to-CI failures are the ones that only show up when the IMU is feeding the ring at 208 Hz, the writer is fighting for `xWriteMutex`, the WiFi is sending WebSocket frames, and a pilot is clicking buttons on the web UI all at once. The unit tests can't see this. The host regression harness can't see this. The only thing that catches it is plugging a real V4P into a real SD card and putting it under realistic load while watching serial and inspecting the resulting log files.

This skill is the **manual, repeatable protocol** for that test, plus the cleanup analysis after. The script lives at `tools/bench/stress_web_handlers.py`. The protocol covers the bench session end-to-end: flash, monitor, drive, observe, post-mortem.

Pair with any PR that touches `LogSensor`, `ApiHandlers`, `ConfigWebServer`, `DataServer`, `SensorIO`, `DataMark`, or anything that takes `xWriteMutex`. PR #501 (2026-05-17) and PR #530 (2026-04) are the canonical examples — both shipped bugs that only surfaced on bench.

The CI follow-up tracked at issue #553 would automate this on a self-hosted runner; until then, this skill is the protocol.

## When to Use

- User says "let's bench test PR #NNN", "run the stress test", "hammer the box", "see if the format crashes again", "is the ring stable under load"
- Before merging any PR that touches SD-writer code, web handlers under `xWriteMutex`, or the IMU/AHRS task chain
- After flashing a release-candidate firmware, before cutting the actual release
- When debugging a reported field issue (silent log end, missing samples, ring stuck at 100%, /api/logs returning 503)
- **For PRs touching EfisSerialPort / EfisRead / VN-300 parser / BoomSerial publishing**: also pair the web stress with the UART EFIS stim — see the "Optional but recommended for VN-300" subsection of Phase 3 below.  This was the missing piece that let PR #656 reproduce Vac's coredump-storm bug on the bench (5 IDLE0 watchdog timeouts in 30 min on the un-fixed firmware, 0 on the fix).

## When NOT to Use

- Pure UI / docs / native-test changes — those don't touch the runtime invariants this skill checks
- Investigation that's already narrowed to a specific function — use targeted reads and the existing native tests
- When the bench box isn't physically available (USB device missing, brownout-looping on insufficient power)

## What You Need

- A V4P (or V4B) on the bench with a real SD card inserted (NOT a fresh card — wear-leveling pauses are part of what we're testing)
- USB-serial connection (usually `/dev/cu.usbserial-*` on macOS; the device must enumerate before flashing)
- The host laptop on the OnSpeed AP (`OnSpeed` / `angleofattack`)
- A way to issue serial commands AND read serial output in parallel — typically a background `Monitor` reading the port, and the user driving the web UI on `192.168.0.1`
- The stress script at `tools/bench/stress_web_handlers.py` (PEP 723 inline metadata; runs via `uv run`)
- For VN-300 / EFIS-touching PRs: a second USB-TTL dongle on the host laptop (CP210x or CH340 — see `uart_efis_stim.py` docstring) wired to the V4P's EFIS RX pin.  Pair the stress with `tools/bench/uart_efis_stim.py --epoch-encode` running in a second terminal to reproduce the IDF UART production workload; the synth firmware builds bypass that path entirely.

## Bench Power Caveat

The V4P will brown-out on USB-only power if the audio amp + WiFi AP come up at the same time. Look for `E BOD: Brownout detector was triggered` and `reset=BROWNOUT` in the boot log. If you see brownouts, ask the user to switch to bench power (5V supply on the JST-XH); otherwise the test is just measuring how often the rail collapses.

## The Protocol

### Phase 1 — Flash and confirm boot

1. Build the PR firmware: `pio run -e esp32s3-v4p` from the worktree.
2. Confirm the USB-serial device: `ls /dev/cu.usbserial-*` — there must be exactly one match. If empty, the user must plug in / replug. Don't proceed until it enumerates.
3. Flash: `pio run -e esp32s3-v4p -t upload --upload-port /dev/cu.usbserial-410` (substitute the actual device path).
4. Start a serial monitor with a filter that keeps the PERF flood out of your context. The reference filter is captured in the script comments but the gist is:
   - Always show: `Format:`, `rotating log`, `Sensor log file`, `Boot #`, `BootDiag`, `BROWNOUT`, `task_wdt`, `Backtrace`, `Rebooting`, `panic`, `ERROR`, `Mount SD`, `CCMP`, `Loaded configuration`, `Loading onspeed2`, `Saved config`
   - Drop PERF heartbeats with `drops=0 dbg_drops=0 short=0 paused_drops=0` AND `imu_lateMaxUs<5000` AND `write_max<80000` (PERF reports both fields in microseconds, so these thresholds are 5 ms and 80 ms respectively)
   - Pass everything else through (any WARNING/ERROR, any PERF with non-zero counters)
5. Watch for the boot banner. Healthy boot looks like:
   ```
   OnSpeed Gen3 4.X.Y-dev.NN+SHA
   Boot #NNN (fw=..., reset=POWERON, prev_alive=>=60s)
   Loading onspeed2.cfg configuration from SD
   ...
   Sensor log file:log_NNN.csv
   Logging at 208Hz
   ```
   If you see `Mount SD failed` followed by `Loaded configuration from flash backup`, the SD card isn't in the slot. Stop and have the user check.

### Phase 2 — Quiescent baseline (60-120 s)

Before stressing anything, watch a clean PERF heartbeat. Healthy V4P at 208 Hz Extreme Pro card looks like:

- `write_max` 1-20 ms (the per-window worst per-batch SD write)
- `sync_max` 3-10 ms every 5 s (file sync cadence)
- `ring` oscillates 0-80 % in waves, drains within a couple PERF windows
- `drops=0 dbg_drops=0 short=0 paused_drops=0`
- `imu_late=0` (count of >1 ms schedule-resets — sub-ms jitter shows up in `imu_lateMaxUs` peak only)
- `imu_lateMaxUs` typically a few hundred μs (per-window peak); recurring `~750us` is the writer-yield pattern, expected
- `imu_lateMaxUsAT` all-time peak since boot; multi-ms here means a real stall happened
- `overflow=0 overflow_bytes=0` mostly; `overflow=1` with `~400-430 bytes` per window during sustained high-ring is the NOSPLIT carryover slot working as designed (each fired window drains exactly one row safely — not data loss)
- `heap=~8 MB, psram=~7.9 MB`, both stable

If any of these are off at idle, the box is unhealthy before you've even stressed it — figure out why first.

### Phase 3 — Drive the stress

Run `tools/bench/stress_web_handlers.py`. The script has three profiles via `--realistic`, default, or `--aggressive`. See `--help` for full options.

```bash
cd tools/bench
uv run ./stress_web_handlers.py --duration 10                   # default
uv run ./stress_web_handlers.py --duration 10 --aggressive      # tight cadence, useful for races
uv run ./stress_web_handlers.py --duration 30 --no-downloads    # no paused_drops expected
```

### Optional but recommended for VN-300 / EFIS-touching PRs: pair with the UART EFIS stim

When the PR touches `EfisSerialPort`, `EfisRead`, the VN-300 parser, or anything in the EFIS publish path, **also run `tools/bench/uart_efis_stim.py` in a second terminal** to feed the V4P's EFIS UART RX with bit-identical VN-300 frames at 400 Hz / 921600 baud.  This reproduces the production Core 0 workload — the IDF UART path that synth firmware builds bypass entirely.

```bash
# In terminal 1 — first build the C++ helper (one-time)
cd tools/bench/efis-stim && make && cd -

# Then pump frames (auto-detects the dongle if exactly one CP210x/CH34x is plugged in)
uv run ./uart_efis_stim.py --rate 400 --baud 921600 --epoch-encode

# In terminal 2 — run the web stress as usual
uv run ./stress_web_handlers.py --duration 60 --aggressive --no-saves --no-downloads
```

The `--epoch-encode` flag is important: it encodes a per-frame counter into Yaw / Pitch / Roll / Lat / Lon / TimeStartupNs so the offline tear-detector below can find atomic-publish regressions.  See `tools/bench/uart_efis_stim.py` docstring for wiring (V4P pin 25 / GPIO 11 via ADM3202, dongle voltage selector to 3.3 V).

After the run, check for torn rows in the resulting CSV:

```bash
python3 tools/bench/check-atomic-publish.py /Volumes/Untitled/log_NNN.csv
# expect: "OK: no tears detected. Atomic publish is working."
```

A non-zero tear count means the producer (EfisSerialPort or BoomSerial) is publishing data field-by-field instead of atomically — see PR #656 for the canonical fix and the structural pattern (assemble staging struct, memcpy under mutex, single publish).

### What the web-stress script does, concurrently:
- Opens multiple WebSocket clients to `/` and keeps them subscribed
- Polls `GET /api/logs` on a fast cadence (the handler returns 503 with `Retry-After: 1` when the SD writer is busy; the test exercises that contention path)
- Loads main pages (`/`, `/aoaconfig`) periodically
- Fetches static bundles (cache check)
- Occasional `POST /aoaconfigsave` with a *full form snapshot* (NOT a partial — partial form posts wipe boolean defaults; see the LogRate stress-trash incident on 2026-05-17 that killed flap config)
- `GET /download?file=log_NNN.csv` to exercise the `g_bPause` pathway and produce visible `paused_drops`

Ctrl-C exits cleanly with a per-endpoint summary.

While the script runs, the user should ALSO manually drive a few things via the web UI:
1. Toggle log rate 208 → 50 → 208 (schema-fingerprint rotation)
2. Toggle SD logging OFF → ON (close/reopen lifecycle, issue #550)
3. Change EFIS type (another schema-fingerprint trigger)
4. Format the SD card via `/format` (verify async task pattern, issue #556 if serial is involved)
5. Delete a log file (mutex-retry path)

Each of those should produce a visible PERF blip or rotation message in the monitor. **No drops, no discards, no crashes.**

### Phase 4 — Pull the artifacts

After the run:
1. Note the latest `log_NNN.csv` and matching `log_NNN.dbg` on the SD card (the box auto-rotated through several during the stress)
2. Download both via the `/logs` page
3. Capture the stress script's stdout summary
4. Capture the serial monitor's full output (`/tmp/onspeed-serial.log` if you used the canonical filter)

### Phase 5 — Analyze

Read the `.dbg` file:

```bash
grep -E "drops=[^0]|short=[^0]|paused_drops=[^0]|imu_lateMaxUs=[1-9][0-9]{4}" log_NNN.dbg
grep -E "ERROR|WDT|panic|Backtrace" log_NNN.dbg
grep -E "rotating log|Sensor log file|Saved config|Format:" log_NNN.dbg
```

Healthy means:
- All `drops`, `short`, `paused_drops` counts are either zero or only fire on the exact handler that's documented to cause them (downloads → paused_drops, NOT ring drops)
- IMU late events under 5 ms in the steady state. A single 15-20 ms spike during config-save is normal (the mutex-hold blocks AHRS briefly); recurring high values are not normal
- Every rotation message corresponds to an actual config change (no spurious rotations)
- No WDT/panic/Backtrace anywhere
- The matching `.csv` row count divided by the duration is within 0.1% of the configured rate (208 Hz or 50 Hz)

If you see something unhealthy, treat the `.dbg` as the source of truth. The CSV alone can't tell you whether a gap was a drop, a pause, or a clean rotation — the `.dbg` says which.

### Phase 6 — Sign-off

The PR is bench-clean to merge if and only if:

- [ ] Phase 2 baseline showed nothing pre-existing
- [ ] Phase 3 stress completed without firmware crash
- [ ] Phase 5 analysis showed zero unexplained drops, zero panics, IMU late events within normal envelope
- [ ] The CSV sample count matches the expected rate within 0.1%
- [ ] If the PR touched the format path: a full format completed AND the log file rotated to a fresh number AND the config persisted to the freshly formatted card

If any of these fail, report the failure (with the `.dbg` snippet) and **do not declare the PR ready**. The bench is the truth.

## Common Bench Failure Signatures

| Symptom | Likely cause | Where to look |
|---|---|---|
| `Log file is not open; discarding queued log data` | Producer is enqueueing but SD/file failed to open or got closed and not reopened. Pre-PR-501 toggling SD logging from web UI did this (#550). | `LogSensor::Open()` / `Close()` call sites |
| `task_wdt: ... did not reset the watchdog. CPU 0: WebServer` | A blocking call in a web handler exceeded 5 s. Web `/api/format` runs the format on a dedicated task to avoid this; serial `FORMAT` does not yet (see #556). | Whatever the handler was doing on Core 0 |
| `task_wdt: ... IDLE0 (CPU 0)` during format | The format work starved IDLE0. PR #501 fix removes IDLE0 from TWDT during format. | `FormatTaskEntry` in `ApiHandlers.cpp` |
| Steady `paused_drops` rising during normal logging | Some handler is holding `g_bPause=true` and not releasing. | `PauseGuard` call sites; suspect new code |
| Ring stuck at 100 % then drops climbing | Writer is starved on `xWriteMutex` by web handlers. PR #501 added writer-yield to fix this. | `LogSensor::CommitTask` mutex hold time, web handler timeouts |
| `wifi:CCMP replay detected` | WiFi link glitch (not OnSpeed firmware) — TCP packets dropped/replayed. Caused stuck POST responses in our bench session. | Disconnect/reconnect from AP |
| `E BOD: Brownout detector was triggered` | Insufficient power (often USB-only). Not a firmware bug. | Bench power supply |
| Format completes but TWDT "task not found" spam during format | Benign — SdFat's internal yield calls `esp_task_wdt_reset()` from worker threads not registered with TWDT. Crash-safe. | Annoying log noise only |

## Anti-patterns

- **Skipping the baseline phase.** "It looked fine yesterday" is not data. Always re-baseline on the current firmware.
- **Running only the script without manual web UI driving.** The script doesn't trigger schema rotations or format. Those are the highest-risk paths.
- **Trusting the CSV without reading the `.dbg`.** A clean CSV can hide a 30-second `paused_drops` window. The `.dbg` always tells the truth.
- **Treating brownouts as bugs.** They're a power supply issue. Fix the power and retest before chasing the firmware.
- **Declaring a PR clean from one short run.** 10 minutes is the minimum useful duration. 30 minutes is much better for sustained-load issues. Card wear-leveling pauses cluster minutes apart on some cards.
- **Comparing against the wrong baseline.** Old firmware behaved differently. Always compare to a recent clean run on the same hardware.

## References

- **`tools/bench/stress_web_handlers.py`** — the script. PEP 723 metadata, runs via `uv run`.
- **`docs/site/docs/contributing/bench-testing.md`** — the publicly-rendered version of this protocol for non-Claude contributors.
- **Issue #553** — CI integration of this harness on a self-hosted runner. Until that lands, this skill is the protocol.
- **PR #501 / PR #530** — the two canonical bug classes this skill exists to catch. Read their PR descriptions before running the skill on similar changes.
