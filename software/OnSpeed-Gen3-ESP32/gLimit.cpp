
#include "Globals.h"
#include "gLimit.h"

// glimit settings
#define GLIMIT_REPEAT_TIMEOUT   3000 // milliseconds to repeat G limit.
#define ASYMMETRIC_GYRO_LIMIT   15 // degrees/sec rotation on either axis to trigger asymmetric G limits.

// ----------------------------------------------------------------------------

void CheckGLimitTask(void * pvParams)
    {
    float       fCalculatedGLimitPositive;
    float       fCalculatedGLimitNegative;

    while (true)
        {
        // Run every 100 msec
        vTaskDelay(pdMS_TO_TICKS(100));

        if (g_Config.bOverGWarning == true)
            {
            // Roll rates, I think.
            if (fabs(g_AHRS.gRoll) >= ASYMMETRIC_GYRO_LIMIT || fabs(g_AHRS.gYaw) >= ASYMMETRIC_GYRO_LIMIT)
                {
                fCalculatedGLimitPositive = g_Config.fLoadLimitPositive * 0.666;
                fCalculatedGLimitNegative = g_Config.fLoadLimitNegative * 0.666;
                }
            else
                {
                fCalculatedGLimitPositive = g_Config.fLoadLimitPositive;
                fCalculatedGLimitNegative = g_Config.fLoadLimitNegative;
                }

            if (g_AHRS.AccelVertCorr >= fCalculatedGLimitPositive || g_AHRS.AccelVertCorr <= fCalculatedGLimitNegative)
                {
                g_AudioPlay.SetVoice(enVoiceGLimit);
                vTaskDelay(GLIMIT_REPEAT_TIMEOUT / portTICK_PERIOD_MS);
                }

            } // end if warning enabled
        } // end while forever
    } // end CheckGLimitTask()
