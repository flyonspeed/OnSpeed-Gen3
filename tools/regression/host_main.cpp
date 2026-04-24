// host_main.cpp — snapshot regression harness for onspeed_core.
//
// Reads a CSV of sensor samples on stdin, runs each row through the
// extracted core pipeline (AHRS + tone calculator), and writes a CSV
// of outputs on stdout.  Python driver at tools/regression/run_snapshot.py
// feeds this a recorded flight log and diffs the result against a
// golden file.
//
// Input CSV columns (header row required, must match exactly):
//   ias_kt,palt_ft,oat_c,ax,ay,az,gx,gy,gz
//
// Output CSV columns (kOutputHeader below) — extended in PR 3.2 to
// include the AHRS outputs that drive every downstream consumer
// (display serial, log CSV, web liveview, tone calc):
//   ias_kt,palt_ft,oat_c,
//   pitch_deg,roll_deg,flight_path_deg,derived_aoa_deg,
//   tas_mps,kalman_alt_ft,kalman_vsi_fpm,earth_vert_g,
//   tone_freq_hz,tone_level
//
// Per Phase 3.2: this is the load-bearing characterization gate.  The
// extracted Ahrs::Step must produce outputs identical to the legacy
// AHRS::Process for every row, within math.isclose float tolerance
// (rtol=1e-5, atol=1e-4 — see run_snapshot.py).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <ahrs/Ahrs.h>
#include <audio/ToneCalc.h>
#include <sensors/IasAlive.h>
#include <types/AhrsInputs.h>
#include <types/AhrsOutputs.h>

