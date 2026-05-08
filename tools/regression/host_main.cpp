// host_main.cpp — multi-subcommand algorithm CLI for onspeed_core.
//
// Each subcommand is a thin shell over an onspeed_core function.
// Run `host_main help` for subcommand summaries.
//
// Subcommands
// -----------
//   ahrs_tone  [--input PATH] [--output-format csv|jsonl]
//     Stream simplified sensor CSV (ias_kt,palt_ft,oat_c,ax,ay,az,gx,gy,gz)
//     through the AHRS + Madgwick + Kalman + ToneCalc pipeline.  This is
//     the bedrock regression test (per PLAN_PYTHON_CONSOLIDATION.md line
//     416-418): it gates against fixtures/golden.csv.  `--input -` reads
//     stdin (default).  Output schema: see kAhrsToneOutputHeader (13 fields).
//
//   replay  [--input PATH] [--output-format csv|jsonl] [--log-rate 50|208]
//     Stream an OnSpeed SD log CSV through the LogReplayEngine pipeline.
//     `--input -` reads stdin (default).  Input must be the real SD log
//     format (timeStamp,Pfwd,PfwdSmoothed,...) — not the simplified AHRS
//     fixture format.  BuildHeaderIndex maps columns by name so logs from
//     different firmware versions are accepted.  --config is reserved for
//     Step 2 (per PLAN_PYTHON_CONSOLIDATION.md) and is an error if passed
//     now.  Output schema: see kReplayEngineOutputHeader (23 fields).
//     --log-rate {50|208}: log sample rate in Hz (default 50); rejected if
//     any other value is supplied.
//
//   percent_lift --aoa F --alpha-0 F --alpha-stall F --stallwarn F
//     Compute percent-of-stall (0..99.9) for a single AOA sample.
//     Calls onspeed::aoa::ComputePercentLift with iasValid=true.
//     Outputs a single float followed by newline.
//
//   parse_config --in PATH
//     Parse a V2 (CONFIG2) or V1 (CONFIG) OnSpeed config file.
//     Emits a flat JSON object with the parsed fields, including
//     flapsByDeg as a nested object keyed by flap-degrees integer.
//
//   display_anchors --config PATH --flap I --raw-adc I
//     Compute display percent anchors for the given config, flap detent
//     index, and raw ADC reading.  Emits a JSON object with
//     pipPctLift, tonesOnPctLift, onSpeedFastPctLift,
//     onSpeedSlowPctLift, stallWarnPctLift, flapsDeg.
//
//   build_frame --record JSON
//     Build the 77-byte #1 wire frame for the given DisplayBuildInputs
//     JSON object.  Emits the frame bytes as lowercase hex on stdout.
//
// No external deps.  Arg parsing is a hand-rolled 40-line dispatcher.
// JSON output is hand-rolled printf-based emission.  JSON input
// (build_frame) is a hand-rolled key=value extractor.
//
// Compiles under -Wall -Wextra -Werror -Wshadow -Wformat=2 (native env).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <ahrs/Ahrs.h>
#include <aoa/DisplayPctAnchors.h>
#include <aoa/PercentLift.h>
#include <audio/ToneCalc.h>
#include <config/ConfigV1Parse.h>
#include <config/ConfigXmlParse.h>
#include <config/OnSpeedConfig.h>
#include <proto/DisplaySerial.h>
#include <proto/LogCsvHeaderIndex.h>
#include <replay/LogReplayEngine.h>
#include <sensors/FlapsDetector.h>
#include <sensors/IasAlive.h>
#include <types/AhrsInputs.h>
#include <types/AhrsOutputs.h>
#include <types/LogRow.h>

// ============================================================================
// Minimal arg parser — recognises named flags of the form "--foo VALUE".
// Returns the value for the first occurrence of `flag`, or `default_val`
// if not found.  Argc/argv are passed by reference so callers can walk them.
// ============================================================================

namespace {

// Return the value of `--flag VALUE` from argv[1..argc-1], or default_val.
const char* ArgGet(int argc, const char* const* argv,
                   const char* flag, const char* default_val = nullptr)
{
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], flag) == 0) {
            return argv[i + 1];
        }
    }
    return default_val;
}

// ---------------------------------------------------------------------------
// Minimal JSON emitter — hand-rolled printf-based, no external deps.
// ---------------------------------------------------------------------------

// Print a float in a way that's round-trippable at 4 decimal places.
void JsonFloat(const char* key, float val, bool last = false)
{
    std::printf("  \"%s\": %.4f%s\n", key, static_cast<double>(val), last ? "" : ",");
}

void JsonInt(const char* key, int val, bool last = false)
{
    std::printf("  \"%s\": %d%s\n", key, val, last ? "" : ",");
}

void JsonBool(const char* key, bool val, bool last = false)
{
    std::printf("  \"%s\": %s%s\n", key, val ? "true" : "false", last ? "" : ",");
}

void JsonStr(const char* key, const std::string& val, bool last = false)
{
    // Minimal escaping: backslash and double-quote only.
    std::printf("  \"%s\": \"", key);
    for (char c : val) {
        if      (c == '"')  std::printf("\\\"");
        else if (c == '\\') std::printf("\\\\");
        else                std::putchar(c);
    }
    std::printf("\"%s\n", last ? "" : ",");
}

