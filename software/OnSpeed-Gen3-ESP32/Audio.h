
#pragma once

#include <stdint.h>
#include <bit>

#include <ESP_I2S.h>


enum EnVoice
    {
    enVoiceNone, enVoiceDatamark, enVoiceDisabled, enVoiceEnabled, enVoiceGLimit, 
    enVoiceCalCancel, enVoiceCalMode, enVoiceCalSaved, enVoiceOverG, enVoiceVnoChime,
    enVoiceLeft, enVoiceRight,
    };

enum EnAudioTone
    {
    enToneNone, enToneDisabled, enToneLow, enToneHigh
    };


void AudioPlayTask(void * psuParams);

// ============================================================================

//#define SAMPLE_RATE         44100
#define SAMPLE_RATE         16000
#define TONE_BUFFER_LEN     SAMPLE_RATE / 10    // 100 msec of audio data

class AudioPlay
{
public:
    AudioPlay();

    // Data

public:
    EnVoice         enVoice;
    EnAudioTone     enTone;
    unsigned        uToneFreq;
    float           fVolume;            // Audio output volume,from 0.0 to 1.0
    float           fLeftGain;          // Gain control, mostly for 3D audio, nominally 1.0 but 
    float           fRightGain;         // can be higher or lower.

    unsigned        uTonePulseMaxSamples;
    unsigned        uTonePulseCounter;

    I2SClass        i2s;
    int             iDataLen;           // Number of data points in the audio tone buffer. Not necessarily the buffer length!

    bool            bAudioTest;

    friend void AudioPlayTask(void * psuParams);

    // Methods
public:
    void Init();
    void inline WriteSample(int16_t iLeftValue, int16_t iRightValue);
    void SetVolume(int iVolumePercent);
    void SetGain(float fLeftGain, float fRightGain);
    void SetVoice(EnVoice enVoice);
    void SetTone(EnAudioTone enAudioTone);
    void SetToneFreq(unsigned uToneFreq);
    void SetPulseFreq(float fPulseFreq);
    void UpdateTones();
    void AudioTest();

private:
    void PlayPcmBuffer(const unsigned char * pData, int iDataLen, float fLeftVolume, float fRightVolume);
    void PlayToneBuffer(const int16_t * pData, int iDataLen, float fLeftVolume, float fRightVolume);
    void PlayVoice();
    void PlayVoice(EnVoice enVoice);
    void PlayTone();
    void PlayTone(EnAudioTone enAudioTone);
};