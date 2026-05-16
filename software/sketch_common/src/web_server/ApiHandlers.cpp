// ApiHandlers.cpp — JSON /api/* endpoint surface added in PR 2.
//
// Companion to ConfigWebServer.cpp.  Every handler here is a thin
// wrapper around an existing read or an existing safety-checked
// write path (the legacy form handlers stay untouched).  This file
// adds no new globals and writes nothing the legacy paths don't
// already write.
//
// Design rules (PLAN_WEB_PREACT_REWRITE §4f, §4k):
//   - JSON bodies in, JSON out.  No form-encoded.
//   - Field names camelCase, units in field names (Kt, Lb, Deg, G).
//   - Errors carry HTTP status + a JSON body
//     `{"ok":false,"errors":[{"path":"...","message":"..."}]}`.
//   - Long-running ops (only /api/format here) return a taskId and a
//     poll endpoint.  The legacy form handler is fully synchronous;
//     the poll endpoint reports state stored in this file.

#include "ApiHandlers.h"

#include <Arduino.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <buildinfo.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "src/Globals.h"

#include <api/CalwizSave.h>
#include <api/CalwizSaveParse.h>
#include <api/CalwizStateJson.h>
#include <api/SensorBiasesJson.h>
#include <log/LogMeta.h>
#include <log/LogMetaFile.h>

extern WebServer CfgServer;

// Volume read helper (Volume.h declares ReadVolume()).
extern uint16_t ReadVolume();

// Soft-restart entry point shared with the legacy /reboot handler.
extern void _softRestart();

// Forward declarations of helpers private to ConfigWebServer.cpp.  We
// don't want to duplicate the safe-filename / active-file / meta-read
// logic here; they're internal-linkage in the legacy file but trivial
// to mirror because they consist of only the lookups we need.