// ---------------------------------------------------------------------------
// Minimal JSON key-value extractor — parses objects produced by this tool
// and by Python wrappers.  Recognises:
//   { "key": value, ... }
// where value is an unquoted number, "string", or true/false.
// Does NOT handle nested objects or arrays.
// ---------------------------------------------------------------------------

// Find the raw JSON value for `key`.  Returns empty string if not found.
//
// NOTE: This parser is for FLAT objects with NUMERIC or BOOLEAN values only.
// `find("\"" + key + "\"")` does a substring search; if a future caller adds
// string-typed fields to DisplayBuildInputs, the search can match key names
// that appear INSIDE a string value.  DisplayBuildInputs has no string fields
// today and must not gain any without revisiting this parser.  See PR #465
// review (M3) for the full discussion.
std::string JsonGetValue(const std::string& json, const std::string& key)
{
    const std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos += search.size();
    // skip whitespace and colon
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ':'))
        ++pos;
    if (pos >= json.size()) return {};
    if (json[pos] == '"') {
        // quoted string
        ++pos;
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
                result += json[pos];
            } else {
                result += json[pos];
            }
            ++pos;
        }
        return result;
    }
    // unquoted value (number, true, false, null)
    size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n')
        ++end;
    std::string raw = json.substr(pos, end - pos);
    // trim trailing whitespace
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t'))
        raw.pop_back();
    return raw;
}

float JsonGetFloat(const std::string& json, const std::string& key,
                   float default_val = 0.0f)
{
    const std::string v = JsonGetValue(json, key);
    if (v.empty()) return default_val;
    try { return std::stof(v); } catch (...) { return default_val; }
}

int JsonGetInt(const std::string& json, const std::string& key,
               int default_val = 0)
{
    const std::string v = JsonGetValue(json, key);
    if (v.empty()) return default_val;
    try { return std::stoi(v); } catch (...) { return default_val; }
}

bool JsonGetBool(const std::string& json, const std::string& key,
                 bool default_val = false)
{
    const std::string v = JsonGetValue(json, key);
    if (v == "true")  return true;
    if (v == "false") return false;
    return default_val;
}

// ---------------------------------------------------------------------------
// Shared config loader — reads a file and parses V1 or V2 XML.
// Writes errors to stderr; returns false on failure.
// ---------------------------------------------------------------------------

bool LoadConfig(const std::string& path,
                onspeed::config::OnSpeedConfig& cfg)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "host_main: cannot open config '%s'\n", path.c_str());
        return false;
    }
    const std::string xml{std::istreambuf_iterator<char>(f),
                           std::istreambuf_iterator<char>()};

    cfg.LoadDefaults();

    if (onspeed::config::IsV1Format(xml)) {
        const auto st = onspeed::config::ParseV1(xml, cfg);
        if (st != onspeed::config::V1ParseStatus::Ok) {
            std::fprintf(stderr, "host_main: V1 parse error: %s\n",
                         onspeed::config::V1ParseStatusToString(st));
            return false;
        }
    } else {
        const auto st = onspeed::config::ParseXml(xml, cfg);
        if (st != onspeed::config::XmlParseStatus::Ok) {
            std::fprintf(stderr, "host_main: V2 parse error: %s\n",
                         onspeed::config::XmlParseStatusToString(st));
            return false;
        }
    }
    return true;
}

// Output format selector — shared by ahrs_tone and replay subcommands.
enum class OutputFormat { Csv, Jsonl };

// ============================================================================
// AHRS_TONE subcommand — streaming AHRS + ToneCalc pipeline.
//
// Reads a simplified sensor CSV (header + rows). Each row is one IMU sample.
// Writes a CSV or JSONL stream on stdout.
//
// This is the bedrock regression test: PLAN_PYTHON_CONSOLIDATION.md line
// 416-418 explicitly designates it as such, and post-PLAN_WASM_CORE.md
// the same harness runs against the WASM build for the parity check.
//
// Input CSV columns (header required):
//   ias_kt,palt_ft,oat_c,ax,ay,az,gx,gy,gz
//
// Output CSV columns (kAhrsToneOutputHeader):
//   ias_kt,palt_ft,oat_c,
//   pitch_deg,roll_deg,flight_path_deg,derived_aoa_deg,
//   tas_mps,kalman_alt_ft,kalman_vsi_fpm,earth_vert_g,
//   tone_freq_hz,tone_level
// ============================================================================

constexpr char kAhrsToneInputHeader[]  = "ias_kt,palt_ft,oat_c,ax,ay,az,gx,gy,gz";
constexpr char kAhrsToneOutputHeader[] =
    "ias_kt,palt_ft,oat_c,"
    "pitch_deg,roll_deg,flight_path_deg,derived_aoa_deg,"
    "tas_mps,kalman_alt_ft,kalman_vsi_fpm,earth_vert_g,"
    "tone_freq_hz,tone_level";

constexpr onspeed::ToneThresholds kCleanThresholds {
    /* fLDMAXAOA      */ 3.0f,
    /* fONSPEEDFASTAOA*/ 6.5f,
    /* fONSPEEDSLOWAOA*/ 9.5f,
    /* fSTALLWARNAOA  */ 12.5f,
};

