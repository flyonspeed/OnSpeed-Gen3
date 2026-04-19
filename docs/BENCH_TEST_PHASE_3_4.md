# Bench-Test Checklist — Phase 3 + 4 Pre-Flight Shakedown

Run this on the actual OnSpeed box on the bench before any flight test that has Phase 3 or Phase 4 firmware loaded. ~90 minutes end to end. Catches the integration, timing, and in-situ bugs that unit tests and the snapshot regression harness can't touch.

**Stakes.** The core-extraction refactor is behind a single flight-test gate because bundling all the changes into one flight is cheaper than many flights. That bet only pays off if the bench catches the 1-in-10 extraction miss that slipped past CI. Run every box below; skipping any is a false economy.

## Prep (one-time)

- [ ] Keep a **known-good SD card with master firmware** in your pocket for the flight. If in-flight anything feels off, land, swap, investigate later.
- [ ] Record the bench test audio with a headset + GoPro or phone. A/B comparisons later are worth the minute of setup.
- [ ] Have your real N720AK config on the SD card, not a bench config. Configs differ in ways that matter (flap count, alpha_0, setpoints).
- [ ] USB serial terminal open to the box throughout. Serial console errors that never make it to the audio are the most common class of regression we've caught.

## Stage 1 — Cold boot (10 min)

- [ ] **Power on from cold.** Observe the LED heartbeat. Should light within 2 seconds, start blinking at the expected rate within ~4 seconds.
- [ ] **Serial console** — no error messages, no assertion fails, no "Config load failed" lines.
- [ ] **Version line** on the console matches the build you flashed. Easy to forget this.
- [ ] **Boot again** after 10 seconds off — confirm the same behavior. First-boot-only bugs happen.

If you have both a V4P (Phil's) and V4B (Bob's) box available, cold-boot both. The `HardwareMap.h` split is supposed to isolate them; regressions in extraction could break the variant you're not testing with.

## Stage 2 — Web UI full sweep (20 min)

This is the 423-call-site test. The Config extraction touched every `g_Config.*` reader; the web UI is the only place that exercises the majority of them at once.