namespace onspeed::api {

namespace {

// ----------------------------------------------------------------------------
// Small JSON helpers — keeps every handler reading the same way.
// ----------------------------------------------------------------------------

void SendJson(int code, const String& body) {
    CfgServer.sendHeader("Cache-Control", "no-store");
    CfgServer.send(code, "application/json", body);
}

void SendOk() {
    SendJson(200, F("{\"ok\":true}"));
}

void SendError(int code, const char* path, const char* message) {
    String s;
    s.reserve(96);
    s += F("{\"ok\":false,\"errors\":[{\"path\":\"");
    s += path;
    s += F("\",\"message\":\"");
    s += message;
    s += F("\"}]}");
    SendJson(code, s);
}

// Emit a number with the same finite/non-finite handling as the WS
// broadcast: NaN/Inf become 0 so the JSON is parser-safe.  Decimal
// places kept compact via %.6g.
String JsonFloat(float v, float fallback = 0.0f) {
    char buf[32];
    if (!std::isfinite(v)) v = fallback;
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
    return String(buf);
}

String JsonInt(long v) {
    return String(v);
}

// ----------------------------------------------------------------------------
// Filename safety — same character allow-list the legacy /delete and
// /download handlers use.  Duplicated rather than reaching across the
// file boundary.
// ----------------------------------------------------------------------------

bool IsSafeLogFilename(const String& s) {
    if (s.length() == 0 || s.length() > 32) return false;
    if (s.indexOf('/')  >= 0) return false;
    if (s.indexOf('\\') >= 0) return false;
    if (s.indexOf("..") >= 0) return false;
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool IsActiveLogFile(const String& sFilename) {
    const char* szActiveBase = g_LogSensor.ActiveBaseName();
    if (!szActiveBase || szActiveBase[0] == '\0') return false;
    String sActive = String(szActiveBase) + ".csv";
    return sFilename.equalsIgnoreCase(sActive);
}

bool TryReadLogMeta(const char* sCsvName, ::onspeed::log::LogMeta* out) {
    char sMeta[32];
    size_t len = std::strlen(sCsvName);
    if (len < 5 || len > 27) return false;
    std::memcpy(sMeta, sCsvName, len);
    sMeta[len] = '\0';
    const char* dot = std::strrchr(sMeta, '.');
    if (!dot) return false;
    size_t dotIdx = static_cast<size_t>(dot - sMeta);
    std::snprintf(sMeta + dotIdx, sizeof(sMeta) - dotIdx, ".meta");

    FsFile f = g_SdFileSys.open(sMeta, O_RDONLY);
    if (!f.isOpen()) return false;

    char buf[512];
    int n = f.read(buf, sizeof(buf) - 1);
    f.close();
    if (n <= 0) return false;
    buf[n] = '\0';
    return ::onspeed::log::ParseMetaFile(std::string_view(buf, static_cast<size_t>(n)), out);
}

// JSON-escape a small subset.  Filenames are constrained by
// IsSafeLogFilename to characters that need no escaping; the meta
// firmware/SHA strings are ASCII-safe.  Belt-and-suspenders:
// double-quote and backslash get escaped, control chars dropped.
String JsonEscape(const char* s) {
    String out;
    out.reserve(strlen(s) + 4);
    while (*s) {
        char c = *s++;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            out += c;
        }
    }
    return out;
}

// ----------------------------------------------------------------------------
// /api/format async-shim state.
//
// The legacy HandleFormat is synchronous: the HTTP response doesn't
// arrive until the format completes (which can take several seconds).
// The new shape is "POST returns a taskId immediately; client polls
// for state."  Today we match the legacy semantics by running the
// format inline; the taskId / status pair lets a future PR break it
// onto its own task without a client breaking.  State is module-
// scoped, guarded by a mutex.
// ----------------------------------------------------------------------------

enum class FormatState : uint8_t {
    Idle    = 0,
    Running = 1,
    Done    = 2,
    Failed  = 3,
};

struct FormatJob {
    char         taskId[32]    = {};
    FormatState  state         = FormatState::Idle;
    char         error[64]     = {};
    float        cardSizeGb    = 0.0f;   // populated from SdFileSys::Format()
    bool         configSaved   = false;  // true iff post-format SaveConfigurationToFile() returned true
};

// Single in-flight job.  HandleApiFormat overwrites it on each new
// trigger; the prior taskId becomes invalid (status returns "unknown
// task").
FormatJob g_FormatJob;
SemaphoreHandle_t g_FormatJobMutex = nullptr;

void EnsureFormatMutex() {
    if (g_FormatJobMutex == nullptr)
        g_FormatJobMutex = xSemaphoreCreateMutex();
}

void RunFormatInline(FormatJob& job) {
    bool  ok          = false;
    bool  configSaved = false;
    float cardSizeGb  = 0.0f;
    char  err[64]     = {};

    // Format is a stop-the-world operation. Two-step pause:
    //   1. Set g_bPause so the producer (SensorReadTask / ImuReadTask)
    //      stops queuing rows immediately. This drains the inflight
    //      pressure on xWriteMutex.
    //   2. Take xWriteMutex with a long timeout — at this point the
    //      writer task should be able to drain quickly and release.
    //      Format runs to completion under the mutex.
    // After: re-open the log file, reset g_bPause, save config.
    g_Log.println(MsgLog::EnWebServer, MsgLog::EnWarning, "Format: pausing producer + draining ring");
    bool bPrevPause = g_bPause;
    g_bPause = true;

    // Give the writer task one drain cycle to flush the ring before we
    // try to take the mutex. The writer's drain loop waits up to 100 ms
    // on the first xRingbufferReceive, so 200 ms is plenty.
    vTaskDelay(pdMS_TO_TICKS(200));

    // Long mutex timeout (10 sec). With g_bPause set, the writer is
    // idle once it finishes its current drain cycle; no producer is
    // adding work. 10 sec is generous — typical wait is < 100 ms.
    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(10000))) {
        g_Log.println(MsgLog::EnWebServer, MsgLog::EnWarning, "Format: mutex acquired, closing log file");
        bool bOrigSdLogging = g_Config.bSdLogging;
        g_Config.bSdLogging = false;
        if (bOrigSdLogging)
            g_LogSensor.Close();

        g_Log.println(MsgLog::EnWebServer, MsgLog::EnWarning, "Format: running SD format");
        ok = g_SdFileSys.Format(nullptr, &cardSizeGb);
        g_Log.printf(MsgLog::EnWebServer, MsgLog::EnWarning,
                     "Format: complete ok=%d cardSizeGb=%.2f\n", (int)ok, cardSizeGb);

        if (bOrigSdLogging) {
            g_Log.println(MsgLog::EnWebServer, MsgLog::EnWarning, "Format: re-opening log file for new flight");
            g_Config.bSdLogging = true;
            g_LogSensor.Open();
        }

        xSemaphoreGive(xWriteMutex);

        // Restore pause flag BEFORE saving config so the producer
        // starts queuing into the freshly opened file.
        g_bPause = bPrevPause;

        // Put the configuration file back onto the card. Mutex is taken
        // inside SaveConfigurationToFile(). Capture the return so a
        // silent failure is visible to the pilot — the spec covers why
        // (Vac, 2026-05-08): post-format the config could be missing
        // even when format itself reported success.
        if (ok) {
            g_Log.println(MsgLog::EnWebServer, MsgLog::EnWarning, "Format: saving config to fresh card");
            configSaved = g_Config.SaveConfigurationToFile();
        }
    } else {
        g_Log.println(MsgLog::EnWebServer, MsgLog::EnError, "Format: xWriteMutex timeout after 10s; abandoning");
        g_bPause = bPrevPause;
        std::snprintf(err, sizeof(err), "SD busy (xWriteMutex) after 10s");
    }

