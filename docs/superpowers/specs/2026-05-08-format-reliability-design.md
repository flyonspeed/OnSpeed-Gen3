# SD-card format reliability and post-format messaging

## Background

Vac reported two issues from his bench session on 4.22.2-dev-40+c62ab11:

1. The post-format page used to show the new card size in GB; the current page only shows "SD card has been formatted."
2. After he formatted the card, his configuration was not on it. He had to navigate to the config page and hit Save before `onspeed2.cfg` reappeared in the file listing.

His screenshots tell a stronger story than the verbal report. IMG_4431 (immediately post-format) shows:

| File | Size | Notes |
|---|---|---|
| `log_001.csv` | 428.8 KB | Fresh log file, growing |
| `==>=5s` | 1894.59 MB | Garbage filename |
| `_c62ab11._we` | 515.65 MB | Garbage filename — `c62ab11` is the build's git short SHA |
| `boot=001.9` | 351 B | Garbage filename, looks like `boot_log.txt` mangled |
| **(no `onspeed2.cfg`)** | — | Configuration file is absent |

IMG_4432 (after he visited /aoaconfig and hit Save) shows the same four files plus `onspeed2.cfg` 3.8 KB. The garbage files are unchanged.

The garbage files are not files the firmware writes between format completion and the user reloading /logs. They are leftover directory entries from before the format. Their corrupted names are consistent with exFAT directory-entry slots whose long-filename hash chain was partially overwritten — the format zeroed some sectors but not others.

The missing config is the second tell. `RunFormatInline` already calls `g_Config.SaveConfigurationToFile()` after the format completes (`software/sketch_common/src/web_server/ApiHandlers.cpp` ~line 224). The fact that the file is not there means either the call did not run or the write failed.

The likely explanation that ties both symptoms together: **the format leaves the SD filesystem in an inconsistent state and/or the format handler does not run to completion.** Three plausible mechanisms, not yet narrowed down:

- **Pre-erase loop hangs the WebServer task long enough to trip a watchdog or browser disconnect.** The pre-erase walks `puSD_Card->erase(firstBlock, lastBlock)` over all 122M sectors of a 64 GB card in 256 K-block chunks (~470 iterations). At ESP32 SPI 10 MHz this can take many minutes. The Gen3 loop has no `vTaskDelay()`, no `taskYIELD()`, and no watchdog feed. The Gen2 equivalent loop on Teensy explicitly called `checkWatchdog()` per iteration. If a WDT or task-watchdog reset fires mid-loop, the firmware reboots before `SaveConfigurationToFile()` ever runs, and the card is left half-zeroed.
- **The exFAT formatter only initializes the first cluster of the root directory.** Subsequent root clusters are left untouched but should be unreachable because the FAT itself is rewritten. If the FAT rewrite is also incomplete (interrupted by mechanism above), old directory entries become reachable through the new FAT and produce the corrupted filenames.
- **`g_SdFileSys.open(O_WRITE | O_CREAT | O_TRUNC)` on `onspeed2.cfg` is failing post-format** even though `LogSensor.Open()` for `log_001.csv` succeeded a few hundred ms earlier. Possible if the card directory is in an unstable state where some allocations work and others don't. SaveConfigurationToFile's `open` failure is silent except for a log line; the user sees no error.

The post-format message regression is mostly cosmetic but worth fixing in the same change: the size string is computed inside `SdFileSys::Format()` (`SdFileSys.cpp:189`) but only printed to a `Print *` argument that the API path passes as `nullptr`. The Preact FormatPage has nowhere to render it.

A separate side-question raised in the session — should `boot_log.txt` and `/coredumps/*.bin` survive a format? — is deferred to a follow-up. The current PR's job is to get format working reliably and tell the pilot what happened. Forensic-data persistence is a real design question on its own (what to evict, where to store, flash-wear policy) and folding it into a reliability fix would muddy the diff.

## Goals

1. After a successful format, the SD card contains exactly: `onspeed2.cfg`, the freshly-opened `log_001.csv` (if logging was on), and nothing else. No leftover directory entries with corrupted names.
2. The post-format page shows "SD card has been formatted. New card size is: X.X GB" — same shape Gen2 had.
3. The pre-format confirm screen tells the pilot what survives and what does not, so a pilot who clicks Format knows their config is preserved and their logs are gone.
4. If the format fails for any reason, the page reports the failure with a useful message (not just "format failed").

