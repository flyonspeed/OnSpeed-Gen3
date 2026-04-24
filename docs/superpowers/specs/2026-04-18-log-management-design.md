# Log Management UI & Metadata Sidecars — Design

## Context

OnSpeed's `/logs` web page today lists every `log_NNN.csv` file on the SD
card with a download link, a size in bytes, and a single-log delete link.
A pilot landing after a flight — or reviewing logs weeks later — has no
way to tell a ten-second bench test apart from a ninety-minute
cross-country without downloading each file. Cleaning out old logs means
N trips through the single-file confirmation page.

The underlying problem is that the ESP32 has no wall-clock RTC. Time of
day is available only indirectly, via EFIS packets, and only when the
EFIS carries it. The VN-300 INS provides full UTC (date + time) when it
has a GPS fix. The Dynon SkyView carries only `HH:MM:SS` (no date), and
the current parser doesn't capture even that. Garmin and MGL vary.

This design solves two pilot-facing problems:

1. **Identification** — when staring at the `/logs` list, which entry
   is the flight I want to download?
2. **Cleanup** — sweeping dead weight off the card shouldn't take one
   confirmation click per file.

It does so with three changes:

1. A **metadata sidecar** `log_NNN.meta` next to each log, written
   once at close. Plain `key=value\n` text. Accumulated during the
   session in a platform-independent builder in `onspeed_core/`.
2. A **rewritten `/logs` page** with per-log start time, duration,
   max IAS, max altitude, and **bulk-delete via checkboxes**. Server
   rendered, no JavaScript.
3. **Fix the Dynon SkyView parser** to capture `HH:MM:SS` from the
   `!1` ADAHRS frame's System Time field, which is currently skipped.

When a full UTC date is captured (VN-300 users), the log and its
sidecar are **renamed at close** from `log_NNN.csv` to
`YYYY-MM-DD_NNN.csv`. Dynon users get time-of-day in the sidecar and
the listing page but keep the `log_NNN.csv` filename on disk (no date
available to build a prefix with).

**Explicit non-goals**: flight segment detection, pilot-editable tags,
aircraft identifiers, bulk download / ZIP, inline CSV header preamble,
JSON sidecars, browser-clock date stamping, Dynon EMS engine-field
decoding, Dynon `!2` SYSTEM frame handling. These are follow-ups if
they turn out to matter; v1 deliberately stays small.

---

## Goals

1. A pilot scanning `/logs` can tell at a glance **which log is which
   flight**, using start-time-of-day and duration.
2. Cleaning out N old logs takes **one action**, not N.
3. Downloaded CSVs from VN-300-equipped installs carry the flight date
   in the filename. Dynon/no-EFIS installs fall back to `log_NNN.csv`
   — users identify flights from the listing page instead.
4. Existing CSV parsers (LogReplay, pandas, offline tooling) continue
   to work unchanged. No header preamble. No column reordering.
5. New code is platform-independent and natively unit-tested where
   possible, consistent with the ongoing `onspeed_core` extraction.

---

## Architecture

### Component layout

```
software/Libraries/onspeed_core/src/log/
  LogMeta.h          # POD struct + small EfisType enum
  LogMetaBuilder.h   # accumulator: Begin / OnRow / Finalize
  LogMetaBuilder.cpp
  LogMetaFile.h      # pure WriteMetaFile / ParseMetaFile
  LogMetaFile.cpp

software/Libraries/onspeed_core/src/efis/
  DynonSkyview.cpp   # MODIFIED: parse System Time HHMMSS from !1 frame

software/Libraries/onspeed_core/src/types/
  EfisFrame.h        # MODIFIED: add timeOfDay field

software/sketch_common/src/tasks/
  LogSensor.{cpp,h}  # MODIFIED: owns a LogMetaBuilder, writes sidecar
                     # and conditionally renames at Close()

software/sketch_common/src/web_server/
  ConfigWebServer.{cpp,h}  # MODIFIED:
                           #   - rewritten HandleLogs (enriched table,
                           #     checkboxes, "Delete selected" form)
                           #   - new HandleDeleteBulk
                           #   - IsSafeLogFilename allows .meta
                           #   - single delete also removes matching .meta

test/test_log_meta/
  test_log_meta.cpp  # round-trip, partial, malformed, buffer limits

test/test_efis_dynon_skyview/
  test_efis_dynon_skyview.cpp  # EXTENDED: System Time cases
```

