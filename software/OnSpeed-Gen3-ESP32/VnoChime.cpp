
#include "Globals.h"

// ----------------------------------------------------------------------------

void CheckVnoChimeTask(void * pvParams)
    {
    while (true)
        {
        // Run every 100 msec
        vTaskDelay(pdMS_TO_TICKS(100));

        // If over Vno play chime
        if ((g_Config.bVnoChimeEnabled) && (g_Sensors.IAS > g_Config.iVno))
            {
            g_AudioPlay.SetVoice(enVoiceVnoChime);
            // Guard: zero interval would spin the task at full speed.
            unsigned uInterval = g_Config.uVnoChimeInterval;
            if (uInterval == 0)
                uInterval = 1;
            vTaskDelay(uInterval * 1000 / portTICK_PERIOD_MS);
            }
        }
    } // end CheckVnoChimeTask()