- [ ] Connect to `OnSpeed` WiFi. Open `http://192.168.4.1/` (or whatever the default IP is).
- [ ] **Home page loads** without "Internal Server Error". Quick CSS/layout sanity check.
- [ ] **Config page** loads. Every field has a value (not blank where it shouldn't be, not showing `NaN` or `null`).
- [ ] **Edit a throwaway field** (e.g. `Asymmetric Gyro Limit`). Change value, save, power-cycle, reopen page — confirm value persisted. **Critical**: the subclass approach for `FOSConfig` means the save/load round-trip goes through the new core XML parser + emitter, and persistence is the real regression target.
- [ ] **Revert** the field to its original value. Save again. Persistence still works.
- [ ] **Flap calibration page** loads. Every flap position shows its setpoints (`LDMAXAOA`, `OnSpeedFast`, `OnSpeedSlow`, `StallWarn`, `AlphaStall`) — nonzero, nonblank, matching your last known-good config.
- [ ] **AOA curve** page loads for each flap. Polynomial coefficients (X3, X2, X1, X0) match your last known good values.
- [ ] **CAS curve** page loads if you have one configured. Enabled/disabled state reflects your config.
- [ ] **Audio page** — volume pot setting, 3D audio on/off, mute-under-IAS threshold, G-limit chime, Vno chime — all display your real config values.
- [ ] **Log Files page** (if present) — SD card log listing works, you can download and open a log.
- [ ] **Live View page** loads. AOA needle position is sensible (box is sitting still — should be at wherever a stationary box reads). Pitch/roll indicators not stuck at extremes.

**Red flags to watch for:**
- A field that shows `0` where it should show a real value → possibly the empty-text-tag bug we caught in review surfacing somewhere else, or a LoadDefaults miss.
- Flap count off by one vs your config → the master flap-loop bug (the new code fixed it but verify it didn't regress in your specific config).
- A field that saves but doesn't persist → XML emit/parse asymmetry. Inspect the saved XML on the SD card to see what got written.

## Stage 3 — Log replay + audio audit (20 min)

This is the end-to-end audio test. Play a real flight log through the box and listen for the tone behavior.

- [ ] Copy your N720AK flight log CSV onto the SD card (e.g. `replay.csv`). Real flight with stall, approach, landing is ideal.
- [ ] Configure **Data Source = REPLAYLOGFILE** in the web UI, set **Log file to replay** to your filename. Save.
- [ ] Power-cycle. Box should boot into replay mode. Console shows `Data Source REPLAYLOGFILE`.
- [ ] Plug headset into the OnSpeed audio output.
- [ ] **Listen to the replay.** Tones should sweep through the expected range:
  - Silence at ground / low IAS
  - Slow pulse → fast pulse → "OnSpeed" steady tone → stall warning → stall
  - Matches what you remember from the source flight. A/B against a previously-recorded audio capture if you have one.
- [ ] **G-limit events** fire at the right moment. If your log has 3+ G pulls, you should hear the chime.
- [ ] **Vno chime** fires if your log has Vno exceedance.

**Red flag**: any tone that's 180° out of phase, wrong frequency, clicks/pops that weren't there before, tones shifted in time. Audio is the first place a subtle AHRS regression surfaces — a derived-AOA off by 0.2° moves the tone boundary audibly.

## Stage 4 — EFIS loopback (15 min)

Only applicable if you have an EFIS or EFIS simulator available.

- [ ] Configure **Data Source = SENSORS** with **EFIS = true** and the correct EFIS type (Dynon, Garmin G5/G3X, MGL, VN-300).
- [ ] Connect the EFIS serial lines.
- [ ] Live view page shows EFIS attitude, IAS, Palt, OAT updating smoothly.
- [ ] **Hold-last-value semantics work**: if the EFIS briefly stops sending (unplug for 2 seconds, replug), the attitude freezes at the last-received value instead of jumping to 0. The PR 2.2 NaN-sentinel design specifically targets this — verify it in-situ.
- [ ] Log a minute of live data. Confirm the CSV log columns for EFIS are populated (not blank, not zero).

## Stage 5 — SD log byte comparison vs master (15 min)

The log format is load-bearing for downstream tools (calibration-explorer, Python analyses). Byte identity is the bar.

- [ ] On master firmware: replay your N720AK flight log, capture the output to SD (let it log at least 60 seconds).
- [ ] On new firmware: same config, same input log, same replay session length.
- [ ] Compare the two output CSVs column-by-column:
  ```bash
  diff <(head -2 master_output.csv) <(head -2 new_output.csv)   # headers
  diff master_output.csv new_output.csv | head -50              # row differences
  ```
- [ ] **Header line must match exactly.** A dropped or added column means the format diverged.
- [ ] **Row-level differences** are expected to be within float-precision noise if AHRS drift exists, but column count and ordering must be identical.
- [ ] If any divergence is pilot-visible — e.g., IAS column is off by 1kt, pitch column has opposite sign — stop, investigate before flight.

## Stage 6 — Calibration wizard (10 min)

This exercises the AOA curve-fit math end-to-end. Skip if you're not planning to recalibrate this flight.

- [ ] Walk through a cal session on the bench (or replay a cal log if you have one).
- [ ] Verify the curve fits produce the same polynomial coefficients (X3, X2, X1, X0) as master firmware did for the same input data. Small floating-point drift is OK; magnitude-different coefficients are a bug.

## Abort conditions

If any of the following happens on the bench, **do not fly this firmware**:

- Cold-boot fails or times out
- Any `ERROR` in the serial console that wasn't there on master
- Config persistence breaks (save then power-cycle shows a different value)
- Audio replay produces visibly-wrong tones (silence where there should be sound, or vice versa)
- Log CSV header doesn't match master byte-for-byte
- EFIS hold-last-value doesn't work (attitude jumps to 0 on brief EFIS dropout)

Roll back to the known-good SD card in your pocket, file an issue with specifics, and we can iterate.

## After a clean bench run

File a short note on the relevant PR(s) saying "bench green, ready for flight test." Keep the audio and log recordings — they're the A/B baseline for post-flight analysis.
