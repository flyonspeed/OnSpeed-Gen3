// BootDiagnostics.cpp
//
// See BootDiagnostics.h for the access paths and calling contract.
//
#include "src/util/BootDiagnostics.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>      // esp_reset_reason_t, esp_reset_reason()
#include <esp_core_dump.h>   // esp_core_dump_image_check / get / erase / get_panic_reason
#include <esp_partition.h>   // esp_partition_find_first / esp_partition_read
#include <buildinfo.h>       // BuildInfo::version

#include "src/Globals.h"     // g_SdFileSys, g_Log, xWriteMutex, xSerialLogMutex

namespace {

// NVS namespace and keys. Namespace must be <= 15 chars (ESP-IDF limit).
// Key names must be <= 15 chars too — abbreviate cd_busy/cd_retry rather than
// "coredump_busy" so the NVS write doesn't truncate silently.
constexpr const char * kNvsNamespace  = "onspeed_boot";
constexpr const char * kKeyBootCount  = "boot_count";
constexpr const char * kKeyAliveMs    = "last_alive_ms";
constexpr const char * kKeyCdBusy     = "cd_busy";    // bool, set during archival
constexpr const char * kKeyCdRetry    = "cd_retry";   // uint, archival retry counter

constexpr const char * kBootLogPath   = "/boot_log.txt";
constexpr const char * kCoredumpDir   = "/coredumps";

// Cap retries so a chronically-bad coredump (parser bug, partition corruption)
// can't burn boot time forever. After this many failed archival attempts we
// erase the partition unread and move on. Three is enough to clear transient
// SD glitches without wedging the box.
constexpr uint32_t  kCoredumpRetryLimit = 3;

// Buffer for the panic reason string extracted from the coredump.
// ESP-IDF documents reasons up to ~200 chars (assert message + filename
// + condition); 256 leaves margin without burning stack at boot.
constexpr size_t    kPanicReasonMaxLen  = 256;

// Sentinel distinguishing "NVS has never been written" (first-ever boot on a
// fresh chip, or wiped partition) from "Init() ran but Heartbeat hasn't
// crossed the first threshold yet" (the 0 we persist at start of each
// boot). Without this split, a boot that dies within the first 5 s looks
// the same as a first-ever boot — and that distinction is exactly what
// the field diagnostics need to make. UINT32_MAX is safe as a sentinel:
// the threshold values Heartbeat writes top out at 3,600,000 (1 hr) and
// the 0 we write in Init is fixed.
constexpr uint32_t  kAliveNeverWritten = 0xFFFFFFFFu;

// Cached state populated by Init().
bool                s_bInitialized   = false;
bool                s_bNvsAvailable  = false;
uint32_t            s_uBootCount     = 0;
esp_reset_reason_t  s_enResetReason  = ESP_RST_UNKNOWN;
uint32_t            s_uPrevAliveMs   = 0;
bool                s_bSdLogWritten  = false;

// Persistent NVS handle. Opened in Init() and held for the life of the
// program so Heartbeat's threshold writes don't need to re-begin() (and
// walk the namespace table) on every HousekeepingTask tick.
//
// Concurrency invariant: s_Nvs is accessed by Init() during setup() and by
// Heartbeat() from HousekeepingTask afterwards — never concurrently. If a
// future caller (e.g. a console command that resets counters) also writes
// to this handle from another task, it must be serialized externally; the
// ESP-IDF Preferences API is not documented as thread-safe per handle.
Preferences         s_Nvs;

// ----------------------------------------------------------------------------
// Map each esp_reset_reason_t to a short, stable text name used in the log
// and on Serial. "BROWNOUT" is the smoking-gun case for Vcc-dip theories.
// The switch has no default: clause on purpose — any future IDF enumerator
// that isn't listed here is caught at build time by -Werror=switch, so we
// never silently collapse a new signal (like when ESP-IDF 5.x added
// ESP_RST_USB / ESP_RST_JTAG / ESP_RST_PWR_GLITCH / etc.) into "UNKNOWN".
const char * ResetReasonName(esp_reset_reason_t reason)
    {
    switch (reason)
        {
        case ESP_RST_UNKNOWN:    return "UNKNOWN";
        case ESP_RST_POWERON:    return "POWERON";
        case ESP_RST_EXT:        return "EXTERNAL";
        case ESP_RST_SW:         return "SW";
        case ESP_RST_PANIC:      return "PANIC";
        case ESP_RST_INT_WDT:    return "WDT_INT";
        case ESP_RST_TASK_WDT:   return "WDT_TASK";
        case ESP_RST_WDT:        return "WDT";
        case ESP_RST_DEEPSLEEP:  return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:   return "BROWNOUT";
        case ESP_RST_SDIO:       return "SDIO";
        case ESP_RST_USB:        return "USB";
        case ESP_RST_JTAG:       return "JTAG";
        case ESP_RST_EFUSE:      return "EFUSE";
        case ESP_RST_PWR_GLITCH: return "PWRGLITCH";
        case ESP_RST_CPU_LOCKUP: return "CPULOCKUP";
        }
    // Switch is intentionally exhaustive over esp_reset_reason_t. If a
    // future IDF upgrade adds a new enumerator, -Werror=switch fails the
    // build, forcing us to add it explicitly (rather than silently
    // collapsing it into a catch-all "UNKNOWN" and losing the data this
    // module exists to capture).
    __builtin_unreachable();
    }

// Formats the previous-boot uptime for display. Returns sz for use in
// printf chains. The NVS value is one of:
//   kAliveNeverWritten  -> "unknown" : first-ever boot or wiped NVS.
//   0                   -> "<5s"     : prior boot's Init() ran but died
//                                      before Heartbeat crossed the first
//                                      threshold (5 s).
//   N > 0               -> ">=Ns"    : prior boot reached the N-millisecond
//                                      threshold (but may have run longer
//                                      or died in the next bucket). Values
//                                      come from kThresholdsMs in Heartbeat.
const char * FormatPrevAlive(char * sz, size_t n, uint32_t ms)
    {
    if (ms == kAliveNeverWritten)
        snprintf(sz, n, "unknown");
    else if (ms == 0)
        snprintf(sz, n, "<5s");
    else
        snprintf(sz, n, ">=%us", static_cast<unsigned>(ms / 1000));
    return sz;
    }

}  // anonymous namespace

