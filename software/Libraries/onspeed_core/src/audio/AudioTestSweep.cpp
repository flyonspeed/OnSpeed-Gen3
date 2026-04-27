// AudioTestSweep.cpp — pure AOA-sweep generator.  See AudioTestSweep.h.

#include <audio/AudioTestSweep.h>

namespace onspeed {
namespace audio {

bool ShouldRunAudioTestSweep(const ToneThresholds& th)
{
    return th.fONSPEEDFASTAOA > 0.0f
        && th.fONSPEEDSLOWAOA > 0.0f
        && th.fSTALLWARNAOA   > 0.0f;
}

std::uint32_t AudioTestSweepStepCount(const AudioTestSweepConfig& cfg)
{
    if (cfg.stepMs == 0) return 0;
    return cfg.durationMs / cfg.stepMs;
}

AudioTestSweepStep GetAudioTestSweepStep(
    const ToneThresholds&       th,
    const AudioTestSweepConfig& cfg,
    std::uint32_t               i)
{
    const std::uint32_t nSteps = AudioTestSweepStepCount(cfg);
    const float fStartAoa = th.fLDMAXAOA    - cfg.bottomMargin;
    const float fEndAoa   = th.fSTALLWARNAOA + cfg.topMargin;

    AudioTestSweepStep out{};
    if (nSteps == 0)
    {
        out.aoaDeg = fStartAoa;
        out.tone   = calculateTone(fStartAoa, th);
        return out;
    }

    const float fAoaStep = (fEndAoa - fStartAoa) / static_cast<float>(nSteps);
    out.aoaDeg = fStartAoa + fAoaStep * static_cast<float>(i);
    out.tone   = calculateTone(out.aoaDeg, th);
    return out;
}

}  // namespace audio
}  // namespace onspeed