The sidecar producer, parser, and POD type all live in `onspeed_core`.
Both the writer (`LogSensor`) and the reader (`ConfigWebServer`) call
into it. `onspeed_core` stays platform-independent — the `EfisType`
enum in `LogMeta.h` is a small local one; we do **not** pull
`EfisSerial.h` from the sketch into the core library.

### Data flow

```
Per-row (50 Hz, inside LogSensor::Write()):
  LogSensor::Write() builds a LogRow for the CSV
    -> m_metaBuilder.OnRow(row, efisTimeOfDayOrNull)
       - update max IAS, max altitude, last timeStampMs
       - on first row: record first timeStampMs
       - if EFIS time-of-day is fresh & valid: capture first, update last
       - if VN-300 UTC fresh & valid: capture first, update last

At close (LogSensor::Close(), already held under xWriteMutex):
  LogMeta meta = m_metaBuilder.Finalize()
  char buf[512]
  size_t n = onspeed::log::WriteMetaFile(meta, buf, sizeof(buf))
  write log_NNN.meta

  if meta.utcStart is populated (VN-300 path):
    snprintf target "YYYY-MM-DD_NNN.csv"
    if target doesn't exist:
      rename log_NNN.csv  -> target
      rename log_NNN.meta -> target with .meta
```

### Web UI flow

```
GET /logs
  PauseGuard + xWriteMutex (existing pattern)
  FileList from SD
  Group by base name (strip .csv / .meta / uppercase variants)
  For each log: if .meta exists, read & ParseMetaFile
  Render:
    <p>N logs, total X MB</p>
    <form method="POST" action="/delete-bulk">
      <table>
        <tr>
          <th></th><th>Name</th><th>Start</th><th>Duration</th>
          <th>Max IAS</th><th>Max Alt</th><th>Size</th><th></th>
        </tr>
        ... one row per log ...
      </table>
      <button type="submit">Delete selected</button>
    </form>

POST /delete-bulk (no confirm arg)
  Collect "f" form params, validate each via IsSafeLogFilename
  Render confirmation page showing selected list with metadata
  [Delete] -> POST /delete-bulk?confirm=yes with same selection
  [Cancel] -> link back to /logs

POST /delete-bulk (confirm=yes)
  xWriteMutex
  For each filename:
    remove(name)
    remove(name with .csv -> .meta)  (best effort, ignore missing)
  Redirect to /logs
```

Single-file `GET /delete?file=…` keeps its existing confirmation flow
unchanged, for backwards compatibility with bookmarks and the per-row
trash icon. It's extended to also remove the matching `.meta`.

---

## Sidecar file format

Plain text. One `key=value\n` per line. Trailing newline on last line.
Keys are stable identifiers; unknown keys are ignored by parsers.
Fields are written only when they have a meaningful value (for
example, `utc_start` is simply absent when no UTC was captured).

Chosen over JSON because:

- The file is tiny — a couple hundred bytes.
- No nesting needed.
- Line-oriented text is recoverable after a mid-write power loss.
- Avoids pulling ArduinoJson's surface area into `onspeed_core`.

### Example: VN-300 flight

```
meta_version=1
log_format_version=1
firmware=4.19.0
firmware_sha=35a3126
duration_ms=5432100
row_count=271605
max_ias_kt=142.3
max_palt_ft=8420
efis_type=vn300
gps_fix_seen=1
utc_start=2026-04-18T14:32:07Z
time_of_day_start=14:32:07
```

### Example: Dynon SkyView flight, GPS locked

```
meta_version=1
log_format_version=1
firmware=4.19.0
firmware_sha=35a3126
duration_ms=3780000
row_count=189000
max_ias_kt=138.6
max_palt_ft=6100
efis_type=dynon
gps_fix_seen=1
time_of_day_start=13:43:48
```

### Example: bench test, no EFIS

```
meta_version=1
log_format_version=1
firmware=4.19.0
firmware_sha=35a3126
duration_ms=12400
row_count=620
max_ias_kt=0.0
max_palt_ft=48
efis_type=none
gps_fix_seen=0
```

### Field list

