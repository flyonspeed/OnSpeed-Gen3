// ToneCalc.cpp - Pure tone-selection logic extracted from Audio.cpp

#include "ToneCalc.h"

namespace onspeed {

ToneResult calculateTone(float fAOA, const ToneThresholds& th)
{
    if (fAOA >= th.fSTALLWARNAOA)
    {
        return { EnToneType::High, HIGH_TONE_STALL_PPS, STALL_VOL_MAX };
    }

    if (fAOA > th.fONSPEEDSLOWAOA)
    {
        float fPPS = mapfloat(fAOA,
                              th.fONSPEEDSLOWAOA, th.fSTALLWARNAOA,
                              HIGH_TONE_PPS_MIN, HIGH_TONE_PPS_MAX);
        // Volume ramps linearly from STALL_VOL_MIN at OnSpeedSlow to
        // STALL_VOL_MAX at StallWarn (faithful Gen2 port from Tones.ino).
        float fVolMult = mapfloat(fAOA,
                                  th.fONSPEEDSLOWAOA, th.fSTALLWARNAOA,
                                  STALL_VOL_MIN, STALL_VOL_MAX);
        return { EnToneType::High, fPPS, fVolMult };
    }

    if (fAOA >= th.fONSPEEDFASTAOA)
    {
        return { EnToneType::Low, 0.0f, STALL_VOL_MIN };
    }

    if (fAOA >= th.fLDMAXAOA && th.fLDMAXAOA < th.fONSPEEDFASTAOA)
    {
        float fPPS = mapfloat(fAOA,
                              th.fLDMAXAOA, th.fONSPEEDFASTAOA,
                              LOW_TONE_PPS_MIN, LOW_TONE_PPS_MAX);
        return { EnToneType::Low, fPPS, STALL_VOL_MIN };
    }

    // Below LDmax — no tone plays, volume mult defaults to 1.0 (no attenuation
    // applied if this value is ever read while no tone is active).
    return { EnToneType::None, 0.0f, STALL_VOL_MAX };
}

ToneResult calculateToneMuted(float fAOA, float fIAS,
                              float fSTALLWARNAOA, int iMuteUnderIAS)
{
    if (fAOA >= fSTALLWARNAOA && fIAS > iMuteUnderIAS)
        return { EnToneType::High, HIGH_TONE_STALL_PPS, STALL_VOL_MAX };

    return { EnToneType::None, 0.0f, STALL_VOL_MAX };
}

} // namespace onspeed
