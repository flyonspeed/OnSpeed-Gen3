// VolumeCurve.h — Volume pot ADC to audio gain mapping
//
// Maps a raw ADC reading from the volume potentiometer into a [0, 1] gain
// value using linear interpolation between the configured low and high
// analog calibration points.
//
// This is a pure function — no state, no side effects. Clamps the output to
// [0, 1] so caller code that passes out-of-range ADC values is safe.
//
// The firmware passes this gain to Audio::SetVolume as a 0-100 integer percent;
// that conversion is done by the sketch side (Housekeeping.cpp), not here.
// Keeping the function as a [0,1] float makes it easier to test and reuse.

#ifndef ONSPEED_CORE_AUDIO_VOLUME_CURVE_H
#define ONSPEED_CORE_AUDIO_VOLUME_CURVE_H

#include <cstdint>

namespace onspeed {
namespace audio {

// Configuration for the volume pot mapping. Populated from Config by caller.
struct VolumeCurveConfig {
    int lowAnalog  = 0;      // ADC reading that maps to 0% volume
    int highAnalog = 4095;   // ADC reading that maps to 100% volume
};

// Map a raw ADC potentiometer reading to a [0, 1] audio gain.
//
// Linear interpolation: gain = (rawAdc - lowAnalog) / (highAnalog - lowAnalog)
// Clamped to [0, 1] so out-of-range ADC values don't produce invalid gains.
//
// If lowAnalog == highAnalog the range collapses; returns 0 to avoid a
// divide-by-zero. The caller should validate config before storing it.
float MapPotToGain(int rawAdc, const VolumeCurveConfig& cfg);

}  // namespace audio
}  // namespace onspeed

#endif  // ONSPEED_CORE_AUDIO_VOLUME_CURVE_H