constexpr float kImuRateHz       = 208.0f;
constexpr float kPressureRateHz  = 50.0f;
constexpr float kImuDtSec        = 1.0f / kImuRateHz;
constexpr uint32_t kPressurePeriodUs = 1'000'000u / 50u;

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
    float ax, ay, az;
    float gx, gy, gz;
};

bool ParseAhrsToneHeader(const std::string& line)
{
    return line == kAhrsToneInputHeader;
}

bool ParseAhrsToneRow(const std::string& line, InputRow& out)
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
                "host_main ahrs_tone: cannot parse field %zu '%s': %s\n",
                i, tok.c_str(), e.what());
            return false;
        }
    }
    return true;
}

onspeed::AhrsInputs BuildAhrsInputs(const std::vector<InputRow>& rows,
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

    const uint32_t pressureFrame = static_cast<uint32_t>(frameIdx / 4u) + 1u;
    in.iasUpdateTimestampUs = pressureFrame * kPressurePeriodUs;

    in.useEfisOat     = false;
    in.useInternalOat = oatPresentInLog;
    in.efisOatCelsius = 0.0f;
    return in;
}

int CmdAhrsTone(int argc, const char* const* argv)
{
    const char* input_path  = ArgGet(argc, argv, "--input",  "-");

    const char* fmt_str = ArgGet(argc, argv, "--output-format", "csv");

    OutputFormat fmt = OutputFormat::Csv;
    if (std::strcmp(fmt_str, "jsonl") == 0) {
        fmt = OutputFormat::Jsonl;
    } else if (std::strcmp(fmt_str, "csv") != 0) {
        std::fprintf(stderr,
            "host_main ahrs_tone: unknown --output-format '%s' (csv|jsonl)\n",
            fmt_str);
        return 1;
    }

    // Open input — "-" means stdin.
    std::istream* in_stream = &std::cin;
    std::ifstream in_file;
    if (std::strcmp(input_path, "-") != 0) {
        in_file.open(input_path);
        if (!in_file.is_open()) {
            std::fprintf(stderr, "host_main ahrs_tone: cannot open '%s'\n", input_path);
            return 1;
        }
        in_stream = &in_file;
    }

    std::string line;

    if (!std::getline(*in_stream, line) || !ParseAhrsToneHeader(line)) {
        std::fprintf(stderr, "host_main ahrs_tone: bad or missing CSV header\n");
        return 1;
    }

    if (fmt == OutputFormat::Csv) {
        std::printf("%s\n", kAhrsToneOutputHeader);
    }

    onspeed::ahrs::Ahrs ahrs{MakeProductionConfig()};

    bool oatPresentInLog = false;
    std::vector<InputRow> rows;
    while (std::getline(*in_stream, line)) {
        if (line.empty()) continue;
        InputRow r{};
        if (!ParseAhrsToneRow(line, r)) {
            std::fprintf(stderr, "host_main ahrs_tone: bad row at %zu: %s\n",
                         rows.size(), line.c_str());
            return 1;
        }
        if (r.oat_c != 0.0f) oatPresentInLog = true;
        rows.push_back(r);
    }

    if (rows.empty()) {
        std::fprintf(stderr, "host_main ahrs_tone: no data rows\n");
        return 1;
    }

    bool iasAlive = false;
    iasAlive = onspeed::sensors::UpdateIasAlive(iasAlive, rows.front().ias_kt);
    onspeed::AhrsInputs seed = BuildAhrsInputs(rows, 0, oatPresentInLog, iasAlive);
    ahrs.Init(seed, rows.front().palt_ft);

    for (size_t i = 0; i < rows.size(); ++i) {
        const InputRow& r = rows[i];
        iasAlive = onspeed::sensors::UpdateIasAlive(iasAlive, r.ias_kt);
        const onspeed::AhrsInputs in = BuildAhrsInputs(rows, i, oatPresentInLog, iasAlive);
        const onspeed::AhrsOutputs out = ahrs.Step(in, kImuDtSec);

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

        if (fmt == OutputFormat::Csv) {
            std::printf(
                "%.4f,%.4f,%.4f,"
                "%.4f,%.4f,%.4f,%.4f,"
                "%.4f,%.4f,%.4f,%.4f,"
                "%.4f,%d\n",
                r.ias_kt, r.palt_ft, r.oat_c,
                out.pitchDeg, out.rollDeg, out.flightPathDeg, out.derivedAoaDeg,
                out.tasMps, out.kalmanAltFt, out.kalmanVsiFpm, out.earthVertG,
                tone_freq_hz, tone_level);
        } else {
            // JSONL: one JSON object per row
            std::printf("{"
                "\"ias_kt\":%.4f,"
                "\"palt_ft\":%.4f,"
                "\"oat_c\":%.4f,"
                "\"pitch_deg\":%.4f,"
                "\"roll_deg\":%.4f,"
                "\"flight_path_deg\":%.4f,"
                "\"derived_aoa_deg\":%.4f,"
                "\"tas_mps\":%.4f,"
                "\"kalman_alt_ft\":%.4f,"
                "\"kalman_vsi_fpm\":%.4f,"
                "\"earth_vert_g\":%.4f,"
                "\"tone_freq_hz\":%.4f,"
                "\"tone_level\":%d"
                "}\n",
                r.ias_kt, r.palt_ft, r.oat_c,
                out.pitchDeg, out.rollDeg, out.flightPathDeg, out.derivedAoaDeg,
                out.tasMps, out.kalmanAltFt, out.kalmanVsiFpm, out.earthVertG,
                tone_freq_hz, tone_level);
        }
    }

    std::fprintf(stderr, "host_main ahrs_tone: %zu rows processed\n", rows.size());
    return 0;
}

