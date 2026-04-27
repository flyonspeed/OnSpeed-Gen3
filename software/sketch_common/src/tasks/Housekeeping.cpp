
#include "src/Globals.h"
#include "src/util/Helpers.h"
#include "src/audio_io/Volume.h"
#include "src/drivers/Mcp3202Adc.h"

#include <audio/Panning.h>

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
    pinMode(kPinLedKnob, OUTPUT);
    ledcAttachChannel(kPinLedKnob, LEDC_BASE_FREQ, LEDC_TIMER_8_BIT, LEDC_CHANNEL);

    // Local state
    uint32_t uTick           = 0;
    int      iVolPos         = 0;
    bool     bVolInit        = false;
    bool     bLedOn          = false;
    int      iSlowBlinkCounter = 0;

    onspeed::audio::PanState  panState;
    onspeed::audio::PanConfig panCfg;   // defaults: smoothingFactor = 0.1f

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        uTick++;

        // Boot diagnostics heartbeat — poll every 20 ticks (2 s); Heartbeat
        // itself is threshold-gated internally and only actually writes NVS
        // on the first poll after crossing 5 s, 60 s, 5 min, 30 min, 1 hr
        // (five writes per boot total). See src/util/BootDiagnostics.h for
        // why we don't write every tick.
        if (uTick % 20 == 0)
            BootDiag::Heartbeat();

        // Monotonic millisecond clock passed into all core decision functions.
        // xTaskGetTickCount() * portTICK_PERIOD_MS gives the elapsed ms since boot.
        uint32_t nowMs = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // --- Snapshot AHRS state under mutex (written by ImuReadTask at 208 Hz) ---
        // All AHRS fields read here are copied into plain local variables so the
        // decision functions below never touch shared globals.  This is the
        // audit #010 fix: the old code read g_AHRS members outside the mutex.
        float fSnapRoll      = 0.0f;
        float fSnapYaw       = 0.0f;
        float fSnapAccelVert = -1.0f;
        float fSnapAccelLat  = 0.0f;
        if (xSemaphoreTake(xAhrsMutex, pdMS_TO_TICKS(5)))
        {
            fSnapRoll      = g_AHRS.gRoll;
            fSnapYaw       = g_AHRS.gYaw;
            fSnapAccelVert = g_AHRS.AccelVertCorr;
            fSnapAccelLat  = g_AHRS.AccelLatCorr;
            xSemaphoreGive(xAhrsMutex);
        }

        // --- GLimit (every tick, 100ms) ---
        // Build input and config snapshots from globals, then delegate the
        // trigger decision to the pure GLimitDetector in onspeed_core.
        if (g_Config.bOverGWarning)
        {
            onspeed::audio::GLimitInputs glInputs;
            glInputs.verticalG   = fSnapAccelVert;
            glInputs.rollRateDps = fSnapRoll;
            glInputs.yawRateDps  = fSnapYaw;
            glInputs.tickMs      = nowMs;

            onspeed::audio::GLimitConfig glCfg;
            glCfg.positiveLimitG      = g_Config.fLoadLimitPositive;
            glCfg.negativeLimitG      = g_Config.fLoadLimitNegative;
            glCfg.asymmetricGyroDps   = g_Config.fAsymmetricGyroLimit;
            glCfg.asymmetricReduction = g_Config.fAsymmetricReduction;
            glCfg.repeatTimeoutMs     = 3000;

            if (g_GLimitDetector.Update(glInputs, glCfg))
            {
                g_AudioPlay.SetVoice(enVoiceGLimit);
            }
        }

        // --- VnoChime (every tick, 100ms) ---
        if (g_Config.bVnoChimeEnabled)
        {
            // IAS is written by the sensor task; snapshot under xSensorMutex.
            float fSnapIAS = 0.0f;
            if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(5)))
            {
                fSnapIAS = g_Sensors.IAS;
                xSemaphoreGive(xSensorMutex);
            }

            onspeed::audio::VnoChimeInputs vnoIn;
            vnoIn.iasKt  = fSnapIAS;
            vnoIn.tickMs = nowMs;

            onspeed::audio::VnoChimeConfig vnoCfg;
            vnoCfg.vnoKt = static_cast<float>(g_Config.iVno);
            // Guard against zero interval; minimum 1 second (10 ticks x 100ms)
            unsigned uInterval = g_Config.uVnoChimeInterval;
            if (uInterval == 0) uInterval = 1;
            vnoCfg.repeatIntervalMs = uInterval * 1000U;

            if (g_VnoChimeDetector.Update(vnoIn, vnoCfg))
            {
                g_AudioPlay.SetVoice(enVoiceVnoChime);
            }
        }

        // --- 3D Audio (every tick, 100ms) ---
        if (g_Config.bAudio3D)
        {
            const float fLateralG = fSnapAccelLat;

            const onspeed::audio::PanResult pan =
                onspeed::audio::Apply3DPan(fLateralG, panState, panCfg);

            g_AudioPlay.SetGain(pan.leftGain, pan.rightGain);

            g_Log.printf(MsgLog::EnAudio, MsgLog::EnDebug,
                         "%0.3fG, Left: %0.3f, Right: %0.3f\n",
                         fLateralG, pan.leftGain, pan.rightGain);
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

                // Use VolumeCurve::MapPotToGain to convert the smoothed ADC
                // reading into a 0–1 gain, then scale to the integer percent
                // that SetVolume expects.
                onspeed::audio::VolumeCurveConfig volCfg;
                volCfg.lowAnalog  = g_Config.iVolumeLowAnalog;
                volCfg.highAnalog = g_Config.iVolumeHighAnalog;

                float fGain = onspeed::audio::MapPotToGain(iVolPos, volCfg);
                int iVolumePercent = static_cast<int>(fGain * 100.0f);

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
