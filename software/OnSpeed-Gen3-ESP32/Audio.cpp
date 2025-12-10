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
#include <math.h>

#include <Arduino.h>
#include <ESP_I2S.h>

#include "Globals.h"
#include "Helpers.h"

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

#include "audio.h"

//i2s_data_bit_width_t  bps  = I2S_DATA_BIT_WIDTH_32BIT;
i2s_data_bit_width_t    bps  = I2S_DATA_BIT_WIDTH_16BIT; // Only 16 seems to work well with tones
//i2s_data_bit_width_t  bps  = I2S_DATA_BIT_WIDTH_8BIT;

i2s_mode_t              mode = I2S_MODE_STD;  // Works

i2s_slot_mode_t         slot = I2S_SLOT_MODE_STEREO;    // Works better
//i2s_slot_mode_t       slot = I2S_SLOT_MODE_MONO;      // Works

// Tone buffer data
int16_t             aTone_400Hz[TONE_BUFFER_LEN];
int16_t             aTone_1600Hz[TONE_BUFFER_LEN];

// Pin defs
#define I2S_BCK                45
#define I2S_DOUT               48
#define I2S_LRCK               47

//#define FREERTOS

// Tone Pulse Per Sec (PPS)
#define HIGH_TONE_STALL_PPS    20                 // how many PPS to play during stall
#define HIGH_TONE_PPS_MAX       6.2     
#define HIGH_TONE_PPS_MIN       1.5               // 1.5
#define HIGH_TONE_HZ         1600                 // Freq of high tone  
#define LOW_TONE_PPS_MAX        8.2
#define LOW_TONE_PPS_MIN        1.5
#define LOW_TONE_HZ           400                 // Freq of low tone
#define TONE_RAMP_TIME         15                 // millisec
#define STALL_RAMP_TIME         5                 // millisec

// ----------------------------------------------------------------------------

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
        // If a voice play has been selected then play it once. Note that PlayVoice()
        // blocks until it is finished.
        if (g_AudioPlay.enVoice != enVoiceNone)
            {
            g_AudioPlay.PlayVoice();
            }

        // Keep playing something even if no tone is selected to keep the audio
        // hardware active. Note that PlayTone() blocks until it finishes writing 
        // 100 msec of tone data.
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

    uTonePulseMaxSamples = 0;
//    uTonePulseMidSamples = 0;
    uTonePulseCounter    = 0;

    bAudioTest           = false;
}


// ----------------------------------------------------------------------------

void AudioPlay::Init()
{
    // start I2S at the sample rate with 16-bits per sample
    i2s.setPins(I2S_BCK, I2S_LRCK, I2S_DOUT);
    if (!i2s.begin(mode, SAMPLE_RATE, bps, slot)) 
    {
        g_Log.println(MsgLog::EnAudio, MsgLog::EnError, "Failed to initialize I2S!");
    }

    for (int iIdx = 0; iIdx < TONE_BUFFER_LEN; iIdx++)
        {
        double  fAngle;

        // Note byte swap to get the tone data in the same endianness as the WAV data
        // 400 Hz tone
#if 1
        fAngle = remainder(2.0*M_PI*iIdx*400.0/SAMPLE_RATE, 2.0*M_PI);
        aTone_400Hz[iIdx] = uint16_t(25000 * cos(fAngle));
#else
        float fAngle1 = remainder(2.0*M_PI*iIdx*440.00/SAMPLE_RATE, 2.0*M_PI);   // A
        // float fAngle2 = remainder(2.0*M_PI*iIdx*523.25/SAMPLE_RATE, 2.0*M_PI);   // C
        // float fAngle3 = remainder(2.0*M_PI*iIdx*659.25/SAMPLE_RATE, 2.0*M_PI);   // E
        float fAngle2 = remainder(2.0*M_PI*iIdx*400.00/SAMPLE_RATE, 2.0*M_PI);   // C
        float fAngle3 = remainder(2.0*M_PI*iIdx*800.00/SAMPLE_RATE, 2.0*M_PI);   // E
        aTone_400Hz[iIdx] = uint16_t(15000 * cos(fAngle1) +
                                      3000 * cos(fAngle2) +
                                      3000 * cos(fAngle3));
#endif
        // Setup 1600 Hz tone
        fAngle = remainder(2.0*M_PI*iIdx*1600.0/SAMPLE_RATE, 2.0*M_PI);
        aTone_1600Hz[iIdx] = uint16_t(25000 * cos(fAngle));
        }

    // Length of the data in the buffer. This may be different for tones that
    // don't fit exactly in the allocated buffer.
    iDataLen = TONE_BUFFER_LEN;

}

