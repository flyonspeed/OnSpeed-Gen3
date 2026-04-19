// VolumeCurve.cpp — Volume pot ADC to audio gain mapping implementation
//
// See VolumeCurve.h for interface notes.

#include <audio/VolumeCurve.h>

namespace onspeed {
namespace audio {

float MapPotToGain(int rawAdc, const VolumeCurveConfig& cfg)
{
    int range = cfg.highAnalog - cfg.lowAnalog;

    // Degenerate config: zero range would cause divide-by-zero.
    // Return silent (0) so audio never mis-behaves.
    if (range == 0)
    {
        return 0.0f;
    }

    float gain = static_cast<float>(rawAdc - cfg.lowAnalog)
               / static_cast<float>(range);

    // Clamp to [0, 1]. Out-of-range ADC values are possible when the pot is
    // at a mechanical extreme or when the ADC reference is slightly off.
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;

    return gain;
}

}  // namespace audio
}  // namespace onspeed
