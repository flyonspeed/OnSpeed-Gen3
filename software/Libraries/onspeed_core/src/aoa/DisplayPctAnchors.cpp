// DisplayPctAnchors.cpp — implementation
//
// Two cues, two rules (Vac, ld_max.pdf §8):
//   * tonesOnPctLift snaps to the active detent's calibrated L/Dmax
//     percent — the operational audio gate.
//   * pipPctLift interpolates linearly across the entire pot range,
//     from the cleanest detent's L/Dmax percent to the most-deployed
//     detent's OnSpeed-band center — the visual aerodynamic reference.
//   * Band edges + flapsDeg behave per the table in DisplayPctAnchors.h.
//
// See DisplayPctAnchors.h for the design rule.

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
// integer; range-clamped to the protocol's signed range as a defensive
// belt-and-braces — the configured values already live in that range.
int LerpDegInt(int degA, int degB, float lambda)
{
    const float v = (1.0f - lambda) * static_cast<float>(degA)
                  +         lambda  * static_cast<float>(degB);
    int d = static_cast<int>(std::lround(v));
    if (d < -99) d = -99;
    if (d >  99) d =  99;
    return d;
}

// Populate snapped band-edge anchors (TonesOn / Fast / Slow / StallWarn)
// from the active detent.  These are the operational audio cues — they
// must match the audio path's gate, which reads
// `g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA` etc. directly.
void FillSnappedOperational(DisplayPctAnchors& out,
                            const SuFlaps& active,
                            bool iasValid)
{
    out.tonesOnPctLift     = ComputePercentLift(active.fLDMAXAOA,        active, iasValid);
    out.onSpeedFastPctLift = ComputePercentLift(active.fONSPEEDFASTAOA,  active, iasValid);
    out.onSpeedSlowPctLift = ComputePercentLift(active.fONSPEEDSLOWAOA,  active, iasValid);
    out.stallWarnPctLift   = ComputePercentLift(active.fSTALLWARNAOA,    active, iasValid);
}

// Compute the visual pip's "full-flap target" percent.
//
// HOTFIX (synth-record demo): land at the BOTTOM HALF of the donut at
// full flaps, not at the geometric center.  Vac feedback for the demo
// videos: with the pip in the band center the chevron has to rise into
// upper donut half to "meet" it, which feels visually too aggressive
// for the on-speed cue.  Bottom-half-of-donut keeps the chevron in the
// lower donut half when at L/Dmax, which reads cleaner on the indexer.
//
// Computed as the midpoint between the fast edge and the band center:
//
//   pipFullFlap = fast + 0.25 × (slow − fast)
//               = (3·fast + slow) / 4
//
// (The original spec used (fast+slow)/2 = the geometric center.)
int FullFlapPipTarget(const SuFlaps& mostDeployed, bool iasValid)
{
    const int fastPct = ComputePercentLift(mostDeployed.fONSPEEDFASTAOA, mostDeployed, iasValid);
    const int slowPct = ComputePercentLift(mostDeployed.fONSPEEDSLOWAOA, mostDeployed, iasValid);
    int target = static_cast<int>(std::lround(
        (3.0f * static_cast<float>(fastPct) + static_cast<float>(slowPct)) / 4.0f));
    if (target < 0)  target = 0;
    if (target > 99) target = 99;
    return target;
}

}   // namespace

