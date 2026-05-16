
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

// Local module state. All access serialised through xWriteMutex —
// SdFat's volume cache is shared with the CSV writer, so even though
// the dbg file and CSV file are different files, concurrent writes
// would corrupt each other's data. The producer-side ring (non-blocking
// xRingbufferSend) is what isolates flight-critical tasks from SD stalls.
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
    // Caller holds xWriteMutex.

    if (m_bOpen)
        Close();

    if (szBaseName == nullptr || szBaseName[0] == '\0')
        return;

    if (!g_SdFileSys.bSdAvailable)
        return;

    char szFilename[32];
    snprintf(szFilename, sizeof(szFilename), "%s.dbg", szBaseName);

    // O_TRUNC matches LogSensor::Open(): each paired (.csv, .dbg) is a
    // fresh session at a never-before-used basename, so we never want
    // to append to a stale .dbg if iMaxFileNum happens to land on a
    // basename whose .dbg file was left behind from a crashed boot.
    m_hDebugFile = g_SdFileSys.open(szFilename, O_RDWR | O_CREAT | O_TRUNC);
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
    // Caller holds xWriteMutex.

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
    // Caller holds xWriteMutex.

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

// Drain the debug ring into the .dbg file. Caller must already hold
// xWriteMutex (typically because they just finished a CSV write/sync
// cycle and are about to release the mutex anyway). Non-blocking on
// the ring receive — returns immediately if nothing is queued.
//
// This used to be a separate task (DebugLogCommitTask), but two writer
// tasks competing for one mutex starved each other under steady load.
// Folding the dbg drain into the data writer's existing critical
// section eliminates the contention; the dbg cost per data-writer
// iteration is small (a few memcpys, occasional 512-byte SD write).
void DebugLog::DrainLocked()
{
    if (xDebugRingBuffer == nullptr || !m_bOpen) return;

    static TickType_t xLastDebugSyncTime = xTaskGetTickCount();
    char      *pchIn;
    size_t     iLen;
    bool       bGotData = false;

    // Non-blocking drain of whatever's currently queued.
    while (true)
        {
        pchIn = (char *)xRingbufferReceive(xDebugRingBuffer, &iLen, 0);
        if (pchIn == nullptr) break;

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

    // Flush 512-byte-aligned chunks; residual stays in the buffer.
    if (bGotData && m_hDebugFile.isOpen())
        {
        size_t uAligned = (m_uDebugBufUsed / kDebugSector) * kDebugSector;
        if (uAligned > 0)
            {
            const size_t uActual = m_hDebugFile.write(m_szDebugBuf, uAligned);
            onspeed::log::ConsumeAlignedWrite(
                uAligned, uActual,
                m_szDebugBuf, kDebugBufSize, &m_uDebugBufUsed);
            }
        }

    // Periodic sync (also flushes any partial-sector residual).
    if ((xTaskGetTickCount() - xLastDebugSyncTime) > pdMS_TO_TICKS(kDebugSyncIntervalMs))
        {
        if (m_hDebugFile.isOpen())
            {
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
        xLastDebugSyncTime = xTaskGetTickCount();
        }
}