| Key | Type | Always present | Source |
|---|---|---|---|
| `meta_version` | int | yes | constant `1`; bump when fields change |
| `log_format_version` | int | yes | `onspeed::proto::log_csv::kFormatVersion` |
| `firmware` | string | yes | `BuildInfo::version` |
| `firmware_sha` | string | yes | `BuildInfo::gitShortSha` |
| `duration_ms` | uint32 | yes | last `LogRow.timeStampMs` − first |
| `row_count` | uint32 | yes | incremented on each `OnRow` |
| `max_ias_kt` | float, 1 dp | yes | running max of `LogRow.iasKt` |
| `max_palt_ft` | int | yes | running max of `LogRow.paltFt` |
| `efis_type` | enum string | yes | `none` / `dynon` / `garmin` / `mgl` / `vn300` |
| `gps_fix_seen` | 0 / 1 | yes | 1 if any time source ever populated |
| `utc_start` | ISO-8601 UTC | only when VN-300 gave a fix | first valid UTC |
| `time_of_day_start` | `HH:MM:SS` | only when any EFIS gave time | first valid time |

Notes:

- No `utc_end` / `time_of_day_end`. Start + `duration_ms` conveys the
  same information; storing both invites drift and wastes bytes.
- When VN-300 is present and locked, **both** `utc_start` and
  `time_of_day_start` are populated. Listing page prefers `utc_start`
  (has date). Dynon/Garmin users get only `time_of_day_start`.

---

## Dynon SkyView System Time capture

### Spec (Dynon SkyView Installation Guide Appendix E, Table 132)

The `!1` ADAHRS packet is 74 bytes. Field #4 is **System Time**:

- 1-indexed spec position 4, width 8 → 0-indexed `buf_[3]..buf_[10]`
- Format: `HHMMSSFF`
  - `HH` hours 00-23
  - `MM` minutes 00-59
  - `SS` seconds 00-59
  - `FF` 1/16-second fraction 00-15 (may skip digits at low baud)
- Sentinel: `--------` (all dashes) = GPS has never been acquired

### Current parser behaviour

`onspeed_core/src/efis/DynonSkyview.cpp`'s `DecodeAdahrs` starts
reading fields at `buf_[11]` (Pitch). Bytes `buf_[3]..buf_[10]` are
silently skipped. `LogRow.efisTime` is always 0 for Dynon.

### Changes

- Parse `HHMMSS` from `buf_[3]..buf_[8]` (6 chars).
  - If any of those six chars is `-`, treat as absent.
  - Otherwise `atoi` each 2-char pair into H, M, S.
  - Range-check: `0 ≤ H ≤ 23 && 0 ≤ M ≤ 59 && 0 ≤ S ≤ 59`; otherwise
    absent.
- **Ignore** `buf_[9]..buf_[10]` (the FF fraction). We don't need
  sub-second precision for sidecar metadata.
- Add `timeOfDay` to `EfisFrame` — a small struct or `int32_t` encoded
  as `HHMMSS`, whichever fits the existing patterns in the file
  best (to be decided during implementation with minimal diff).
- When the frame applies to `g_EfisSerial` in the sketch, propagate
  to `LogRow.efisTime` and feed into `LogMetaBuilder.OnRow`.

### Test coverage to add (in `test_efis_dynon_skyview`)

- Valid ADAHRS frame with `143247FF` (plausible fraction byte) →
  time-of-day = 14:32:47.
- Valid frame with all dashes `--------` → time-of-day absent.
- Partial dashes `14----FF` → absent (conservative).
- Out-of-range `255999FF` → absent.
- Two valid frames with different times → parser always returns
  current frame's time, not the previous one.

---

## On-disk rename (VN-300 only in practice for v1)

At `LogSensor::Close()`, after the sidecar is written, if
`meta.utcStart` is populated:

```cpp
// Both files already exist as log_NNN.{csv,meta}
char target[32];
snprintf(target, sizeof(target), "%s_%03d.csv", datePrefix, iFileNum);
if (!g_SdFileSys.exists(target)) {
    g_SdFileSys.rename("log_NNN.csv",  target);
    // derive .meta paths from the above
    g_SdFileSys.rename("log_NNN.meta", target_with_meta);
}
```

If `SdFileSys` lacks a `rename` helper, add one that delegates to
`uSD_FAT.rename(old, new)`.

If the target name already exists — extremely unlikely since `NNN` is
monotonic within a session — skip both renames. The sidecar's
`utc_start` still identifies the log in the listing.

---

## `IsSafeLogFilename` — extension whitelist dropped

Current allow-list (`ConfigWebServer.cpp:137`): length ≤ 32,
alphanumeric + `_.-`, no slashes / `..`.

