// VnoChimeDecision.cpp — Vno overspeed chime trigger implementation
//
// See VnoChimeDecision.h for design notes.

#include <audio/VnoChimeDecision.h>

namespace onspeed {
namespace audio {

bool VnoChimeDetector::Update(const VnoChimeInputs& in, const VnoChimeConfig& cfg)
{
    // Strictly above Vno — not at or below.
    if (in.iasKt <= cfg.vnoKt)
    {
        return false;
    }

    // Debounce: suppress re-trigger until repeatIntervalMs has elapsed.
    // On the first call (haveTriggered_=false) always trigger.
    if (haveTriggered_)
    {
        uint32_t elapsed = in.tickMs - lastTriggerMs_;  // wraps correctly
        if (elapsed < cfg.repeatIntervalMs)
        {
            return false;
        }
    }

    lastTriggerMs_ = in.tickMs;
    haveTriggered_ = true;
    return true;
}

void VnoChimeDetector::Reset()
{
    lastTriggerMs_ = 0;
    haveTriggered_ = false;
}

}  // namespace audio
}  // namespace onspeed
