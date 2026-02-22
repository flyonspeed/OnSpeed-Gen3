/*
    https://docs.arduino.cc/learn/built-in-libraries/i2s/
    https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/api/i2s.html
    https://esp32.com/viewtopic.php?t=8919

    Convert WAV to Raw (i.e. PCM)
        ffmpeg -i file.wav     -f s16le -ar 16000 file.pcm
            -f s16le    Output format is signed 16-bit little-endian.
            -ar 16000   Audio rate resampled to 16000 samples per second

    Convert Raw to header file
        xxd -c 16 -i file.pcm PCM_file.h
    Be sure to add "const" to the array holding the PCM data in the header file.
        const unsigned char overg_pcm[] ....

    Consider using this library instead...
    https://github.com/earlephilhower/BackgroundAudio

 */

#include <bit>
#include <cstdarg>
#include <atomic>

#include <math.h>

#include <Arduino.h>
#include <ESP_I2S.h>

#include "Globals.h"
#include "Helpers.h"
#include <ToneCalc.h>

#include "Audio/PCM_cal_canceled.h"
#include "Audio/PCM_cal_mode.h"
#include "Audio/PCM_cal_saved.h"
#include "Audio/PCM_datamark.h"
#include "Audio/PCM_disabled.h"
#include "Audio/PCM_enabled.h"
#include "Audio/PCM_glimit.h"
#include "Audio/PCM_overg.h"
#include "Audio/PCM_VnoChime.h"
#include "Audio/PCM_left_speaker.h"
#include "Audio/PCM_right_speaker.h"

#include "Audio.h"

//i2s_data_bit_width_t  bps  = I2S_DATA_BIT_WIDTH_32BIT;  //
i2s_data_bit_width_t  bps  = I2S_DATA_BIT_WIDTH_16BIT; // Only 16 seems to work well with tones
//i2s_data_bit_width_t  bps  = I2S_DATA_BIT_WIDTH_8BIT;  //

i2s_mode_t            mode = I2S_MODE_STD;  // Works

i2s_slot_mode_t       slot = I2S_SLOT_MODE_STEREO;    // Works better
//i2s_slot_mode_t       slot = I2S_SLOT_MODE_MONO;    // Works

int16_t            aTone_400Hz[TONE_BUFFER_LEN];
int16_t            aTone_1600Hz[TONE_BUFFER_LEN];

#ifdef HW_V4P
    #define I2S_BCK     20
    #define I2S_DOUT    19
    #define I2S_LRCK    8
#else  // V4B defaults
    #define I2S_BCK     45
    #define I2S_DOUT    48
    #define I2S_LRCK    47
#endif

#define FREERTOS

// Tone frequency and ramp constants (PPS constants moved to onspeed_core/ToneCalc.h)
#define HIGH_TONE_HZ         1600                 // freq of high tone
#define LOW_TONE_HZ           400                 // freq of low tone
// Anti-click ramp: 2 ms linear ramp (32 samples at 16 kHz) on pulse edges
// and tone start/stop.  Short enough to keep pulses crisp, long enough to
// eliminate the hard amplitude discontinuity that causes audible clicks.
#define ANTI_CLICK_MS            2
static constexpr float RAMP_PER_SAMPLE = 1.0f / (ANTI_CLICK_MS * 0.001f * SAMPLE_RATE);

// ----------------------------------------------------------------------------

static bool         s_bI2sOk = false;
static volatile TaskHandle_t s_xAudioTestTask = nullptr;
static std::atomic<bool>     s_bAudioTestStopRequested{false};
static std::atomic<bool>     s_bAudioTestStarting{false};

static inline int16_t ScaleAndClampI16(int16_t sample, float scale)
    {
    int32_t scaled = static_cast<int32_t>(sample * scale);
    if (scaled > 32767)
        return 32767;
    if (scaled < -32768)
        return -32768;
    return static_cast<int16_t>(scaled);
    }

