// ToneCalc.h - Pure tone-selection logic extracted from Audio.cpp
//
// Maps current AOA against per-flap thresholds to determine which audio tone
// to play and at what pulse rate.  This is the core safety logic of the OnSpeed
// system — a bug here means a pilot gets the wrong audio cue near stall.

#pragma once

#include "OnSpeedTypes.h"

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
    EnToneType enTone;
    float      fPulseFreq;   ///< 0 = solid tone, >0 = pulses per second
};

// ============================================================================
// CONSTANTS  (match Audio.cpp #defines)
// ============================================================================

constexpr float HIGH_TONE_STALL_PPS = 20.0f;
constexpr float HIGH_TONE_PPS_MIN   =  1.5f;
constexpr float HIGH_TONE_PPS_MAX   =  6.2f;
constexpr float LOW_TONE_PPS_MIN    =  1.5f;
constexpr float LOW_TONE_PPS_MAX    =  8.2f;

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
