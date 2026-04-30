// display_anchors_harness.cpp — host harness around
// onspeed::aoa::ComputeDisplayPctAnchors.
//
// Input on stdin:
//   First line:  "<entry_count>\n"
//   Then for each entry:
//     "<iDegrees> <iPotPosition> <fLDMAXAOA> <fONSPEEDFASTAOA>"
//     " <fONSPEEDSLOWAOA> <fSTALLWARNAOA> <fAlpha0> <fAlphaStall>\n"
//   Then per-tick:
//     "<rawAdc> <activeIndex>\n"
//
// Output on stdout (per tick):
//   "<tonesOnPct> <onSpeedFastPct> <onSpeedSlowPct> <stallWarnPct> <flapsDeg>\n"
//
// Ends on stdin EOF.  Uses the firmware's exact interpolation logic
// (DisplayPctAnchors.cpp) — no Python re-implementation, no
// constant duplication.

#include <aoa/DisplayPctAnchors.h>
#include <config/OnSpeedConfig.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int /*argc*/, char** /*argv*/)
{
    using onspeed::config::OnSpeedConfig;

    int entry_count = 0;
    if (std::scanf("%d", &entry_count) != 1 || entry_count <= 0) {
        std::fprintf(stderr, "display_anchors_harness: bad entry count\n");
        return 2;
    }

    std::vector<OnSpeedConfig::SuFlaps> flaps(static_cast<size_t>(entry_count));
    for (int i = 0; i < entry_count; ++i) {
        OnSpeedConfig::SuFlaps& f = flaps[static_cast<size_t>(i)];
        if (std::scanf("%d %d %f %f %f %f %f %f",
                       &f.iDegrees, &f.iPotPosition,
                       &f.fLDMAXAOA, &f.fONSPEEDFASTAOA,
                       &f.fONSPEEDSLOWAOA, &f.fSTALLWARNAOA,
                       &f.fAlpha0, &f.fAlphaStall) != 8) {
            std::fprintf(stderr, "display_anchors_harness: bad flap entry %d\n", i);
            return 2;
        }
    }

    int rawAdc = 0;
    int activeIdx = 0;
    while (std::scanf("%d %d", &rawAdc, &activeIdx) == 2) {
        const auto a = onspeed::aoa::ComputeDisplayPctAnchors(
            static_cast<uint16_t>(rawAdc),
            flaps.data(),
            flaps.size(),
            static_cast<size_t>(activeIdx),
            /*iasValid=*/true);
        std::printf("%d %d %d %d %d\n",
                    a.tonesOnPctLift,
                    a.onSpeedFastPctLift,
                    a.onSpeedSlowPctLift,
                    a.stallWarnPctLift,
                    a.flapsDeg);
        std::fflush(stdout);
    }
    return 0;
}