// ============================================================================
// REPLAY subcommand — LogReplayEngine pipeline over a real SD log CSV.
//
// Reads an OnSpeed SD log CSV (the format written by firmware LogSensor).
// The header is parsed via BuildHeaderIndex for name-keyed column mapping,
// so logs from different firmware versions (different column sets) all work.
// Each row is fed to onspeed::replay::LogReplayEngine::step(), which mirrors
// the firmware's ReadLogLine() pipeline: AOA smoothing, flap index lookup,
// pressure-coefficient computation.
//
// Input: real SD log CSV (timeStamp,Pfwd,PfwdSmoothed,...,DerivedAOA,CoeffP)
//   Optional tail column: flapsRawADC (format version 2+)
//   Optional groups: boom columns, EFIS columns (standard or VN-300)
//   BuildHeaderIndex detects all of these from the header line.
//
// Output CSV columns (kReplayEngineOutputHeader), stable order:
//   ias_kt, palt_ft, ias_valid,
//   aoa_deg, coeff_p,
//   flaps_pos, flaps_index, flaps_raw_adc, flaps_raw_adc_present,
//   pitch_deg, roll_deg, flight_path_deg, kalman_vsi_mps,
//   imu_fwd_g, imu_lat_g, imu_vert_g,
//   imu_roll_dps, imu_pitch_dps, imu_yaw_dps,
//   accel_lat_corr, accel_vert_corr,
//   data_mark
//
// These fields map 1:1 to ReplayStepResult members.  No fields added or
// dropped.  Column order is fixed here and in the golden fixture; it is
// the contract Python Step 2 deserialises.
//
// --config is reserved for Step 2 (per PLAN_PYTHON_CONSOLIDATION.md).
// Passing it is an error — refused explicitly rather than silently ignored.
//
// Sample rate: read from the log header if `iLogRate` is present; otherwise
// defaults to 50 Hz (the firmware default for pre-version-2 logs).  Stored
// in the engine for PRs 2/3; not yet used to correct EMA rate.
// ============================================================================

// Output column header — stable order, matches ReplayStepResult field order.
constexpr char kReplayEngineOutputHeader[] =
    "ias_kt,palt_ft,ias_valid,"
    "aoa_deg,coeff_p,"
    "flaps_pos,flaps_index,flaps_raw_adc,flaps_raw_adc_present,"
    "pitch_deg,roll_deg,flight_path_deg,kalman_vsi_mps,"
    "imu_fwd_g,imu_lat_g,imu_vert_g,"
    "imu_roll_dps,imu_pitch_dps,imu_yaw_dps,"
    "accel_lat_corr,accel_vert_corr,"
    "data_mark";

// Emit one ReplayStepResult row in CSV format.
static void EmitCsvRow(const onspeed::replay::ReplayStepResult& r)
{
    std::printf(
        "%.4f,%.4f,%d,"
        "%.4f,%.4f,"
        "%d,%d,%u,%d,"
        "%.4f,%.4f,%.4f,%.6f,"
        "%.4f,%.4f,%.4f,"
        "%.4f,%.4f,%.4f,"
        "%.4f,%.4f,"
        "%d\n",
        static_cast<double>(r.iasKt),
        static_cast<double>(r.paltFt),
        r.iasValid ? 1 : 0,
        static_cast<double>(r.aoa),
        static_cast<double>(r.coeffP),
        r.flapsPos,
        r.flapsIndex,
        static_cast<unsigned>(r.flapsRawAdc),
        r.flapsRawAdcPresent ? 1 : 0,
        static_cast<double>(r.pitchDeg),
        static_cast<double>(r.rollDeg),
        static_cast<double>(r.flightPathDeg),
        static_cast<double>(r.kalmanVSI),
        static_cast<double>(r.imuForwardG),
        static_cast<double>(r.imuLateralG),
        static_cast<double>(r.imuVerticalG),
        static_cast<double>(r.imuRollRateDps),
        static_cast<double>(r.imuPitchRateDps),
        static_cast<double>(r.imuYawRateDps),
        static_cast<double>(r.accelLatCorr),
        static_cast<double>(r.accelVertCorr),
        r.dataMark);
}

