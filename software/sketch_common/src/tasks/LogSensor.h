
#pragma once

#include "src/Globals.h"
#include <log/LogMetaBuilder.h>

// FreeRTOS task for writing to disk
void LogSensorCommitTask(void *pvParams);

// ============================================================================

class LogSensor
{
public:
    LogSensor();

    // Methods
public:
    void Open();
    void Open(FsFile * phFile);
    void Close();
    void Write();

    // Base filename (without extension) of the log currently being written,
    // e.g. "log_042". Returns "" when logging is disabled or no file is open.
    // Used by the /logs web handler to flag the active row as non-deletable.
    const char* ActiveBaseName() const { return m_szBaseName; }

    // Data
private:
    // Base filename WITHOUT extension, e.g. "log_042". Used at Close()
    // to write the sidecar and conditionally rename both files.
    char                         m_szBaseName[16] = {};

    // Accumulates sidecar metadata across the session. Reset in Open(),
    // fed in Write(), finalised in Close().
    onspeed::log::LogMetaBuilder m_metaBuilder;
};
