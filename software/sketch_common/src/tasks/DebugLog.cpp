
#ifndef DISABLE_FS_H_WARNING
#define DISABLE_FS_H_WARNING
#endif
#include "SdFat.h"

#include "src/Globals.h"
#include "src/tasks/DebugLog.h"

#include <log/ConsumeAlignedWrite.h>

// How often to sync the .dbg file. Same cadence as the CSV writer to
// bound power-yank loss to ~5 s.
static const uint32_t  kDebugSyncIntervalMs = 5000;

// Local module state. All access serialized through xDebugWriteMutex.
static FsFile          m_hDebugFile;

static const size_t    kDebugSector    = 512;
static const size_t    kDebugBufSize   = kDebugSector * 4;   // 2 KB staging
static char            m_szDebugBuf[kDebugBufSize];
static size_t          m_uDebugBufUsed = 0;

// Producer-side drop counter (ring full). Reported by the PERF tick.
volatile uint32_t       g_uDebugRingDrops = 0;

// ---------------------------------------------------------------------------

void DebugLog::Open(const char *szBaseName)
{
    // Caller holds xDebugWriteMutex.

    if (m_bOpen)
        Close();

    if (szBaseName == nullptr || szBaseName[0] == '\0')
        return;

    if (!g_SdFileSys.bSdAvailable)
        return;

    char szFilename[32];
    snprintf(szFilename, sizeof(szFilename), "%s.dbg", szBaseName);

    m_hDebugFile = g_SdFileSys.open(szFilename, O_RDWR | O_CREAT | O_APPEND);
    if (!m_hDebugFile.isOpen())
        {
        Serial.printf("DebugLog: open(%s) failed; SD debug disabled\n", szFilename);
        return;
        }

    // Cache base name for paired rename at Close().
    strncpy(m_szBaseName, szBaseName, sizeof(m_szBaseName) - 1);
    m_szBaseName[sizeof(m_szBaseName) - 1] = '\0';
    m_bOpen          = true;
    m_uDebugBufUsed  = 0;
}

// ---------------------------------------------------------------------------

void DebugLog::Close()
{
    // Caller holds xDebugWriteMutex.

    if (!m_bOpen)
        return;

    // Best-effort flush of any residual staging-buffer bytes.
    if (m_uDebugBufUsed > 0 && m_hDebugFile.isOpen())
        {
        const size_t uRequested = m_uDebugBufUsed;
        const size_t uActual    = m_hDebugFile.write(m_szDebugBuf, uRequested);
        // No retry here — file is about to close. ConsumeAlignedWrite
        // updates m_uDebugBufUsed to reflect what didn't make it.
        onspeed::log::ConsumeAlignedWrite(
            uRequested, uActual,
            m_szDebugBuf, kDebugBufSize, &m_uDebugBufUsed);
        }

    m_hDebugFile.sync();
    m_hDebugFile.close();
    m_bOpen         = false;
    m_uDebugBufUsed = 0;
}

// ---------------------------------------------------------------------------

void DebugLog::RenameWithPrefix(const char *szDatePrefix)
{
    // Caller holds xDebugWriteMutex.

    if (m_szBaseName[0] == '\0' || szDatePrefix == nullptr)
        return;

    // m_szBaseName is "log_NNN"; the prefix is "YYYY-MM-DD"; new
    // filename is "<prefix>_NNN.dbg".
    const char *nnn = m_szBaseName + 4;   // skip "log_"

    char oldName[32];
    char newName[32];
    snprintf(oldName, sizeof(oldName), "%s.dbg", m_szBaseName);
    snprintf(newName, sizeof(newName), "%s_%s.dbg", szDatePrefix, nnn);

    if (g_SdFileSys.exists(newName))
        return;   // collision; leave it alone

    g_SdFileSys.rename(oldName, newName);
}

// ---------------------------------------------------------------------------