## Non-goals

- Persisting `/boot_log.txt` or `/coredumps/*.bin` across format. Real design question; separate PR.
- Calibration backups or any other "save everything" persistence policy.
- Changing the format algorithm (still SdFat exFAT formatter for >32 GB cards, FAT32 below).
- Async-task offload of the format. The current "POST returns task ID, client polls /api/format/status" plumbing is in place; this PR keeps using inline-execution semantics inside the HTTP handler.

## Design

### Format reliability fix — drop the pre-erase

The pre-erase loop in `SdFileSys::Format()` walks `puSD_Card->erase(firstBlock, lastBlock)` over all card sectors in 256 K-block chunks (`SdFileSys.cpp:155-173`). On a 64 GB card this is ~470 iterations of a SPI@10MHz erase command, several minutes wall-clock. The loop has no `vTaskDelay()`, no `taskYIELD()`, no watchdog feed. Whatever process is reaping the format mid-loop (task watchdog, brownout, browser-induced reset, or some other path) leaves the card in the inconsistent state Vac saw.

The `puSD_Card->erase()` calls are a SD-spec wear-leveling / TRIM hint to the card controller. They are **not required** for the exFAT formatter to produce a clean filesystem — `ExFatFormatter::format()` writes all the sectors it needs (boot sector, FAT, root cluster) regardless of whether the underlying flash was pre-erased. Gen2 included the pre-erase but Teensy's SDIO is roughly an order of magnitude faster than ESP32 SPI@10MHz, so the loop actually completed there. On Gen3 it does not, and there is no reason to keep paying for it.

**Fix:** drop the pre-erase. Pass `bErase=false` from the API path and either delete the `if (bErase)` block in `SdFileSys::Format()` or leave it inert behind the parameter. The "I'm not sure what the need for this is" comment already in `SdFileSys.cpp:156` is a hint the prior author was already skeptical.

After this change, format completes in seconds instead of minutes. The interruption window collapses to roughly the duration of `ExFatFormatter::format()` itself (small), and the post-format `SaveConfigurationToFile()` call has time to run before any reset or browser timeout.

### Card size in API response

`SdFileSys::Format()` already computes `uCardSectorCount * 5.12e-7` for the GB string. Surface it through the `FormatJob` struct so `/api/format/status` returns it on success.

```cpp
struct FormatJob {
    char         taskId[32]    = {};
    FormatState  state         = FormatState::Idle;
    char         error[64]     = {};
    float        cardSizeGb    = 0.0f;   // new — populated when state == Done
};
```

Modify `SdFileSys::Format()` signature to optionally return the size: `bool Format(Print* pStatusOut, bool bErase, float* pSizeGb)` — adding a defaulted output parameter rather than changing existing callers. Or a sibling getter on `SdFileSys` that re-reads `puSD_Card->sectorCount()` after format completes.

`/api/format/status` JSON when `state == "done"`:
```json
{"state": "done", "cardSizeGb": 63.9}
```

`FormatPage.js` `done` branch:
```jsx
${phase === 'done' && html`
  <p>SD card has been formatted.</p>
  ${cardSizeGb && html`<p>New card size is: ${cardSizeGb.toFixed(1)} GB</p>`}
  <p><a href="/logs">Go to logs</a> | <a href="/">Home</a></p>`}
```

### Pre-format confirm screen warning

Current copy:
> Confirm that you want to format the internal SD card. You will lose all the files currently on the card.

New copy:
> Confirm that you want to format the internal SD card. **All log files and crash diagnostics will be erased. Your configuration will be preserved.**

One-line change in `FormatPage.js`. Sets correct expectations: pilots stop wondering "is my config gone too?" and stop asking us about it.

### Verification of post-format config save

`RunFormatInline` already calls `SaveConfigurationToFile()` after format but discards its bool return. Capture it. If it returns false after a successful format, surface a partial-failure state: format succeeded, config save did not. The page can render "SD card was formatted but the configuration could not be re-saved. Visit the configuration page and click Save." This makes the failure self-explaining instead of silent.