static inline uint32_t PackStereoI16(int16_t left, int16_t right)
    {
    return static_cast<uint16_t>(left) | (static_cast<uint32_t>(static_cast<uint16_t>(right)) << 16);
    }

static void AudioLogDebugNoBlock(const char * szFmt, ...)
    {
    if (!g_Log.Test(MsgLog::EnAudio, MsgLog::EnDebug))
        return;

    // Never block the audio path on serial output.
    if (!xSemaphoreTake(xSerialLogMutex, 0))
        return;

    va_list args;
    va_start(args, szFmt);
    Serial.print("DEBUG   Audio - ");
    Serial.vprintf(szFmt, args);
    va_end(args);

    xSemaphoreGive(xSerialLogMutex);
    }

static void AudioTestTask(void * pvParams)
    {
    (void)pvParams;

    g_AudioPlay.AudioTest();

    s_xAudioTestTask = nullptr;
    vTaskDelete(nullptr);
    }

/*
FreeRTOS task to play the appropriate noise at the appropriate time.
When run as a FreeRTOS task about 50 msec is the most audio that gets
buffered. Make sure no higher priority task takes that much time or
else the output audio will have gaps and glitches.
This AudioPlayTask() should run at a higher priority to make sure it
can write data out to the audio chip in a timely fashion. Even though
some voice audio playbacks are long (1 sec or more) and the write routine
will block because the DMA memory is full, the block seems to give up
the processor gracefully, even at the higher priority. In other words,
this doesn't seem to hog the CPU, even at a higher priority.
*/

void AudioPlayTask(void * psuParams)
{
    while (true)
    {
        if (!s_bI2sOk)
            {
            // If I2S init failed, don't spin at high priority.
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
            }

        // This would be more efficient with a semaphore but it works OK for now
        if (g_AudioPlay.enTone == enToneNone)
            vTaskDelay(pdMS_TO_TICKS(100));

        // If a voice play has been selected then play it once. Note that PlayVoice()
        // blocks until it is finished.
        if (g_AudioPlay.enVoice != enVoiceNone)
            {
            g_AudioPlay.PlayVoice();
            }

        // If there is a tone play then keep pumping out tone buffers. Note that PlayTone()
        // blocks until it finishes writing 100 msec of tone data.
        if (g_AudioPlay.enTone != enToneNone)
            g_AudioPlay.PlayTone();

    } // end while forever

}


// ============================================================================

AudioPlay::AudioPlay()
{
    enVoice              = enVoiceNone;
    enTone               = enToneNone;
    fVolume              = 0.5;
    fLeftGain            = 1.0;
    fRightGain           = 1.0;

    fTonePulseMaxSamples = 0;
    fTonePulseCounter    = 0;

    bAudioTest           = false;

    // Anti-click envelope state
    fEnvelopeLevel       = 0.0f;
    bPulseHigh           = true;
}


// ----------------------------------------------------------------------------

void AudioPlay::Init()
{
    // start I2S at the sample rate with 16-bits per sample
    i2s.setPins(I2S_BCK, I2S_LRCK, I2S_DOUT);

    for (int iAttempt = 0; iAttempt < 3; iAttempt++)
    {
        if (iAttempt > 0)
        {
            i2s.end();
            delay(50);
        }
        s_bI2sOk = i2s.begin(mode, SAMPLE_RATE, bps, slot);
        if (s_bI2sOk)
            break;
        g_Log.printf(MsgLog::EnAudio, MsgLog::EnWarning, "I2S init attempt %d/3 failed\n", iAttempt + 1);
    }

    if (!s_bI2sOk)
    {
        g_Log.println(MsgLog::EnAudio, MsgLog::EnError, "Failed to initialize I2S after 3 attempts!");
    }

    for (int iIdx = 0; iIdx < TONE_BUFFER_LEN; iIdx++)
        {
        double  fAngle;

        // 400 Hz tone
        fAngle = remainder(2.0*M_PI*iIdx*400.0/SAMPLE_RATE, 2.0*M_PI);
        aTone_400Hz[iIdx] = static_cast<int16_t>(25000 * cos(fAngle));

        // 1600 Hz tone
        fAngle = remainder(2.0*M_PI*iIdx*1600.0/SAMPLE_RATE, 2.0*M_PI);
        aTone_1600Hz[iIdx] = static_cast<int16_t>(25000 * cos(fAngle));
        }

    // Length of the data in the buffer. This may be different for tones that
    // don't fit exactly in the allocated buffer.
    iDataLen = TONE_BUFFER_LEN;

}