// Emit one ReplayStepResult row in JSONL format.
static void EmitJsonlRow(const onspeed::replay::ReplayStepResult& r)
{
    std::printf("{"
        "\"ias_kt\":%.4f,"
        "\"palt_ft\":%.4f,"
        "\"ias_valid\":%s,"
        "\"aoa_deg\":%.4f,"
        "\"coeff_p\":%.4f,"
        "\"flaps_pos\":%d,"
        "\"flaps_index\":%d,"
        "\"flaps_raw_adc\":%u,"
        "\"flaps_raw_adc_present\":%s,"
        "\"pitch_deg\":%.4f,"
        "\"roll_deg\":%.4f,"
        "\"flight_path_deg\":%.4f,"
        "\"kalman_vsi_mps\":%.6f,"
        "\"imu_fwd_g\":%.4f,"
        "\"imu_lat_g\":%.4f,"
        "\"imu_vert_g\":%.4f,"
        "\"imu_roll_dps\":%.4f,"
        "\"imu_pitch_dps\":%.4f,"
        "\"imu_yaw_dps\":%.4f,"
        "\"accel_lat_corr\":%.4f,"
        "\"accel_vert_corr\":%.4f,"
        "\"data_mark\":%d"
        "}\n",
        static_cast<double>(r.iasKt),
        static_cast<double>(r.paltFt),
        r.iasValid ? "true" : "false",
        static_cast<double>(r.aoa),
        static_cast<double>(r.coeffP),
        r.flapsPos,
        r.flapsIndex,
        static_cast<unsigned>(r.flapsRawAdc),
        r.flapsRawAdcPresent ? "true" : "false",
        static_cast<double>(r.pitchDeg),
        static_cast<double>(r.rollDeg),
        static_cast<double>(r.flightPathDeg),
        static_cast<double>(r.kalmanVSI),
        static_cast<double>(r.imuForwardG),
        static_cast<double>(r.imuLateralG),
        static_cast<double>(r.imuVerticalG),
        static_cast<double>(r.imuRollRateDps),
        static_cast<double>(r.imuPitchRateDps),
        static_cast<double>(r.imuYawRateDps),
        static_cast<double>(r.accelLatCorr),
        static_cast<double>(r.accelVertCorr),
        r.dataMark);
}

int CmdReplay(int argc, const char* const* argv)
{
    const char* input_path = ArgGet(argc, argv, "--input", "-");

    // --config is reserved for Step 2 (per-flap threshold wiring).
    // Refuse now rather than silently accept and ignore the value —
    // a caller that passes --config and gets wrong output is harder
    // to debug than an explicit error.
    const char* config_path = ArgGet(argc, argv, "--config");
    if (config_path != nullptr) {
        std::fprintf(stderr,
            "host_main replay: --config is reserved for Step 2 "
            "(per PLAN_PYTHON_CONSOLIDATION.md). Not yet wired to "
            "per-flap thresholds. Refusing to silently ignore.\n");
        return 1;
    }

    const char* fmt_str = ArgGet(argc, argv, "--output-format", "csv");

    OutputFormat fmt = OutputFormat::Csv;
    if (std::strcmp(fmt_str, "jsonl") == 0) {
        fmt = OutputFormat::Jsonl;
    } else if (std::strcmp(fmt_str, "csv") != 0) {
        std::fprintf(stderr,
            "host_main replay: unknown --output-format '%s' (csv|jsonl)\n",
            fmt_str);
        return 1;
    }

    // Open input — "-" means stdin.
    std::istream* in_stream = &std::cin;
    std::ifstream in_file;
    if (std::strcmp(input_path, "-") != 0) {
        in_file.open(input_path);
        if (!in_file.is_open()) {
            std::fprintf(stderr, "host_main replay: cannot open '%s'\n", input_path);
            return 1;
        }
        in_stream = &in_file;
    }

    // Parse the header via BuildHeaderIndex for name-keyed column mapping.
    // This tolerates column reordering, addition, and removal across firmware
    // versions — the same approach used by the firmware's LogReplay task.
    std::string header_line;
    if (!std::getline(*in_stream, header_line)) {
        std::fprintf(stderr, "host_main replay: empty input — no header line\n");
        return 1;
    }

    onspeed::proto::log_csv::HeaderIndex hdr_idx;
    auto warn_sink = [](const char* col) {
        std::fprintf(stderr,
            "host_main replay: header warning: missing column '%s'\n", col);
    };
    if (!onspeed::proto::log_csv::BuildHeaderIndex(header_line, hdr_idx, warn_sink)) {
        std::fprintf(stderr,
            "host_main replay: header index build failed "
            "(too many columns or no recognized OnSpeed columns)\n");
        return 1;
    }

    // Log sample rate: supplied via --log-rate {50|208} (default 50 Hz).
    // 50 Hz is the firmware default; 208 Hz logs are produced when iLogRate
    // is set to 208 in the config.  The engine stores this for PRs 2/3
    // (rate-correct EMA, synth ADC) but does not yet use it to correct EMA.
    // A dedicated log-rate column in the log header would let this be auto-
    // detected; until that lands, the caller must supply --log-rate for 208 Hz
    // logs.
    const char* log_rate_str = ArgGet(argc, argv, "--log-rate");
    int log_sample_rate_hz = 50;
    if (log_rate_str != nullptr) {
        log_sample_rate_hz = std::atoi(log_rate_str);
        if (log_sample_rate_hz != 50 && log_sample_rate_hz != 208) {
            std::fprintf(stderr,
                "host_main replay: --log-rate must be 50 or 208 (got %s)\n",
                log_rate_str);
            return 1;
        }
    }

    // flapsRawAdcAvailable: true when the header carries the optional column.
    const bool flaps_raw_adc_available = (hdr_idx.idxFlapsRawAdc >= 0);

    // Build engine with default config (no --config yet — Step 2).
    // LoadDefaults() installs the explicit "uncalibrated" config: a single
    // flap detent with iAoaSmoothing=20 and ALL polynomial coefficients
    // zeroed.  This means aoa_deg = 0.0 in the output for every row.
    // coeffP, IAS, palt, pitch, roll, and IMU axes still vary realistically
    // from the log data.  To regenerate the replay golden against a real
    // calibration in the future, plumb --config through (deferred per PR
    // #466 review) and ship a representative test config (e.g., RV-10:
    // alpha_0=-3.72, alpha_stall=10.31, real polynomial fit).
    onspeed::config::OnSpeedConfig cfg;
    cfg.LoadDefaults();

    onspeed::replay::LogReplayEngine engine(cfg, log_sample_rate_hz,
                                            flaps_raw_adc_available);

    if (fmt == OutputFormat::Csv) {
        std::printf("%s\n", kReplayEngineOutputHeader);
    }

    size_t row_count = 0;
    std::string line;
    while (std::getline(*in_stream, line)) {
        // Trim trailing CR so the tokenizer sees clean input on both
        // platforms (the firmware writes \r\n on SD; git on macOS may
        // strip \r in checkout, leaving clean \n — handle both).
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;

        onspeed::LogRow row;
        row.flapsRawAdcPresent = flaps_raw_adc_available;
        row.boomEnabled  = hdr_idx.boomEnabled;
        row.efisEnabled  = hdr_idx.efisEnabled;
        row.efisIsVn300  = hdr_idx.efisIsVn300;

        if (!onspeed::proto::log_csv::ParseRowByIndex(line, hdr_idx, row)) {
            std::fprintf(stderr,
                "host_main replay: parse error at row %zu: %s\n",
                row_count, line.c_str());
            return 1;
        }

        const onspeed::replay::ReplayStepResult result = engine.step(row);

        if (fmt == OutputFormat::Csv) {
            EmitCsvRow(result);
        } else {
            EmitJsonlRow(result);
        }
        ++row_count;
    }

    if (row_count == 0) {
        std::fprintf(stderr, "host_main replay: no data rows\n");
        return 1;
    }

    std::fprintf(stderr, "host_main replay: %zu rows processed\n", row_count);
    return 0;
}

