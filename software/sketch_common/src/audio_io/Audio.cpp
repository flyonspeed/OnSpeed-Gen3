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

    ------------------------------------------------------------------------

    PR 3.3 split:

      Pure PCM math now lives in onspeed_core/audio/:
        - ToneSynth   : cosine synthesis + pulse gate
        - WavDecode   : PCM byte array / WAV header → PcmAsset view
        - AudioMixer  : mono PCM → stereo int16 with gain/pan/pulse

      This file is now a thin I2S driver / task wrapper that feeds the
      core modules.  Buffer arithmetic, clamping, pan, and pulse shaping
      are validated by native unit tests (test_tone_synth,
      test_wav_decode, test_audio_mixer) — on-device we only have to
      trust the i2s.write pump.

 */

#include <bit>
#include <cstdarg>
#include <atomic>

#include <math.h>

#include <Arduino.h>
#include <ESP_I2S.h>

#include "src/Globals.h"
#include "src/util/Helpers.h"
#include <audio/AudioMixer.h>
#include <audio/ToneCalc.h>
#include <audio/ToneSynth.h>
#include <audio/WavDecode.h>

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


//i2s_data_bit_width_t  bps  = I2S_DATA_BIT_WIDTH_32BIT;  //
i2s_data_bit_width_t  bps  = I2S_DATA_BIT_WIDTH_16BIT; // Only 16 seems to work well with tones
//i2s_data_bit_width_t  bps  = I2S_DATA_BIT_WIDTH_8BIT;  //

i2s_mode_t            mode = I2S_MODE_STD;  // Works

i2s_slot_mode_t       slot = I2S_SLOT_MODE_STEREO;    // Works better
//i2s_slot_mode_t       slot = I2S_SLOT_MODE_MONO;    // Works

static int16_t     aTone_400Hz[TONE_BUFFER_LEN];
static int16_t     aTone_1600Hz[TONE_BUFFER_LEN];

// Persistent mixer state used by PlayToneBuffer so the pulse phase survives
// across the 50 ms-per-call pump loop.  Replaces the old `static bool
// bPulseLevel` inside PlayToneBuffer.
static onspeed::audio::MixerState s_ToneMixerState;

// I2S pins are defined in HardwareMap.h as kI2sBck, kI2sDout, kI2sLrck.

#define FREERTOS

// Tone frequency and ramp constants (PPS constants moved to onspeed_core/ToneCalc.h)
#define HIGH_TONE_HZ         1600                 // freq of high tone
#define LOW_TONE_HZ           400                 // freq of low tone
#define TONE_RAMP_TIME         15                 // millisec
#define STALL_RAMP_TIME         5                 // millisec

// ----------------------------------------------------------------------------

static bool         s_bI2sOk = false;
static bool         s_bAudioUnmuted = false;    // Hysteretic mute state; see UpdateTones().
static volatile TaskHandle_t s_xAudioTestTask = nullptr;
static std::atomic<bool>     s_bAudioTestStopRequested{false};
static std::atomic<bool>     s_bAudioTestStarting{false};

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

// Wake the audio play task if it is blocked on ulTaskNotifyTake().
// Safe to call from any task context on either core.
static void NotifyAudioTask()
    {
    if (xTaskAudioPlay != NULL)
        xTaskNotifyGive(xTaskAudioPlay);
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

        // Block until SetTone/SetVoice sends a notification, or 100ms
        // elapses as a safety net.  pdTRUE clears the notification count
        // so stale notifications don't cause extra wakeups.
        if (g_AudioPlay.enTone == enToneNone && g_AudioPlay.enVoice == enVoiceNone)
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        // If a voice play has been selected then play it once. Note that PlayVoice()
        // blocks until it is finished.
        if (g_AudioPlay.enVoice != enVoiceNone)
            {
            g_AudioPlay.PlayVoice();
            }

        // If there is a tone play then keep pumping out tone buffers. Note that PlayTone()
        // blocks until it finishes writing 50 msec of tone data.
        if (g_AudioPlay.enTone != enToneNone)
            g_AudioPlay.PlayTone();

    } // end while forever

}


