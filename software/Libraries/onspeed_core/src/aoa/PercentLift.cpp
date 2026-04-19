// PercentLift.cpp - implementation
//
// Verbatim port of the inline percent-lift math that used to live at the
// top of DisplaySerial::Write() (see DisplaySerial.cpp:147-176 prior to
// PR 3.1 Task 5).  Kept literal — including the ints-from-floats casts
// and the fStallCeiling fallback — so behavior on the wire is bit-for-bit
// identical.

#include <aoa/PercentLift.h>

#include <util/OnSpeedTypes.h>  // mapfloat

namespace onspeed {
namespace aoa {

int ComputePercentLift(float aoaDeg,
                       const ::onspeed::config::OnSpeedConfig::SuFlaps& flapCfg,
                       bool iasValid)
{
    if (!iasValid) {
        return 0;
    }

    int iPercentLift;

    // Scale percent lift (alpha_0 is the zero-lift floor; defaults to 0
    // for uncalibrated configs).
    const float fAlpha0Floor = flapCfg.fAlpha0;

    if (aoaDeg < flapCfg.fLDMAXAOA) {
        // Below LDmax — 0..50% range
        iPercentLift = static_cast<int>(
            mapfloat(aoaDeg, fAlpha0Floor, flapCfg.fLDMAXAOA, 0.0f, 50.0f));
    } else if ((aoaDeg >= flapCfg.fLDMAXAOA) &&
               (aoaDeg <= flapCfg.fONSPEEDFASTAOA)) {
        // LDmax..OnSpeedFast — 50..55% range
        iPercentLift = static_cast<int>(
            mapfloat(aoaDeg, flapCfg.fLDMAXAOA, flapCfg.fONSPEEDFASTAOA,
                     50.0f, 55.0f));
    } else if ((aoaDeg > flapCfg.fONSPEEDFASTAOA) &&
               (aoaDeg <= flapCfg.fONSPEEDSLOWAOA)) {
        // OnSpeedFast..OnSpeedSlow — 55..66% range
        iPercentLift = static_cast<int>(
            mapfloat(aoaDeg, flapCfg.fONSPEEDFASTAOA, flapCfg.fONSPEEDSLOWAOA,
                     55.0f, 66.0f));
    } else if ((aoaDeg > flapCfg.fONSPEEDSLOWAOA) &&
               (aoaDeg <= flapCfg.fSTALLWARNAOA)) {
        // OnSpeedSlow..StallWarn — 66..90% range
        iPercentLift = static_cast<int>(
            mapfloat(aoaDeg, flapCfg.fONSPEEDSLOWAOA, flapCfg.fSTALLWARNAOA,
                     66.0f, 90.0f));
    } else {
        // Above StallWarn — 90..100% range, ceiling is fAlphaStall when
        // calibrated, otherwise the original geometric extrapolation.
        float fStallCeiling = flapCfg.fAlphaStall;
        if (fStallCeiling <= flapCfg.fSTALLWARNAOA) {
            fStallCeiling = flapCfg.fSTALLWARNAOA * 100.0f / 90.0f;
        }
        iPercentLift = static_cast<int>(
            mapfloat(aoaDeg, flapCfg.fSTALLWARNAOA, fStallCeiling,
                     90.0f, 100.0f));
    }

    // Clamp to [0, 99] — same constrain() the sketch used.
    if (iPercentLift < 0)  iPercentLift = 0;
    if (iPercentLift > 99) iPercentLift = 99;
    return iPercentLift;
}

}  // namespace aoa
}  // namespace onspeed