// ============================================================================
// PERCENT_LIFT subcommand
// ============================================================================

int CmdPercentLift(int argc, const char* const* argv)
{
    const char* aoa_s      = ArgGet(argc, argv, "--aoa");
    const char* alpha0_s   = ArgGet(argc, argv, "--alpha-0");
    const char* stall_s    = ArgGet(argc, argv, "--alpha-stall");
    const char* sw_s       = ArgGet(argc, argv, "--stallwarn");

    if (!aoa_s || !alpha0_s || !stall_s || !sw_s) {
        std::fprintf(stderr,
            "usage: host_main percent_lift "
            "--aoa F --alpha-0 F --alpha-stall F --stallwarn F\n");
        return 1;
    }

    float aoa        = 0.0f;
    float alpha0     = 0.0f;
    float alphastall = 0.0f;
    float stallwarn  = 0.0f;
    try {
        aoa        = std::stof(aoa_s);
        alpha0     = std::stof(alpha0_s);
        alphastall = std::stof(stall_s);
        stallwarn  = std::stof(sw_s);
    } catch (...) {
        std::fprintf(stderr, "host_main percent_lift: bad float argument\n");
        return 1;
    }

    onspeed::config::OnSpeedConfig::SuFlaps flap;
    flap.fAlpha0      = alpha0;
    flap.fAlphaStall  = alphastall;
    flap.fSTALLWARNAOA = stallwarn;

    const float pct = onspeed::aoa::ComputePercentLift(aoa, flap, /*iasValid=*/true);
    std::printf("%.4f\n", static_cast<double>(pct));
    return 0;
}

// ============================================================================
// PARSE_CONFIG subcommand
// ============================================================================

