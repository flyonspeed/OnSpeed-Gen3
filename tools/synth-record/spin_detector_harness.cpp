// spin_detector_harness.cpp — host harness around onspeed_core::SpinDetector.
//
// Reads tick-state lines on stdin, writes one cue value per tick to stdout.
//
//   stdin line:  "<dt_sec> <yaw_dps> <aoa_deg> <stall_aoa_deg>\n"
//   stdout line: "<cue>\n"     where cue ∈ {-1, 0, +1}
//
// The orchestrator runs this once per scenario, piping tick states through
// it so the same SpinDetector code that ships in firmware decides the
// cue.  Reuses the real C++ class — no Python re-implementation.

#include <sensors/SpinDetector.h>

#include <cstdio>
#include <cstdlib>

int main(int argc, char**)
{
    (void)argc;

    onspeed::SpinDetector detector;
    detector.Reset();

    char line[256];
    while (std::fgets(line, sizeof(line), stdin))
    {
        float dt = 0.0f, yaw = 0.0f, aoa = 0.0f, stall = 0.0f;
        if (std::sscanf(line, "%f %f %f %f", &dt, &yaw, &aoa, &stall) != 4) {
            std::fprintf(stderr, "spin_detector_harness: bad input line: %s", line);
            return 2;
        }
        int cue = detector.Update(dt, yaw, aoa, stall);
        std::printf("%d\n", cue);
        std::fflush(stdout);
    }
    return 0;
}