bool DebugLog::Write(const char *szLine, size_t uLen)
{
    // Producer side: must NOT block. May be called from any task,
    // including flight-critical ones with xSerialLogMutex held.

    if (!m_bOpen || szLine == nullptr || uLen == 0)
        return false;

    if (xDebugRingBuffer == nullptr)
        return false;

    const bool bSent = xRingbufferSend(xDebugRingBuffer, szLine, uLen, 0);
    if (!bSent)
        __atomic_fetch_add(&g_uDebugRingDrops, 1u, __ATOMIC_RELAXED);
    return bSent;
}

// ===========================================================================

// FreeRTOS task: drain the debug ring, write 512-byte-aligned chunks
// to the .dbg file under xDebugWriteMutex, sync periodically.
//
// Runs at lower priority than LogSensorCommitTask and pinned to Core
// 0 so it never competes with flight-critical sampling on Core 1.

void DebugLogCommitTask(void *pvParams)
{
    (void)pvParams;

    static TickType_t xLastSyncTime = xTaskGetTickCount();
    static char      *pchIn;
    static size_t     iLen;

    while (true)
        {
        if (xDebugRingBuffer == nullptr)
            {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
            }

        // If the box is in pause mode (web download / listing), idle
        // the same way LogSensorCommitTask does.
        if (g_bPause)
            {
            pchIn = (char *)xRingbufferReceive(xDebugRingBuffer, &iLen, pdMS_TO_TICKS(100));
            if (pchIn != nullptr)
                vRingbufferReturnItem(xDebugRingBuffer, pchIn);
            continue;
            }

        if (!xSemaphoreTake(xDebugWriteMutex, pdMS_TO_TICKS(1000)))
            continue;

        // Drain available items into the staging buffer.
        bool       bGotData = false;
        TickType_t xWait    = pdMS_TO_TICKS(100);

        while (true)
            {
            pchIn = (char *)xRingbufferReceive(xDebugRingBuffer, &iLen, xWait);
            xWait = 0;
            if (pchIn == nullptr)
                break;

            if (m_uDebugBufUsed + iLen > kDebugBufSize)
                {
                vRingbufferReturnItem(xDebugRingBuffer, pchIn);
                break;
                }

            memcpy(m_szDebugBuf + m_uDebugBufUsed, pchIn, iLen);
            m_uDebugBufUsed += iLen;
            vRingbufferReturnItem(xDebugRingBuffer, pchIn);
            bGotData = true;
            }

        // Flush 512-byte-aligned chunks; retain residual for next round.
        if (bGotData && m_hDebugFile.isOpen())
            {
            size_t uAligned = (m_uDebugBufUsed / kDebugSector) * kDebugSector;
            if (uAligned > 0)
                {
                const size_t uActual = m_hDebugFile.write(m_szDebugBuf, uAligned);
                // Short-write retry: helper leaves the unwritten head
                // at the front so the next iteration picks it up.
                onspeed::log::ConsumeAlignedWrite(
                    uAligned, uActual,
                    m_szDebugBuf, kDebugBufSize, &m_uDebugBufUsed);
                }
            }

        if ((xTaskGetTickCount() - xLastSyncTime) > pdMS_TO_TICKS(kDebugSyncIntervalMs))
            {
            if (m_hDebugFile.isOpen())
                {
                // Flush partial sector before sync.
                if (m_uDebugBufUsed > 0)
                    {
                    const size_t uRequested = m_uDebugBufUsed;
                    const size_t uActual    = m_hDebugFile.write(m_szDebugBuf, uRequested);
                    onspeed::log::ConsumeAlignedWrite(
                        uRequested, uActual,
                        m_szDebugBuf, kDebugBufSize, &m_uDebugBufUsed);
                    }
                m_hDebugFile.sync();
                }
            xLastSyncTime = xTaskGetTickCount();
            }

        xSemaphoreGive(xDebugWriteMutex);
        }
}
