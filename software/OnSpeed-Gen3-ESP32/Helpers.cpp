
#include "Globals.h"
#include "Helpers.h"

// --------------------------------------------------

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max)
    {
    if ((in_max - in_min) + out_min == 0)
        return 0;
    else
        return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    }

// --------------------------------------------------

void _softRestart()
{
    g_Log.println(MsgLog::EnMain, MsgLog::EnDebug, "System restarting...");

    // Attempt to gracefully close the log file before restarting.
    // Wait up to 1 second to acquire the lock. If the disk is busy, we might not get it, but we'll reboot anyway.
    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
    {
        g_LogSensor.Close();
        xSemaphoreGive(xWriteMutex);
    }
    Serial.end();  //clears the serial monitor if used
    delay(100);
    esp_restart();
}

// --------------------------------------------------

uint32_t freeMemory()
{
    return esp_get_minimum_free_heap_size();
}


// --------------------------------------------------