DisplayPctAnchors ComputeDisplayPctAnchors(
    uint16_t rawAdc,
    const SuFlaps* flapEntries,
    size_t entryCount,
    size_t activeIndex,
    bool iasValid)
{
    DisplayPctAnchors out;

    if (flapEntries == nullptr || entryCount == 0u) {
        // No calibration; consumer renders "uncalibrated".  Both pip
        // and operational anchors stay at zero.
        return out;
    }

    // Clamp active index defensively so the active-detent reads below
    // never go out of bounds.
    if (activeIndex >= entryCount) {
        activeIndex = entryCount - 1u;
    }
    const SuFlaps& active = flapEntries[activeIndex];

    // OPERATIONAL anchors — always snap to the active detent.
    // tonesOnPctLift is the audio gate the M5 chevron mirrors;
    // band edges set the donut and stall-warn screen positions.
    FillSnappedOperational(out, active, iasValid);

    if (entryCount == 1u) {
        // Only one detent configured: pip cannot slide, lock at the
        // single detent's L/Dmax percent (already in tonesOnPctLift).
        out.pipPctLift = out.tonesOnPctLift;
        out.flapsDeg   = active.iDegrees;
        return out;
    }

    // PIP — linearly interpolate across the ENTIRE configured pot range
    // from the cleanest detent (`flapEntries[0]`) to the most-deployed
    // detent (`flapEntries[entryCount-1]`).  Intermediate detents
    // (e.g., a 16° flap setting in a 0/16/33 config) are intentionally
    // ignored: the pip is a smooth visual aerodynamic reference, not a
    // per-detent calibration anchor.  See spec §"Behavior contract" /
    // pipPctLift, and Vac's ld_max.pdf §8.
    {
        const SuFlaps& cleanest     = flapEntries[0];
        const SuFlaps& mostDeployed = flapEntries[entryCount - 1u];

        const int rawAdcInt = static_cast<int>(rawAdc);
        const int potA      = cleanest.iPotPosition;
        const int potB      = mostDeployed.iPotPosition;
        const int span      = potB - potA;

        const int pipClean    = ComputePercentLift(cleanest.fLDMAXAOA, cleanest, iasValid);
        const int pipFullFlap = FullFlapPipTarget(mostDeployed, iasValid);

        float lambda;
        if (span == 0) {
            // Degenerate wiring: cleanest and most-deployed share the
            // same pot.  Lock pip at the clean endpoint.
            lambda = 0.0f;
        } else {
            lambda = static_cast<float>(rawAdcInt - potA)
                   / static_cast<float>(span);
            if (lambda < 0.0f) lambda = 0.0f;
            if (lambda > 1.0f) lambda = 1.0f;
        }
        out.pipPctLift = LerpPctInt(pipClean, pipFullFlap, lambda);
    }

    // flapsDeg — per-bracket interpolation using each adjacent-detent
    // pair's iDegrees and pot positions.  Different math from the pip:
    // the lever angle must visit every detent's iDegrees exactly when
    // the lever pot equals that detent's iPotPosition.
    const int rawAdcInt = static_cast<int>(rawAdc);

    for (size_t i = 0u; i + 1u < entryCount; ++i) {
        const int potA = flapEntries[i].iPotPosition;
        const int potB = flapEntries[i + 1u].iPotPosition;

        const int lo = std::min(potA, potB);
        const int hi = std::max(potA, potB);

        if (rawAdcInt >= lo && rawAdcInt <= hi) {
            const int span = potB - potA;
            float lambda;
            if (span == 0) {
                lambda = 0.0f;
            } else {
                lambda = static_cast<float>(rawAdcInt - potA)
                       / static_cast<float>(span);
                if (lambda < 0.0f) lambda = 0.0f;
                if (lambda > 1.0f) lambda = 1.0f;
            }
            const SuFlaps& a = flapEntries[i];
            const SuFlaps& b = flapEntries[i + 1u];
            out.flapsDeg = LerpDegInt(a.iDegrees, b.iDegrees, lambda);
            return out;
        }
    }

    // Lever is outside the entire configured range — flapsDeg snaps to
    // the closest endpoint detent.  (Pip is already lambda-clamped to
    // its endpoint above; operational anchors come from active.)
    size_t closestIdx = 0u;
    int    closestDist = std::abs(static_cast<int>(flapEntries[0].iPotPosition) - rawAdcInt);
    for (size_t i = 1u; i < entryCount; ++i) {
        const int d = std::abs(static_cast<int>(flapEntries[i].iPotPosition) - rawAdcInt);
        if (d < closestDist) {
            closestDist = d;
            closestIdx  = i;
        }
    }
    out.flapsDeg = flapEntries[closestIdx].iDegrees;
    return out;
}

}   // namespace onspeed::aoa
