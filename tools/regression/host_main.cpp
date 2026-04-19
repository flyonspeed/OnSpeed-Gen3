// host_main.cpp — snapshot regression harness for onspeed_core.
//
// Reads a CSV of sensor samples on stdin, runs each row through the
// currently-extracted core pipeline, and writes a CSV of outputs on
// stdout. Python driver at tools/regression/run_snapshot.py feeds this
// a recorded flight log and diffs the result against a golden file.
//
// Input CSV columns (header row required, must match exactly):
//   ias_kt,palt_ft,oat_c,ax,ay,az,gx,gy,gz
//
// Output CSV columns:
//   ias_kt,palt_ft,oat_c,tone_freq_hz,tone_level
//
// The output columns are minimal at PR 0.3 because onspeed_core does not
// yet contain AHRS, audio synthesis, or other later-phase modules. Each
// Phase 1+ PR that moves a new module to core adds corresponding output
// columns here.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sstream>

// onspeed_core includes (only the ones currently in core)
#include <ToneCalc.h>

namespace {

struct InputRow {
    float ias_kt;
    float palt_ft;
    float oat_c;
    float ax, ay, az;    // accelerometer in g
    float gx, gy, gz;    // gyroscope in deg/s
};

bool ParseHeader(const std::string& line)
{
    // Expect an exact header — if this changes, bump the golden and
    // every caller. Keeping it strict is the feature.
    const char* kExpected =
        "ias_kt,palt_ft,oat_c,ax,ay,az,gx,gy,gz";
    return line == kExpected;
}

bool ParseRow(const std::string& line, InputRow& out)
{
    float* fields[] = {
        &out.ias_kt, &out.palt_ft, &out.oat_c,
        &out.ax, &out.ay, &out.az,
        &out.gx, &out.gy, &out.gz,
    };
    const size_t kExpected = sizeof(fields) / sizeof(fields[0]);

    std::stringstream ss(line);
    std::string tok;
    for (size_t i = 0; i < kExpected; ++i) {
        if (!std::getline(ss, tok, ',')) return false;
        try {
            *fields[i] = std::stof(tok);
        }
        catch (const std::exception& e) {
            std::fprintf(stderr,
                "host_main: cannot parse field %zu '%s': %s\n",
                i, tok.c_str(), e.what());
            return false;
        }
    }
    return true;
}

}   // namespace

int main()
{
    std::string line;

    // Read and verify header.
    if (!std::getline(std::cin, line) || !ParseHeader(line)) {
        std::fprintf(stderr, "host_main: bad or missing CSV header\n");
        return 1;
    }

    // Emit output header.
    std::printf("ias_kt,palt_ft,oat_c,tone_freq_hz,tone_level\n");

    size_t row_count = 0;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        InputRow r{};
        if (!ParseRow(line, r)) {
            std::fprintf(stderr, "host_main: bad row at %zu: %s\n",
                         row_count, line.c_str());
            return 1;
        }

        // For PR 0.3 the pipeline is trivial — pass IAS / Palt / OAT through
        // and compute a placeholder tone decision based on raw IAS. Later
        // PRs add: AOA calculation, AHRS, audio mixer outputs, etc.
        float tone_freq_hz = (r.ias_kt > 60.0f) ? 500.0f : 1000.0f;
        int tone_level = (r.ias_kt > 40.0f) ? 1 : 0;

        std::printf("%.4f,%.4f,%.4f,%.4f,%d\n",
                    r.ias_kt, r.palt_ft, r.oat_c,
                    tone_freq_hz, tone_level);
        ++row_count;
    }

    std::fprintf(stderr, "host_main: %zu rows processed\n", row_count);
    return 0;
}