**Deviation from the original spec**: during review the user noted
that the pre-PR `/logs` page listed every file on the SD card
(config backups, any user-uploaded file, future `boot_log.txt`), and
restricting to `.csv`/`.log`/`.meta` would hide `onspeed2.cfg`. The
extension whitelist was removed entirely. Path traversal is still
blocked by the `/`, `\`, and `..` checks plus the alphanumeric+`_.-`
character class, which is the real security boundary.

The `/logs` page renders two tables: a "Logs" section (csv/log with
metadata columns + checkboxes + bulk delete) and an "Other files"
section (everything else, simple name/size/download/delete).

Maximum real filenames:

- `YYYY-MM-DD_NNN.csv` = 19 chars.
- `YYYY-MM-DD_NNN.meta` = 20 chars.
- `log_999999.csv` = 14 chars.
- `onspeed2.cfg` = 12 chars.

## VN-300 date caveat

The spec earlier described `utc_start` as ISO-8601
`"YYYY-MM-DDTHH:MM:SSZ"` and the rename path as activating "for
VN-300 users." **Implementation reality**: the current
`Vn300Parser::Decode` only reads the VN-300 packet's hour/minute/
second bytes (not year/month/day), so `szTimeUTC` is populated with
`"H:M:S"` alone. As a result, the rename path activates only when
some future code (extended VN-300 parser, GPS module, etc.) writes
a real ISO-8601 string into `utc_start`. The rename block in
`LogSensor::Close()` explicitly validates that `utc_start` begins
with `YYYY-MM-DDT` before using it — a malformed prefix containing
`:` would otherwise produce an illegal FAT filename.

Until that parser extension lands, every session keeps its
`log_NNN.{csv,meta}` filename. The sidecar still records
`time_of_day_start` from whichever EFIS supplies it (Dynon does
as of this PR, via System Time capture).

---

## `LogMetaBuilder` interface

```cpp
// onspeed_core/src/log/LogMeta.h
namespace onspeed::log {

enum class EfisType : uint8_t {
    None = 0, Dynon, Garmin, Mgl, Vn300
};

static constexpr size_t kUtcLen = 24;   // "YYYY-MM-DDTHH:MM:SSZ" + slack
static constexpr size_t kHmsLen = 9;    // "HH:MM:SS" + NUL

struct LogMeta {
    uint8_t  metaVersion       = 1;
    int      logFormatVersion  = 0;
    char     firmware[24]      = {};
    char     firmwareSha[16]   = {};
    uint32_t durationMs        = 0;
    uint32_t rowCount          = 0;
    float    maxIasKt          = 0.0f;
    float    maxPaltFt         = 0.0f;
    EfisType efisType          = EfisType::None;
    bool     gpsFixSeen        = false;
    char     utcStart[kUtcLen] = {};
    char     timeOfDayStart[kHmsLen] = {};
};

} // namespace onspeed::log
```

```cpp
// onspeed_core/src/log/LogMetaBuilder.h
namespace onspeed::log {

class LogMetaBuilder {
public:
    // Call once at session start. Safe against null/oversized strings.
    void Begin(const char* firmware,
               const char* firmwareSha,
               int         logFormatVersion,
               EfisType    efisType);

    // Call once per row. `hmsOrNull` is an 8-char "HH:MM:SS" string
    // (or nullptr when absent). `utcOrNull` is ISO-8601 UTC (or nullptr).
    void OnRow(const onspeed::LogRow& row,
               const char* hmsOrNull,
               const char* utcOrNull);

    // Finalise accumulated state into a populated LogMeta.
    LogMeta Finalize() const;

    void Reset();

private:
    LogMeta  m_meta{};
    uint32_t m_firstTimeMs  = 0;
    bool     m_haveFirstRow = false;
};

} // namespace onspeed::log
```

Per-row cost: two float compares, one int increment, two pointer-null
tests, one conditional string copy (only on *first* non-null time
seen). Dominated by CSV formatting already in `LogSensor::Write()`.

---

## `LogMetaFile` interface

```cpp
// onspeed_core/src/log/LogMetaFile.h
namespace onspeed::log {

// Returns bytes written on success, 0 on buffer-too-small.
size_t WriteMetaFile(const LogMeta& meta, char* buf, size_t bufLen);

// Parse key=value text. Unknown keys ignored, missing keys leave
// the corresponding LogMeta field at its default. Returns false only
// when no keys are recognised at all (malformed input).
bool ParseMetaFile(std::string_view text, LogMeta* out);

} // namespace onspeed::log
```

Both functions are pure. No I/O, no globals. `LogSensor` calls
`WriteMetaFile` + writes bytes to SD. `ConfigWebServer` reads bytes
from SD + calls `ParseMetaFile`.

---

## `/logs` page layout

```
N logs, 86.4 MB total

