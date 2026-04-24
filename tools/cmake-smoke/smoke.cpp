// CMake-shim smoke test for onspeed_core. Links the library, calls one
// function from each major subdirectory we care about, asserts a
// round-numbered expectation. If this fails to build the shim is broken;
// if it builds but the assertions fire the includes wired up wrong.

#include <audio/ToneCalc.h>
#include <cstdio>
#include <cstdlib>

int main() {
    // OnSpeed band: AOA between OnSpeedFast (8.0) and OnSpeedSlow (10.0)
    // should yield a solid Low tone (pulse_freq == 0).
    onspeed::ToneThresholds th{
        /*fLDMAXAOA*/        6.0f,
        /*fONSPEEDFASTAOA*/  8.0f,
        /*fONSPEEDSLOWAOA*/ 10.0f,
        /*fSTALLWARNAOA*/   13.0f,
    };
    auto r = onspeed::calculateTone(9.0f, th);
    if (r.enTone != onspeed::EnToneType::Low || r.fPulseFreq != 0.0f) {
        std::fprintf(stderr,
            "smoke FAIL: expected Low/0pps in OnSpeed band, got tone=%d freq=%f\n",
            static_cast<int>(r.enTone), r.fPulseFreq);
        return EXIT_FAILURE;
    }
    std::printf("onspeed_core CMake shim OK\n");
    return EXIT_SUCCESS;
}
