
#include "Globals.h"
#include "Helpers.h"
#include "Volume.h"
#ifdef HW_V4P
#include "Mcp3204Adc.h"
#endif

float fVolumeSmoothingFactor = 0.5;

// ----------------------------------------------------------------------------
// FreeRTOS task to read the volume control
// ----------------------------------------------------------------------------

void CheckVolumeTask(void * pvParams)
    {
    int         iVolPos = 0;
    bool        bInitialized = false;

    while (true)
        {
        // Run every 200 msec
        vTaskDelay(pdMS_TO_TICKS(200));

        if (g_Config.bVolumeControl)
            {
            if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(5)))
                {
                const int iRaw = (int)ReadVolume();
                if (!bInitialized)
                    {
                    iVolPos = iRaw;
                    bInitialized = true;
                    }
                else
                    {
                    iVolPos = (int)(fVolumeSmoothingFactor * iRaw + (1.0f - fVolumeSmoothingFactor) * iVolPos);
                    }
                xSemaphoreGive(xSensorMutex);
                }

            // Set audio volume
            int iVolumePercent = mapfloat(iVolPos, g_Config.iVolumeLowAnalog, g_Config.iVolumeHighAnalog, 0, 100);
            g_AudioPlay.SetVolume(iVolumePercent);

            g_Log.printf(MsgLog::EnVolume, MsgLog::EnDebug, "Raw %d  Percent %d\n", iVolPos, iVolumePercent);
            } // end if volume control enabled
        else
            {
            g_AudioPlay.SetVolume(g_Config.iDefaultVolume);
            }

        } // end while forever

    } // end CheckVolumeTask()

// ----------------------------------------------------------------------------

// Read the voltage value from the volume pot.

uint16_t    ReadVolume()
    {
#ifdef HW_V4P
    return Mcp3204Read(ADC_CH_VOLUME);
#else
    return analogRead(VOLUME_PIN);
#endif
    }