// ----------------------------------------------------------------------------

// Set the audio volume.

void AudioPlay::SetVolume(int iVolumePercent)
{
    if      (iVolumePercent <   0) fVolume = 0.0;
    else if (iVolumePercent > 100) fVolume = 1.0;
    else                           fVolume = iVolumePercent / 100.0;
}

// ----------------------------------------------------------------------------

// Set the channel gains. This is mostly for 3D audio support
// Nominal gain is 1.0

void AudioPlay::SetGain(float fLeftGain, float fRightGain)
{
    // I should probably put in limit checking someday
    this->fLeftGain  = fLeftGain;
    this->fRightGain = fRightGain;
}

// ----------------------------------------------------------------------------

// Select a voice to play. Voice will play once and reset.

void AudioPlay::SetVoice(EnVoice enVoice)
{
    this->enVoice = enVoice;
}

// ----------------------------------------------------------------------------

// Select a precomputed tone to play. Tone will continue to play until turned off.

void AudioPlay::SetTone(EnAudioTone enAudioTone)
{
    this->enTone = enAudioTone;
}

// ----------------------------------------------------------------------------

// Maybe someday.

void AudioPlay::SetToneFreq(unsigned uToneFreq)
{

}

// ----------------------------------------------------------------------------

// Make a tone envelope that generates a 50% duty cycle pulse at the commanded
// frequency. In the previous code the range of allowed frequencies was
// from 1.5 PPS to 6.2 PPS depending on AOA value.

void AudioPlay::SetPulseFreq(float fPulseFreq)
{
    // Outside limits disables tone pulse (solid tone)
    if ((fPulseFreq < 1.0) || (fPulseFreq > 25.0))
        fTonePulseMaxSamples = 0;
    else
        fTonePulseMaxSamples = SAMPLE_RATE / (fPulseFreq * 2.0);  // Half-period in samples
}

// ----------------------------------------------------------------------------

// Play converted PCM audio buffer
void AudioPlay::PlayPcmBuffer(const unsigned char * pData, int iDataLen, float fLeftVolume, float fRightVolume)
{
    if (!s_bI2sOk)
        return;

    constexpr size_t kFramesPerWrite = 240; // Match default I2S DMA frame count.
    uint32_t         aFrames[kFramesPerWrite];
    size_t           iFrameCount = 0;

    const int16_t * pPCM = reinterpret_cast<const int16_t *>(pData);
    const int       iSampleCount = iDataLen / static_cast<int>(sizeof(int16_t));

    for (int iSampleIdx = 0; iSampleIdx < iSampleCount; iSampleIdx++)
    {
        const int16_t iSample = pPCM[iSampleIdx];
        const int16_t iLeftValue = ScaleAndClampI16(iSample, fLeftVolume);
        const int16_t iRightValue = ScaleAndClampI16(iSample, fRightVolume);

        aFrames[iFrameCount++] = PackStereoI16(iLeftValue, iRightValue);
        if (iFrameCount == kFramesPerWrite)
            {
            i2s.write(reinterpret_cast<const uint8_t *>(aFrames), iFrameCount * sizeof(aFrames[0]));
            iFrameCount = 0;
            }
    }

    if (iFrameCount > 0)
        {
        i2s.write(reinterpret_cast<const uint8_t *>(aFrames), iFrameCount * sizeof(aFrames[0]));
        }
}

// ----------------------------------------------------------------------------

