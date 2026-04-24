// ToneCalc.cpp - Pure tone-selection logic extracted from Audio.cpp

#include "ToneCalc.h"

namespace onspeed {

ToneResult calculateTone(float fAOA, const ToneThresholds& th)
{
    // Uncalibrated gate: if the stall-warning threshold is at or
    // below zero, treat the config as unconfigured and stay silent.
    // Without this, a factory/reset config with all-zero setpoints
    // would trigger `AOA >= 0` → a constant stall warning as soon as
    // the aircraft crossed the mute-under-IAS threshold. Silence is
    // the safer default when the stall AOA isn't known.
    if (th.fSTALLWARNAOA <= 0.0f)
    {
        return { EnToneType::None, 0.0f };
    }

    if (fAOA >= th.fSTALLWARNAOA)
    {
        return { EnToneType::High, HIGH_TONE_STALL_PPS };
    }

    if (fAOA > th.fONSPEEDSLOWAOA)
    {
        float fPPS = mapfloat(fAOA,
                              th.fONSPEEDSLOWAOA, th.fSTALLWARNAOA,
                              HIGH_TONE_PPS_MIN, HIGH_TONE_PPS_MAX);
        return { EnToneType::High, fPPS };
    }

    if (fAOA >= th.fONSPEEDFASTAOA)
    {
        return { EnToneType::Low, 0.0f };
    }

    if (fAOA >= th.fLDMAXAOA && th.fLDMAXAOA < th.fONSPEEDFASTAOA)
    {
        float fPPS = mapfloat(fAOA,
                              th.fLDMAXAOA, th.fONSPEEDFASTAOA,
                              LOW_TONE_PPS_MIN, LOW_TONE_PPS_MAX);
        return { EnToneType::Low, fPPS };
    }

    return { EnToneType::None, 0.0f };
}

ToneResult calculateToneMuted(float fAOA, float fIAS,
                              float fSTALLWARNAOA, int iMuteUnderIAS)
{
    // Same uncalibrated gate as calculateTone: muted mode exists so
    // the stall warning can cut through user-muted audio, but with
    // no stall threshold configured there is nothing meaningful to
    // cut through.
    if (fSTALLWARNAOA <= 0.0f)
        return { EnToneType::None, 0.0f };

    if (fAOA >= fSTALLWARNAOA && fIAS > iMuteUnderIAS)
        return { EnToneType::High, HIGH_TONE_STALL_PPS };

    return { EnToneType::None, 0.0f };
}

} // namespace onspeed