    if (xSemaphoreTake(g_FormatJobMutex, pdMS_TO_TICKS(100))) {
        job.cardSizeGb  = cardSizeGb;
        job.configSaved = configSaved;
        if (ok) {
            job.state = FormatState::Done;
        } else {
            job.state = FormatState::Failed;
            if (err[0])
                std::snprintf(job.error, sizeof(job.error), "%s", err);
            else
                std::snprintf(job.error, sizeof(job.error), "format returned false");
        }
        xSemaphoreGive(g_FormatJobMutex);
    }
}

}  // namespace

// ============================================================================
// Sample endpoints
// ============================================================================

void HandleApiSampleAoa() {
    // Mirror the WebSocket gate: bIasAlive is the canonical sensor-
    // level air-data validity flag (rising-edge 20 kt, falling 15 kt,
    // hysteresis).  When false or AOA is non-finite, emit JSON null
    // matching the convention from issues #358 / #455.
    String body;
    body.reserve(32);
    if (!g_Sensors.bIasAlive || !std::isfinite(g_Sensors.AOA))
    {
        body = F("{\"aoa\":null}");
    }
    else
    {
        // Snapshot once so the formatting and the gate see the same value.
        const float aoaSnap = g_Sensors.AOA;
        char buf[32];
        snprintf(buf, sizeof(buf), "{\"aoa\":%.2f}", aoaSnap);
        body = buf;
    }
    SendJson(200, body);
}

void HandleApiSampleFlapsRaw() {
    // Snapshot adcCounts + degrees position under xAhrsMutex so the
    // two readings are coherent.  Same pattern DataServer.cpp uses.
    uint16_t adc        = 0;
    int      positionDeg = -1;
    bool     ok         = false;

    if (xSemaphoreTake(xAhrsMutex, pdMS_TO_TICKS(50))) {
        adc         = g_Flaps.uValue;
        positionDeg = g_Flaps.iPosition;
        ok          = true;
        xSemaphoreGive(xAhrsMutex);
    }

    if (!ok) {
        SendError(503, "flaps", "AHRS mutex busy");
        return;
    }

    String body;
    body.reserve(48);
    body  = F("{\"adcCounts\":");
    body += JsonInt(adc);
    body += F(",\"position\":");
    body += JsonInt(positionDeg);
    body += F("}");
    SendJson(200, body);
}

void HandleApiSampleVolume() {
    // ReadVolume() drives the MCP3202 ADC's chip select; the sensor
    // SPI bus is shared with the IMU and three pressure sensors, so
    // the call must hold xSensorMutex.  Same pattern the legacy
    // /getvalue?name=VOLUME path uses.
    uint16_t volRaw = 0;
    if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(50))) {
        volRaw = ReadVolume();
        xSemaphoreGive(xSensorMutex);
    } else {
        SendError(503, "volume", "Sensor SPI busy");
        return;
    }

    String body;
    body.reserve(32);
    body  = F("{\"adcCounts\":");
    body += JsonInt(volRaw);
    body += F("}");
    SendJson(200, body);
}

void HandleApiSamplePfwd() {
    // Raw pitot-pressure ADC counts.  The HscPressureSensor::ReadPressureCounts()
    // method bus-shares with sibling sensors, so xSensorMutex is held.
    uint16_t counts = 0;
    bool     ok     = false;
    if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(50))) {
        counts = g_pPitot ? g_pPitot->ReadPressureCounts() : 0;
        ok = (g_pPitot != nullptr);
        xSemaphoreGive(xSensorMutex);
    } else {
        SendError(503, "pfwd", "Sensor SPI busy");
        return;
    }
    if (!ok) {
        SendError(500, "pfwd", "pitot sensor not initialized");
        return;
    }

    String body;
    body.reserve(32);
    body  = F("{\"counts\":");
    body += JsonInt(counts);
    body += F("}");
    SendJson(200, body);
}

