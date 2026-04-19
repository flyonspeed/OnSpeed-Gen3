// VnoChimeDecision.h — Vno (never-exceed speed) chime trigger
//
// Decides whether to trigger the Vno overspeed audio chime based on IAS
// (indicated airspeed) versus the configured Vno threshold, with a
// configurable repeat interval so the chime is not annoyingly continuous.
//
// Simpler than GLimitDetector — no asymmetric modifier, just a threshold
// comparison plus debounce with a configurable repeat interval.
//
// Usage: the sketch reads IAS from the sensor snapshot under xSensorMutex,
// copies it into VnoChimeInputs, and calls VnoChimeDetector::Update().
// VnoChimeDetector is instantiated once per firmware task.

#ifndef ONSPEED_CORE_AUDIO_VNO_CHIME_DECISION_H
#define ONSPEED_CORE_AUDIO_VNO_CHIME_DECISION_H

#include <cstdint>

namespace onspeed {
namespace audio {

// Configuration for Vno chime. Populated from Config by the caller.
struct VnoChimeConfig {
    float    vnoKt             = 0.0f;   // Vno in knots; chime when IAS > this
    uint32_t repeatIntervalMs  = 3000;   // minimum ms between consecutive chimes;
                                         // must be > 0 (caller enforces)
};

// Snapshot of IAS needed for the Vno decision.
struct VnoChimeInputs {
    float    iasKt  = 0.0f;   // indicated airspeed (knots)
    uint32_t tickMs = 0;      // monotonic millisecond clock from caller
};

// VnoChimeDetector — stateful debounce for the Vno overspeed chime.
// Instantiate once; call Reset() on config change.
class VnoChimeDetector {
public:
    // Returns true if the Vno chime should trigger now.
    bool Update(const VnoChimeInputs& in, const VnoChimeConfig& cfg);

    // Reset debounce state (e.g. on config change or firmware boot).
    void Reset();

private:
    uint32_t lastTriggerMs_ = 0;
    bool     haveTriggered_ = false;
};

}  // namespace audio
}  // namespace onspeed

#endif  // ONSPEED_CORE_AUDIO_VNO_CHIME_DECISION_H
