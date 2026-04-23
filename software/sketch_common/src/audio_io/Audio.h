
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
    enToneNone, enToneLow, enToneHigh
    };


void AudioPlayTask(void * psuParams);

// ============================================================================

//#define SAMPLE_RATE         44100
#define SAMPLE_RATE         16000
// Source PCM tone buffer (precomputed cosine).  Size is sample-rate / 20 so
// the index wraps every 50 ms — phase-continuous across pump calls because
// the buffer length is exactly 1/20 of a second of full cycles for both
// 400 Hz and 1600 Hz (both periods divide evenly).
#define TONE_BUFFER_LEN     SAMPLE_RATE / 20

// Audio buffer pump size: 240 samples = 15 ms at 16 kHz.  Matches the
// I2S DMA chunk size.  Keeping the pump short (vs the historical 50 ms)
// bounds the latency between SetTone() and the next audible attack at
// ~15 ms, so Gen2's 61 ms solid→high transition timing still lands
// inside one half-period at 6.2 PPS (80 ms).  See Tones.ino design notes
// in Envelope.h.
#define TONE_PUMP_FRAMES    240

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

    // AOA-driven per-PPS volume multiplier ported from Gen2 Tones.ino.
    // Cruise/on-speed tones play at STALL_VOL_MIN (0.25), stall warning
    // hits STALL_VOL_MAX (1.0), pulsed-high interpolates between.
    // Updated by UpdateTones() at the sensor task rate (~208 Hz).
    // Tones only — voice clips bypass this multiplier.
    float           fStallVolumeMult;

    float           fTonePulseMaxSamples;
    float           fTonePulseCounter;

    I2SClass        i2s;
    int             iDataLen;           // Number of data points in the audio tone buffer. Not necessarily the buffer length!

    bool            bAudioTest;

    friend void AudioPlayTask(void * psuParams);

    // Methods
public:
    void Init();
    void SetVolume(int iVolumePercent);
    void SetGain(float fLeftGainIn, float fRightGainIn);
    void SetVoice(EnVoice enVoiceIn);
    void SetTone(EnAudioTone enAudioTone);
    void SetToneFreq(unsigned uToneFreqIn);
    void SetPulseFreq(float fPulseFreq);
    void UpdateTones();
    bool StartAudioTest();
    void StopAudioTest();
    bool IsAudioTestRunning() const;
    void AudioTest();

private:
    void PlayPcmBuffer(const unsigned char * pData, int iNumBytes, float fLeftVolume, float fRightVolume);
    void PlayToneBuffer(const int16_t * pData, int iNumSamples, float fLeftVolume, float fRightVolume);
    void PlayVoice();
    void PlayVoice(EnVoice enVoiceIn);
    void PlayTone();
    void PlayTone(EnAudioTone enAudioTone);
};