void HandleApiSampleP45() {
    uint16_t counts = 0;
    bool     ok     = false;
    if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(50))) {
        counts = g_pAOA ? g_pAOA->ReadPressureCounts() : 0;
        ok = (g_pAOA != nullptr);
        xSemaphoreGive(xSensorMutex);
    } else {
        SendError(503, "p45", "Sensor SPI busy");
        return;
    }
    if (!ok) {
        SendError(500, "p45", "AOA sensor not initialized");
        return;
    }

    String body;
    body.reserve(32);
    body  = F("{\"counts\":");
    body += JsonInt(counts);
    body += F("}");
    SendJson(200, body);
}

// ============================================================================
// Audio test
// ============================================================================

void HandleApiAudioTestStart() {
    // StartAudioTest returns false when an audio task is already
    // playing (legacy /getvalue?name=AUDIOTEST mapped that to a
    // "Audio Test Busy" string).  The new contract is 409 Conflict
    // with the standard path-keyed error body so the client can
    // distinguish "already running" from "no audio device".
    if (!g_AudioPlay.StartAudioTest()) {
        SendError(409, "audiotest", "already running");
        return;
    }
    SendOk();
}

void HandleApiAudioTestStop() {
    g_AudioPlay.StopAudioTest();
    SendOk();
}

void HandleApiAudioTestStatus() {
    const bool running = g_AudioPlay.IsAudioTestRunning();
    String body;
    body.reserve(32);
    body  = F("{\"state\":\"");
    body += running ? F("running") : F("idle");
    body += F("\"}");
    SendJson(200, body);
}

void HandleApiVnoChimeTest() {
    g_AudioPlay.SetVoice(enVoiceVnoChime);
    SendOk();
}

// ============================================================================
// Logs API
// ============================================================================

