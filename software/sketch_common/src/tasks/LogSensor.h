
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

    // Data
private:
    // Base filename WITHOUT extension, e.g. "log_042". Used at Close()
    // to write the sidecar and conditionally rename both files.
    char                         m_szBaseName[16] = {};

    // Accumulates sidecar metadata across the session. Reset in Open(),
    // fed in Write(), finalised in Close().
    onspeed::log::LogMetaBuilder m_metaBuilder;
};