namespace {

// Input and output schemas are kept side-by-side so that adding a column
// to one prompts updating the other.
constexpr char kInputHeader[]  = "ias_kt,palt_ft,oat_c,ax,ay,az,gx,gy,gz";
constexpr char kOutputHeader[] =
    "ias_kt,palt_ft,oat_c,"
    "pitch_deg,roll_deg,flight_path_deg,derived_aoa_deg,"
    "tas_mps,kalman_alt_ft,kalman_vsi_fpm,earth_vert_g,"
    "tone_freq_hz,tone_level";

// Placeholder AOA thresholds (degrees) matching clean-flap setpoints for a
// typical GA aircraft.  These are not part of the AHRS-extraction
// invariant; they're the same constants the harness used pre-PR-3.2 so
// the tone columns continue to mean the same thing.
constexpr onspeed::ToneThresholds kCleanThresholds {
    /* fLDMAXAOA      */ 3.0f,
    /* fONSPEEDFASTAOA*/ 6.5f,
    /* fONSPEEDSLOWAOA*/ 9.5f,
    /* fSTALLWARNAOA  */ 12.5f,
};

// IMU and pressure cadences from the production firmware (HardwareMap.h).
constexpr float kImuRateHz       = 208.0f;
constexpr float kPressureRateHz  = 50.0f;
constexpr float kImuDtSec        = 1.0f / kImuRateHz;
constexpr uint32_t kPressurePeriodUs = 1'000'000u / 50u;   // 20 ms

// Build an AHRS config matching the production defaults: zero biases
// (the recorded log was captured on a level installation), Madgwick
// algorithm, gyro window of 30 samples, IMU at 208 Hz.  These mirror
// the constants in HardwareMap.h and Globals.h.
onspeed::ahrs::AhrsConfig MakeProductionConfig()
{
    onspeed::ahrs::AhrsConfig cfg;
    cfg.pitchBiasDeg          = 0.0f;
    cfg.rollBiasDeg           = 0.0f;
    cfg.algorithm             = onspeed::ahrs::Algorithm::Madgwick;
    cfg.gyroSmoothingWindow   = 30;
    cfg.imuSampleRateHz       = kImuRateHz;
    cfg.pressureSampleRateHz  = kPressureRateHz;
    return cfg;
}

struct InputRow {
    float ias_kt;
    float palt_ft;
    float oat_c;
    float ax, ay, az;     // accelerometer in g
    float gx, gy, gz;     // gyroscope in deg/s
};

bool ParseHeader(const std::string& line)
{
    return line == kInputHeader;
}

bool ParseRow(const std::string& line, InputRow& out)
{
    float* fields[] = {
        &out.ias_kt, &out.palt_ft, &out.oat_c,
        &out.ax, &out.ay, &out.az,
        &out.gx, &out.gy, &out.gz,
    };
    constexpr size_t kExpected = sizeof(fields) / sizeof(fields[0]);

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

// Build the per-frame AhrsInputs from a parsed row.  Each row is treated
// as one IMU frame at 208 Hz; the IAS update timestamp advances every
// 20 ms (50 Hz cadence) by stepping the timestamp in 4-frame increments
// (~208 / 50).  This mirrors the production firmware's pressure-task
// cadence — the AHRS sees a fresh IAS value every ~4 IMU frames.
//
// IMPORTANT — what this harness IS and is NOT:
//
// IS: a bit-stability gate. Reruns of this harness on the same input
//     CSV should produce a byte-identical golden. Future PRs that
//     touch onspeed_core/ahrs/ that change any output field by more
//     than rtol=1e-5 will fail the snapshot check and require either
//     justification or a regenerated golden.
//
// IS NOT: a behavioral-correctness gate vs. legacy AHRS::Process.
//     The recorded `short_replay.csv` is the SD-card CSV log written
//     by production firmware at 50 Hz, not raw 208 Hz IMU data. The
//     harness feeds it as if each row is one 208 Hz IMU frame, so
//     paltFt jumps that span ~20 ms of real time get processed at
//     ~4.8 ms harness dt. Result: Kalman VSI values inflate ~4× and
//     the asin(VSI/TAS) flight-path saturates at ±90°. Many golden
//     values are physically absurd. They are still REPRODUCIBLE,
//     which is what the gate checks.
//
//     A future harness PR could either (a) generate genuine 208 Hz
//     IMU input by holding paltFt across the appropriate number of
//     IMU frames, or (b) record a real 208 Hz IMU stream during a
//     bench session. Either would let the golden contain physically
//     sane values. Out of scope for the AHRS extraction.
//
// `iasUpdateTimestampUs` advances at the production 50 Hz pressure
// cadence so the TAS density correction inside Ahrs::Step is gated
// correctly even though we provide one input row per IMU frame.
onspeed::AhrsInputs BuildInputs(const std::vector<InputRow>& rows,
                                size_t frameIdx,
                                bool oatPresentInLog,
                                bool iasAlive)
{
    onspeed::AhrsInputs in;
    const InputRow& row = rows[frameIdx];
    in.imu.accelXG      = row.ax;
    in.imu.accelYG      = row.ay;
    in.imu.accelZG      = row.az;
    in.imu.gyroRollDps  = row.gx;
    in.imu.gyroPitchDps = row.gy;
    in.imu.gyroYawDps   = row.gz;
    in.imu.tempCelsius  = 25.0f;
    in.imu.timestampUs  = static_cast<uint32_t>(frameIdx
                            * static_cast<uint32_t>(kImuDtSec * 1.0e6f));

    in.sensors.iasKt      = row.ias_kt;
    in.sensors.paltFt     = row.palt_ft;
    in.sensors.oatCelsius = row.oat_c;
    in.sensors.iasAlive   = iasAlive;

    // IAS update timestamp advances every 4 frames (~50 Hz).  This
    // gates the TAS density correction inside Ahrs::Step.
    const uint32_t pressureFrame = static_cast<uint32_t>(frameIdx / 4u) + 1u;
    in.iasUpdateTimestampUs = pressureFrame * kPressurePeriodUs;

    in.useEfisOat     = false;
    in.useInternalOat = oatPresentInLog;
    in.efisOatCelsius = 0.0f;
    return in;
}

// IAS-alive hysteresis is shared with the sketch driver (SensorIO.cpp)
// via onspeed::sensors::UpdateIasAlive — see sensors/IasAlive.h.  Using
// one implementation in both places removes the drift risk this file
// used to warn about ("kept in sync" by hope, not by mechanism).

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

    // Construct the AHRS pipeline once.  All filter state persists for
    // the entire run, mirroring the firmware where the AHRS object
    // lives for the full session.
    onspeed::ahrs::Ahrs ahrs{MakeProductionConfig()};

    // Detect whether the log carries a usable OAT column.  If the entire
    // log has oat_c == 0 (the short_replay.csv case), treat OAT as
    // absent — the firmware would do the same when bOatSensor is false.
    // We can't know this until we read all rows, so we use the first
    // row and trust that the log is consistent.  A non-zero OAT in any
    // row enables density correction.
    bool oatPresentInLog = false;

    // Initialize AHRS from the first row before stepping.  Mirrors the
    // sketch's two-phase Init/Process: the IMU's first valid sample
    // seeds the algorithm's initial pitch/roll.  We can't peek without
    // buffering, so process rows in two passes: first read all rows,
    // then init from row 0 and step through 0..N-1.
    std::vector<InputRow> rows;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        InputRow r{};
        if (!ParseRow(line, r)) {
            std::fprintf(stderr, "host_main: bad row at %zu: %s\n",
                         rows.size(), line.c_str());
            return 1;
        }
        if (r.oat_c != 0.0f) oatPresentInLog = true;
        rows.push_back(r);
    }