void HandleApiLogs() {
    // No PauseGuard: file-list typically completes in <500ms; the
    // 230 KB usable ring (~2.9 sec at 80 KB/s) absorbs the queued rows
    // while xWriteMutex is held by the listing path below. Pausing the
    // producer here used to drop every IMU sample during the listing
    // window — visible to pilots who loaded /logs mid-flight as
    // ~10-25 ms CSV gaps. Download / bulk-delete handlers keep their
    // guards (multi-second mutex holds would overflow the ring).

    SdFileSys::SuFileInfoList suFileList;
    String sActiveCsvName;
    bool   bListStatus  = false;
    uint64_t uTotalSize = 0;

    struct Entry {
        String   sName;
        uint64_t uSize = 0;
        bool     bHaveMeta = false;
        ::onspeed::log::LogMeta meta;
    };
    std::vector<Entry> entries;
    entries.reserve(32);

    // Wait up to 5 seconds for the writer mutex. Typical wait is <100 ms
    // even under load; 5s covers the worst case of a card-side stall
    // (write_max can hit 100+ ms; sidecar refresh holds for ~150 ms).
    // Going beyond 5s would block the browser too long; the browser
    // gets a 503 with Retry-After=1 so it can retry once the writer
    // releases.
    static const TickType_t kLogsListMutexWaitTicks = pdMS_TO_TICKS(5000);
    if (xSemaphoreTake(xWriteMutex, kLogsListMutexWaitTicks)) {
        const char* szActiveBase = g_LogSensor.ActiveBaseName();
        if (szActiveBase && szActiveBase[0] != '\0') {
            sActiveCsvName  = szActiveBase;
            sActiveCsvName += ".csv";
        }
        bListStatus = g_SdFileSys.FileList(&suFileList);
        if (bListStatus) {
            std::vector<String> metaNames;
            metaNames.reserve(suFileList.size());
            for (size_t i = 0; i < suFileList.size(); ++i) {
                const char* name = suFileList[i].szFileName;
                size_t nlen = strlen(name);
                if (nlen >= 5 && strcasecmp(name + nlen - 5, ".meta") == 0)
                    metaNames.emplace_back(name);
            }
            for (size_t i = 0; i < suFileList.size(); ++i) {
                const char* name = suFileList[i].szFileName;
                size_t nlen = strlen(name);
                if (nlen >= 5 && strcasecmp(name + nlen - 5, ".meta") == 0)
                    continue;
                if (nlen >= 9 && strcasecmp(name + nlen - 9, ".meta.tmp") == 0)
                    continue;

                Entry e;
                e.sName = name;
                e.uSize = suFileList[i].uFileSize;
                String sExpectedMeta = e.sName.substring(0, e.sName.length() - 4) + ".meta";
                bool bMetaExists = false;
                for (const String& mn : metaNames)
                    if (mn.equalsIgnoreCase(sExpectedMeta)) { bMetaExists = true; break; }
                if (bMetaExists)
                    e.bHaveMeta = TryReadLogMeta(name, &e.meta);

                uTotalSize += e.uSize;
                entries.push_back(std::move(e));
            }
        }
        xSemaphoreGive(xWriteMutex);
    } else {
        // Writer task is genuinely busy. Emit a one-shot warning into
        // the dbg log so post-flight forensics show WHEN this fires;
        // the page can retry in 1 sec.
        g_Log.printf(MsgLog::EnWebServer, MsgLog::EnWarning,
                     "/api/logs: mutex timeout after %lu ms\n",
                     (unsigned long)(kLogsListMutexWaitTicks * portTICK_PERIOD_MS));
        CfgServer.sendHeader("Retry-After", "1");
        SendError(503, "logs", "SD busy, retry shortly");
        return;
    }

    String body;
    body.reserve(2048);
    body += F("{\"activeLog\":\"");
    body += JsonEscape(sActiveCsvName.c_str());
    body += F("\",\"totalSize\":");
    body += JsonInt(static_cast<long>(uTotalSize));
    body += F(",\"files\":[");

    bool first = true;
    for (const Entry& e : entries) {
        if (!first) body += ',';
        first = false;
        body += F("{\"name\":\"");
        body += JsonEscape(e.sName.c_str());
        body += F("\",\"size\":");
        body += JsonInt(static_cast<long>(e.uSize));
        body += F(",\"hasMeta\":");
        body += e.bHaveMeta ? F("true") : F("false");
        if (e.bHaveMeta) {
            body += F(",\"meta\":{");
            body += F("\"durationMs\":");
            body += JsonInt(static_cast<long>(e.meta.durationMs));
            body += F(",\"rowCount\":");
            body += JsonInt(static_cast<long>(e.meta.rowCount));
            body += F(",\"maxIasKt\":");
            body += JsonFloat(e.meta.maxIasKt);
            body += F(",\"maxPaltFt\":");
            body += JsonFloat(e.meta.maxPaltFt);
            body += F(",\"firmware\":\"");
            body += JsonEscape(e.meta.firmware);
            body += F("\",\"firmwareSha\":\"");
            body += JsonEscape(e.meta.firmwareSha);
            body += F("\",\"efisType\":\"");
            body += ::onspeed::log::EfisTypeToString(e.meta.efisType);
            body += F("\",\"gpsFixSeen\":");
            body += e.meta.gpsFixSeen ? F("true") : F("false");
            body += F(",\"utcStart\":\"");
            body += JsonEscape(e.meta.utcStart);
            body += F("\",\"timeOfDayStart\":\"");
            body += JsonEscape(e.meta.timeOfDayStart);
            body += F("\"}");
        }
        body += F("}");
    }
    body += F("]}");
    SendJson(200, body);
}