[☐]  Name               Start                 Duration    Max IAS  Max Alt   Size
[☐]  2026-04-18_042.csv 2026-04-18 14:32      1h 30m 42s  142 kt   8,420 ft  14.2 MB  [🗑]
[☐]  log_041.csv        13:43                 1h 3m 0s    138 kt   6,100 ft  11.8 MB  [🗑]
[☐]  log_040.csv        —                     12s         —        —         45 KB    [🗑]
...
                                                                   [Delete selected]
```

- Checkbox column submits names via a single form.
- Start column: `YYYY-MM-DD HH:MM` when `utc_start` is present;
  `HH:MM` when only `time_of_day_start` is present; `—` otherwise.
- Duration: `1h 30m 42s`, `1m 12s`, `12s` — drop leading zero units.
- Max IAS: rounded integer knots, or `—` when the sidecar reports 0
  AND no EFIS (bench-test heuristic — the 0 could be legitimate but
  we show `—` rather than misleading).

Actually revise that last point: **always show the numeric value**.
A 0.0 max is information ("never saw airspeed"); `—` should be
reserved for "no sidecar at all." Keeps the rendering rule simple.

### Bulk delete confirmation

```
Delete these 3 files?

  2026-04-18_042.csv   2026-04-18 14:32  1h 30m   14.2 MB
  log_040.csv          —                 12s      45 KB
  log_038.csv          13:01             42s      1.2 MB