    if (rows.empty()) {
        std::fprintf(stderr, "host_main: no data rows\n");
        return 1;
    }

    // Init from the very first row's IMU sample (and that row's Palt).
    // iasAlive starts false (matches SensorIO boot state) and rises once
    // the recorded IAS crosses the 20 kt hysteresis threshold.
    bool iasAlive = false;
    iasAlive = onspeed::sensors::UpdateIasAlive(iasAlive, rows.front().ias_kt);
    onspeed::AhrsInputs seed = BuildInputs(rows, 0, oatPresentInLog, iasAlive);
    ahrs.Init(seed, rows.front().palt_ft);

    for (size_t i = 0; i < rows.size(); ++i) {
        const InputRow& r = rows[i];
        iasAlive = onspeed::sensors::UpdateIasAlive(iasAlive, r.ias_kt);
        const onspeed::AhrsInputs in = BuildInputs(rows, i, oatPresentInLog, iasAlive);
        const onspeed::AhrsOutputs out = ahrs.Step(in, kImuDtSec);

        // Tone decision driven by the AHRS-derived AOA (real signal,
        // not the lift-equation placeholder we used pre-PR-3.2).
        const onspeed::ToneResult result =
            onspeed::calculateTone(out.derivedAoaDeg, kCleanThresholds);

        float tone_freq_hz = 0.0f;
        int   tone_level   = 0;
        switch (result.enTone) {
        case onspeed::EnToneType::None:
            tone_freq_hz = 0.0f; tone_level = 0; break;
        case onspeed::EnToneType::Low:
            tone_freq_hz = result.fPulseFreq; tone_level = 1; break;
        case onspeed::EnToneType::High:
            tone_freq_hz = result.fPulseFreq; tone_level = 2; break;
        }

        std::printf(
            "%.4f,%.4f,%.4f,"
            "%.4f,%.4f,%.4f,%.4f,"
            "%.4f,%.4f,%.4f,%.4f,"
            "%.4f,%d\n",
            r.ias_kt, r.palt_ft, r.oat_c,
            out.pitchDeg, out.rollDeg, out.flightPathDeg, out.derivedAoaDeg,
            out.tasMps, out.kalmanAltFt, out.kalmanVsiFpm, out.earthVertG,
            tone_freq_hz, tone_level);
    }

    std::fprintf(stderr, "host_main: %zu rows processed\n", rows.size());
    return 0;
}
