// ToneCalc.h - Pure tone-selection logic extracted from Audio.cpp
//
// Maps current AOA against per-flap thresholds to determine which audio tone
// to play and at what pulse rate.  This is the core safety logic of the OnSpeed
// system — a bug here means a pilot gets the wrong audio cue near stall.

#pragma once

#include <util/OnSpeedTypes.h>

namespace onspeed {

// ============================================================================
// TYPES
// ============================================================================

enum class EnToneType { None, Low, High };

struct ToneThresholds {
    float fLDMAXAOA;
    float fONSPEEDFASTAOA;
    float fONSPEEDSLOWAOA;
    float fSTALLWARNAOA;
};

struct ToneResult {
    EnToneType enTone      = EnToneType::None;
    float      fPulseFreq  = 0.0f;   ///< 0 = solid tone, >0 = pulses per second
    float      fVolumeMult = 1.0f;   ///< 0.0-1.0 multiplier applied to master volume.
                                     ///< 0.25 in low-tone/on-speed regions, ramps
                                     ///< 0.25->1.0 in pulsed-high (approaching stall)
                                     ///< region, 1.0 at stall warning.  Defaults to
                                     ///< 1.0 (safe "no attenuation") for None tones.
};

// ============================================================================
// CONSTANTS  (match Audio.cpp #defines)
// ============================================================================

constexpr float HIGH_TONE_STALL_PPS = 20.0f;
constexpr float HIGH_TONE_PPS_MIN   =  1.5f;
constexpr float HIGH_TONE_PPS_MAX   =  6.2f;
constexpr float LOW_TONE_PPS_MIN    =  1.5f;
constexpr float LOW_TONE_PPS_MAX    =  8.2f;

// Stall volume ramp: cruise/on-speed tones attenuated to STALL_VOL_MIN,
// stall-warning tones at STALL_VOL_MAX, linear ramp between OnSpeedSlow
// and StallWarn.  Matches Gen2's HIGH_TONE_VOLUME_MIN / HIGH_TONE_VOLUME_MAX.
constexpr float STALL_VOL_MIN       =  0.25f;
constexpr float STALL_VOL_MAX       =  1.0f;

// ============================================================================
// FUNCTIONS
// ============================================================================

/// Given AOA and per-flap thresholds, determine tone type and pulse rate.
///
/// AOA regions (evaluated top-down, first match wins):
///   >= StallWarn        → High tone, 20 PPS (stall warning)
///   >  OnSpeedSlow      → High tone, 1.5–6.2 PPS (interpolated)
///   >= OnSpeedFast      → Low tone, solid (0 PPS)
///   >= LDmax (if < Fast)→ Low tone, 1.5–8.2 PPS (interpolated)
///   below LDmax         → No tone
ToneResult calculateTone(float fAOA, const ToneThresholds& th);

/// Muted variant: all tones silenced except stall warning.
/// Used when the pilot has pressed the audio-disable button.
/// Stall warning only fires if AOA >= StallWarn AND IAS > mute threshold.
ToneResult calculateToneMuted(float fAOA, float fIAS,
                              float fSTALLWARNAOA, int iMuteUnderIAS);

} // namespace onspeed
