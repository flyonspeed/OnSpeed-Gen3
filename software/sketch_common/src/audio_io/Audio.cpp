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
#include <audio/Envelope.h>
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

// Tone source phase index — advanced modulo TONE_BUFFER_LEN as we pump
// frames out.  Keeping this monotonic across pump calls preserves the
// continuous-sine carrier behaviour of Gen2 (sinewave1 was free-running;
// only its amplitude was modulated by the envelope).
static size_t s_ToneSrcIdx = 0;

// Mixer state retained for the legacy pulse-gate path.  Tones now use the
// DAHDR envelope below as their primary gate; pulseSpec is left zero.
static onspeed::audio::MixerState s_ToneMixerState;

// DAHDR envelope owned by AudioPlay's tone path.  All amplitude shaping
// (per-pulse onset, release on stop, smooth re-trigger on tone/PPS change)
// flows through this single state machine — the Gen2 perceptual model.
static onspeed::audio::Envelope   s_ToneEnvelope;

// Carrier identity of the currently-armed (or releasing) tone.  Sketch-
// side state because the envelope core knows nothing about Low vs High
// 16 kHz cosine tables.  Two consumers:
//   1. PlayTone() during Release: keep feeding the released tone's
//      source PCM so the envelope tail decays on the correct carrier.
//   2. SetTone() solid→pulsed transition: lets us notice when the
//      previous *carrier* was the high tone (Gen2's high→solid
//      shortened-delay trick, Tones.ino:63).
// Spec-shape debouncing and solid/pulsed detection live in the
// Envelope class itself — this is the only sketch-side state needed.
static EnAudioTone s_LastEnvTone = enToneNone;

// I2S pins are defined in HardwareMap.h as kI2sBck, kI2sDout, kI2sLrck.

#define FREERTOS

// Tone frequency and ramp constants (PPS constants moved to onspeed_core/ToneCalc.h).
// Ramp times ported verbatim from Gen2 Tones.ino.
#define HIGH_TONE_HZ         1600                 // freq of high tone
#define LOW_TONE_HZ           400                 // freq of low tone
#define TONE_RAMP_TIME         15                 // millisec, normal ramp
#define STALL_RAMP_TIME         5                 // millisec, stall (20 PPS)

// Verbatim port of Gen2's solid↔high transition delay constant:
//   1000 / LOW_TONE_PPS_MAX / 2 = 1000 / 8.2 / 2 ≈ 60.97 ms
// Used as the first-pulse silent delay when transitioning solid → pulsed
// or as the silent delay before a solid tone begins (high → solid).
// See Tones.ino:11 + Tones.ino:63.
static constexpr float SOLID_TRANSITION_DELAY_MS =
    1000.0f / onspeed::LOW_TONE_PPS_MAX / 2.0f;

// ----------------------------------------------------------------------------

static bool         s_bI2sOk = false;
static bool         s_bAudioUnmuted = false;    // Hysteretic mute state; see UpdateTones().
static volatile TaskHandle_t s_xAudioTestTask = nullptr;
static std::atomic<bool>     s_bAudioTestStopRequested{false};
static std::atomic<bool>     s_bAudioTestStarting{false};

// ----------------------------------------------------------------------------
// Envelope spec builders — port Gen2's per-pulse DAHD shape and per-mode
// transitions.  See Envelope.h for the lineage.

static inline float MsToSamples(float ms)
{
    return ms * (SAMPLE_RATE / 1000.0f);
}