// ----------------------------------------------------------------------------

// Write left and write audio samples to the I2S device.

void AudioPlay::WriteSample(int16_t iLeftValue, int16_t iRightValue)
{
    union 
    {
        int16_t     iSample;
        uint8_t     abySample[2];
    } unLeftSample, unRightSample;

    unLeftSample.iSample  = iLeftValue;
    unRightSample.iSample = iRightValue;

    i2s.write(unLeftSample.abySample[0]);
    i2s.write(unLeftSample.abySample[1]);
    i2s.write(unRightSample.abySample[0]);
    i2s.write(unRightSample.abySample[1]);
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
    // Outside limits disables tone pulse
    if ((fPulseFreq < 1.0) || (fPulseFreq > 25.0))
        uTonePulseMaxSamples = 0;
    else
        uTonePulseMaxSamples = unsigned(SAMPLE_RATE / (fPulseFreq * 2));  // Tone period in audio samples

}

// ----------------------------------------------------------------------------

// Play converted PCM audio buffer
void AudioPlay::PlayPcmBuffer(const unsigned char * pData, int iDataLen, float fLeftVolume, float fRightVolume)
{
    int16_t       * pPCM = (int16_t *)pData;
    int16_t         iLeftValue, iRightValue;

    for (int iWordIdx = 0; iWordIdx < iDataLen/2; iWordIdx++)
    {
        iLeftValue  = pPCM[iWordIdx] * fLeftVolume;
        iRightValue = pPCM[iWordIdx] * fRightVolume;

        WriteSample(iLeftValue, iRightValue);
    }    
}

// ----------------------------------------------------------------------------

// Play locally generated audio tone buffer
void AudioPlay::PlayToneBuffer(const int16_t * pData, int iDataLen, float fLeftVolume, float fRightVolume)
{
    static bool     bPulseLevel = true;
    int16_t         iLeftValue, iRightValue;

    for (int iWordIdx = 0; iWordIdx < iDataLen; iWordIdx++)
    {
        // Apply tone pulse modulation
        if ((bPulseLevel == true) || (uTonePulseMaxSamples == 0))
        {
            iLeftValue  = pData[iWordIdx] * fLeftVolume;
            iRightValue = pData[iWordIdx] * fRightVolume;
        }
        else
        {
            iLeftValue  = pData[iWordIdx] * fLeftVolume  * 0.2;
            iRightValue = pData[iWordIdx] * fRightVolume * 0.2;
        }

        // If pulse period exceeded then change pulse level
        if (uTonePulseCounter >= uTonePulseMaxSamples)
        {
            uTonePulseCounter = 0;
            bPulseLevel       = !bPulseLevel;
        }
        else
            uTonePulseCounter++;

        WriteSample(iLeftValue, iRightValue);

    } // end for each sample in buffer

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
    g_Log.printf(MsgLog::EnAudio, MsgLog::EnDebug, "PlayVoice %d\n", enVoice);
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
    if (g_bAudioEnable == false)
        enAudioTone = enToneDisabled;

    g_Log.printf(MsgLog::EnAudio, MsgLog::EnDebug, "PlayTone %d\n", enAudioTone);

    switch (enAudioTone)
    {
        case enToneLow : 
            PlayToneBuffer(aTone_400Hz, iDataLen, fVolume * fLeftGain, fVolume * fRightGain);
            break;

        case enToneHigh : 
            PlayToneBuffer(aTone_1600Hz, iDataLen, fVolume * fLeftGain, fVolume * fRightGain);
            break;
        
        default: // This catches enToneNone and enToneDisabled
            // Play 100 msec of silence
            for (int iIdx = 0; iIdx < SAMPLE_RATE / 10; iIdx++)
                g_AudioPlay.WriteSample(0, 0);
            break;
    }
}


// ----------------------------------------------------------------------------

