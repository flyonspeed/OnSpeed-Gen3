// Characterization test pinning the X-Plane plugin's tone-decision
// boundaries. Built with the plugin's CMake target via add_subdirectory;
// invoked by ctest.
//
// The test exists because the plugin's audio decisions now go through
// onspeed::calculateTone() — same code path as the panel-mounted
// firmware. If a future onspeed_core change shifts the
// (toneType, pulsePps) result for any of the plugin's default
// boundary AOAs, this test fails and the change has to be explicitly
// acknowledged (by updating the fixture). That's exactly the
// regression posture we want.
//
// We do NOT re-test calculateTone() exhaustively — that already happens
// in software/Libraries/onspeed_core/test/test_tone_calc/. We test the
// plugin's *contract*: with these specific defaults, here are the
// boundary outcomes a pilot would hear.

#include <audio/ToneCalc.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

// Plugin's default thresholds (kept in sync manually with
// src/aoa_audio.cpp:75-79). If those defaults move, this fixture
// must move too — by intent.
constexpr float kDefaultLDmax        =  6.0f;
constexpr float kDefaultBelowOnSpeed =  7.3f;
constexpr float kDefaultOnSpeedMax   =  9.6f;
constexpr float kDefaultStallWarn    = 12.5f;

const onspeed::ToneThresholds kDefaultThresholds{
    kDefaultLDmax,
    kDefaultBelowOnSpeed,
    kDefaultOnSpeedMax,
    kDefaultStallWarn,
};

constexpr float kEpsilon = 0.001f;
constexpr float kPpsTolerance = 0.01f;

struct Fixture {
    const char*           label;
    float                 aoa;
    onspeed::EnToneType   expectedTone;
    float                 expectedPps;   // 0 = solid, exact for boundary points
};

// Boundary table. Each row is one boundary or just inside/outside it,
// with the expected (tone, pps) the plugin should produce. Numbers
// computed from onspeed_core/audio/ToneCalc.cpp's mapping.
const Fixture kFixtures[] = {
    // Below LDmax: silence
    {"well below LDmax",          0.0f,                            onspeed::EnToneType::None,  0.0f},
    {"just below LDmax",          kDefaultLDmax - kEpsilon,        onspeed::EnToneType::None,  0.0f},

    // At LDmax: low pulse at PPS_MIN (1.5)
    {"at LDmax",                  kDefaultLDmax,                   onspeed::EnToneType::Low,   1.5f},

    // Mid LDmax↔BelowOnSpeed: linear interp [1.5, 8.2]; midpoint = 4.85
    {"mid LDmax→BelowOnSpeed",   (kDefaultLDmax + kDefaultBelowOnSpeed) * 0.5f,
                                                                   onspeed::EnToneType::Low,   4.85f},

    // Just below BelowOnSpeed: low pulse near PPS_MAX (8.2)
    {"just below BelowOnSpeed",   kDefaultBelowOnSpeed - kEpsilon, onspeed::EnToneType::Low,   8.2f},

    // At BelowOnSpeed (entry to OnSpeed band): solid Low
    {"at BelowOnSpeed",           kDefaultBelowOnSpeed,            onspeed::EnToneType::Low,   0.0f},

    // Within OnSpeed band: solid Low
    {"mid OnSpeed band",         (kDefaultBelowOnSpeed + kDefaultOnSpeedMax) * 0.5f,
                                                                   onspeed::EnToneType::Low,   0.0f},

    // At OnSpeedMax (top of band, still solid Low — strict-greater check above this)
    {"at OnSpeedMax",             kDefaultOnSpeedMax,              onspeed::EnToneType::Low,   0.0f},

    // Just above OnSpeedMax: high pulse at PPS_MIN (1.5)
    {"just above OnSpeedMax",     kDefaultOnSpeedMax + kEpsilon,   onspeed::EnToneType::High,  1.5f},

    // Mid OnSpeedMax↔StallWarn: midpoint of [1.5, 6.2] = 3.85
    {"mid OnSpeedMax→StallWarn", (kDefaultOnSpeedMax + kDefaultStallWarn) * 0.5f,
                                                                   onspeed::EnToneType::High,  3.85f},

    // Just below StallWarn: high pulse near PPS_MAX (6.2)
    {"just below StallWarn",      kDefaultStallWarn - kEpsilon,    onspeed::EnToneType::High,  6.2f},

    // At StallWarn: stall pulse (20 PPS)
    {"at StallWarn",              kDefaultStallWarn,               onspeed::EnToneType::High, 20.0f},

    // Above StallWarn: stall pulse (20 PPS)
    {"above StallWarn",           kDefaultStallWarn + 1.0f,        onspeed::EnToneType::High, 20.0f},
    {"way above StallWarn",       30.0f,                           onspeed::EnToneType::High, 20.0f},
};

const char* toneName(onspeed::EnToneType t) {
    switch (t) {
        case onspeed::EnToneType::None: return "None";
        case onspeed::EnToneType::Low:  return "Low";
        case onspeed::EnToneType::High: return "High";
    }
    return "?";
}

bool runFixture(const Fixture& f) {
    const onspeed::ToneResult got = onspeed::calculateTone(f.aoa, kDefaultThresholds);
    const bool toneOk = got.enTone == f.expectedTone;
    const bool ppsOk  = std::fabs(got.fPulseFreq - f.expectedPps) <= kPpsTolerance;
    if (!toneOk || !ppsOk) {
        std::fprintf(stderr,
            "FAIL: %-30s  aoa=%.4f  expected (%s, %.3f pps)  got (%s, %.3f pps)\n",
            f.label, static_cast<double>(f.aoa),
            toneName(f.expectedTone), static_cast<double>(f.expectedPps),
            toneName(got.enTone),    static_cast<double>(got.fPulseFreq));
        return false;
    }
    return true;
}

} // namespace

int main() {
    int failures = 0;
    int total = 0;
    for (const Fixture& f : kFixtures) {
        ++total;
        if (!runFixture(f)) ++failures;
    }
    std::printf("%d/%d fixtures passed\n", total - failures, total);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