If investigation reveals that `SaveConfigurationToFile()` returns true but the file does not actually appear on a subsequent read (the symptom in IMG_4431), add an `g_SdFileSys.exists(szDefaultConfigFilename)` check after the save and treat absence as a partial failure too. Defer this second check until investigation confirms it is the actual failure mode.

### Error reporting on format failure

Current `RunFormatInline` writes "format returned false" or "SD busy (xWriteMutex)". That is enough for an engineer; a pilot would benefit from one extra hop. If `g_SdFileSys.Format()` returned false, the most actionable next step is "power-cycle and try again, or pop the card and check it on a computer." Add that to the failure-page copy in FormatPage.js, similar to how the firmware-upgrade-failure page already does.

## Components touched

| File | Change |
|---|---|
| `software/sketch_common/src/drivers/SdFileSys.cpp` | Drop or guard the pre-erase loop; return card size to caller |
| `software/sketch_common/src/drivers/SdFileSys.h` | Format() signature: add `float* pSizeGb = nullptr` |
| `software/sketch_common/src/web_server/ApiHandlers.cpp` | Plumb size into FormatJob; verify post-format config save; richer error strings |
| `tools/web/lib/pages/FormatPage.js` | Render card size on success; new confirm-screen copy; better failure copy |
| `tools/web/dev-server/...` (if mock JSON exists for /api/format/status) | Add cardSizeGb to mock response so dev-server preview works |
| `test/test_format/...` | New native test (only if practical — formatter logic is on-device, may be hard to fixture) |

## Test plan

**Bench testing (the only way to be sure):**
- Format a 64 GB card via the web UI. Time it — should be seconds, not minutes. Pop the card. Confirm only `onspeed2.cfg` and `log_001.csv` are present, no zombie entries with corrupted names.
- Repeat on a card that previously had hundreds of log files (drop a fixture set, reformat, verify clean).
- Format with logging off (`bSdLogging=false`). Confirm only `onspeed2.cfg` is present.
- Format with logging on. Confirm `log_001.csv` is created and has logging data accruing.
- Confirm `BootDiag` boot count in NVS is unchanged across format (no reboot occurred). This is a regression check against the pre-erase issue ever creeping back.
- Confirm card-size message appears on the page and matches `diskutil info` output on the host.
- Trigger a format failure (e.g., pull SD card mid-format). Confirm the failure copy renders.

**Unit / dev-server testing:**
- FormatPage with mock `/api/format/status` returning `{state:"done", cardSizeGb:63.9}` renders the size line.
- FormatPage with `{state:"failed", error:"..."}` renders the failure copy.
- Confirm-screen text matches the new wording.

## Open questions

- Should the `BootDiag` boot-count check be promoted into a regression test? An automated check that "boot counter does not increment during a format" would catch the WDT-reset class of bug if it ever recurs.
- Does the M5 display port care about post-format messaging? (Probably no — M5 doesn't render the format page; it would just see SD logging resume.)
- Is there a value in a console command `FORMATSTATUS` that prints the most recent format job's outcome? Useful for serial-port debugging in the field.

## Appendix: prior code references

- Gen2 format flow (Teensy side): `Software/OnSpeedTeensy_AHRS/WifiSerial.ino` ~line 200-280. Includes per-iteration `checkWatchdog()`, post-format `configurationToString` + `saveConfigurationToFile`, and the `<FORMATDONE>%.1f GB</FORMATDONE>` envelope.
- Gen2 format UI (WiFi side): `Software/OnSpeedWifi/OnSpeedWifi.ino:445-490`. Format confirm screen, format-running spinner, "SD card has been formatted. New card size is: X.X GB" success page.
- Gen3 format flow today: `software/sketch_common/src/web_server/ApiHandlers.cpp` (`HandleApiFormat`, `RunFormatInline`), `software/sketch_common/src/drivers/SdFileSys.cpp` (`Format()`).
- Gen3 format UI today: `tools/web/lib/pages/FormatPage.js`.
- Config file constant: `software/Libraries/onspeed_core/src/config/OnSpeedConfig.h:247` — `szDefaultConfigFilename = "onspeed2.cfg"`.