// ============================================================================

AudioPlay::AudioPlay()
{
    enVoice              = enVoiceNone;
    enTone               = enToneNone;
    fVolume              = 0.5f;
    fLeftGain            = 1.0f;
    fRightGain           = 1.0f;

    fTonePulseMaxSamples = 0;
    fTonePulseCounter    = 0;

    bAudioTest           = false;
}


// ----------------------------------------------------------------------------

void AudioPlay::Init()
{
    // start I2S at the sample rate with 16-bits per sample
    i2s.setPins(kI2sBck, kI2sLrck, kI2sDout);

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

    // Precompute the 400 Hz and 1600 Hz tone buffers using the shared
    // onspeed_core synth routine.  Byte-for-byte matches the legacy
    // Audio.cpp init loop that seeded aTone_400Hz / aTone_1600Hz with
    // 25000 * cos(2*pi*i*freq/SR).
    onspeed::audio::SynthesizeLegacyCosine(
        static_cast<float>(LOW_TONE_HZ), SAMPLE_RATE,
        aTone_400Hz, TONE_BUFFER_LEN);
    onspeed::audio::SynthesizeLegacyCosine(
        static_cast<float>(HIGH_TONE_HZ), SAMPLE_RATE,
        aTone_1600Hz, TONE_BUFFER_LEN);

    // Length of the data in the buffer. This may be different for tones that
    // don't fit exactly in the allocated buffer.
    iDataLen = TONE_BUFFER_LEN;
}

// ----------------------------------------------------------------------------

// Set the audio volume.

void AudioPlay::SetVolume(int iVolumePercent)
{
    if      (iVolumePercent <   0) fVolume = 0.0f;
    else if (iVolumePercent > 100) fVolume = 1.0f;
    else                           fVolume = iVolumePercent / 100.0f;
}

// ----------------------------------------------------------------------------

// Set the channel gains. This is mostly for 3D audio support
// Nominal gain is 1.0

void AudioPlay::SetGain(float fLeftGainIn, float fRightGainIn)
{
    // I should probably put in limit checking someday
    fLeftGain  = fLeftGainIn;
    fRightGain = fRightGainIn;
}

// ----------------------------------------------------------------------------

// Select a voice to play. Voice will play once and reset.

void AudioPlay::SetVoice(EnVoice enVoiceIn)
{
    enVoice = enVoiceIn;

    if (enVoiceIn != enVoiceNone)
        NotifyAudioTask();
}

// ----------------------------------------------------------------------------

// Select a precomputed tone to play. Tone will continue to play until turned off.

void AudioPlay::SetTone(EnAudioTone enAudioTone)
{
    this->enTone = enAudioTone;

    if (enAudioTone != enToneNone)
        NotifyAudioTask();
}

// ----------------------------------------------------------------------------

// Maybe someday.

void AudioPlay::SetToneFreq(unsigned uToneFreqIn)
{
    (void)uToneFreqIn;
}

// ----------------------------------------------------------------------------

// Make a tone envelope that generates a 50% duty cycle pulse at the commanded
// frequency. In the previous code the range of allowed frequencies was
// from 1.5 PPS to 6.2 PPS depending on AOA value.

void AudioPlay::SetPulseFreq(float fPulseFreq)
{
    // Outside limits disables tone pulse
    if ((fPulseFreq < 1.0f) || (fPulseFreq > 25.0f))
        fTonePulseMaxSamples = 0;
    else
        fTonePulseMaxSamples = SAMPLE_RATE / (fPulseFreq * 2.0f);  // Tone period in audio samples

}

// ----------------------------------------------------------------------------

