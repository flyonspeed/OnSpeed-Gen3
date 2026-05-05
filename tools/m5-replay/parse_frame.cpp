// parse_frame.cpp — firmware-parser harness for the m5-replay test.
//
// Reads a single #1 display-serial frame (kDisplayFrameSizeBytes bytes)
// from stdin and prints each parsed field, one per line, as `key=value`.
// test_replay.py spawns this binary, pipes a frame built by replay.py's
// Python builder into its stdin, and asserts every parsed field matches
// the original inputs (within wire resolution).
//
// The point: the test verifies the bench-replay tool produces frames
// the firmware can actually decode, by exercising the same
// `ParseDisplayFrame` that runs on the M5.  Self-referential
// Python-only round-trip tests cannot catch wire-format drift; this one
// can.
//
// On a parse failure, prints `error=parse_failed` and exits 1.

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>

#include <proto/DisplaySerial.h>

int main()
{
    using onspeed::proto::ParseDisplayFrame;
    using onspeed::proto::kDisplayFrameSizeBytes;

    std::vector<uint8_t> buf;
    buf.reserve(kDisplayFrameSizeBytes);
    int ch;
    while ((ch = std::fgetc(stdin)) != EOF) {
        buf.push_back(static_cast<uint8_t>(ch));
    }

    if (buf.size() != kDisplayFrameSizeBytes) {
        std::printf("error=wrong_size got=%zu expected=%zu\n",
                    buf.size(), kDisplayFrameSizeBytes);
        return 1;
    }

    auto opt = ParseDisplayFrame(buf.data(), buf.size());
    if (!opt.has_value()) {
        std::printf("error=parse_failed\n");
        return 1;
    }

    const auto& f = opt.value();
    // %.4f preserves wire resolution (×10 = 0.1 deg, ×100 = 0.01 g)
    // without leaking float-print noise into the diff.
    std::printf("pitchDeg=%.4f\n",            f.pitchDeg);
    std::printf("rollDeg=%.4f\n",             f.rollDeg);
    std::printf("iasKt=%.4f\n",               f.iasKt);
    std::printf("paltFt=%.4f\n",              f.paltFt);
    std::printf("turnRateDps=%.4f\n",         f.turnRateDps);
    std::printf("lateralG=%.4f\n",            f.lateralG);
    std::printf("verticalG=%.4f\n",           f.verticalG);
    std::printf("percentLiftPct=%.4f\n",      f.percentLiftPct);
    std::printf("vsiFpm=%.4f\n",              f.vsiFpm);
    std::printf("oatC=%d\n",                  f.oatC);
    std::printf("flightPathDeg=%.4f\n",       f.flightPathDeg);
    std::printf("flapsDeg=%d\n",              f.flapsDeg);
    std::printf("tonesOnPctLift=%d\n",        f.tonesOnPctLift);
    std::printf("onSpeedFastPctLift=%d\n",    f.onSpeedFastPctLift);
    std::printf("onSpeedSlowPctLift=%d\n",    f.onSpeedSlowPctLift);
    std::printf("stallWarnPctLift=%d\n",      f.stallWarnPctLift);
    std::printf("flapsMinDeg=%d\n",           f.flapsMinDeg);
    std::printf("flapsMaxDeg=%d\n",           f.flapsMaxDeg);
    std::printf("gOnsetRate=%.4f\n",          f.gOnsetRate);
    std::printf("spinRecoveryCue=%d\n",       f.spinRecoveryCue);
    std::printf("dataMark=%d\n",              f.dataMark);
    std::printf("pipPctLift=%d\n",            f.pipPctLift);
    return 0;
}