void AudioPlay::UpdateTones()
    {
    float   fNewPulseFreq;

    // If audio test is in progress then don't do anything
    if (bAudioTest)
        return;

    // If airspeed is low (like taxiing) don't make audio
    if (g_Sensors.IAS <= g_Config.iMuteAudioUnderIAS)
        {
#ifdef TONEDEBUG
        // show audio muted and debug info.
        sprintf(tempBuf, "AUDIO MUTED: Airspeed too low. Min:%i IAS:%.2f",muteAudioUnderIAS, IAS);
        Serial.println(tempBuf);
#endif
        SetTone(enToneNone);
        SetPulseFreq(20); // set the update rate to LOW_TONE_PPS_MAX if no tone is playing to pick up a pulsed tone quickly
        return;
        }

    // check AOA value and set tone and pauses between tones according to
    if      (g_Sensors.AOA >= g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA) // stallWarningAOA
        {
        // play 20 pps HIGH tone
        SetTone(enToneHigh);
        SetPulseFreq(HIGH_TONE_STALL_PPS);
        }
    else if (g_Sensors.AOA > (g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDSLOWAOA))    // onSpeedAOAslow
        {
        // play HIGH tone at Pulse Rate 1.5 PPS to 6.2 PPS (depending on AOA value)
        SetTone(enToneHigh);
        fNewPulseFreq = mapfloat(
            g_Sensors.AOA, 
            g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDSLOWAOA,    // onSpeedAOAslow
            g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA,      // stallWarningAOA
            HIGH_TONE_PPS_MIN, 
            HIGH_TONE_PPS_MAX);
        SetPulseFreq(fNewPulseFreq); // when transitioning from solid to high tone make the first one shorter
        }
    else if(g_Sensors.AOA >= (g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA)) // onSpeedAOAfast
        {
        // play a steady LOW tone
        SetTone(enToneLow);
        SetPulseFreq(0);
        }
    else if ((g_Sensors.AOA >= g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA) && // LDmaxAOA
             (g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA < g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA)) // onSpeedAOAfast
        {  // if L/D max AOA is higher than OnSpeedfast, skip the low tone. This usually happens with full flaps.
        SetTone(enToneLow);
        // play LOW tone at Pulse Rate 1.5 PPS to 8.2 PPS (depending on AOA value)
        fNewPulseFreq = mapfloat(
            g_Sensors.AOA,
            g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA,       // LDmaxAOA
            g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA, // onSpeedAOAfast,
            LOW_TONE_PPS_MIN,
            LOW_TONE_PPS_MAX);
        SetPulseFreq(fNewPulseFreq);
        }
    else
        {
        SetTone(enToneNone);
        SetPulseFreq(0);
        }
    }

// ----------------------------------------------------------------------------

void AudioPlay::AudioTest()
{
    bAudioTest = true;

//    pSerial->printf("Left Voice\n");
    g_AudioPlay.SetVoice(enVoiceLeft);
    vTaskDelay(2000 / portTICK_PERIOD_MS);

//    pSerial->printf("Right Voice\n");
    g_AudioPlay.SetVoice(enVoiceRight);
    vTaskDelay(2000 / portTICK_PERIOD_MS);

//    pSerial->printf("Tone LOW\n");
    g_AudioPlay.SetTone(enToneLow);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
//    pSerial->printf("G Limit\n");
    g_AudioPlay.SetVoice(enVoiceGLimit);
    vTaskDelay(3000 / portTICK_PERIOD_MS);

//    pSerial->printf("Tone HIGH\n");
    g_AudioPlay.SetTone(enToneHigh);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
//    pSerial->printf("Tone LOW\n");
    g_AudioPlay.SetTone(enToneLow);
    vTaskDelay(1500 / portTICK_PERIOD_MS);

//    pSerial->printf("Pulse 2.0 Hz\n");
    g_AudioPlay.SetPulseFreq(3.0);
    vTaskDelay(2000 / portTICK_PERIOD_MS);

//    pSerial->printf("Pulse 4.0 Hz\n");
    g_AudioPlay.SetPulseFreq(3.0);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
//    pSerial->printf("Pulse 4.0 Hz\n");
    g_AudioPlay.SetPulseFreq(5.0);
    vTaskDelay(2000 / portTICK_PERIOD_MS);

//    pSerial->printf("Tone HIGH\n");
    g_AudioPlay.SetTone(enToneHigh);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
//    pSerial->printf("Pulse 3.0 Hz\n");
    g_AudioPlay.SetPulseFreq(4.0);
    vTaskDelay(2000 / portTICK_PERIOD_MS);

//    pSerial->printf("Tone OFF, Pulse 4.0 Hz\n");
    g_AudioPlay.SetPulseFreq(3.0);
    g_AudioPlay.SetPulseFreq(0);

//    pSerial->printf("\n");
    g_AudioPlay.SetTone(enToneNone);

    bAudioTest = false;
}