// Play converted PCM audio buffer (voice clip).  Composes pan/gain via
// onspeed_core AudioMixer into a local stereo frame buffer, then hands
// the packed frames to the I2S driver in 240-frame DMA chunks.
void AudioPlay::PlayPcmBuffer(const unsigned char * pData, int iNumBytes, float fLeftVolume, float fRightVolume)
{
    if (!s_bI2sOk)
        return;

    constexpr size_t kFramesPerWrite = 240; // Match default I2S DMA frame count.
    int16_t          aStereo[kFramesPerWrite * 2];  // interleaved L,R
    uint32_t         aFrames[kFramesPerWrite];

    onspeed::audio::PcmAsset asset = onspeed::audio::FromRawPcm(
        pData, static_cast<size_t>(iNumBytes), SAMPLE_RATE, 1);
    if (asset.empty())
        return;

    onspeed::audio::MixerInputs inp;
    inp.leftScale  = fLeftVolume;
    inp.rightScale = fRightVolume;
    // Voice playback: no pulse gating.
    inp.pulseSpec.halfPeriodSamples = 0.0f;

    onspeed::audio::MixerState voiceState;   // local, voice has no persistent pulse

    size_t remaining = asset.sampleCount;
    const int16_t* src = asset.samples;

    while (remaining > 0)
    {
        const size_t n = remaining < kFramesPerWrite ? remaining : kFramesPerWrite;
        inp.in = src;
        onspeed::audio::Mix(inp, aStereo, n, voiceState);

        for (size_t j = 0; j < n; ++j)
        {
            aFrames[j] = onspeed::audio::PackStereoI16(aStereo[2 * j + 0],
                                                        aStereo[2 * j + 1]);
        }
        i2s.write(reinterpret_cast<const uint8_t *>(aFrames), n * sizeof(aFrames[0]));

        src       += n;
        remaining -= n;
    }
}

// ----------------------------------------------------------------------------

