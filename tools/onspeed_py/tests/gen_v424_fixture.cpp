// gen_v424_fixture.cpp — one-shot generator for the v4.24 byte-parity
// golden binary used by test_v424_byte_parity.py.  Compiles against
// onspeed_core/proto/DisplaySerial.{h,cpp} so the bytes it writes are
// exactly what the firmware would emit at flight time for the same
// inputs.
//
// Build (from this directory):
//   g++ -std=c++17 \
//       -I ../../../software/Libraries/onspeed_core/src \
//       gen_v424_fixture.cpp \
//       ../../../software/Libraries/onspeed_core/src/proto/DisplaySerial.cpp \
//       -o gen_v424_fixture
//   ./gen_v424_fixture fixtures/v424_golden.bin
//
// After running, commit the generated fixtures/v424_golden.bin alongside
// this file.  Regenerate any time onspeed_core/proto/DisplaySerial.{h,cpp}
// changes the wire format; the parity test will catch silent drift.

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <vector>

#include <proto/DisplaySerial.h>
#include <types/AirDataValid.h>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output-path>\n", argv[0]);
        return 1;
    }

    onspeed::proto::DisplayBuildInputs in{};
    in.pitchDeg            = 2.3f;
    in.rollDeg             = -1.5f;
    in.iasKt               = 147.0f;
    in.paltFt              = 8000;
    in.turnRateDps         = 0.5f;
    in.lateralG            = 0.02f;
    // verticalGScaled10 is already scaled ×10 (raw int stored as float).
    // 1.0 g × 10 = 10.
    in.verticalGScaled10   = 10.0f;
    in.percentLiftPct      = 55.4f;
    in.vsiFpm10            = 20;
    in.oatC                = 5;
    in.flightPathDeg       = 1.8f;
    in.flapsDeg            = 0;
    in.tonesOnPctLift      = 63;
    in.onSpeedFastPctLift  = 70;
    in.onSpeedSlowPctLift  = 80;
    in.stallWarnPctLift    = 95;
    in.flapsMinDeg         = 0;
    in.flapsMaxDeg         = 30;
    in.gOnsetRate          = 0.0f;
    in.spinRecoveryCue     = 0;
    in.dataMark            = 0;
    in.pipPctLift          = 63;
    // kOatRaw|kOatSat|kIas|kPalt|kTas|kDensityAlt = bits 0..5 = 0x3F.
    in.valid.bits          = 0x3Fu;

    std::vector<uint8_t> buf(onspeed::proto::kDisplayFrameSizeBytes, 0);
    const std::size_t n = onspeed::proto::BuildDisplayFrame(
        in, buf.data(), buf.size());
    if (n != onspeed::proto::kDisplayFrameSizeBytes) {
        std::fprintf(stderr,
                     "BuildDisplayFrame returned %zu, expected %zu\n",
                     n,
                     onspeed::proto::kDisplayFrameSizeBytes);
        return 2;
    }

    std::ofstream out(argv[1], std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "cannot open %s for writing\n", argv[1]);
        return 3;
    }
    out.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
    out.close();

    std::printf("wrote %zu bytes to %s\n", buf.size(), argv[1]);
    return 0;
}