// Build an envelope spec for a pulsed tone at `pps`.
//   isStall    — true at stall warning PPS; uses the snappier 5 ms ramp.
//   fromSolid  — true if the previous note was solid; uses Gen2's
//                shortened ~61 ms first-pulse delay so the new pulsed
//                tone arrives within one perceptual half-period.
//
// Gen2's per-pulse shape (Tones.ino:104-120) plus the IntervalTimer
// cadence (Tones.ino:14):
//   pulse_delay = 1000 / pps                  (full cycle period)
//   tone_length = pulse_delay - 3 ms          (envelope-active window)
//   delay       = tone_length / 2             (silent first half)
//   attack      = ramp_time
//   hold        = tone_length/2 - 2*ramp_time
//   decay       = ramp_time
//   gap         = pulse_delay - tone_length    (= 3 ms inter-pulse silence,
//                                                Gen2's IntervalTimer wait)
//   release     = ramp_time                    (only used on NoteOff)
//
// Without `gap`, the auto-loop after Decay would give a cycle period of
// `tone_length` (Gen2 had IntervalTimer firing on `pulse_delay`), so
// pulses would be ~3 ms early per cycle — measurably wrong at stall PPS
// (~21 PPS observed vs configured 20) and audibly different from Gen2.
static onspeed::audio::EnvelopeSpec MakePulseSpec(float pps,
                                                  bool isStall,
                                                  bool fromSolid)
{
    const float pulseDelayMs = 1000.0f / pps;          // full period
    const float toneLengthMs = pulseDelayMs - 3.0f;    // envelope-active window
    const float gapMs        = pulseDelayMs - toneLengthMs;   // = 3.0f
    const float rampMs       = isStall ? STALL_RAMP_TIME : TONE_RAMP_TIME;
    const float delayMs      = fromSolid
                                 ? SOLID_TRANSITION_DELAY_MS
                                 : (toneLengthMs * 0.5f);
    float       holdMs       = (toneLengthMs * 0.5f) - 2.0f * rampMs;
    if (holdMs < 0.0f) holdMs = 0.0f;

    onspeed::audio::EnvelopeSpec s;
    s.delaySamples   = MsToSamples(delayMs);
    s.attackSamples  = MsToSamples(rampMs);
    s.holdSamples    = MsToSamples(holdMs);
    s.decaySamples   = MsToSamples(rampMs);
    s.gapSamples     = MsToSamples(gapMs);
    s.releaseSamples = MsToSamples(rampMs);
    s.isSolid        = false;
    return s;
}

// Build an envelope spec for a solid tone.  Gen2 (Tones.ino:51-74)
// always plays a 60.97 ms silent delay before the solid tone's attack,
// regardless of what was playing before.  The comment in Gen2:
//
//   "this timing provides a smooth transition from low tones into solid
//    and a quick transition from high tones back into to solid"
//
// — the "quick from high" part comes from the Teensy envelope's
// `releaseNoteOn(0)` mode, which lets the new note's silent delay run
// concurrently with the previous tone's release tail.  The delay
// itself is constant.  Unconditional delay also keeps the spec stable
// across UpdateTones cycles, so the `SameSpec` debounce in
// `Envelope::NoteOn` correctly leaves the running Sustain alone.
static onspeed::audio::EnvelopeSpec MakeSolidSpec()
{
    onspeed::audio::EnvelopeSpec s;
    s.delaySamples   = MsToSamples(SOLID_TRANSITION_DELAY_MS);
    s.attackSamples  = MsToSamples(TONE_RAMP_TIME);
    s.holdSamples    = 0.0f;
    s.decaySamples   = 0.0f;
    s.gapSamples     = 0.0f;     // solid tones latch in Sustain, never auto-loop
    s.releaseSamples = MsToSamples(TONE_RAMP_TIME);
    s.isSolid        = true;
    return s;
}

// ----------------------------------------------------------------------------

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
Each PlayTone() call writes one 15 ms (240-frame) chunk to I2S, so this
task wakes ~67 times per second when a tone is active.  The short pump
bounds the SetTone()→audible-attack latency at ~15 ms, which keeps Gen2's
61 ms solid→pulsed transition timing inside one half-period at 6.2 PPS
(80 ms half-period).  Make sure no higher-priority task takes longer
than the pump period or audio will gap.
*/