// Play locally generated audio tone buffer with anti-click envelope.
//
// A short 2 ms linear ramp is applied at pulse edges and tone start/stop
// to eliminate the hard amplitude step that causes audible clicks.  The
// ramp is short enough to keep pulses sounding crisp.

void AudioPlay::PlayToneBuffer(const int16_t * pData, int iDataLen, float fLeftVolume, float fRightVolume)
{
    if (!s_bI2sOk)
        return;

    constexpr size_t kFramesPerWrite = 240; // Match default I2S DMA frame count.
    uint32_t         aFrames[kFramesPerWrite];
    size_t           iFrameCount = 0;

    for (int iWordIdx = 0; iWordIdx < iDataLen; iWordIdx++)
    {
        // Determine target: 1.0 for solid tone or pulse-on, 0.0 for pulse-off
        const float fTarget = (bPulseHigh || fTonePulseMaxSamples == 0) ? 1.0f : 0.0f;

        // Ramp envelope toward target
        if (fEnvelopeLevel < fTarget)
            {
            fEnvelopeLevel += RAMP_PER_SAMPLE;
            if (fEnvelopeLevel > fTarget)
                fEnvelopeLevel = fTarget;
            }
        else if (fEnvelopeLevel > fTarget)
            {
            fEnvelopeLevel -= RAMP_PER_SAMPLE;
            if (fEnvelopeLevel < fTarget)
                fEnvelopeLevel = fTarget;
            }

        const int16_t iSample = pData[iWordIdx];
        const int16_t iLeftValue  = ScaleAndClampI16(iSample, fLeftVolume  * fEnvelopeLevel);
        const int16_t iRightValue = ScaleAndClampI16(iSample, fRightVolume * fEnvelopeLevel);

        // Advance pulse counter and toggle at each half-period
        if (fTonePulseMaxSamples > 0)
            {
            fTonePulseCounter++;
            if (fTonePulseCounter >= fTonePulseMaxSamples)
                {
                fTonePulseCounter -= fTonePulseMaxSamples;
                bPulseHigh = !bPulseHigh;
                }
            }

        aFrames[iFrameCount++] = PackStereoI16(iLeftValue, iRightValue);
        if (iFrameCount == kFramesPerWrite)
            {
            i2s.write(reinterpret_cast<const uint8_t *>(aFrames), iFrameCount * sizeof(aFrames[0]));
            iFrameCount = 0;
            }
    } // end for each sample in buffer

    if (iFrameCount > 0)
        {
        i2s.write(reinterpret_cast<const uint8_t *>(aFrames), iFrameCount * sizeof(aFrames[0]));
        }
}

// ----------------------------------------------------------------------------

// Play the audio voice set earier

void AudioPlay::PlayVoice()
{
    PlayVoice(enVoice);

    enVoice = enVoiceNone;

}

// ----------------------------------------------------------------------------

// Play the commanded voice

#define VOICE_BOOST     3.0

