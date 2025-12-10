
#include "Globals.h"
#include "Helpers.h"
#include "Volume.h"

float fVolumeSmoothingFactor = 0.5;

// ----------------------------------------------------------------------------
// FreeRTOS task to read the volume control
// ----------------------------------------------------------------------------

void CheckVolumeTask(void * pvParams)
    {
    int         iVolPos;

    // Volume control defaults
    //if (!g_Config.bVolumeControl)
    //{
    //    // Set default volume when volume potentiometer is disabled
    //    g_Config.iVolumePercent = g_Config.iDefaultVolume;
    //}

    while (true)
        {
        // Run every 200 msec
        vTaskDelay(pdMS_TO_TICKS(200));

        if (g_Config.bVolumeControl)
            {
            if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(5)))
                {
                iVolPos = fVolumeSmoothingFactor * ReadVolume() + (1.0 - fVolumeSmoothingFactor) * iVolPos;
                xSemaphoreGive(xSensorMutex);
                }

            // Set audio volume
            int iVolumePercent = mapfloat(iVolPos, g_Config.iVolumeLowAnalog, g_Config.iVolumeHighAnalog, 0, 100);
            g_AudioPlay.SetVolume(iVolumePercent);

            g_Log.printf(MsgLog::EnVolume, MsgLog::EnDebug, "Raw %d  Percent %d\n", iVolPos, iVolumePercent);
            } // end if volume control enabled

        } // end while forever

    } // end CheckVolumeTask()

// ----------------------------------------------------------------------------

// Read the voltage value from the volume pot.

uint16_t    ReadVolume()
    {
    return analogRead(VOLUME_PIN);
    }