namespace BootDiag {

// ----------------------------------------------------------------------------

void Init()
    {
    s_enResetReason = esp_reset_reason();

    // Open the NVS handle and keep it open for the life of the program.
    if (!s_Nvs.begin(kNvsNamespace, /*readOnly=*/false))
        {
        // NVS unavailable (corrupted partition table, etc.) — fall back to
        // counter=1, prev_alive=unknown, and mark ourselves initialized so
        // later calls no-op instead of crashing. s_bNvsAvailable stays false
        // so Heartbeat() skips its NVS write on every tick.
        s_uBootCount    = 1;
        s_uPrevAliveMs  = kAliveNeverWritten;
        s_bInitialized  = true;
        g_Log.printf("Boot #%u (fw=%s, reset=%s, prev_alive=unknown) [NVS unavailable]\n",
                     static_cast<unsigned>(s_uBootCount),
                     BuildInfo::version,
                     ResetReasonName(s_enResetReason));
        return;
        }

    // Missing keys return the supplied default — kAliveNeverWritten marks a
    // true NVS miss (first-ever boot) distinct from the 0 we persist below.
    s_uBootCount   = s_Nvs.getUInt(kKeyBootCount, 0) + 1;
    s_uPrevAliveMs = s_Nvs.getUInt(kKeyAliveMs,   kAliveNeverWritten);

    // Safe-mode check: if cd_busy is still set from the prior boot it means
    // archival itself crashed (parser bug, SD-driver fault, mid-archival
    // power-pull, etc). Sacrifice the data this once and erase the coredump
    // partition so this boot can complete normally. Without this guard a
    // single bad coredump could brick every box it ever touches.
    if (s_Nvs.getBool(kKeyCdBusy, false))
        {
        g_Log.println("BootDiag: prior boot crashed during coredump archival; "
                      "erasing coredump partition to recover");
        esp_core_dump_image_erase();
        s_Nvs.putBool(kKeyCdBusy, false);
        s_Nvs.putUInt(kKeyCdRetry, 0);
        }

    // Persist the new boot counter and reset the alive marker to 0, meaning
    // "this boot has started but Heartbeat hasn't crossed its first threshold
    // yet." A next-boot read of 0 is unambiguous evidence that the prior
    // boot died within the first 5 s (the first threshold).
    s_Nvs.putUInt(kKeyBootCount, s_uBootCount);
    s_Nvs.putUInt(kKeyAliveMs,   0);

    s_bInitialized  = true;
    s_bNvsAvailable = true;

    char szPrevAlive[16];
    g_Log.printf("Boot #%u (fw=%s, reset=%s, prev_alive=%s)\n",
                 static_cast<unsigned>(s_uBootCount),
                 BuildInfo::version,
                 ResetReasonName(s_enResetReason),
                 FormatPrevAlive(szPrevAlive, sizeof(szPrevAlive), s_uPrevAliveMs));
    }

// ----------------------------------------------------------------------------

void Heartbeat()
    {
    if (!s_bNvsAvailable)
        return;

    // Threshold-based alive tracking — write NVS only when the prior boot's
    // uptime crosses into a new diagnostic bucket. Continuous writes at
    // 0.5 Hz would trigger NVS sector erases every ~10 min of flight time;
    // an erase pauses both CPU cores (cache-off) for 40-400 ms, causing an
    // audible audio pop and missed IMU samples. That cost is unacceptable
    // for a flight instrument, and the extra resolution bought by per-tick
    // writes isn't needed — "died in 5-60 s" vs "died in 60-300 s" is
    // plenty for root-cause analysis.
    //
    // Post-boot write counts: one per threshold crossed. After the 1-hour
    // threshold we stop writing entirely. A 20 KB NVS partition (5 pages)
    // comfortably holds thousands of entries before GC triggers a sector
    // erase, and with ≤5 writes per boot, GC now happens on the order of
    // hundreds of boots apart instead of every 10 minutes of flight.
    static constexpr uint32_t kThresholdsMs[] = {
        5000u,        // 5 s    — "died during early init"
        60000u,       // 1 min  — "died shortly after startup"
        300000u,      // 5 min  — "died during early flight"
        1800000u,     // 30 min — "died mid-flight"
        3600000u,     // 1 hr   — "probably ran to healthy shutdown"
    };
    constexpr size_t kThresholdCount = sizeof(kThresholdsMs) / sizeof(kThresholdsMs[0]);

    static size_t sNextThreshold = 0;
    if (sNextThreshold >= kThresholdCount)
        return;

    const uint32_t uNowMs = millis();
    if (uNowMs < kThresholdsMs[sNextThreshold])
        return;

    s_Nvs.putUInt(kKeyAliveMs, kThresholdsMs[sNextThreshold]);
    sNextThreshold++;
    }

// ----------------------------------------------------------------------------

namespace {

// Replace characters that don't survive FAT32 cleanly (notably '+' from
// our build version semver-meta) with '_'. Edits in-place, returns sz.
char * SanitizeForFatFilename(char * sz)
    {
    for (char * p = sz; *p; ++p)
        if (*p == '+' || *p == ' ' || *p == ':' || *p == '/' || *p == '\\')
            *p = '_';
    return sz;
    }

// Best-effort archival of any panic coredump left in flash by a prior boot.
// Called from AppendToSd while holding xWriteMutex with the boot_log file
// already open. Appends indented panic-summary lines under the current
// boot's row when there's something to report; silent when there isn't.
//
// The whole flow is wrapped in a NVS "cd_busy" sentinel so that if this
// function itself crashes (parser bug, partition read fault), the next
// boot's Init() detects the unfinished archival and erases the partition
// to recover. Without that guard a chronically-bad coredump would brick
// the box on every reboot.
void ArchivePriorPanicCoredump(FsFile & hBootLogFile)
    {
    if (!s_bNvsAvailable)
        return;  // Can't safely arm the safe-mode sentinel without NVS.

    // Cheap check first — most boots have nothing to archive. ESP_OK means
    // "valid, decodable dump present"; any other code (NOT_FOUND, INVALID_CRC,
    // etc.) means we have nothing to do.
    if (esp_core_dump_image_check() != ESP_OK)
        return;

    // Bound retries so a coredump we can't process doesn't burn boot time
    // forever. After kCoredumpRetryLimit failed attempts we erase the
    // partition and accept the data loss — better than a permanently
    // degraded boot path.
    const uint32_t uRetryCount = s_Nvs.getUInt(kKeyCdRetry, 0);
    if (uRetryCount >= kCoredumpRetryLimit)
        {
        g_Log.printf(MsgLog::EnMain, MsgLog::EnWarning,
                     "BootDiag: coredump archival has failed %u times, erasing\n",
                     static_cast<unsigned>(uRetryCount));
        esp_core_dump_image_erase();
        s_Nvs.putUInt(kKeyCdRetry, 0);
        return;
        }

    // Arm the safe-mode sentinel BEFORE touching the partition. If anything
    // below crashes the chip, Init() on the next boot sees this flag still
    // set and clears it + erases the partition.
    s_Nvs.putBool(kKeyCdBusy, true);

    // Get the partition contents location and size in flash.
    size_t uDumpAddr = 0;
    size_t uDumpSize = 0;
    if (esp_core_dump_image_get(&uDumpAddr, &uDumpSize) != ESP_OK || uDumpSize == 0)
        {
        g_Log.println(MsgLog::EnMain, MsgLog::EnWarning,
                      "BootDiag: image_check OK but image_get failed; erasing");
        esp_core_dump_image_erase();
        s_Nvs.putBool(kKeyCdBusy, false);
        s_Nvs.putUInt(kKeyCdRetry, 0);
        return;
        }

    // Find the coredump partition object so we can read raw bytes.
    const esp_partition_t * pPart = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
    if (pPart == nullptr)
        {
        g_Log.println(MsgLog::EnMain, MsgLog::EnError,
                      "BootDiag: coredump partition not found in partition table");
        s_Nvs.putBool(kKeyCdBusy, false);
        s_Nvs.putUInt(kKeyCdRetry, uRetryCount + 1);
        return;
        }

    // Pull the panic reason string and a summary if the IDF can parse them.
    // Either may fail independently of the raw read; we still archive the
    // .bin even if these come back empty (offline tools can do more than
    // these on-device parsers).
    char szPanicReason[kPanicReasonMaxLen] = {0};
    bool bHavePanicReason =
        esp_core_dump_get_panic_reason(szPanicReason, sizeof(szPanicReason)) == ESP_OK;

    esp_core_dump_summary_t * pSummary = static_cast<esp_core_dump_summary_t *>(
        heap_caps_malloc(sizeof(esp_core_dump_summary_t),
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    bool bHaveSummary = false;
    if (pSummary != nullptr)
        {
        memset(pSummary, 0, sizeof(*pSummary));
        bHaveSummary = (esp_core_dump_get_summary(pSummary) == ESP_OK);
        }

    // Build a filename. Format: coredump_<NNNN>_<version>_<task>.bin
    // <NNNN> is THIS boot's count (the recovery boot, since the dying boot
    // didn't get to write its own log line); pilot pairs the file with the
    // matching boot row by reading boot_log.txt. Version is the recovery
    // boot's firmware string, which equals the dying boot's firmware in
    // every case where the user didn't reflash between panic and recovery
    // (the common case). Task name is the crashing-task name when the
    // summary parser succeeded, omitted otherwise.
    char szVersion[64];
    snprintf(szVersion, sizeof(szVersion), "%s", BuildInfo::version);
    SanitizeForFatFilename(szVersion);

    char szTaskSuffix[24] = {0};
    if (bHaveSummary && pSummary->exc_task[0] != '\0')
        {
        char szTaskName[20];
        snprintf(szTaskName, sizeof(szTaskName), "%.15s", pSummary->exc_task);
        SanitizeForFatFilename(szTaskName);
        snprintf(szTaskSuffix, sizeof(szTaskSuffix), "_%s", szTaskName);
        }

    // Sized to fit the worst case the compiler can prove: kCoredumpDir(10)
    // + "/coredump_NNNN_" + szVersion (up to 63) + szTaskSuffix (up to 23)
    // + ".bin\0" — call it 128 with margin. -Werror=format-truncation forces
    // this to be tight; if either component grows, bump this and re-verify.
    char szDumpPath[128];
    snprintf(szDumpPath, sizeof(szDumpPath),
             "%s/coredump_%04u_%s%s.bin",
             kCoredumpDir,
             static_cast<unsigned>(s_uBootCount),
             szVersion,
             szTaskSuffix);

    // Make /coredumps/ if it doesn't exist. SdFat's mkdir is harmless if
    // the directory already exists.
    if (!g_SdFileSys.exists(kCoredumpDir))
        {
        if (!g_SdFileSys.mkdir(kCoredumpDir))
            {
            g_Log.printf(MsgLog::EnMain, MsgLog::EnWarning,
                         "BootDiag: failed to mkdir %s, archival aborted\n",
                         kCoredumpDir);
            if (pSummary) heap_caps_free(pSummary);
            s_Nvs.putBool(kKeyCdBusy, false);
            s_Nvs.putUInt(kKeyCdRetry, uRetryCount + 1);
            return;
            }
        }

    // Allocate a single buffer for the full dump. Coredumps top out at the
    // partition size (64 KB on this build) and we have ~290 KB free heap
    // here at boot time, well before WiFi softAP. Internal DRAM only —
    // PSRAM access at this stage of boot is fragile.
    uint8_t * pBuffer = static_cast<uint8_t *>(
        heap_caps_malloc(uDumpSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (pBuffer == nullptr)
        {
        g_Log.printf(MsgLog::EnMain, MsgLog::EnError,
                     "BootDiag: heap_caps_malloc(%u) failed for coredump archival\n",
                     static_cast<unsigned>(uDumpSize));
        if (pSummary) heap_caps_free(pSummary);
        s_Nvs.putBool(kKeyCdBusy, false);
        s_Nvs.putUInt(kKeyCdRetry, uRetryCount + 1);
        return;
        }

    // esp_partition_read takes an offset within the partition (0-based),
    // not the absolute flash address esp_core_dump_image_get returned.
    // The image always starts at offset 0 within the coredump partition.
    esp_err_t enReadErr = esp_partition_read(pPart, 0, pBuffer, uDumpSize);
    if (enReadErr != ESP_OK)
        {
        g_Log.printf(MsgLog::EnMain, MsgLog::EnError,
                     "BootDiag: esp_partition_read failed: %d\n",
                     static_cast<int>(enReadErr));
        heap_caps_free(pBuffer);
        if (pSummary) heap_caps_free(pSummary);
        s_Nvs.putBool(kKeyCdBusy, false);
        s_Nvs.putUInt(kKeyCdRetry, uRetryCount + 1);
        return;
        }

    // Write to SD. O_WRONLY | O_CREAT | O_TRUNC so a duplicate filename
    // (same boot count, which can happen if a prior archival made it to
    // the file but didn't survive long enough to erase) gets overwritten
    // cleanly rather than appended.
    FsFile hDumpFile = g_SdFileSys.open(szDumpPath,
                                        O_WRONLY | O_CREAT | O_TRUNC);
    if (!hDumpFile.isOpen())
        {
        g_Log.printf(MsgLog::EnMain, MsgLog::EnError,
                     "BootDiag: failed to open %s for write\n", szDumpPath);
        heap_caps_free(pBuffer);
        if (pSummary) heap_caps_free(pSummary);
        s_Nvs.putBool(kKeyCdBusy, false);
        s_Nvs.putUInt(kKeyCdRetry, uRetryCount + 1);
        return;
        }

    const size_t cbWritten = hDumpFile.write(pBuffer, uDumpSize);
    hDumpFile.sync();
    hDumpFile.close();
    heap_caps_free(pBuffer);

    if (cbWritten != uDumpSize)
        {
        g_Log.printf(MsgLog::EnMain, MsgLog::EnError,
                     "BootDiag: short write to %s (%u of %u)\n",
                     szDumpPath, static_cast<unsigned>(cbWritten),
                     static_cast<unsigned>(uDumpSize));
        // Don't erase the partition — leave it for next boot's retry.
        if (pSummary) heap_caps_free(pSummary);
        s_Nvs.putBool(kKeyCdBusy, false);
        s_Nvs.putUInt(kKeyCdRetry, uRetryCount + 1);
        return;
        }

    // Append indented panic-summary lines under the current boot's row in
    // boot_log.txt. The panic-reason line is the high-value piece even
    // without the rest — most assert messages are self-explanatory to a
    // developer who has seen the failure mode before.
    char szSummaryLine[320];
    int iLen = snprintf(szSummaryLine, sizeof(szSummaryLine),
                        "  archived prior-boot coredump to %s\n",
                        szDumpPath);
    if (iLen > 0)
        {
        size_t cb = static_cast<size_t>(iLen);
        if (cb >= sizeof(szSummaryLine)) cb = sizeof(szSummaryLine) - 1;
        hBootLogFile.write(reinterpret_cast<const uint8_t *>(szSummaryLine), cb);
        }

    if (bHavePanicReason && szPanicReason[0] != '\0')
        {
        iLen = snprintf(szSummaryLine, sizeof(szSummaryLine),
                        "  panic: %s\n", szPanicReason);
        if (iLen > 0)
            {
            size_t cb = static_cast<size_t>(iLen);
            if (cb >= sizeof(szSummaryLine)) cb = sizeof(szSummaryLine) - 1;
            hBootLogFile.write(reinterpret_cast<const uint8_t *>(szSummaryLine), cb);
            }
        }

    if (bHaveSummary)
        {
        iLen = snprintf(szSummaryLine, sizeof(szSummaryLine),
                        "  task: %.15s  pc: 0x%08x  tcb: 0x%08x\n",
                        pSummary->exc_task,
                        static_cast<unsigned>(pSummary->exc_pc),
                        static_cast<unsigned>(pSummary->exc_tcb));
        if (iLen > 0)
            {
            size_t cb = static_cast<size_t>(iLen);
            if (cb >= sizeof(szSummaryLine)) cb = sizeof(szSummaryLine) - 1;
            hBootLogFile.write(reinterpret_cast<const uint8_t *>(szSummaryLine), cb);
            }
        }

    if (pSummary) heap_caps_free(pSummary);

    // Success: erase the partition and clear retry/sentinel state.
    esp_core_dump_image_erase();
    s_Nvs.putUInt(kKeyCdRetry, 0);
    s_Nvs.putBool(kKeyCdBusy, false);

    g_Log.printf("BootDiag: archived coredump (%u bytes) to %s\n",
                 static_cast<unsigned>(uDumpSize), szDumpPath);
    }

}  // anonymous namespace

// ----------------------------------------------------------------------------

void AppendToSd()
    {
    if (!s_bInitialized)
        return;

    if (s_bSdLogWritten)
        return;  // One-shot guard against accidental double-call.

    if (!g_SdFileSys.bSdAvailable)
        {
        g_Log.println(MsgLog::EnMain, MsgLog::EnWarning,
                      "BootDiag: SD unavailable, skipping boot_log.txt append");
        return;
        }

    // Matches the SD-access pattern used in LogSensor: take xWriteMutex,
    // open/write/close, give. setup() has no contention so the take is
    // essentially free here, but the pattern documents the invariant.
    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(500)) != pdTRUE)
        {
        g_Log.println(MsgLog::EnMain, MsgLog::EnWarning,
                      "BootDiag: xWriteMutex timeout, skipping boot_log.txt");
        return;
        }

    FsFile hFile = g_SdFileSys.open(kBootLogPath, O_WRONLY | O_CREAT | O_APPEND);
    if (!hFile.isOpen())
        {
        xSemaphoreGive(xWriteMutex);
        g_Log.println(MsgLog::EnMain, MsgLog::EnWarning,
                      "BootDiag: failed to open /boot_log.txt for append");
        return;
        }

    char szPrevAlive[16];
    FormatPrevAlive(szPrevAlive, sizeof(szPrevAlive), s_uPrevAliveMs);

    // Fixed-width columns so a pilot can eyeball the file in any text editor.
    // Widths: boot=%04u (4), fw=<var>, reset=%-9s (9 covers "DEEPSLEEP"),
    // prev_alive=%-9s (9 covers "unknown"/large uptimes), heap=%u.
    char szLine[160];
    int iLen = snprintf(szLine, sizeof(szLine),
                        "boot=%04u  fw=%s  reset=%-9s  prev_alive=%-9s  heap=%u\n",
                        static_cast<unsigned>(s_uBootCount),
                        BuildInfo::version,
                        ResetReasonName(s_enResetReason),
                        szPrevAlive,
                        static_cast<unsigned>(ESP.getFreeHeap()));

    // Clamp: snprintf returns the length it *would* have written, not what it
    // did; if the version string ever grew past the buffer, writing iLen
    // would read off the end of szLine.
    if (iLen > 0)
        {
        size_t cbWrite = static_cast<size_t>(iLen);
        if (cbWrite >= sizeof(szLine))
            cbWrite = sizeof(szLine) - 1;
        hFile.write(reinterpret_cast<const uint8_t *>(szLine), cbWrite);
        }

    // Archive any panic coredump from the prior boot under the same mutex
    // and the same open file (so the indented summary lines land directly
    // under the boot row they describe). Silent no-op when there's nothing
    // to archive — most boots.
    ArchivePriorPanicCoredump(hFile);

    hFile.sync();
    hFile.close();
    xSemaphoreGive(xWriteMutex);

    s_bSdLogWritten = true;
    }

// ----------------------------------------------------------------------------

void PrintBootLog(Print & out, int iMaxLines)
    {
    // Stack scratch sized to hold comfortably more than iMaxLines worth of
    // lines at ~110 B/line. Always reads the *tail* of the file, so an older
    // bigger file still shows just the most recent activity.
    constexpr size_t kBufBytes = 3072;
    static_assert(kBufBytes >= 2048, "Need headroom beyond 20 * ~110 B");

    char   achBuf[kBufBytes];
    size_t cbRead  = 0;
    bool   bTruncatedHead = false;  // true if we seeked into the middle of a line

    // ---- Critical section 1: SD read under xWriteMutex ----
    {
    if (!g_SdFileSys.bSdAvailable)
        {
        out.println("BootDiag: SD unavailable");
        return;
        }

    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(500)) != pdTRUE)
        {
        out.println("BootDiag: SD busy");
        return;
        }

    FsFile hFile = g_SdFileSys.open(kBootLogPath, O_READ);
    if (!hFile.isOpen())
        {
        xSemaphoreGive(xWriteMutex);
        out.println("BootDiag: /boot_log.txt not found");
        return;
        }

    const uint32_t uFileSize = hFile.size();
    if (uFileSize == 0)
        {
        hFile.close();
        xSemaphoreGive(xWriteMutex);
        out.println("BootDiag: /boot_log.txt is empty");
        return;
        }

    // Seek to the last kBufBytes bytes (or the start of file, whichever
    // is later).
    uint32_t uSeekTo = 0;
    if (uFileSize > kBufBytes)
        {
        uSeekTo = uFileSize - kBufBytes;
        bTruncatedHead = true;
        }
    hFile.seek(uSeekTo);

    const size_t cbWant = uFileSize < kBufBytes ? uFileSize : kBufBytes;
    const int    iRead  = hFile.read(reinterpret_cast<uint8_t *>(achBuf), cbWant);
    hFile.close();
    xSemaphoreGive(xWriteMutex);

    if (iRead <= 0)
        {
        out.println("BootDiag: /boot_log.txt read failed");
        return;
        }
    cbRead = static_cast<size_t>(iRead);
    }
    // ---- End SD critical section ----