void AudioPlay::PlayVoice(EnVoice enVoice)
{
    AudioLogDebugNoBlock("PlayVoice %d\n", enVoice);
    // These WAV based audio clips need a volume boost
    float   fLeftVoiceVolume  = fVolume * VOICE_BOOST * fLeftGain;
    float   fRightVoiceVolume = fVolume * VOICE_BOOST * fRightGain;

    switch (enVoice)
    {
        case enVoiceDatamark  : PlayPcmBuffer(datamark_pcm,      datamark_pcm_len,      fLeftVoiceVolume, fRightVoiceVolume); break;
        case enVoiceDisabled  : PlayPcmBuffer(disabled_pcm,      disabled_pcm_len,      fLeftVoiceVolume, fRightVoiceVolume); break;
        case enVoiceEnabled   : PlayPcmBuffer(enabled_pcm,       enabled_pcm_len,       fLeftVoiceVolume, fRightVoiceVolume); break;
        case enVoiceGLimit    : PlayPcmBuffer(glimit_pcm,        glimit_pcm_len,        fLeftVoiceVolume, fRightVoiceVolume); break;
        case enVoiceCalCancel : PlayPcmBuffer(cal_canceled_pcm,  cal_canceled_pcm_len,  fLeftVoiceVolume, fRightVoiceVolume); break;
        case enVoiceCalMode   : PlayPcmBuffer(cal_mode_pcm,      cal_mode_pcm_len,      fLeftVoiceVolume, fRightVoiceVolume); break;
        case enVoiceCalSaved  : PlayPcmBuffer(cal_saved_pcm,     cal_saved_pcm_len,     fLeftVoiceVolume, fRightVoiceVolume); break;
        case enVoiceOverG     : PlayPcmBuffer(overg_pcm,         overg_pcm_len,         fLeftVoiceVolume, fRightVoiceVolume); break;
        case enVoiceVnoChime  : PlayPcmBuffer(VnoChime_pcm,      VnoChime_pcm_len,      fLeftVoiceVolume, fRightVoiceVolume); break;
        case enVoiceLeft      : PlayPcmBuffer(left_speaker_pcm,  left_speaker_pcm_len,  fLeftVoiceVolume, fRightVoiceVolume*.25); break;
        case enVoiceRight     : PlayPcmBuffer(right_speaker_pcm, right_speaker_pcm_len, fLeftVoiceVolume*.25, fRightVoiceVolume); break;
        default               : break;
    }

}

// ----------------------------------------------------------------------------

// Play the tone that was previously set

void AudioPlay::PlayTone()
{
    PlayTone(enTone);
}

// ----------------------------------------------------------------------------

// Play the selected tone

void AudioPlay::PlayTone(EnAudioTone enAudioTone)
{
    AudioLogDebugNoBlock("PlayTone %d\n", enAudioTone);

    switch (enAudioTone)
    {
        case enToneLow :
            PlayToneBuffer(aTone_400Hz, iDataLen, fVolume * fLeftGain, fVolume * fRightGain);
            break;

        case enToneHigh :
            PlayToneBuffer(aTone_1600Hz, iDataLen, fVolume * fLeftGain, fVolume * fRightGain);
            break;

        default :
            break;
    }
}

// ----------------------------------------------------------------------------

void AudioPlay::UpdateTones()
    {
    // If audio test is in progress then don't do anything
    if (bAudioTest)
        return;

    onspeed::ToneResult result;

    if (!g_bAudioEnable)
        {
        // Audio disabled by button — only allow stall warning through
        result = onspeed::calculateToneMuted(
            g_Sensors.AOA, g_Sensors.IAS,
            g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA,
            g_Config.iMuteAudioUnderIAS);
        }
    else if (g_Sensors.IAS <= g_Config.iMuteAudioUnderIAS)
        {
        // Airspeed too low (taxiing) — mute, but set pulse rate high for quick pickup
#ifdef TONEDEBUG
        AudioLogDebugNoBlock("AUDIO MUTED: Airspeed too low. Min:%i IAS:%.2f\n",
            g_Config.iMuteAudioUnderIAS, g_Sensors.IAS);
#endif
        result = { onspeed::EnToneType::None, 20.0f };
        }
    else
        {
        const onspeed::ToneThresholds th = {
            g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA,
            g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA,
            g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDSLOWAOA,
            g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA,
            };
        result = onspeed::calculateTone(g_Sensors.AOA, th);
        }

    SetTone(static_cast<EnAudioTone>(result.enTone));
    SetPulseFreq(result.fPulseFreq);
    }

// ----------------------------------------------------------------------------

bool AudioPlay::StartAudioTest()
{
    bool expected = false;
    if (!s_bAudioTestStarting.compare_exchange_strong(expected, true))
        return false;

    if (s_xAudioTestTask != nullptr)
        {
        s_bAudioTestStarting.store(false);
        return false;
        }

    s_bAudioTestStopRequested.store(false);

    TaskHandle_t newTask = nullptr;

    BaseType_t status = xTaskCreatePinnedToCore(
        AudioTestTask,
        "AudioTest",
        3000,
        nullptr,
        1,
        &newTask,
        0);

    if (status != pdPASS)
        {
        s_xAudioTestTask = nullptr;
        s_bAudioTestStarting.store(false);
        return false;
        }

    s_xAudioTestTask = newTask;
    s_bAudioTestStarting.store(false);
    return true;
}

