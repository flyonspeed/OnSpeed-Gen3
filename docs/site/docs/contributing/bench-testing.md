# Bench Testing

OnSpeed's hardest reliability bugs only appear when the firmware is running on real hardware under realistic load: IMU feeding the ring at 208 Hz, the SD writer fighting for `xWriteMutex`, WiFi serving WebSocket frames, and a pilot clicking buttons on the web UI. Unit tests cannot reproduce these conditions. The host-side regression harness at `tools/regression/` cannot reproduce these conditions. The only thing that catches them is putting a real V4P under realistic load and watching what comes out the other side.

This page is the **manual protocol** for bench testing a firmware change before merge. It is the standard the maintainers apply to any PR that touches SD-writer code, web handlers, or anything that contends on `xWriteMutex`. If your PR touches those areas and you want it reviewed without "did you run the bench?" delays, run this protocol first.

The CI-automated version of this protocol is tracked at [issue #553](https://github.com/flyonspeed/OnSpeed-Gen3/issues/553) — a self-hosted runner with a USB-attached V4P. Until that lands, this is the protocol.

## What you need

- A V4P (or V4B) wired up on a bench with adequate power (not USB-only — the audio amp + WiFi AP coming up together will brown out a V4P on USB power; you'll see `E BOD: Brownout detector was triggered` in the boot log)
- A real SD card installed (NOT a freshly formatted card — wear-leveling pauses are part of what we're testing)
- USB-serial cable connected to the host laptop, with the device enumerating as `/dev/cu.usbserial-*` (macOS) or equivalent
- The host laptop on the OnSpeed WiFi AP (`OnSpeed` / `angleofattack`)
- Python 3.10+ with [uv](https://github.com/astral-sh/uv) installed (the stress script declares its dependencies inline via PEP 723 and `uv run` resolves them)
- PlatformIO to flash the firmware
- A serial-monitor approach that lets you watch output AND issue commands — typically a separate terminal running `screen /dev/cu.usbserial-N 921600` or `pio device monitor`

## The protocol, step by step

### 1. Flash the firmware under test

From your PR's worktree:

```bash
pio run -e esp32s3-v4p
ls /dev/cu.usbserial-*    # must show exactly one device
pio run -e esp32s3-v4p -t upload --upload-port /dev/cu.usbserial-410
```

Substitute the actual device path. Flash should complete in under a minute.

### 2. Watch the boot

Attach a serial monitor at 921 600 baud. A healthy boot prints:

```
OnSpeed Gen3 4.X.Y-dev.NN+SHA
Boot #NNN (fw=..., reset=POWERON, prev_alive=>=60s)
Loading onspeed2.cfg configuration from SD
...
Sensor log file:log_NNN.csv
Logging at 208Hz
```

If you see `Mount SD failed` followed by `Loaded configuration from flash backup`, the SD card isn't in the slot. Stop and fix that before continuing — the entire test is about SD reliability, and you can't measure it without an SD card.

If you see `reset=BROWNOUT`, you're on inadequate power. Switch to bench power.

### 3. Establish the quiescent baseline (60-120 seconds)

Before you stress anything, watch a clean PERF heartbeat. The writer emits a periodic `WARNING Disk - PERF write_max=… sync_max=… ring=…` line every 10 seconds (and on any anomaly within the window). At idle on a healthy V4P with an Extreme Pro card, you should see:

| Field | Healthy range at 208 Hz |
|---|---|
| `write_max` | 1-20 ms per window (occasional 30-60 ms wear-leveling spike) |
| `sync_max` | 3-10 ms every 5 s (file sync cadence) |
| `ring` | oscillates 0-80 % in waves, drains within a few windows |
| `drops` | 0 |
| `dbg_drops` | 0 |
| `short` | 0 |
| `paused_drops` | 0 |
| `imu_late` | 0, or 1 with `imu_lateMaxUs<1000` (recurring sub-ms pattern is the writer-yield, expected) |
| `overflow` | 0 mostly; 1 with ~400-430 bytes during sustained high ring is the NOSPLIT carryover slot — not a drop |
| `heap` | ~8 MB stable |
| `psram` | ~7.9 MB stable |

If any of these are wrong at idle, the firmware is unhealthy before you've stressed it. Stop and investigate.

### 4. Run the stress script

From your worktree:

```bash
cd tools/bench
uv run ./stress_web_handlers.py --duration 10                  # default
uv run ./stress_web_handlers.py --duration 30 --aggressive     # tight cadence, catches races
uv run ./stress_web_handlers.py --duration 30 --no-downloads   # no paused_drops expected
```

`--help` lists every option. The script:

- Opens multiple WebSocket clients to the live data feed
- Polls `GET /api/logs` on a fast cadence (this handler returns a `Retry-After: 1` 503 when the SD writer is busy; healthy behavior is fast 200s with the occasional retry-honored 503)
- Loads main HTML pages periodically
- Fetches static bundles to exercise the ETag cache
- Posts occasional config saves (with a **full** form snapshot — partial POSTs wipe boolean defaults)
- Downloads log files to exercise the producer-pause pathway

Ctrl-C exits cleanly with a per-endpoint summary.

### 5. Drive the manual paths in parallel

The script doesn't cover everything. While it's running, use the web UI to exercise:

- **Log rate toggle** — 208 → 50 → 208 (this is a schema-affecting change and should rotate the log file)
- **SD logging toggle** — OFF → save → ON → save (close/reopen lifecycle; see [issue #550](https://github.com/flyonspeed/OnSpeed-Gen3/issues/550) for context)
- **EFIS type change** — another schema-fingerprint trigger
- **Format the SD card** — verify the format completes via the async-task pattern (HTTP returns immediately with a taskId, browser polls `/api/format/status` while a dedicated FreeRTOS task runs SdFat's format on Core 0)
- **Delete a log file** — exercises the mutex-retry path

Each of those should produce a visible PERF blip or rotation message in the monitor. None should produce drops, discards, or crashes.

### 6. Pull and analyse the artifacts

After the run:

1. Note the latest `log_NNN.csv` and matching `log_NNN.dbg` on the SD card
2. Download both via the `/logs` page
3. Save the stress script's stdout summary
4. Save the serial monitor's output

The `.dbg` file is the source of truth. Grep it:

```bash
# Anything non-zero in the counters
grep -E "drops=[^0]|short=[^0]|paused_drops=[^0]" log_NNN.dbg

# Large IMU stalls (single-event >10 ms)
grep -E "imu_lateMaxUs=[1-9][0-9]{4}" log_NNN.dbg

# Any crash
grep -E "ERROR|WDT|panic|Backtrace" log_NNN.dbg

# Lifecycle events for cross-checking
grep -E "rotating log|Sensor log file|Saved config|Format:" log_NNN.dbg
```

The CSV alone cannot distinguish a paused-drop window from a clean rotation from a genuine crash. The `.dbg` always says which.

### 7. Sign-off criteria

A PR is bench-clean to merge if and only if all of these hold:

- The quiescent baseline (step 3) showed nothing pre-existing
- The stress run (step 4) completed without firmware crash
- The analysis (step 6) showed zero unexplained drops, zero panics, IMU late events within the normal envelope (no sustained >5 ms; a one-shot 15-20 ms spike during config-save is normal)
- The CSV row count divided by the duration matches the expected rate within 0.1 %
- If the PR touched the format path: a full format completed, the log file rotated to a fresh number, and the config persisted to the freshly formatted card

If any of these fail, the PR is not ready. Report what failed (with the `.dbg` snippet) and iterate.

## Common failure signatures

| Symptom in serial / .dbg | Likely cause |
|---|---|
| `Log file is not open; discarding queued log data` | Producer is enqueueing but the SD file failed to open or was closed without reopen. |
| `task_wdt: ... CPU 0: WebServer` | A web handler blocked for >5 s on Core 0. |
| `task_wdt: ... IDLE0 (CPU 0)` during format | The format saturated Core 0 and IDLE0 starved. Should be impossible after PR #501. |
| `paused_drops` rising during normal logging | A handler is holding `g_bPause=true` without releasing it. |
| Ring stuck at 100 % then drops climbing | Writer is starved on `xWriteMutex` by web handlers. |
| `wifi:CCMP replay detected` | WiFi link glitch — TCP packets dropped or replayed. Not a firmware bug. Disconnect and reconnect from the AP. |
| `E BOD: Brownout detector was triggered` | Insufficient bench power. |
| `esp_task_wdt_reset(...): task not found` spam during format | Benign. SdFat's internal yield calls reset from worker threads that aren't TWDT-registered. Crash-safe. |

## Things that look wrong but aren't

- **A single `overflow=1` per PERF window with 400-430 bytes during high ring.** The NOSPLIT carryover slot is doing its job; each fired window drains exactly one row safely. Not a drop. Only worry about overflow if it's accompanied by non-zero `drops`.
- **A recurring `imu_late=1 imu_lateMaxUs=~750us`.** This is the writer's `vTaskDelay(1)` yield producing a sub-millisecond IMU stall. Not a flight-critical lateness. Below the 4.8 ms IMU period by a factor of six.
- **`paused_drops` rising during a `/download` operation.** That's the whole point of the counter — visibility into the cost of holding the producer paused while a download runs. The PR #501 change made this counter exist so the cost is no longer silent.

## Why this matters

The bench is the only place where the integrated path — IMU → ring → writer → SD card under WiFi/web load — exists. Unit tests can't see it; the regression harness can't see it. Two recent PRs (PR #501 and PR #530) shipped fixes for bugs that:

- Passed all native tests (1091/1091 green)
- Built cleanly with zero warnings under `-Werror`
- Reviewed clean with multiple eyes
- And were caught only by running the bench protocol on a real V4P

The script and the protocol exist because the box is the only thing that knows the truth.

## See also

- [`tools/bench/stress_web_handlers.py`](https://github.com/flyonspeed/OnSpeed-Gen3/blob/master/tools/bench/stress_web_handlers.py) — the stress script
- [Understanding Logs](../data-and-logs/log-format.md) — what the CSV and `.dbg` columns mean
- [Issue #553](https://github.com/flyonspeed/OnSpeed-Gen3/issues/553) — CI integration of this protocol (future work)