void HandleApiLogsDeleteBulk() {
    if (!CfgServer.hasArg("plain")) {
        SendError(400, "body", "JSON body required");
        return;
    }
    const String body = CfgServer.arg("plain");

    // Tiny JSON parse: we need only the names array of strings.  No
    // ArduinoJson dep here — pull names manually from the document.
    // Format expected: {"names":["a.csv","b.csv"]}.
    int nameStart = body.indexOf("\"names\"");
    if (nameStart < 0) {
        SendError(400, "names", "missing names array");
        return;
    }
    int arrStart = body.indexOf('[', nameStart);
    int arrEnd   = body.indexOf(']', arrStart);
    if (arrStart < 0 || arrEnd < 0) {
        SendError(400, "names", "malformed names array");
        return;
    }

    std::vector<String> selected;
    int pos = arrStart + 1;
    while (pos < arrEnd) {
        int q1 = body.indexOf('"', pos);
        if (q1 < 0 || q1 >= arrEnd) break;
        int q2 = body.indexOf('"', q1 + 1);
        if (q2 < 0 || q2 >= arrEnd) break;
        String name = body.substring(q1 + 1, q2);
        if (IsSafeLogFilename(name))
            selected.push_back(name);
        pos = q2 + 1;
    }

    // Pause logger so it stops queueing rows during the batch — same
    // pattern HandleDeleteBulk uses.  Yield between deletes so the
    // logger drain task and watchdog stay happy on big batches.
    struct PauseGuard {
        bool bPrevPause;
        PauseGuard() : bPrevPause(g_bPause) { g_bPause = true; }
        ~PauseGuard() { g_bPause = bPrevPause; }
    } pauseGuard;

    String deleted; deleted.reserve(256);
    String errors;  errors.reserve(256);
    bool firstDeleted = true;
    bool firstError   = true;
    auto appendItem = [](String& target, bool& first, const String& item) {
        if (!first) target += ',';
        first = false;
        target += '"';
        target += item;
        target += '"';
    };
    auto appendErrorItem = [](String& target, bool& first, const String& name, const char* reason) {
        if (!first) target += ',';
        first = false;
        target += F("{\"name\":\"");
        target += name;
        target += F("\",\"reason\":\"");
        target += reason;
        target += F("\"}");
    };

    for (const String& f : selected) {
        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(2000))) {
            if (IsActiveLogFile(f)) {
                xSemaphoreGive(xWriteMutex);
                appendErrorItem(errors, firstError, f, "active log");
                vTaskDelay(1);
                continue;
            }
            g_SdFileSys.remove(f.c_str());
            int iDot = f.lastIndexOf('.');
            if (iDot > 0) {
                String sMeta = f.substring(0, iDot) + ".meta";
                g_SdFileSys.remove(sMeta.c_str());
            }
            xSemaphoreGive(xWriteMutex);
            appendItem(deleted, firstDeleted, f);
        } else {
            appendErrorItem(errors, firstError, f, "SD busy");
        }
        vTaskDelay(1);
    }

    String response;
    response.reserve(deleted.length() + errors.length() + 64);
    response  = F("{\"ok\":true,\"deleted\":[");
    response += deleted;
    response += F("],\"errors\":[");
    response += errors;
    response += F("]}");
    SendJson(200, response);
}

// ============================================================================
// Triggers
// ============================================================================

void HandleApiFormat() {
    EnsureFormatMutex();
    char taskId[32];
    std::snprintf(taskId, sizeof(taskId), "format-%lu", static_cast<unsigned long>(millis()));

    if (xSemaphoreTake(g_FormatJobMutex, pdMS_TO_TICKS(100))) {
        std::snprintf(g_FormatJob.taskId, sizeof(g_FormatJob.taskId), "%s", taskId);
        g_FormatJob.state       = FormatState::Running;
        g_FormatJob.error[0]    = '\0';
        g_FormatJob.cardSizeGb  = 0.0f;
        g_FormatJob.configSaved = false;
        xSemaphoreGive(g_FormatJobMutex);
    }

    String body;
    body.reserve(64);
    body  = F("{\"taskId\":\"");
    body += taskId;
    body += F("\"}");
    SendJson(200, body);

    // Run the format inline (legacy semantics).  The HTTP response is
    // already on the wire; the client will poll /api/format/status.
    RunFormatInline(g_FormatJob);
}

void HandleApiFormatStatus() {
    EnsureFormatMutex();
    if (!CfgServer.hasArg("id")) {
        SendError(400, "id", "missing id");
        return;
    }
    const String id = CfgServer.arg("id");

    FormatState state             = FormatState::Idle;
    char        err[64]           = {};
    char        activeId[32]      = {};
    float       cardSizeGb_local  = 0.0f;
    bool        configSaved_local = false;
    if (xSemaphoreTake(g_FormatJobMutex, pdMS_TO_TICKS(100))) {
        std::snprintf(activeId, sizeof(activeId), "%s", g_FormatJob.taskId);
        state             = g_FormatJob.state;
        std::snprintf(err, sizeof(err), "%s", g_FormatJob.error);
        cardSizeGb_local  = g_FormatJob.cardSizeGb;
        configSaved_local = g_FormatJob.configSaved;
        xSemaphoreGive(g_FormatJobMutex);
    }

    if (id != String(activeId)) {
        SendError(404, "id", "unknown task");
        return;
    }

    const char* sState = "idle";
    switch (state) {
        case FormatState::Idle:    sState = "idle";    break;
        case FormatState::Running: sState = "running"; break;
        case FormatState::Done:    sState = "done";    break;
        case FormatState::Failed:  sState = "failed";  break;
    }

    String body;
    body.reserve(160);
    body  = F("{\"state\":\"");
    body += sState;
    body += F("\"");

    if (state == FormatState::Done) {
        char szSize[16];
        std::snprintf(szSize, sizeof(szSize), "%.1f", cardSizeGb_local);
        body += F(",\"cardSizeGb\":");
        body += szSize;
        if (!configSaved_local) {
            body += F(",\"warning\":\"Configuration was not saved to the SD card. Open the configuration page and click Save.\"");
        }
    }
    if (state == FormatState::Failed && err[0]) {
        body += F(",\"error\":\"");
        body += JsonEscape(err);
        body += F("\"");
    }
    body += F("}");
    SendJson(200, body);
}

