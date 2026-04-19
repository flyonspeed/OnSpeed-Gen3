// GLimitDecision.cpp — G-load chime trigger implementation
//
// See GLimitDecision.h for the design rationale and audit #010 fix notes.

#include <audio/GLimitDecision.h>

#include <cmath>   // fabsf

namespace onspeed {
namespace audio {

bool GLimitDetector::Update(const GLimitInputs& in, const GLimitConfig& cfg)
{
    // Apply asymmetric-flight limit reduction when roll or yaw rate exceeds the
    // configured threshold. This reflects the real-world limitation that
    // structural margins are lower in non-coordinated or rolled flight.
    float posLimit = cfg.positiveLimitG;
    float negLimit = cfg.negativeLimitG;

    if (fabsf(in.rollRateDps) >= cfg.asymmetricGyroDps ||
        fabsf(in.yawRateDps)  >= cfg.asymmetricGyroDps)
    {
        posLimit *= cfg.asymmetricReduction;
        negLimit *= cfg.asymmetricReduction;
    }

    // Inclusive inequality: match original firmware — reaching exactly the
    // configured limit triggers the chime.
    bool exceeded = (in.verticalG >= posLimit) || (in.verticalG <= negLimit);
    if (!exceeded)
    {
        return false;
    }

    // Debounce: suppress re-trigger until repeatTimeoutMs has elapsed since
    // the last chime. On the very first call (haveTriggered_=false) always
    // trigger so the pilot hears the first over-G event even at tickMs=0.
    if (haveTriggered_)
    {
        uint32_t elapsed = in.tickMs - lastTriggerMs_;  // wraps correctly
        if (elapsed < cfg.repeatTimeoutMs)
        {
            return false;
        }
    }

    // Record trigger time and signal the caller to play the chime.
    lastTriggerMs_ = in.tickMs;
    haveTriggered_ = true;
    return true;
}

void GLimitDetector::Reset()
{
    lastTriggerMs_ = 0;
    haveTriggered_ = false;
}

}  // namespace audio
}  // namespace onspeed
