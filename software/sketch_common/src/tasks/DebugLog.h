
#pragma once

#include "src/Globals.h"

// FreeRTOS task for committing debug-log bytes to disk.
void DebugLogCommitTask(void *pvParams);

// ============================================================================

// On-box debug log paired with the flight CSV: log_NNN.dbg holds the
// Warning- and Error-level g_Log output emitted during that session.
//
// Producer side: MsgLog::print / println / printf format a line into a
// stack buffer, then call g_DebugLog.Write(buf, n). Send is non-blocking
// (timeout 0); a full ring drops the line and bumps a counter that the
// PERF tick reports.
//
// Consumer side: DebugLogCommitTask drains the ring, writes 512-byte-
// aligned chunks to log_NNN.dbg under its own mutex xDebugWriteMutex,
// and syncs every kDebugSyncIntervalMs. Held mutex is INTENTIONALLY
// separate from xWriteMutex so the very SD stalls we want to diagnose
// (which hold xWriteMutex inside LogSensorCommitTask) do not block
// debug writes about those stalls.
//
// Lifetime: opened in g_LogSensor::Open() once the CSV file name is
// known, closed in g_LogSensor::Close() (and renamed in step if the
// dated rename happens). When SD is unavailable or open fails, every
// Write() is a no-op and Enabled() returns false.

class DebugLog
{
public:
    // Open <baseName>.dbg for append. Caller must hold xDebugWriteMutex.
    // Safe to call multiple times — if a file is already open it is
    // closed first.
    void Open(const char *szBaseName);

    // Flush staging buffer and close the file. Caller must hold
    // xDebugWriteMutex.
    void Close();

    // Rename the .dbg file to follow the CSV's date prefix, e.g.
    // log_007.dbg -> 2026-05-12_007.dbg. No-op if the file is closed
    // or the rename target already exists. Caller must hold
    // xDebugWriteMutex.
    void RenameWithPrefix(const char *szDatePrefix);

    // Send formatted bytes into the debug ring. Returns true if the
    // line entered the ring, false if the ring was full or sink is
    // disabled. Safe to call from any task; does not block.
    bool Write(const char *szLine, size_t uLen);

    // True when a file is open and ready to accept writes.
    bool Enabled() const { return m_bOpen; }

    // Active base name (no extension), or "" when closed.
    const char *ActiveBaseName() const { return m_szBaseName; }

private:
    char m_szBaseName[16] = {};
    bool m_bOpen          = false;
};