int CmdParseConfig(int argc, const char* const* argv)
{
    const char* in_path = ArgGet(argc, argv, "--in");
    if (!in_path) {
        std::fprintf(stderr, "usage: host_main parse_config --in PATH\n");
        return 1;
    }

    onspeed::config::OnSpeedConfig cfg;
    if (!LoadConfig(in_path, cfg)) return 1;

    std::printf("{\n");
    JsonInt("aoaSmoothing",      cfg.iAoaSmoothing);
    JsonInt("pressureSmoothing", cfg.iPressureSmoothing);
    JsonInt("muteUnderIas",      cfg.iMuteAudioUnderIAS);
    JsonStr("dataSource",        std::string(cfg.suDataSrc.toCStr()));
    JsonBool("volumeControl",    cfg.bVolumeControl);
    JsonInt("defaultVolume",     cfg.iDefaultVolume);
    JsonBool("audio3D",          cfg.bAudio3D);
    JsonBool("overGWarning",     cfg.bOverGWarning);
    JsonStr("efisType",          cfg.sEfisType);
    JsonStr("serialOutFormat",   cfg.sSerialOutFormat);
    JsonFloat("pitchBias",       cfg.fPitchBias);
    JsonFloat("rollBias",        cfg.fRollBias);
    JsonFloat("gxBias",          cfg.fGxBias);
    JsonFloat("gyBias",          cfg.fGyBias);
    JsonFloat("gzBias",          cfg.fGzBias);
    JsonFloat("pstaticBias",     cfg.fPStaticBias);
    JsonFloat("loadLimitPositive", cfg.fLoadLimitPositive);
    JsonFloat("loadLimitNegative", cfg.fLoadLimitNegative);
    JsonInt("iAhrsAlgorithm",    cfg.iAhrsAlgorithm);
    JsonBool("sdLogging",        cfg.bSdLogging);
    JsonBool("boomConvertData",  cfg.bBoomConvertData);
    JsonInt("logRate",           cfg.iLogRate);
    JsonInt("vno",               cfg.iVno);

    // flapsByDeg — nested object keyed by flap-degrees integer
    std::printf("  \"flapsByDeg\": {\n");
    for (size_t fi = 0; fi < cfg.aFlaps.size(); ++fi) {
        const auto& f = cfg.aFlaps[fi];
        const bool last_flap = (fi + 1 == cfg.aFlaps.size());
        std::printf("    \"%d\": {\n", f.iDegrees);
        std::printf("      \"degrees\": %d,\n", f.iDegrees);
        std::printf("      \"potPosition\": %d,\n", f.iPotPosition);
        std::printf("      \"ldmaxAoa\": %.4f,\n",        static_cast<double>(f.fLDMAXAOA));
        std::printf("      \"onSpeedFastAoa\": %.4f,\n",  static_cast<double>(f.fONSPEEDFASTAOA));
        std::printf("      \"onSpeedSlowAoa\": %.4f,\n",  static_cast<double>(f.fONSPEEDSLOWAOA));
        std::printf("      \"stallWarnAoa\": %.4f,\n",    static_cast<double>(f.fSTALLWARNAOA));
        std::printf("      \"stallAoa\": %.4f,\n",        static_cast<double>(f.fSTALLAOA));
        std::printf("      \"alpha0\": %.4f,\n",          static_cast<double>(f.fAlpha0));
        std::printf("      \"alphaStall\": %.4f,\n",      static_cast<double>(f.fAlphaStall));
        std::printf("      \"kFit\": %.4f\n",             static_cast<double>(f.fKFit));
        std::printf("    }%s\n", last_flap ? "" : ",");
    }
    std::printf("  }\n");
    std::printf("}\n");
    return 0;
}

// ============================================================================
// DISPLAY_ANCHORS subcommand
// ============================================================================

int CmdDisplayAnchors(int argc, const char* const* argv)
{
    const char* cfg_path = ArgGet(argc, argv, "--config");
    const char* flap_s   = ArgGet(argc, argv, "--flap");
    const char* adc_s    = ArgGet(argc, argv, "--raw-adc");

    if (!cfg_path || !flap_s || !adc_s) {
        std::fprintf(stderr,
            "usage: host_main display_anchors "
            "--config PATH --flap I --raw-adc I\n");
        return 1;
    }

    int flap_idx = 0;
    int raw_adc  = 0;
    try {
        flap_idx = std::stoi(flap_s);
        raw_adc  = std::stoi(adc_s);
    } catch (...) {
        std::fprintf(stderr, "host_main display_anchors: bad integer argument\n");
        return 1;
    }

    onspeed::config::OnSpeedConfig cfg;
    if (!LoadConfig(cfg_path, cfg)) return 1;

    if (cfg.aFlaps.empty()) {
        std::fprintf(stderr, "host_main display_anchors: config has no flap entries\n");
        return 1;
    }

    // Clamp flap_idx to valid range.
    if (flap_idx < 0) flap_idx = 0;
    if (static_cast<size_t>(flap_idx) >= cfg.aFlaps.size())
        flap_idx = static_cast<int>(cfg.aFlaps.size()) - 1;

    const onspeed::aoa::DisplayPctAnchors anchors =
        onspeed::aoa::ComputeDisplayPctAnchors(
            static_cast<uint16_t>(raw_adc),
            cfg.aFlaps.data(),
            cfg.aFlaps.size(),
            static_cast<size_t>(flap_idx),
            /*iasValid=*/true);

    std::printf("{\n");
    JsonInt("pipPctLift",         anchors.pipPctLift);
    JsonInt("tonesOnPctLift",     anchors.tonesOnPctLift);
    JsonInt("onSpeedFastPctLift", anchors.onSpeedFastPctLift);
    JsonInt("onSpeedSlowPctLift", anchors.onSpeedSlowPctLift);
    JsonInt("stallWarnPctLift",   anchors.stallWarnPctLift);
    JsonInt("flapsDeg",           anchors.flapsDeg, /*last=*/true);
    std::printf("}\n");
    return 0;
}

// ============================================================================
// BUILD_FRAME subcommand
// ============================================================================

