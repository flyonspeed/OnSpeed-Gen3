# tools/bench

Tooling for human-driven bench testing of OnSpeed firmware on real V4P / V4B hardware.

This directory holds scripts and supporting material for the **manual** bench protocol, which is the maintainer-required pre-merge check for any PR that touches SD writer code, web handlers, or anything that contends on `xWriteMutex`. The protocol is documented in:

- **The docs site**: [`docs/site/docs/contributing/bench-testing.md`](../../docs/site/docs/contributing/bench-testing.md) — public, contributor-facing.
- **The Claude skill**: [`.claude/skills/bench-stress/SKILL.md`](../../.claude/skills/bench-stress/SKILL.md) — Claude-facing, with the same content plus operating notes for the agent.

A future CI version that runs this protocol automatically on a self-hosted runner with a USB-attached V4P is tracked at [issue #553](https://github.com/flyonspeed/OnSpeed-Gen3/issues/553).

## Files

- **`stress_web_handlers.py`** — the stress script. Hammers `GET /api/logs`, the main HTML pages, the static bundles, occasional `POST /aoaconfigsave`, log file downloads, and multiple concurrent WebSocket clients, in a scripted pattern that mirrors realistic + worst-case pilot activity. PEP 723 inline metadata; runs via `uv run`.

  ```bash
  uv run ./stress_web_handlers.py --help
  uv run ./stress_web_handlers.py --duration 10                  # default profile
  uv run ./stress_web_handlers.py --duration 30 --aggressive     # tight cadence, race-catching
  uv run ./stress_web_handlers.py --duration 30 --no-downloads   # no paused_drops expected
  ```

- **`uart_efis_stim.py`** + **`efis-stim/`** — UART EFIS stim rig.  Pumps bit-identical VN-300 binary frames into the V4P's EFIS RX from a USB-TTL dongle at 400 Hz / 921600 baud.  Pairs with the web stress to reproduce the production Core 0 workload (the IDF UART path that synth firmware builds bypass).  The `--epoch-encode` flag encodes a per-frame counter N into Yaw / Pitch / Roll / Lat / Lon / TimeStartupNs so the offline analyzer below can detect torn rows.

  ```bash
  # First time: build the C++ helper
  cd efis-stim && make && cd -

  # Then pump frames (auto-detects the dongle if exactly one CP210x/CH34x is plugged in)
  uv run ./uart_efis_stim.py --rate 400 --baud 921600 --epoch-encode
  uv run ./uart_efis_stim.py --port /dev/cu.usbserial-XXXX        # explicit port
  ```

  Wiring (V4P): dongle TX → GPIO 11 (J1 pin 25, post-ADM3202), dongle GND → V4P GND, dongle voltage selector to 3.3 V (NOT 5 V).  V4B: TX → GPIO 9 (DB-15 pin 2).  See the docstring at the top of `uart_efis_stim.py` for the full pin map and what to watch for in the box console.

- **`check-atomic-publish.py`** — offline detector for atomic-publish regressions in EFIS data.  Reads a CSV log produced under `--epoch-encode` stim and reports torn rows (where Pitch/Roll come from frame N but Lat/Lon come from frame N±1).  Recommended as a manual sanity-check after any change touching `EfisSerialPort::applyVn300Data` or `BoomSerial::Read`.

  ```bash
  python3 ./check-atomic-publish.py /Volumes/Untitled/log_NNN.csv
  ```

  The companion file `test_check_atomic_publish.py` is a Python-side regression suite for the analyzer itself (catches future regressions of the detection logic, with synthetic clean + torn fixture rows).  Run with `python3 ./test_check_atomic_publish.py`.

- **`check_snapshot_sanity.py`** — offline torn-read scan for the AHRS / Sensor / IMU lock-free snapshots (`g_AhrsSnapshot`, `g_SensorSnapshot`, `g_ImuSnapshot`).  Where `check-atomic-publish.py` proves the EFIS snapshot via the `--epoch-encode` counter, these snapshots carry physical flight quantities with no encoded counter, so this scans their log columns (accel/gyro triplets, IAS/AOA/Palt, Pitch/Roll/DerivedAOA/VSI) for the torn-read *fingerprint*: a single column making a physically impossible one-sample out-and-back excursion.  Recommended after any change touching those snapshots or their readers (`SensorIO`, `LogSensor`, `DisplaySerial`, `DataServer`, `Flaps`, the AHRS publish path).

  ```bash
  uv run ./check_snapshot_sanity.py /Volumes/Untitled/log_NNN.csv
  ```

  Heuristic, not a proof — a clean result is evidence-of-absence for the common reverting tear, NOT a guarantee (it misses tears that land on a non-reverting step; see the docstring's KNOWN BLIND SPOT).  The authoritative coherence guarantee is the seqcount itself (`test_snapshot_publisher`).  Companion self-test: `uv run ./test_check_snapshot_sanity.py`.

## Related

- `tools/regression/` — host-side regression harness that runs `onspeed_core` algorithms against a recorded flight log. Different layer: this catches algorithm drift across `onspeed_core` extraction PRs, not real-hardware reliability.
- `tools/web/dev-server/` — local server that serves the firmware's PROGMEM bundle for browser testing of UI changes without flashing. Different layer: catches UI bugs without exercising SD / WiFi / FreeRTOS.
