
#pragma once

#include "Globals.h"

// FreeRTOS task for writing to disk
void LogSensorCommitTask(void *pvParams);

// ============================================================================

class LogSensor
{
public:
    LogSensor();

    // Data
public:

    // Methods
public:
    void Open();
    void Open(FsFile * phFile);
    void Close();
    void Write();

};