int CmdBuildFrame(int argc, const char* const* argv)
{
    const char* record_s = ArgGet(argc, argv, "--record");
    if (!record_s) {
        std::fprintf(stderr,
            "usage: host_main build_frame --record JSON\n");
        return 1;
    }

    const std::string json(record_s);

    onspeed::proto::DisplayBuildInputs inp;
    inp.pitchDeg           = JsonGetFloat(json, "pitchDeg");
    inp.rollDeg            = JsonGetFloat(json, "rollDeg");
    inp.iasKt              = JsonGetFloat(json, "iasKt");
    inp.iasValid           = JsonGetBool(json, "iasValid", true);
    inp.paltFt             = JsonGetFloat(json, "paltFt");
    inp.turnRateDps        = JsonGetFloat(json, "turnRateDps");
    inp.lateralG           = JsonGetFloat(json, "lateralG");
    inp.verticalGScaled10  = JsonGetFloat(json, "verticalGScaled10");
    inp.percentLiftPct     = JsonGetFloat(json, "percentLiftPct");
    inp.vsiFpm10           = JsonGetInt(json, "vsiFpm10");
    inp.oatC               = JsonGetInt(json, "oatC");
    inp.flightPathDeg      = JsonGetFloat(json, "flightPathDeg");
    inp.flapsDeg           = JsonGetInt(json, "flapsDeg");
    inp.tonesOnPctLift     = JsonGetInt(json, "tonesOnPctLift");
    inp.onSpeedFastPctLift = JsonGetInt(json, "onSpeedFastPctLift");
    inp.onSpeedSlowPctLift = JsonGetInt(json, "onSpeedSlowPctLift");
    inp.stallWarnPctLift   = JsonGetInt(json, "stallWarnPctLift");
    inp.flapsMinDeg        = JsonGetInt(json, "flapsMinDeg");
    inp.flapsMaxDeg        = JsonGetInt(json, "flapsMaxDeg");
    inp.gOnsetRate         = JsonGetFloat(json, "gOnsetRate");
    inp.spinRecoveryCue    = JsonGetInt(json, "spinRecoveryCue");
    inp.dataMark           = JsonGetInt(json, "dataMark");
    inp.pipPctLift         = JsonGetInt(json, "pipPctLift");

    uint8_t buf[onspeed::proto::kDisplayFrameSizeBytes];
    const size_t n = onspeed::proto::BuildDisplayFrame(inp, buf,
                                                       sizeof(buf));
    if (n == 0) {
        std::fprintf(stderr, "host_main build_frame: BuildDisplayFrame failed\n");
        return 1;
    }

    // Emit as lowercase hex, one line, no spaces.
    for (size_t i = 0; i < n; ++i) {
        std::printf("%02x", buf[i]);
    }
    std::printf("\n");
    return 0;
}

// ============================================================================
// HELP subcommand
// ============================================================================

int CmdHelp()
{
    std::printf(
        "Usage: host_main <subcommand> [flags]\n\n"
        "Subcommands:\n"
        "  ahrs_tone  --input PATH|'-' [--output-format csv|jsonl]\n"
        "    Stream simplified sensor CSV (ias_kt,palt_ft,oat_c,ax,ay,az,gx,gy,gz)\n"
        "    through AHRS + Madgwick + Kalman + ToneCalc pipeline.\n"
        "    Gates against fixtures/golden.csv — the bedrock regression test.\n\n"
        "  replay  --input PATH|'-' [--output-format csv|jsonl] [--log-rate 50|208]\n"
        "    Stream an OnSpeed SD log CSV through LogReplayEngine.\n"
        "    Input: real SD log format (timeStamp,Pfwd,...,DerivedAOA,CoeffP).\n"
        "    --log-rate: log sample rate in Hz (50 or 208; default 50).\n"
        "    (--config is reserved for Step 2; passing it now is an error.)\n\n"
        "  percent_lift --aoa F --alpha-0 F --alpha-stall F --stallwarn F\n"
        "    Compute percent-of-stall for a single AOA reading.\n\n"
        "  parse_config --in PATH\n"
        "    Parse a V1/V2 OnSpeed config and emit JSON.\n\n"
        "  display_anchors --config PATH --flap I --raw-adc I\n"
        "    Compute display percent anchors for a given flap and pot position.\n\n"
        "  build_frame --record JSON\n"
        "    Build a 77-byte #1 wire frame; emit as hex.\n\n"
        "  help\n"
        "    Show this message.\n"
    );
    return 0;
}

}  // namespace

// ============================================================================
// main — dispatch table
// ============================================================================

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::fprintf(stderr,
            "host_main: subcommand required. Run 'host_main help' for usage.\n");
        return 1;
    }

    const char* sub = argv[1];

    // Pass the full argc/argv so each subcommand sees its own flags.
    if (std::strcmp(sub, "ahrs_tone")       == 0) return CmdAhrsTone(argc, argv);
    if (std::strcmp(sub, "replay")          == 0) return CmdReplay(argc, argv);
    if (std::strcmp(sub, "percent_lift")    == 0) return CmdPercentLift(argc, argv);
    if (std::strcmp(sub, "parse_config")    == 0) return CmdParseConfig(argc, argv);
    if (std::strcmp(sub, "display_anchors") == 0) return CmdDisplayAnchors(argc, argv);
    if (std::strcmp(sub, "build_frame")     == 0) return CmdBuildFrame(argc, argv);
    if (std::strcmp(sub, "help")            == 0) return CmdHelp();

    std::fprintf(stderr,
        "host_main: unknown subcommand '%s'. Run 'host_main help'.\n", sub);
    return 1;
}
