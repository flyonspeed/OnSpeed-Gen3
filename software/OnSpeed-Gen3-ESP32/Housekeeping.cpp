
#include "Globals.h"
#include "Helpers.h"
#include "Housekeeping.h"
#include "Volume.h"
#ifdef HW_V4P
#include "Mcp3202Adc.h"
#endif

// GLimit settings
#define GLIMIT_REPEAT_TIMEOUT_TICKS   30   // 30 x 100ms = 3000ms
#define ASYMMETRIC_GYRO_LIMIT         15   // degrees/sec rotation on either axis

// 3D Audio: move audio with the ball, scaling is 0.08 LateralG/ball width
#define AUDIO_3D_CURVE(x)             (-92.822f*x*x + 20.025f*x)
static const float fSmoothingFactor = 0.1f;

// Volume smoothing
static const float fVolumeSmoothingFactor = 0.5f;

// Heartbeat LED
#define LEDC_TIMER_8_BIT    8
#define LEDC_BASE_FREQ      5000
#define LEDC_CHANNEL        0

// ----------------------------------------------------------------------------

void HousekeepingTask(void * pvParams)
{
    // One-time heartbeat LED init
    pinMode(PIN_LED_KNOB, OUTPUT);
    ledcAttachChannel(PIN_LED_KNOB, LEDC_BASE_FREQ, LEDC_TIMER_8_BIT, LEDC_CHANNEL);

    // Local state
    uint32_t uTick           = 0;
    int      iGLimitCooldown = 0;
    int      iVnoCooldown    = 0;
    int      iVolPos         = 0;
    bool     bVolInit        = false;
    float    fChannelGain    = 0.0f;
    bool     bLedOn          = false;
    int      iSlowBlinkCounter = 0;

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        uTick++;

        // --- GLimit (every tick, 100ms) with cooldown ---
        if (iGLimitCooldown > 0)
        {
            iGLimitCooldown--;
        }
        else if (g_Config.bOverGWarning)
        {
            float fCalcGLimitPos;
            float fCalcGLimitNeg;

            if (fabs(g_AHRS.gRoll) >= ASYMMETRIC_GYRO_LIMIT || fabs(g_AHRS.gYaw) >= ASYMMETRIC_GYRO_LIMIT)
            {
                fCalcGLimitPos = g_Config.fLoadLimitPositive * 0.666f;
                fCalcGLimitNeg = g_Config.fLoadLimitNegative * 0.666f;
            }
            else
            {
                fCalcGLimitPos = g_Config.fLoadLimitPositive;
                fCalcGLimitNeg = g_Config.fLoadLimitNegative;
            }

            if (g_AHRS.AccelVertCorr >= fCalcGLimitPos || g_AHRS.AccelVertCorr <= fCalcGLimitNeg)
            {
                g_AudioPlay.SetVoice(enVoiceGLimit);
                iGLimitCooldown = GLIMIT_REPEAT_TIMEOUT_TICKS;
            }
        }

        // --- VnoChime (every tick, 100ms) with cooldown ---
        if (iVnoCooldown > 0)
        {
            iVnoCooldown--;
        }
        else if (g_Config.bVnoChimeEnabled && (g_Sensors.IAS > g_Config.iVno))
        {
            g_AudioPlay.SetVoice(enVoiceVnoChime);
            unsigned uInterval = g_Config.uVnoChimeInterval;
            if (uInterval == 0)
                uInterval = 1;
            iVnoCooldown = uInterval * 10;   // seconds -> 100ms ticks
        }

        // --- 3D Audio (every tick, 100ms) ---
        if (g_Config.bAudio3D)
        {
            float fLateralG     = g_AHRS.AccelLatCorr;
            int   iSignLateralG = fLateralG >= 0 ? 1 : -1;

            float fCurveGain = AUDIO_3D_CURVE(fabs(fLateralG));
            if (fCurveGain > 1.0f) fCurveGain = 1.0f;
            if (fCurveGain < 0.0f) fCurveGain = 0.0f;

            fCurveGain   = fCurveGain * iSignLateralG;
            fChannelGain = fSmoothingFactor * fCurveGain + (1.0f - fSmoothingFactor) * fChannelGain;
            if (fChannelGain >  1.0f) fChannelGain =  1.0f;
            if (fChannelGain < -1.0f) fChannelGain = -1.0f;

            float fLeftGain  = fabs(-1.0f + fChannelGain);
            float fRightGain = fabs( 1.0f + fChannelGain);
            g_AudioPlay.SetGain(fLeftGain, fRightGain);

            g_Log.printf(MsgLog::EnAudio, MsgLog::EnDebug, "%0.3fG, Left: %0.3f, Right: %0.3f\n", fLateralG, fLeftGain, fRightGain);
        }

        // --- Volume (every 2nd tick, 200ms) ---
        if (uTick % 2 == 0)
        {
            if (g_Config.bVolumeControl)
            {
                if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(5)))
                {
                    const int iRaw = (int)ReadVolume();
                    if (!bVolInit)
                    {
                        iVolPos = iRaw;
                        bVolInit = true;
                    }
                    else
                    {
                        iVolPos = (int)(fVolumeSmoothingFactor * iRaw + (1.0f - fVolumeSmoothingFactor) * iVolPos);
                    }
                    xSemaphoreGive(xSensorMutex);
                }

                int iVolumePercent = mapfloat(iVolPos, g_Config.iVolumeLowAnalog, g_Config.iVolumeHighAnalog, 0, 100);
                g_AudioPlay.SetVolume(iVolumePercent);

                g_Log.printf(MsgLog::EnVolume, MsgLog::EnDebug, "Raw %d  Percent %d\n", iVolPos, iVolumePercent);
            }
            else
            {
                g_AudioPlay.SetVolume(g_Config.iDefaultVolume);
            }
        }

        // --- Heartbeat LED (every 3rd tick, 300ms) ---
        if (uTick % 3 == 0)
        {
            if (g_bAudioEnable)
            {
                // Audio enabled: fast blink (300ms interval)
                iSlowBlinkCounter = 0;
                if (bLedOn)
                {
                    ledcWriteChannel(LEDC_CHANNEL, 0);
                    bLedOn = false;
                }
                else
                {
                    ledcWriteChannel(LEDC_CHANNEL, 200);
                    bLedOn = true;
                }
            }
            else
            {
                // Audio muted: slow blink (1000ms ~ 3 ticks of 300ms)
                iSlowBlinkCounter++;
                if (iSlowBlinkCounter >= 3)
                {
                    iSlowBlinkCounter = 0;
                    if (bLedOn)
                    {
                        ledcWriteChannel(LEDC_CHANNEL, 0);
                        bLedOn = false;
                    }
                    else
                    {
                        ledcWriteChannel(LEDC_CHANNEL, 200);
                        bLedOn = true;
                    }
                }
            }
        }

    } // end while forever
} // end HousekeepingTask()