// Play locally generated audio tone buffer
void AudioPlay::PlayToneBuffer(const int16_t * pData, int iNumSamples, float fLeftVolume, float fRightVolume)
{
    if (!s_bI2sOk)
        return;

    constexpr size_t kFramesPerWrite = 240; // Match default I2S DMA frame count.
    int16_t          aStereo[kFramesPerWrite * 2];
    uint32_t         aFrames[kFramesPerWrite];

    onspeed::audio::MixerInputs inp;
    inp.leftScale  = fLeftVolume;
    inp.rightScale = fRightVolume;
    inp.pulseSpec.halfPeriodSamples = fTonePulseMaxSamples;
    inp.pulseSpec.offScale          = 0.2f;   // Legacy "ducked" pulse level

    // Persist the tone-mixer state across calls / chunks so the pulse
    // gate phase continues smoothly (matches the legacy `static bool
    // bPulseLevel` behaviour of the old PlayToneBuffer()).
    size_t remaining = static_cast<size_t>(iNumSamples);
    const int16_t* src = pData;

    while (remaining > 0)
    {
        const size_t n = remaining < kFramesPerWrite ? remaining : kFramesPerWrite;
        inp.in = src;
        onspeed::audio::Mix(inp, aStereo, n, s_ToneMixerState);

        for (size_t j = 0; j < n; ++j)
        {
            aFrames[j] = onspeed::audio::PackStereoI16(aStereo[2 * j + 0],
                                                        aStereo[2 * j + 1]);
        }
        i2s.write(reinterpret_cast<const uint8_t *>(aFrames), n * sizeof(aFrames[0]));

        src       += n;
        remaining -= n;
    }

    // Keep the legacy public counter in sync so external observers (if any)
    // can still see the current pulse progress.
    fTonePulseCounter = s_ToneMixerState.pulse.counter;
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

#define VOICE_BOOST     3.0f  // float to avoid double promotion on ESP32 soft-FPU

void AudioPlay::PlayVoice(EnVoice enVoiceIn)
{
    AudioLogDebugNoBlock("PlayVoice %d\n", enVoiceIn);
    // These WAV based audio clips need a volume boost.
    // Cap at 1.0 to prevent clipping when 3D panning gain (up to 2.0) combines with VOICE_BOOST.
    float   fLeftVoiceVolume  = fVolume * VOICE_BOOST * fLeftGain;
    float   fRightVoiceVolume = fVolume * VOICE_BOOST * fRightGain;
    if (fLeftVoiceVolume  > 1.0f) fLeftVoiceVolume  = 1.0f;
    if (fRightVoiceVolume > 1.0f) fRightVoiceVolume = 1.0f;

    switch (enVoiceIn)
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
        case enVoiceLeft      : PlayPcmBuffer(left_speaker_pcm,  left_speaker_pcm_len,  fLeftVoiceVolume, fRightVoiceVolume*.25f); break;
        case enVoiceRight     : PlayPcmBuffer(right_speaker_pcm, right_speaker_pcm_len, fLeftVoiceVolume*.25f, fRightVoiceVolume); break;
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

    // Cap per-channel gain to prevent waveform clipping when 3D audio
    // panning drives fLeftGain/fRightGain up to 2.0. Without this cap,
    // the tone sine wave clips to a square wave at full volume + full pan.
    float fL = fVolume * fLeftGain;
    float fR = fVolume * fRightGain;
    if (fL > 1.0f) fL = 1.0f;
    if (fR > 1.0f) fR = 1.0f;

    switch (enAudioTone)
    {
        case enToneLow :
            PlayToneBuffer(aTone_400Hz, iDataLen, fL, fR);
            break;

        case enToneHigh :
            PlayToneBuffer(aTone_1600Hz, iDataLen, fL, fR);
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

    // Audio mute hysteresis: unmute at iMuteAudioUnderIAS + 5 kt, mute back
    // at iMuteAudioUnderIAS.  Fixes audio chatter on touchdown (AUD-01) when
    // IAS oscillates a few knots around the configured mute threshold —
    // without hysteresis, the filter flips state several times per second
    // and the pilot hears a fraction of a tone burst on each bounce.
    //
    // iMuteAudioUnderIAS == 0 is a sentinel for "always on, never mute" —
    // bypass the hysteresis state machine so audio is live from boot
    // regardless of the +5 kt unmute band.
    if (g_Config.iMuteAudioUnderIAS == 0)
        {
        s_bAudioUnmuted = true;
        }
    else
        {
        static constexpr int kAudioHysteresisKt = 5;
        const int iUnmuteThreshold = g_Config.iMuteAudioUnderIAS + kAudioHysteresisKt;
        if (!s_bAudioUnmuted && g_Sensors.IAS >= iUnmuteThreshold)
            s_bAudioUnmuted = true;
        else if (s_bAudioUnmuted && g_Sensors.IAS < g_Config.iMuteAudioUnderIAS)
            s_bAudioUnmuted = false;
        }

    if (!g_bAudioEnable)
        {
        // Audio disabled by button — only allow stall warning through
        result = onspeed::calculateToneMuted(
            g_Sensors.AOA, g_Sensors.IAS,
            g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA,
            g_Config.iMuteAudioUnderIAS);
        }
    else if (!s_bAudioUnmuted)
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

    g_AudioPlay.SetVoice(enVoiceLeft);
    if (!DelayOrStop(2000)) goto done;

    g_AudioPlay.SetVoice(enVoiceRight);
    if (!DelayOrStop(2000)) goto done;

    g_AudioPlay.SetTone(enToneLow);
    if (!DelayOrStop(2000)) goto done;

    g_AudioPlay.SetVoice(enVoiceGLimit);
    if (!DelayOrStop(3000)) goto done;

    g_AudioPlay.SetTone(enToneHigh);
    if (!DelayOrStop(2000)) goto done;

    g_AudioPlay.SetTone(enToneLow);
    if (!DelayOrStop(1500)) goto done;

    g_AudioPlay.SetPulseFreq(3.0);
    if (!DelayOrStop(2000)) goto done;

    g_AudioPlay.SetPulseFreq(3.0);
    if (!DelayOrStop(2000)) goto done;

    g_AudioPlay.SetPulseFreq(5.0);
    if (!DelayOrStop(2000)) goto done;

    g_AudioPlay.SetTone(enToneHigh);
    if (!DelayOrStop(2000)) goto done;

    g_AudioPlay.SetPulseFreq(4.0);
    if (!DelayOrStop(2000)) goto done;

done:
    g_AudioPlay.SetPulseFreq(0);

    g_AudioPlay.SetTone(enToneNone);

    bAudioTest = false;
    s_bAudioTestStopRequested.store(false);
}
