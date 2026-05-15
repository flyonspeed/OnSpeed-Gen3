
#pragma once

#include "src/Globals.h"

// ============================================================================

// On-box debug log paired with the flight CSV: log_NNN.dbg holds the
// Warning- and Error-level g_Log output emitted during that session.
//
// Producer side: MsgLog::print / println / printf format a line into a
// stack buffer, then call g_DebugLog.Write(buf, n). Send is non-blocking
// (timeout 0); a full ring drops the line and bumps a counter that the
// PERF tick reports. Producers never block on the SD path.
//
// Consumer side: LogSensorCommitTask calls g_DebugLog.DrainLocked()
// at the end of its critical section, while it still holds xWriteMutex
// from its data write. SdFat's volume cache is shared between the
// .csv and .dbg files, so concurrent writers (even to different files)
// corrupt each other's data — folding the dbg drain into the data
// writer's existing mutex window is the only correct serialisation.
// The "isolation" between the dbg and CSV producers lives at the ring
// layer (non-blocking Write into PSRAM ring), not at the file layer.
//
// Lifetime: opened in g_LogSensor::Open() once the CSV file name is
// known, closed in g_LogSensor::Close() (and renamed in step if the
// dated rename happens). When SD is unavailable or open fails, every
// Write() is a no-op and Enabled() returns false.

class DebugLog
{
public:
    // Open <baseName>.dbg. Caller must hold xWriteMutex. Safe to call
    // multiple times — if a file is already open it is closed first.
    void Open(const char *szBaseName);

    // Flush staging buffer and close the file. Caller must hold
    // xWriteMutex.
    void Close();

    // Rename the .dbg file to follow the CSV's date prefix, e.g.
    // log_007.dbg -> 2026-05-12_007.dbg. No-op if the file is closed
    // or the rename target already exists. Caller must hold xWriteMutex.
    void RenameWithPrefix(const char *szDatePrefix);

    // Send formatted bytes into the debug ring. Returns true if the
    // line entered the ring, false if the ring was full or sink is
    // disabled. Safe to call from any task; does not block.
    bool Write(const char *szLine, size_t uLen);

    // Drain the dbg ring into the .dbg file. Caller must already hold
    // xWriteMutex. Called from LogSensorCommitTask at the end of its
    // critical section so the dbg writes ride along with the data
    // writer's existing mutex window — no separate writer task, no
    // mutex contention.
    void DrainLocked();

    // True when a file is open and ready to accept writes.
    bool Enabled() const { return m_bOpen; }

    // Active base name (no extension), or "" when closed.
    const char *ActiveBaseName() const { return m_szBaseName; }

private:
    char m_szBaseName[16] = {};
    bool m_bOpen          = false;
};
