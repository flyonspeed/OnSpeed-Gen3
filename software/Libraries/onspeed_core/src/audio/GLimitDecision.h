// GLimitDecision.h — G-load chime trigger
//
// Decides whether to trigger the "overG" audio chime based on vertical
// G-load, configured limits, asymmetric-flight modifier, and a debouncing
// repeat-timeout.
//
// Called at the HousekeepingTask cadence (100 ms / 10 Hz). The returned
// decision is acted on by the caller (sketch) by invoking the audio
// subsystem. Separating the decision from the I/O lets the logic be unit
// tested on the native host without any Arduino or FreeRTOS dependency.
//
// Audit #010 fix: the sketch must snapshot all AHRS state into GLimitInputs
// while holding xAhrsMutex, then call Update() outside the mutex. This
// avoids the data race that existed when Housekeeping read directly from
// g_AHRS fields without protection.

#ifndef ONSPEED_CORE_AUDIO_GLIMIT_DECISION_H
#define ONSPEED_CORE_AUDIO_GLIMIT_DECISION_H

#include <cstdint>

namespace onspeed {
namespace audio {

// Configuration for the G-limit detector. All values come from Config and
// should be copied by the caller before calling Update().
struct GLimitConfig {
    float    positiveLimitG      = 0.0f;   // e.g. +4.0 for a typical LSA
    float    negativeLimitG      = 0.0f;   // e.g. -2.0 (must be <= 0)
    float    asymmetricGyroDps   = 0.0f;   // roll/yaw threshold that triggers
                                           // limit reduction (deg/sec)
    float    asymmetricReduction = 1.0f;   // multiplier applied to both limits
                                           // when asymmetric flight detected
                                           // (0 < value <= 1.0); 1.0 = no reduction
    uint32_t repeatTimeoutMs     = 3000;   // minimum ms between consecutive chimes
};

// Snapshot of aircraft state needed for the G-limit decision.
// All fields are captured by the caller from AHRS data under the
// appropriate mutex; GLimitDetector never touches shared globals.
struct GLimitInputs {
    float    verticalG    = 0.0f;    // earth-frame vertical G (positive = up)
    float    rollRateDps  = 0.0f;    // filtered roll rate  (deg/sec)
    float    yawRateDps   = 0.0f;    // filtered yaw rate   (deg/sec)
    uint32_t tickMs       = 0;       // monotonic millisecond clock from caller
};

// GLimitDetector — stateful debounce wrapper around the G-limit threshold
// comparison. Instantiate once per firmware task; call Reset() on config change.
class GLimitDetector {
public:
    // Update with the current frame.
    // Returns true if the chime should trigger now.
    // The caller is responsible for invoking audio playback on true.
    bool Update(const GLimitInputs& in, const GLimitConfig& cfg);

    // Reset internal debounce state.
    // Call after a config change or at firmware boot to ensure the first
    // over-G event always triggers regardless of lastTriggerMs_.
    void Reset();

private:
    uint32_t lastTriggerMs_ = 0;
    bool     haveTriggered_ = false;   // false until first trigger; lets the
                                       // first over-G event fire even when
                                       // tickMs happens to be 0 at boot.
};

}  // namespace audio
}  // namespace onspeed

#endif  // ONSPEED_CORE_AUDIO_GLIMIT_DECISION_H