void HandleApiReboot() {
    SendOk();
    delay(500);
    _softRestart();
}

// ============================================================================
// Calwiz state (read-only)
// ============================================================================

void HandleApiCalwizState() {
    ::onspeed::api::CalwizStateInputs in;
    int currentFlapIndex = 0;

    // Snapshot under xAhrsMutex so the per-flap vector and the
    // current detent index agree (HandleConfigSave swaps under this
    // mutex).  The aircraft scalars live in g_Config and are written
    // only by SaveConfigurationToFile; reading them outside the mutex
    // is fine, but we batch under the same lock for one-shot
    // consistency.
    if (xSemaphoreTake(xAhrsMutex, pdMS_TO_TICKS(50))) {
        in.acGrossWeightLb = g_Config.iAcGrossWeight;
        in.acBestGlideKt   = g_Config.fAcBestGlideIAS;
        in.acVfeKt         = g_Config.fAcVfe;
        in.acGLimit        = g_Config.fAcGlimit;
        for (const auto& f : g_Config.aFlaps)
            in.flaps.push_back(f);
        currentFlapIndex = g_Flaps.iIndex;
        xSemaphoreGive(xAhrsMutex);
    } else {
        SendError(503, "calwiz", "AHRS mutex busy");
        return;
    }
    in.currentFlapIndex = currentFlapIndex;

    std::string json = ::onspeed::api::SerializeCalwizState(in);
    SendJson(200, String(json.c_str()));
}

// ============================================================================
// Cal wizard save (write side).
// ============================================================================
//
// Reads a JSON body (~200 bytes) of 12 named floats + the integer
// flapsPos, locates the matching SuFlaps by iDegrees, applies via
// the pure helper `ApplyCalwizSave`, saves config, and reports any
// SetpointOrderError as a JSON warning.
//
// Body parsing is in onspeed_core (api/CalwizSaveParse.cpp) so
// test_calwiz_save_diff exercises the actual lexer the firmware runs.
// The lexer rejects NaN/Infinity/overflow tokens so non-finite values
// never reach the SuFlaps; missing required fields produce a 400
// with the path-keyed error.
//
// Concurrency: the per-flap mutation runs under xAhrsMutex (matches
// the read-side handler's snapshot lock).  SaveConfigurationToFile()
// runs OUTSIDE the mutex — it takes its own internal mutex.

void HandleApiCalwizSave() {
    if (!CfgServer.hasArg("plain")) {
        SendError(400, "body", "JSON body required");
        return;
    }
    const String body = CfgServer.arg("plain");

    auto parsed = onspeed::api::ExtractCalwizSave(
        std::string_view(body.c_str(), body.length()));
    if (!parsed.ok) {
        SendError(400, parsed.errorField.c_str(), parsed.errorMessage.c_str());
        return;
    }
    const int flapsPos = parsed.flapsPos;
    const onspeed::api::CalwizSaveInput& in = parsed.input;

    // Locate the matching SuFlaps under xAhrsMutex (the read-side
    // endpoint snapshots under the same lock).  Mutate in-place via
    // the pure helper; capture SetpointOrderError text before
    // releasing so a parallel detent change can't race the response.
    bool   found = false;
    String warning;
    if (xSemaphoreTake(xAhrsMutex, pdMS_TO_TICKS(100))) {
        for (auto& flap : g_Config.aFlaps) {
            if (flap.iDegrees == flapsPos) {
                onspeed::api::ApplyCalwizSave(flap, in);
                std::string sErr = flap.SetpointOrderError();
                if (!sErr.empty())
                    warning = sErr.c_str();
                found = true;
                break;
            }
        }
        xSemaphoreGive(xAhrsMutex);
    } else {
        SendError(503, "calwiz", "AHRS mutex busy");
        return;
    }

    if (!found) {
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "Could not find the flap position %d degrees in the configuration",
                      flapsPos);
        SendError(400, "flapsPos", msg);
        return;
    }

    // Save unconditionally, including when the order check fails.  A
    // pilot may want the partially-bad save as a starting point to
    // edit by hand.  SaveConfigurationToFile() ignores its own return
    // value here; preserve that.
    g_Config.SaveConfigurationToFile();

    if (warning.length() > 0) {
        g_Log.printf(MsgLog::EnConfig, MsgLog::EnWarning,
                     "Calwiz flap %d deg: %s\n", flapsPos, warning.c_str());

        String body2;
        body2.reserve(warning.length() + 96);
        body2  = F("{\"ok\":true,\"warnings\":[{\"path\":\"setpoints\",\"message\":\"");
        body2 += JsonEscape(warning.c_str());
        body2 += F("\"}]}");
        SendJson(200, body2);
        return;
    }

    SendOk();
}

