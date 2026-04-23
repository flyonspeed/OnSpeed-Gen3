// ToneCalc.cpp - Pure tone-selection logic extracted from Audio.cpp

#include "ToneCalc.h"

namespace onspeed {

ToneResult calculateTone(float fAOA, const ToneThresholds& th)
{
    // Uncalibrated / partially-calibrated gate: OnSpeedFast,
    // OnSpeedSlow, and StallWarn must all be positive before this
    // function produces any tone.  These three thresholds sit
    // between zero-lift and stall in body-angle terms and are
    // always positive on a real calibration; any zero is the
    // factory/unconfigured signal.
    //
    // LDmax is intentionally excluded.  The stored thresholds are
    // body angles, and at high flap settings LDmax can be at or
    // below the fuselage-zero reference (the wing produces L/D-max
    // lift at low body angle once flaps are deployed).  Reference
    // calibration N720AK flaps 33° has fLDMAXAOA = -1.12; gating on
    // LDmax > 0 would silence both the OnSpeed cues and the stall
    // warning across the entire approach envelope.
    //
    // Without this gate:
    //   * All-zero config: `AOA >= 0` trivially true → constant
    //     stall warning as soon as IAS crosses the mute threshold.
    //   * Partial config (only StallWarn set, others 0):
    //     `AOA > fONSPEEDSLOWAOA (0)` → spurious pulsed high tone
    //     for any positive AOA across the entire flight envelope.
    //
    // Defense-in-depth against the UI's SetpointOrderError() check:
    // the web save page already warns on misordered setpoints, but
    // we don't trust that a config reaching the audio path has been
    // validated.  Silence is safer than wrong tones in the air.
    if (th.fONSPEEDFASTAOA <= 0.0f ||
        th.fONSPEEDSLOWAOA <= 0.0f ||
        th.fSTALLWARNAOA   <= 0.0f)
    {
        return { EnToneType::None, 0.0f, STALL_VOL_MIN };
    }

    if (fAOA >= th.fSTALLWARNAOA)
    {
        return { EnToneType::High, HIGH_TONE_STALL_PPS, STALL_VOL_MAX };
    }

    if (fAOA > th.fONSPEEDSLOWAOA)
    {
        float fPPS = mapfloat(fAOA,
                              th.fONSPEEDSLOWAOA, th.fSTALLWARNAOA,
                              HIGH_TONE_PPS_MIN, HIGH_TONE_PPS_MAX);
        // Match Gen2's per-PPS amplitude ramp on the high pulsed region.
        float fVol = mapfloat(fAOA,
                              th.fONSPEEDSLOWAOA, th.fSTALLWARNAOA,
                              STALL_VOL_MIN, STALL_VOL_MAX);
        return { EnToneType::High, fPPS, fVol };
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

    return { EnToneType::None, 0.0f, STALL_VOL_MIN };
}

ToneResult calculateToneMuted(float fAOA, float fIAS,
                              float fSTALLWARNAOA, int iMuteUnderIAS)
{
    // Same uncalibrated gate as calculateTone: muted mode exists so
    // the stall warning can cut through user-muted audio, but with
    // no stall threshold configured there is nothing meaningful to
    // cut through.
    if (fSTALLWARNAOA <= 0.0f)
        return { EnToneType::None, 0.0f, STALL_VOL_MIN };

    if (fAOA >= fSTALLWARNAOA && fIAS > iMuteUnderIAS)
        return { EnToneType::High, HIGH_TONE_STALL_PPS, STALL_VOL_MAX };

    return { EnToneType::None, 0.0f, STALL_VOL_MIN };
}

} // namespace onspeed