// ----------------------------------------------------------------------------

void AudioPlay::StopAudioTest()
{
    if (!IsAudioTestRunning())
        return;

    s_bAudioTestStopRequested.store(true);

    // Stop any continuous tone quickly (voice clips are allowed to finish).
    SetPulseFreq(0);
    SetTone(enToneNone);
    SetVoice(enVoiceNone);
}

// ----------------------------------------------------------------------------

bool AudioPlay::IsAudioTestRunning() const
{
    return (s_xAudioTestTask != nullptr) || s_bAudioTestStarting.load();
}

// ----------------------------------------------------------------------------

void AudioPlay::AudioTest()
{
    bAudioTest = true;
    s_bAudioTestStopRequested.store(false);

    auto DelayOrStop = [&](uint32_t delayMs) -> bool
        {
        TickType_t remaining = pdMS_TO_TICKS(delayMs);
        while (remaining > 0)
            {
            if (s_bAudioTestStopRequested.load())
                {
                SetPulseFreq(0);
                SetTone(enToneNone);
                SetVoice(enVoiceNone);
                return false;
                }

            TickType_t slice = remaining > pdMS_TO_TICKS(50) ? pdMS_TO_TICKS(50) : remaining;
            vTaskDelay(slice);
            remaining -= slice;
            }

        return !s_bAudioTestStopRequested.load();
        };

    if (s_bAudioTestStopRequested.load())
        goto done;

//    pSerial->printf("Left Voice\n");
    g_AudioPlay.SetVoice(enVoiceLeft);
    if (!DelayOrStop(2000)) goto done;

//    pSerial->printf("Right Voice\n");
    g_AudioPlay.SetVoice(enVoiceRight);
    if (!DelayOrStop(2000)) goto done;

//    pSerial->printf("Tone LOW\n");
    g_AudioPlay.SetTone(enToneLow);
    if (!DelayOrStop(2000)) goto done;

//    pSerial->printf("G Limit\n");
    g_AudioPlay.SetVoice(enVoiceGLimit);
    if (!DelayOrStop(3000)) goto done;

//    pSerial->printf("Tone HIGH\n");
    g_AudioPlay.SetTone(enToneHigh);
    if (!DelayOrStop(2000)) goto done;

//    pSerial->printf("Tone LOW\n");
    g_AudioPlay.SetTone(enToneLow);
    if (!DelayOrStop(1500)) goto done;

//    pSerial->printf("Pulse 2.0 Hz\n");
    g_AudioPlay.SetPulseFreq(3.0);
    if (!DelayOrStop(2000)) goto done;

//    pSerial->printf("Pulse 4.0 Hz\n");
    g_AudioPlay.SetPulseFreq(3.0);
    if (!DelayOrStop(2000)) goto done;

//    pSerial->printf("Pulse 4.0 Hz\n");
    g_AudioPlay.SetPulseFreq(5.0);
    if (!DelayOrStop(2000)) goto done;

//    pSerial->printf("Tone HIGH\n");
    g_AudioPlay.SetTone(enToneHigh);
    if (!DelayOrStop(2000)) goto done;

//    pSerial->printf("Pulse 3.0 Hz\n");
    g_AudioPlay.SetPulseFreq(4.0);
    if (!DelayOrStop(2000)) goto done;

done:
//    pSerial->printf("Tone OFF, Pulse 4.0 Hz\n");
    g_AudioPlay.SetPulseFreq(0);

//    pSerial->printf("\n");
    g_AudioPlay.SetTone(enToneNone);

    bAudioTest = false;
    s_bAudioTestStopRequested.store(false);
}