// ============================================================================
// Sensor calibration snapshot (read-only)
// ============================================================================

void HandleApiSensorsBiases() {
    ::onspeed::api::SensorBiasesInputs in;

    // Bias values + IMU + AHRS readings under xSensorMutex/xAhrsMutex —
    // matches the legacy /sensorconfig handler's read pattern (no
    // explicit mutex on the read-side panel today, but the AHRS values
    // are written under xAhrsMutex).  Short timeout: this is a UI poll,
    // not a flight-critical path.
    if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(50))) {
        in.imuPitchDeg = g_pIMU ? g_pIMU->PitchAC() : 0.0f;
        in.imuRollDeg  = g_pIMU ? g_pIMU->RollAC()  : 0.0f;
        xSemaphoreGive(xSensorMutex);
    }

    if (xSemaphoreTake(xAhrsMutex, pdMS_TO_TICKS(50))) {
        in.truePitchDeg = g_AHRS.PitchWithBias();
        in.trueRollDeg  = g_AHRS.RollWithBias();
        xSemaphoreGive(xAhrsMutex);
    }

    in.pFwdBiasCounts = g_Config.iPFwdBias;
    in.p45BiasCounts  = g_Config.iP45Bias;
    in.pStaticBiasMb  = g_Config.fPStaticBias;
    in.gxBias         = g_Config.fGxBias;
    in.gyBias         = g_Config.fGyBias;
    in.gzBias         = g_Config.fGzBias;
    in.pitchBiasDeg   = g_Config.fPitchBias;
    in.rollBiasDeg    = g_Config.fRollBias;

    // EFIS source classification.  Mirrors the freshness + sentinel
    // check in ConfigWebServer.cpp's HandleSensorConfig: pitch and roll
    // are valid when bReadEfisData is on, the timestamp is < 2 s old,
    // and neither sentinel is set.  Beyond that, VN-300 carries no
    // baro on the wire, so we mark the source separately.  The
    // baro-supplying types are a positive list — a future non-baro
    // EFIS added to EnEfisType stays out of the list by default and
    // the PAlt field stays blank.
    in.efisSource = ::onspeed::api::EfisBaroSource::None;
    if (g_Config.bReadEfisData &&
        (millis() - g_EfisSerial.uTimestamp < 2000) &&
        g_EfisSerial.suEfis.Pitch > -90.0f &&     // not the -100 invalid sentinel
        g_EfisSerial.suEfis.Roll  > -179.0f)      // not the -180 invalid sentinel
    {
        in.efisPitchDeg = g_EfisSerial.suEfis.Pitch;
        in.efisRollDeg  = g_EfisSerial.suEfis.Roll;

        const auto enType = g_EfisSerial.enType;
        const bool bEfisSuppliesBaro =
            (enType == EfisSerialPort::EnDynonSkyview) ||
            (enType == EfisSerialPort::EnDynonD10)     ||
            (enType == EfisSerialPort::EnGarminG5)     ||
            (enType == EfisSerialPort::EnGarminG3X)    ||
            (enType == EfisSerialPort::EnMglBinary);

        if (enType == EfisSerialPort::EnVN300) {
            in.efisSource = ::onspeed::api::EfisBaroSource::Vn300;
        } else if (bEfisSuppliesBaro) {
            in.efisSource = ::onspeed::api::EfisBaroSource::Baro;
            in.efisPaltFt = static_cast<float>(g_EfisSerial.suEfis.Palt);
        }
        // else: source remains None (set above)
    }

    std::string json = ::onspeed::api::SerializeSensorBiases(in);
    SendJson(200, String(json.c_str()));
}

// ============================================================================
// Version metadata
// ============================================================================

void HandleApiVersion() {
    String body;
    body.reserve(160);
    body  = F("{\"version\":\"");
    body += JsonEscape(BuildInfo::version);
    body += F("\",\"gitShortSha\":\"");
    body += JsonEscape(BuildInfo::gitShortSha);
    body += F("\",\"buildDate\":\"");
    body += JsonEscape(BuildInfo::buildDate);
    body += F("\"}");
    SendJson(200, body);
}

}  // namespace onspeed::api
