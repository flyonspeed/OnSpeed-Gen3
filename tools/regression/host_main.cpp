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
// Output CSV columns (kOutputHeader below):
//   ias_kt,palt_ft,oat_c,tone_freq_hz,tone_level
//
// The output columns are minimal at PR 0.3 because onspeed_core does not
// yet contain AHRS, audio synthesis, or other later-phase modules. Each
// Phase 1+ PR that moves a new module to core adds corresponding output
// columns here and updates kOutputHeader.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sstream>

// onspeed_core includes (only the ones currently in core)
#include <ToneCalc.h>

namespace {

// Input and output schemas are kept side-by-side so that adding a column
// to one prompts updating the other.
constexpr char kInputHeader[]  = "ias_kt,palt_ft,oat_c,ax,ay,az,gx,gy,gz";
constexpr char kOutputHeader[] = "ias_kt,palt_ft,oat_c,tone_freq_hz,tone_level";

// Placeholder AOA thresholds (degrees) matching clean-flap setpoints for a
// typical GA aircraft.  Phase 1+ will replace these with values read from the
// per-flap config loaded at startup.
constexpr onspeed::ToneThresholds kCleanThresholds {
    /* fLDMAXAOA      */ 3.0f,
    /* fONSPEEDFASTAOA*/ 6.5f,
    /* fONSPEEDSLOWAOA*/ 9.5f,
    /* fSTALLWARNAOA  */ 12.5f,
};

// Lift-equation constant K such that AOA = K / IAS² gives a plausible
// AOA at cruise.  Chosen so that 100 kt → ~4° (mid-cruise, Low tone solid).
constexpr float kLiftK = 40000.0f;

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
    return line == kInputHeader;
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
    std::printf("%s\n", kOutputHeader);

    size_t row_count = 0;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        InputRow r{};
        if (!ParseRow(line, r)) {
            std::fprintf(stderr, "host_main: bad row at %zu: %s\n",
                         row_count, line.c_str());
            return 1;
        }

        // Derive a placeholder AOA from IAS using the lift equation
        //   AOA = K / IAS²
        // This is intentionally simplified — no AHRS pitch, no pressure
        // sensors — because Phase 0.3 is about harness plumbing, not
        // algorithm accuracy.  The constant K is tuned so cruise IAS
        // maps to a plausible on-speed AOA.  Phase 1+ will replace this
        // with the real DerivedAOA from the extracted AHRS module.
        float fake_aoa = (r.ias_kt > 1.0f)
                         ? (kLiftK / (r.ias_kt * r.ias_kt))
                         : onspeed::AOA_MAX_VALUE;

        onspeed::ToneResult result = onspeed::calculateTone(fake_aoa, kCleanThresholds);

        // Map ToneResult to the two output columns.
        //   tone_freq_hz: pulse rate (Hz), or the base carrier frequency as a
        //                 sentinel for "solid" (0 PPS → 440 Hz) and "no tone" (0).
        //   tone_level:   0 = no tone, 1 = low tone, 2 = high tone.
        float tone_freq_hz = 0.0f;
        int   tone_level   = 0;
        switch (result.enTone) {
        case onspeed::EnToneType::None:
            tone_freq_hz = 0.0f;
            tone_level   = 0;
            break;
        case onspeed::EnToneType::Low:
            tone_freq_hz = result.fPulseFreq;   // 0 = solid low tone
            tone_level   = 1;
            break;
        case onspeed::EnToneType::High:
            tone_freq_hz = result.fPulseFreq;   // 1.5–20 PPS
            tone_level   = 2;
            break;
        }

        std::printf("%.4f,%.4f,%.4f,%.4f,%d\n",
                    r.ias_kt, r.palt_ft, r.oat_c,
                    tone_freq_hz, tone_level);
        ++row_count;
    }

    std::fprintf(stderr, "host_main: %zu rows processed\n", row_count);
    return 0;
}
