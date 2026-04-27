// DisplayPctAnchors.cpp — implementation
//
// Bracket-search interpolation between adjacent flap detents in
// percent-lift space.  See DisplayPctAnchors.h for the contract.

#include <aoa/DisplayPctAnchors.h>

#include <aoa/PercentLift.h>

#include <algorithm>
#include <cmath>

namespace onspeed::aoa {

namespace {

using SuFlaps = ::onspeed::config::OnSpeedConfig::SuFlaps;

// Lerp two percent integers by lambda in [0, 1].  Each input is the
// honest envelope-fraction percent for one detent; the output is the
// interpolated percent.  Rounded to nearest integer; clamped to the
// same [0, 99] saturation range that `ComputePercentLift` produces.
int LerpPctInt(int pctA, int pctB, float lambda)
{
    const float v = (1.0f - lambda) * static_cast<float>(pctA)
                  +         lambda  * static_cast<float>(pctB);
    int p = static_cast<int>(std::lround(v));
    if (p < 0)  p = 0;
    if (p > 99) p = 99;
    return p;
}

// Lerp two flapsDeg integers by lambda in [0, 1].  Rounded to nearest
// integer; range-clamped to the protocol's signed 8-bit-ish range
// (-99..+99) only as a defensive belt-and-braces — the configured
// values already live in that range.
int LerpDegInt(int degA, int degB, float lambda)
{
    const float v = (1.0f - lambda) * static_cast<float>(degA)
                  +         lambda  * static_cast<float>(degB);
    int d = static_cast<int>(std::lround(v));
    if (d < -99) d = -99;
    if (d >  99) d =  99;
    return d;
}

// Build a DisplayPctAnchors snapshot for a single detent (no
// interpolation).  Used at the endpoints (lever clamped) and when
// only one detent is configured.
DisplayPctAnchors AnchorsForOneDetent(const SuFlaps& f, bool iasValid)
{
    DisplayPctAnchors a;
    a.tonesOnPctLift     = ComputePercentLift(f.fLDMAXAOA,       f, iasValid);
    a.onSpeedFastPctLift = ComputePercentLift(f.fONSPEEDFASTAOA, f, iasValid);
    a.onSpeedSlowPctLift = ComputePercentLift(f.fONSPEEDSLOWAOA, f, iasValid);
    a.stallWarnPctLift   = ComputePercentLift(f.fSTALLWARNAOA,   f, iasValid);
    a.flapsDeg           = f.iDegrees;
    return a;
}

}   // namespace

DisplayPctAnchors ComputeDisplayPctAnchors(
    uint16_t rawAdc,
    const SuFlaps* flapEntries,
    size_t entryCount,
    bool iasValid)
{
    DisplayPctAnchors out;

    if (flapEntries == nullptr || entryCount == 0u) {
        // No calibration; consumer renders "uncalibrated".
        return out;
    }

    if (entryCount == 1u) {
        return AnchorsForOneDetent(flapEntries[0], iasValid);
    }

    // Walk adjacent pairs (i, i+1) and find the one whose pot positions
    // bracket `rawAdc`.  The detector supports both ascending and
    // descending wiring; bracket search is order-agnostic since it
    // tests min/max of each pair.
    //
    // Endpoint clamping: if `rawAdc` is outside the entire range of
    // configured pot positions (below the smallest or above the
    // largest), the loop falls through and we return the endpoint
    // detent's anchors via the post-loop endpoint check.
    const int rawAdcInt = static_cast<int>(rawAdc);

    for (size_t i = 0u; i + 1u < entryCount; ++i) {
        const int potA = flapEntries[i].iPotPosition;
        const int potB = flapEntries[i + 1u].iPotPosition;

        const int lo = std::min(potA, potB);
        const int hi = std::max(potA, potB);

        if (rawAdcInt >= lo && rawAdcInt <= hi) {
            // Lever is within this bracket.  Compute lambda along the
            // (potA -> potB) direction so detent a's values appear at
            // lambda=0 and detent b's at lambda=1.
            const int span = potB - potA;       // signed; can be negative for descending wiring
            float lambda;
            if (span == 0) {
                // Two adjacent detents share the same pot position
                // (degenerate config).  Lambda doesn't matter; default
                // to 0 so detent a's values are returned.
                lambda = 0.0f;
            } else {
                lambda = static_cast<float>(rawAdcInt - potA)
                       / static_cast<float>(span);
                if (lambda < 0.0f) lambda = 0.0f;
                if (lambda > 1.0f) lambda = 1.0f;
            }

            // Compute each endpoint detent's percents at iasValid, then
            // lerp.  Calling ComputePercentLift on each side keeps the
            // formula in exactly one place.
            const SuFlaps& a = flapEntries[i];
            const SuFlaps& b = flapEntries[i + 1u];

            const int tonesA = ComputePercentLift(a.fLDMAXAOA,       a, iasValid);
            const int tonesB = ComputePercentLift(b.fLDMAXAOA,       b, iasValid);
            const int fastA  = ComputePercentLift(a.fONSPEEDFASTAOA, a, iasValid);
            const int fastB  = ComputePercentLift(b.fONSPEEDFASTAOA, b, iasValid);
            const int slowA  = ComputePercentLift(a.fONSPEEDSLOWAOA, a, iasValid);
            const int slowB  = ComputePercentLift(b.fONSPEEDSLOWAOA, b, iasValid);
            const int warnA  = ComputePercentLift(a.fSTALLWARNAOA,   a, iasValid);
            const int warnB  = ComputePercentLift(b.fSTALLWARNAOA,   b, iasValid);

            out.tonesOnPctLift     = LerpPctInt(tonesA, tonesB, lambda);
            out.onSpeedFastPctLift = LerpPctInt(fastA,  fastB,  lambda);
            out.onSpeedSlowPctLift = LerpPctInt(slowA,  slowB,  lambda);
            out.stallWarnPctLift   = LerpPctInt(warnA,  warnB,  lambda);
            out.flapsDeg           = LerpDegInt(a.iDegrees, b.iDegrees, lambda);
            return out;
        }
    }

    // Lever is outside the entire range.  Find the endpoint detent
    // (the one whose pot position is on the side `rawAdc` exceeds) and
    // return its anchors unmodified.  Wiring may be ascending or
    // descending; pick the entry whose pot position is closest to the
    // reading.
    size_t closestIdx = 0u;
    int    closestDist = std::abs(static_cast<int>(flapEntries[0].iPotPosition) - rawAdcInt);
    for (size_t i = 1u; i < entryCount; ++i) {
        const int d = std::abs(static_cast<int>(flapEntries[i].iPotPosition) - rawAdcInt);
        if (d < closestDist) {
            closestDist = d;
            closestIdx  = i;
        }
    }
    return AnchorsForOneDetent(flapEntries[closestIdx], iasValid);
}

}   // namespace onspeed::aoa
