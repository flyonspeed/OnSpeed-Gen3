// Async download spike — minimal mirror of /download on port 8080.
//
// Goal: A/B steady-state download throughput against the synchronous
// Arduino WebServer on port 80, without changing the existing handler.
// AsyncTCP uses lwIP's raw API and is not subject to the BSD-socket
// send-buffer limit (CONFIG_LWIP_TCP_SND_BUF_DEFAULT=5744) that pins
// the existing server to ~340 KB/s. If single-stream throughput on
// 8080 noticeably beats 80, the full async port is worth doing; if
// it doesn't, the bottleneck is elsewhere (lwIP TCP-thread CPU, WiFi
// MAC, SD SPI) and we drop the async direction.

#include "AsyncDownloadServer.h"

#include <Arduino.h>
#include <memory>

#include <ESPAsyncWebServer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "src/Globals.h"

namespace
{

constexpr uint16_t kAsyncPort = 8080;

AsyncWebServer  g_AsyncServer(kAsyncPort);

// Same allow-list as ConfigWebServer's IsSafeLogFilename. Duplicated
// because it's static there; if this spike graduates we'll lift the
// helper into a shared header.
bool IsSafeLogFilename(const String& s)
    {
    if (s.length() == 0 || s.length() > 32) return false;
    if (s.indexOf('/')  >= 0) return false;
    if (s.indexOf('\\') >= 0) return false;
    if (s.indexOf("..") >= 0) return false;
    for (size_t i = 0; i < s.length(); i++)
        {
        char c = s.charAt(i);
        if (!(isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-')) return false;
        }
    return true;
    }

// State carried across chunked-response callback invocations. Heap-
// allocated and held by shared_ptr so the file handle stays open
// until the framework stops calling us back.
struct DownloadState
    {
    FsFile  file;
    size_t  totalSize = 0;
    bool    bPrevPause = false;

    DownloadState()
        {
        bPrevPause = g_bPause;
        g_bPause = true;
        }
    ~DownloadState()
        {
        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(500)))
            {
            if (file)
                file.close();
            xSemaphoreGive(xWriteMutex);
            }
        g_bPause = bPrevPause;
        }
    };

void HandleAsyncDownload(AsyncWebServerRequest* request)
    {
    if (!request->hasParam("file"))
        {
        request->send(400, "text/plain", "Missing file parameter");
        return;
        }
    String sRawName = request->getParam("file")->value();
    if (!IsSafeLogFilename(sRawName))
        {
        request->send(400, "text/plain", "Invalid filename");
        return;
        }
    String sFilename = "/" + sRawName;

    auto state = std::make_shared<DownloadState>();

    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(2000)))
        {
        state->file = g_SdFileSys.open(sFilename.c_str(), O_READ);
        if (state->file)
            state->totalSize = state->file.size();
        xSemaphoreGive(xWriteMutex);
        }
    else
        {
        request->send(503, "text/plain", "SD busy");
        return;
        }

    if (!state->file)
        {
        request->send(404, "text/plain", "File not found");
        return;
        }

    AsyncWebServerResponse* response = request->beginChunkedResponse(
        "application/octet-stream",
        [state](uint8_t* buffer, size_t maxLen, size_t /*index*/) -> size_t
            {
            if (!state->file)
                return 0;
            size_t iLen = 0;
            if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(2000)))
                {
                int iRead = state->file.read(buffer, maxLen);
                if (iRead > 0)
                    iLen = static_cast<size_t>(iRead);
                xSemaphoreGive(xWriteMutex);
                }
            // iLen == 0 signals end-of-response to ESPAsyncWebServer.
            return iLen;
            });

    response->addHeader("Content-Disposition", "attachment; filename=" + sRawName);
    response->addHeader("Connection", "close");
    // /bench is served from port 80; this server is on a different port,
    // so a fetch() from the bench page is cross-origin. Allow it.
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    }

}   // namespace

void AsyncDownloadServerInit()
    {
    g_AsyncServer.on("/download", HTTP_GET, HandleAsyncDownload);

    g_AsyncServer.onNotFound([](AsyncWebServerRequest* request)
        {
        request->send(404, "text/plain",
                      "Async spike: only /download is implemented on port 8080. "
                      "Use port 80 for everything else.");
        });

    g_AsyncServer.begin();
    }
