
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
            vTaskDelay(g_Config.uVnoChimeInterval * 1000 / portTICK_PERIOD_MS);
            }
        }
    } // end CheckVnoChimeTask()