[Delete]   [Cancel]
```

Each delete removes both `.csv` and matching `.meta`. Individual
failures don't abort the batch — the page reload shows what remains.

---

## Files to modify

| Path | Change |
|---|---|
| `software/Libraries/onspeed_core/src/log/LogMeta.h` | **NEW** — POD + `EfisType` |
| `software/Libraries/onspeed_core/src/log/LogMetaBuilder.{h,cpp}` | **NEW** — accumulator |
| `software/Libraries/onspeed_core/src/log/LogMetaFile.{h,cpp}` | **NEW** — pure writer/parser |
| `software/Libraries/onspeed_core/CMakeLists.txt` | Register the four new files |
| `software/Libraries/onspeed_core/library.json` | (if it enumerates headers) register them |
| `software/Libraries/onspeed_core/src/efis/DynonSkyview.cpp` | Parse System Time; range-check; handle `--------` |
| `software/Libraries/onspeed_core/src/types/EfisFrame.h` | Add `timeOfDay` field |
| `software/sketch_common/src/drivers/SdFileSys.{h,cpp}` | Add `rename(const char*, const char*)` if not present |
| `software/sketch_common/src/tasks/LogSensor.{cpp,h}` | Hold `LogMetaBuilder`; wire through `Open`/`Write`/`Close`; write sidecar; conditional rename |
| `software/sketch_common/src/web_server/ConfigWebServer.cpp` | Rewrite `HandleLogs`; add `HandleDeleteBulk`; extend single-delete; extend `IsSafeLogFilename`; register `POST /delete-bulk` route |
| `test/test_log_meta/test_log_meta.cpp` | **NEW** |
| `test/test_efis_dynon_skyview/test_efis_dynon_skyview.cpp` | Extend with System Time cases |

### Helpers to reuse as-is

- `sFormatBytes()` — `ConfigWebServer.cpp:3476`
- `IsSafeLogFilename()` — `ConfigWebServer.cpp:137` (extend, don't replace)
- `PauseGuard` pattern in `HandleLogs` — copy for bulk-delete handler
- `BuildInfo::version`, `BuildInfo::gitShortSha`
- `onspeed::LogRow`, `onspeed::proto::log_csv::kFormatVersion`
- `DynonSkyviewParser::parseFieldInt`

---

## Failure modes

| Mode | Behaviour |
|---|---|
| Power loss during sidecar write | CSV is already closed; sidecar may be truncated. `/logs` renders the log with `—` in metadata cols; delete works. |
| Sidecar missing for an old log | Same as above — graceful `—`. |
| Malformed sidecar (corrupt bytes) | `ParseMetaFile` returns false; UI treats as missing. |
| Rename target collision | Skip both renames; keep `log_NNN.*`. Sidecar's `utc_start` still identifies the log. |
| SD full during sidecar write | Log an `EnDisk` / `EnWarning`. Not fatal — CSV is safe. |
| Session with no GPS fix | No rename, `utc_start` absent, `time_of_day_start` absent. Duration + max values still shown. |
| Dynon frame with `--------` | Time-of-day absent. `gps_fix_seen` stays at 0 unless another source populates it. |

---

## Verification

### Unit tests (`pio test -e native -v`)

**`test_log_meta`** (new suite):

1. Round-trip: `LogMeta` populated with all fields → `WriteMetaFile`
   → `ParseMetaFile` reproduces every field byte-for-byte.
2. Round-trip without `utc_start` and `time_of_day_start` — missing
   fields stay at defaults after parse.
3. Parser ignores unknown `foo=bar` lines.
4. Empty input → returns false, `out` untouched.
5. Buffer-too-small on write → returns 0, buffer first byte is NUL.
6. `LogMetaBuilder.OnRow`:
   - max IAS/alt monotonic across a synthetic 100-row sequence
   - first row's `timeStampMs` captured as start
   - first non-null `hmsOrNull` captured, later ones don't overwrite
   - first non-null `utcOrNull` captured, later ones don't overwrite
   - `gps_fix_seen` becomes 1 on first time source seen, stays 1
7. `Finalize()` with zero rows → all values zero, `gps_fix_seen=false`,
   no crash.

**`test_efis_dynon_skyview`** (extend):

1. Valid ADAHRS frame with `143247FF` → `timeOfDay` encodes 14:32:47.
2. All-dashes `--------` → `timeOfDay` absent.
3. Partial dashes `14----FF` → absent.
4. Out-of-range `253247FF`, `146047FF`, `143260FF` → absent.
5. Two consecutive frames with different valid times → second frame
   reports second time (parser is stateless per frame).

### Firmware build

- `pio run -e esp32s3-v4p` — clean under `-Werror`.
- `scripts/cppcheck.sh` — passes.

### Manual end-to-end on V4P hardware

1. **No EFIS**, 30-second run: sidecar `efis_type=none`, no time
   fields, `gps_fix_seen=0`. `/logs` shows `— · 30s`.
2. **Dynon SkyView with GPS locked**, 60-second run: sidecar has
   `efis_type=dynon`, `time_of_day_start=HH:MM:SS`, no `utc_start`.
   `/logs` shows `HH:MM · 1m 0s`. Filename stays `log_NNN.csv`.
3. **Dynon SkyView without GPS lock**: `gps_fix_seen=0`, no time.
   `/logs` shows `— · 1m 0s`.
4. **VN-300 with GPS fix** (if accessible): sidecar has both UTC
   and time-of-day. File renamed to `YYYY-MM-DD_NNN.csv`. `/logs`
   shows date+time.
5. **5+ logs on card**: check three, "Delete selected", confirm —
   six files (3 .csv + 3 .meta) removed. Redirect back cleanly.
6. **Hand-edit a `.meta` to truncate mid-line**, reload `/logs` —
   row renders with partial fields, no server error.

### CI

- `pio test -e native` picks up `test_log_meta` automatically.
- `pio run -e esp32s3-v4p` stays green.
- `cppcheck` stays green.

---

## Non-goals (explicit)

- **Flight segment detection** — no airborne/landed state machine,
  no touch-and-go counting. Offline tooling can do this from the CSV.
- **Pilot-editable tags / notes / aircraft IDs** — one plane per box
  for now.
- **Bulk download / ZIP** — one click per file is acceptable.
- **Histograms, percentiles, stats beyond running max** — sidecar
  stays small.
- **Index / manifest file** — each sidecar is self-contained.
- **Rebuild-metadata tool for old logs** — `LogMetaBuilder`'s shape
  supports adding this later.
- **Inline CSV header preamble (Dynon-style key=value block)** —
  rejected. Sidecars do the job without breaking CSV semantics.
- **`Content-Disposition`-based renaming at download time** —
  rejected. Date needs to be visible in the listing, not just after
  download.
- **Browser-clock-based date stamping** — "date of download" isn't
  "date flown." Silently wrong is worse than absent.
- **Dynon EMS engine-field decoding (RPM, MAP, fuel)** — the `!3`
  decoder currently skips everything; leave for a follow-up PR.
- **Dynon `!2` SYSTEM frame handling** — user's install is
  ADAHRS + EMS only; no SYSTEM frames on the wire.

---

## Open questions

None blocking. Any issue that surfaces during implementation should
be raised as a follow-up rather than bundled into this PR.