    // If we seeked into the middle of the file, skip the (likely partial)
    // first line so output always starts on a line boundary.
    size_t iStart = 0;
    if (bTruncatedHead)
        {
        while (iStart < cbRead && achBuf[iStart] != '\n')
            iStart++;
        if (iStart < cbRead)
            iStart++;  // step past the '\n'
        }

    // Walk backwards from end to find the iMaxLines-th-from-last newline.
    // Each '\n' terminates a line; we keep everything after the
    // iMaxLines-th-newline-from-end so that exactly iMaxLines lines print
    // (fewer if the buffer holds fewer).
    int iLinesSeen = 0;
    size_t iEmit = iStart;
    for (size_t i = cbRead; i-- > iStart; )
        {
        if (achBuf[i] == '\n')
            {
            iLinesSeen++;
            if (iLinesSeen > iMaxLines)
                {
                iEmit = i + 1;
                break;
                }
            }
        }

    // ---- Critical section 2: serial output under xSerialLogMutex ----
    // Strict W-then-S ordering (W is already released above), matching the
    // W-outer-S-inner pattern LogSensor uses — no deadlock cycle possible.
    if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
        if (iEmit < cbRead)
            out.write(reinterpret_cast<const uint8_t *>(&achBuf[iEmit]),
                      cbRead - iEmit);
        if (cbRead > 0 && achBuf[cbRead - 1] != '\n')
            out.println();
        xSemaphoreGive(xSerialLogMutex);
        }
    else
        {
        // Tell the user the command ran — otherwise they'd just see
        // "Last 20 boots:" / "DONE." with nothing between and wonder why.
        out.println("BootDiag: serial mutex timeout");
        }
    }

}  // namespace BootDiag
