
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
    
    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100))) 
    {
        g_LogSensor.Close();
        Serial.end();  //clears the serial monitor  if used
        delay(1000);
        esp_restart();
    }
}

// --------------------------------------------------

uint32_t freeMemory() 
{
    return esp_get_minimum_free_heap_size();
}


// --------------------------------------------------

