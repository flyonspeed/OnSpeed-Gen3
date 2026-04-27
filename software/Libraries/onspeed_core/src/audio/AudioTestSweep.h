// AudioTestSweep.h — pure AOA-sweep generator for the box's bench
// AudioTest mode.
//
// The sketch's AudioTest() function plays voices, reference tones, then
// walks AOA linearly from just below LDmax to a comfortable distance
// past stall-warn so the technician hears every region of the active
// flap's tone map: silent → pulsed-low ramp → solid-low (on-speed
// band) → pulsed-high ramp → solid-high (saturated stall warning).
//
// This module is the pure-math half of that sweep:
//   - StepCount() — how many steps for a given duration/step config
//   - ShouldRun()  — gate matching calculateTone's uncalibrated check
//   - GetStep()    — for step index i, return the AOA and ToneResult
//
// The sketch owns the FreeRTOS pump (vTaskDelay, stop-button polling,
// writing globals).  Keeping the math here lets tests pin endpoint
// formulas, region coverage, and the gate without dragging in
// sketch-side state.

#ifndef ONSPEED_CORE_AUDIO_AUDIO_TEST_SWEEP_H
#define ONSPEED_CORE_AUDIO_AUDIO_TEST_SWEEP_H

#include <cstdint>

#include "ToneCalc.h"

namespace onspeed {
namespace audio {

struct AudioTestSweepConfig {
    // Sweep starts at (fLDMAXAOA - bottomMargin) so the first ~half-second
    // is silent and the listener's ear lands on the first low pulse cleanly.
    float bottomMargin = 0.2f;

    // Sweep ends at (fSTALLWARNAOA + topMargin) so the saturated stall
    // warning is held audibly for ~3 s of the sweep — long enough to
    // recognise as "this is what stall sounds like."
    float topMargin = 1.5f;

    // 20 s total, 50 ms per step → 400 steps.  Step rate is well below
    // the slowest pulse-rate the sweep produces (LOW_TONE_PPS_MIN = 1.5 PPS
    // = 666 ms half-period), so PPS updates are perceptually smooth.
    std::uint32_t durationMs = 20000;
    std::uint32_t stepMs     = 50;
};

struct AudioTestSweepStep {
    float      aoaDeg;
    ToneResult tone;
};

// True when the active flap's setpoints are calibrated enough for the
// sweep to produce meaningful audio.  Mirrors calculateTone's own
// three-threshold uncalibrated gate exactly: a partially-calibrated
// config with only StallWarn set would otherwise run a 20 s sweep
// that emits only the saturated stall warning at the very top.
bool ShouldRunAudioTestSweep(const ToneThresholds& th);

// Number of steps the sweep emits for the given config.  Equal to
// (durationMs / stepMs).  The caller iterates 0..StepCount-1.
std::uint32_t AudioTestSweepStepCount(const AudioTestSweepConfig& cfg);

// Compute the i-th step.  Linear AOA ramp from
// (fLDMAXAOA - bottomMargin) to (fSTALLWARNAOA + topMargin), then
// calculateTone() applied to that AOA.  Step 0 is the bottom; step
// (StepCount - 1) is just shy of the top.
AudioTestSweepStep GetAudioTestSweepStep(
    const ToneThresholds&         th,
    const AudioTestSweepConfig&   cfg,
    std::uint32_t                 i);

}  // namespace audio
}  // namespace onspeed

#endif  // ONSPEED_CORE_AUDIO_AUDIO_TEST_SWEEP_H