void AudioPlayTask(void * psuParams)
{
    (void)psuParams;
    while (true)
    {
        if (!s_bI2sOk)
            {
            // If I2S init failed, don't spin at high priority.
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
            }

        // Sleep when nothing is playing AND the envelope has fully drained.
        // The release ramp on SetTone(None) needs a few more pump calls to
        // reach zero — keep pumping until s_ToneEnvelope is idle so the
        // tone fades out cleanly instead of cutting off mid-cycle.
        if (g_AudioPlay.enTone == enToneNone &&
            g_AudioPlay.enVoice == enVoiceNone &&
            s_ToneEnvelope.IsIdle())
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        // Voice clip plays once and resets.  Blocks until done.
        if (g_AudioPlay.enVoice != enVoiceNone)
            g_AudioPlay.PlayVoice();

        // Tone path: pump while a tone is selected OR the envelope is
        // still releasing.  The envelope's idle state is the canonical
        // "tone fully stopped" signal.
        if (g_AudioPlay.enTone != enToneNone || !s_ToneEnvelope.IsIdle())
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
    // Default to STALL_VOL_MAX so a tone-via-AudioTest before UpdateTones()
    // fires plays at full amplitude, not the cruise floor.
    fStallVolumeMult     = onspeed::STALL_VOL_MAX;

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

// Select a precomputed tone to play. Tone will continue to play until
// turned off.  This is the single funnel for tone-state changes — it
// builds a fresh envelope spec from the current (enTone, PPS) pair and
// hands it to the envelope's NoteOn().  If the envelope is mid-flight
// it will release cleanly first, then arm the new spec — Gen2's
// `noteOff(); noteOn();` pattern.

void AudioPlay::SetTone(EnAudioTone enAudioTone)
{
    if (enAudioTone == enToneNone)
    {
        // Stop request: release the envelope.  AudioPlayTask keeps
        // pumping (using s_LastEnvTone to source the right carrier)
        // until the release reaches zero, so the tail fades cleanly.
        s_ToneEnvelope.NoteOff();
        this->enTone = enAudioTone;
        return;
    }

    // Build the envelope spec for the requested tone.  Solid tones
    // always carry the 60.97 ms entry delay (matches Gen2's
    // unconditional `envelope1.delay(...)` in the SOLID_TONE branch).
    // Pulsed tones consult the envelope to detect a solid-to-pulsed
    // transition so the first new pulse uses Gen2's shortened
    // first-pulse delay.
    onspeed::audio::EnvelopeSpec spec;
    if (fTonePulseMaxSamples == 0.0f)
    {
        // Solid tone (currently used only for low cruise).
        spec = MakeSolidSpec();
    }
    else
    {
        const float fPps     = SAMPLE_RATE / (fTonePulseMaxSamples * 2.0f);
        const bool isStall   = (fPps >= onspeed::HIGH_TONE_STALL_PPS - 0.5f);
        const bool fromSolid = s_ToneEnvelope.IsCurrentSolid();
        spec = MakePulseSpec(fPps, isStall, fromSolid);
    }

    // The Envelope owns all state-transition policy: this is a no-op
    // if active and the spec matches, queues if mid-pulse, releases
    // if Sustain.  Safe to call from UpdateTones() at 208 Hz.
    s_ToneEnvelope.NoteOn(spec);

    s_LastEnvTone = enAudioTone;
    this->enTone  = enAudioTone;

    NotifyAudioTask();
}

// ----------------------------------------------------------------------------

// Maybe someday.

void AudioPlay::SetToneFreq(unsigned uToneFreqIn)
{
    (void)uToneFreqIn;
}

// ----------------------------------------------------------------------------

// Set the per-tone pulse rate in Hz.  Range 1.0–25.0 PPS produces a
// pulsed tone; anything outside disables pulsing (solid).  Stores the
// half-period; the envelope shape is rebuilt by the next SetTone()
// call.  Callers that change PPS independently of tone-type should
// call SetTone(currentTone) afterwards if they want the envelope to
// pick up the new rate (UpdateTones does this naturally; AudioTest
// also chains through SetTone for transitions).

void AudioPlay::SetPulseFreq(float fPulseFreq)
{
    if ((fPulseFreq < 1.0f) || (fPulseFreq > 25.0f))
        fTonePulseMaxSamples = 0;
    else
        fTonePulseMaxSamples = SAMPLE_RATE / (fPulseFreq * 2.0f);

    // If a tone is active, refresh the envelope so the new rate takes
    // effect on the next pulse boundary.  NoteOn debounces internally
    // if the rebuilt spec matches what's already running.
    if (this->enTone != enToneNone)
        SetTone(this->enTone);
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

// Play one pump-sized chunk of the source PCM tone buffer through the
// envelope-driven mixer to I2S.  Source PCM is read with a wrap-around
// index so the cosine carrier stays phase-continuous across pump calls
// (Gen2 model: continuous sine, envelope-shaped amplitude).
//
// `pData` is the precomputed cosine table; `iNumSamples` is its length
// (TONE_BUFFER_LEN).  Per-channel gain (`fLeftVolume`, `fRightVolume`)
// is the master*stallVol*pan composition computed by PlayTone().
void AudioPlay::PlayToneBuffer(const int16_t * pData, int iNumSamples, float fLeftVolume, float fRightVolume)
{
    if (!s_bI2sOk)
        return;

    constexpr size_t kFramesPerWrite = TONE_PUMP_FRAMES;  // 240 samples = 15 ms
    int16_t          aSource[kFramesPerWrite];
    int16_t          aStereo[kFramesPerWrite * 2];
    uint32_t         aFrames[kFramesPerWrite];

    const size_t srcLen = static_cast<size_t>(iNumSamples);

    // Copy one pump chunk from the source table with phase-continuous
    // wrap-around.  Cheaper than mod-indexing per sample inside the mixer
    // and lets the mixer remain agnostic to source layout.
    for (size_t i = 0; i < kFramesPerWrite; ++i)
    {
        aSource[i] = pData[(s_ToneSrcIdx + i) % srcLen];
    }
    s_ToneSrcIdx = (s_ToneSrcIdx + kFramesPerWrite) % srcLen;

    onspeed::audio::MixerInputs inp;
    inp.in         = aSource;
    inp.leftScale  = fLeftVolume;
    inp.rightScale = fRightVolume;
    inp.envelope   = &s_ToneEnvelope;
    // Envelope path supersedes the legacy pulse-gate; halfPeriodSamples
    // stays 0 so the mixer ignores it.

    onspeed::audio::Mix(inp, aStereo, kFramesPerWrite, s_ToneMixerState);

    for (size_t j = 0; j < kFramesPerWrite; ++j)
    {
        aFrames[j] = onspeed::audio::PackStereoI16(aStereo[2 * j + 0],
                                                    aStereo[2 * j + 1]);
    }
    i2s.write(reinterpret_cast<const uint8_t *>(aFrames), sizeof(aFrames));
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

// Play one pump-sized chunk of the currently-selected tone.
//
// During envelope release (after SetTone(None)), enTone has been cleared
// but s_ToneEnvelope is still ramping down — we use s_LastEnvTone to
// keep feeding the same source table so the release ramp lands on a
// continuous carrier instead of a sudden source switch.
//
// Per-channel scaling composes Gen2's four amplitude layers in one
// expression, in the order (from inside out):
//
//   sample × fStallVolumeMult × fVolume × fLeftGain  (× envelope.gate)
//   ──────  ──────────────────  ───────  ─────────     ──────────────
//   carrier  per-PPS volume     master    3D pan       per-sample shape
//
// Each non-envelope layer can step between buffers; the envelope's
// silent phases (delay/release) mask those steps perceptually.

void AudioPlay::PlayTone(EnAudioTone enAudioTone)
{
    AudioLogDebugNoBlock("PlayTone %d\n", enAudioTone);

    // During release the caller passes enToneNone but we want to keep
    // pumping the last tone's source so the envelope decays cleanly.
    EnAudioTone effectiveTone = (enAudioTone != enToneNone)
                                  ? enAudioTone
                                  : s_LastEnvTone;
    if (effectiveTone == enToneNone)
        return;   // nothing to play, nothing to release

    // Compose the four amplitude layers, capping to 1.0 to prevent
    // waveform clipping when 3D pan ≥ 1.0 combines with master volume.
    float fL = fVolume * fStallVolumeMult * fLeftGain;
    float fR = fVolume * fStallVolumeMult * fRightGain;
    if (fL > 1.0f) fL = 1.0f;
    if (fR > 1.0f) fR = 1.0f;

    switch (effectiveTone)
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

void AudioPlay::UpdateTones(const ActiveFlapSnapshot& snap)
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

    // The setpoints come in via `snap`, built by SensorIO::Read under the
    // same xAhrsMutex that HandleConfigSave's flap-vector swap takes; this
    // function therefore never touches g_Config.aFlaps directly.  When
    // `snap.bValid` is false (mutex timeout or out-of-bounds index in the
    // producer) the thresholds are zero and the uncalibrated gate in
    // calculateTone keeps the output silent -- strictly safer than acting
    // on torn or freed setpoint memory.
    const onspeed::ToneThresholds& th = snap.th;
    const int iMuteThreshold = g_Config.iMuteAudioUnderIAS;

    if (!g_bAudioEnable)
        {
        // Audio disabled by button — only allow stall warning through
        result = onspeed::calculateToneMuted(
            g_Sensors.AOA, g_Sensors.IAS,
            th.fSTALLWARNAOA,
            iMuteThreshold);
        }
    else if (!s_bAudioUnmuted)
        {
        // Airspeed too low (taxiing) — mute, but set pulse rate high for quick pickup
#ifdef TONEDEBUG
        AudioLogDebugNoBlock("AUDIO MUTED: Airspeed too low. Min:%i IAS:%.2f\n",
            iMuteThreshold, g_Sensors.IAS);
#endif
        result = { onspeed::EnToneType::None, 20.0f, onspeed::STALL_VOL_MIN };
        }
    else
        {
        result = onspeed::calculateTone(g_Sensors.AOA, th);
        }

    // Capture the per-PPS amplitude multiplier so PlayTone() picks it up
    // on the next pump.  Single float written by this task and read by
    // the audio task — no mutex needed (matches the existing pattern for
    // fVolume / fLeftGain / fRightGain).
    fStallVolumeMult = result.fVolumeMult;

    // SetPulseFreq stores the half-period; SetTone builds the envelope
    // spec from it.  Both calls funnel into Envelope::NoteOn(), which
    // debounces identical re-triggers internally — safe to invoke at
    // 208 Hz without disturbing the running pulse cycle.
    SetPulseFreq(result.fPulseFreq);
    SetTone(static_cast<EnAudioTone>(result.enTone));
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

    // Save the per-PPS volume multiplier and restore it on exit so that
    // when bAudioTest clears and UpdateTones() resumes writing the live
    // value, there's no glitchy one-tick window at whatever the test
    // sequence last set.
    const float fSavedStallVolumeMult = fStallVolumeMult;

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

    // Voice waits are tight to each clip's WAV duration plus a small
    // tail (the WAVs end on a sharp cutoff; ~200 ms of silence between
    // segments lets the ear resolve the boundary without dragging on).
    g_AudioPlay.SetVoice(enVoiceLeft);
    if (!DelayOrStop(1600)) goto done;        // left_speaker: 1.31 s

    g_AudioPlay.SetVoice(enVoiceRight);
    if (!DelayOrStop(1500)) goto done;        // right_speaker: 1.20 s

    // Solid low (cruise / on-speed) → attenuated to STALL_VOL_MIN
    g_AudioPlay.fStallVolumeMult = onspeed::STALL_VOL_MIN;
    g_AudioPlay.SetTone(enToneLow);
    if (!DelayOrStop(2000)) goto done;

    g_AudioPlay.SetVoice(enVoiceGLimit);
    if (!DelayOrStop(800)) goto done;          // glimit: 0.49 s

    // Range sweep (replaces the prior fixed-PPS demonstration segments
    // and the standalone solid-high reference; the sweep ends in
    // saturated solid-high stall, so the standalone segment was
    // redundant).  Walks AOA linearly from just below LDmax to a
    // comfortable distance past stall-warn, hitting every region of
    // the tone map: silent → pulsed-low ramp → solid-low (on-speed
    // band) → pulsed-high ramp → solid-high (saturated stall warning).
    // Bypasses UpdateTones' bAudioTest early-return by calling the
    // pure ToneCalc directly so we don't have to fake IAS or unmute
    // state.
    {
    const ActiveFlapSnapshot snap = SnapshotActiveFlap();
    // Skip the sweep on uncalibrated configs.  calculateTone's own
    // gate already returns silent when fONSPEEDFAST/SLOW/STALLWARN <= 0,
    // but starting a 20 s sweep that produces no tones is a worse user
    // experience than skipping outright.
    if (snap.bValid && snap.th.fSTALLWARNAOA > 0.0f)
        {
        constexpr float kBottomMargin = 0.2f;   // start just below LDmax
        constexpr float kTopMargin    = 1.5f;   // end firmly into solid-stall
        constexpr uint32_t kSweepMs   = 20000;
        constexpr uint32_t kStepMs    = 50;     // 400 steps total

        const float fStartAoa = snap.th.fLDMAXAOA    - kBottomMargin;
        const float fEndAoa   = snap.th.fSTALLWARNAOA + kTopMargin;
        const uint32_t kSteps = kSweepMs / kStepMs;
        const float fAoaStep  = (fEndAoa - fStartAoa) / static_cast<float>(kSteps);

        for (uint32_t i = 0; i < kSteps; ++i)
            {
            const float fAoa = fStartAoa + fAoaStep * static_cast<float>(i);
            const onspeed::ToneResult result = onspeed::calculateTone(fAoa, snap.th);

            g_AudioPlay.fStallVolumeMult = result.fVolumeMult;
            g_AudioPlay.SetPulseFreq(result.fPulseFreq);
            g_AudioPlay.SetTone(static_cast<EnAudioTone>(result.enTone));

            if (!DelayOrStop(kStepMs)) goto done;
            }
        }
    }

done:
    g_AudioPlay.SetPulseFreq(0);
    g_AudioPlay.SetTone(enToneNone);

    fStallVolumeMult = fSavedStallVolumeMult;
    bAudioTest = false;
    s_bAudioTestStopRequested.store(false);
}
