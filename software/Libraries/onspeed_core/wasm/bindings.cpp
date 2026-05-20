// bindings.cpp — Emscripten embind exports for onspeed_core.
//
// Step 0 (PR #462): one export to prove the pipeline (compute_percent_lift).
// Step 1 (PR #467): extend with compute_anchors and parse_config.
// Step 2 (this PR): extend with LogReplayEngineHandle for streaming replay.
//
// Each exported function is a thin C-shaped wrapper that constructs
// whatever struct the C++ API requires, calls it, and returns a value
// that Emscripten can copy across the WASM boundary.  Structured return
// values use emscripten::val so JS receives a plain object without
// needing embind class<> registrations.
//
// Build: included by wasm/build_wasm.sh via the SOURCES list.
// The --bind flag enables EMSCRIPTEN_BINDINGS.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <aoa/DisplayPctAnchors.h>
#include <aoa/PercentLift.h>
#include <audio/ToneCalc.h>
#include <config/ConfigV1Parse.h>
#include <config/ConfigXmlParse.h>
#include <config/OnSpeedConfig.h>
#include <proto/DisplaySerial.h>
#include <replay/LogReplayEngine.h>
#include <replay/LogReplayTask.h>
#include <types/LogRow.h>

using namespace emscripten;
using namespace onspeed;
using namespace onspeed::config;
using namespace onspeed::aoa;

// ---------------------------------------------------------------------------
// compute_percent_lift (from Step 0)
//
// Maps a body-angle reading (aoaDeg) to percent-of-stall using the
// honest single-linear normalization from onspeed_core/aoa/PercentLift.cpp.
//
// Parameters:
//   aoaDeg      — body angle in degrees (DerivedAOA from the IMU/pressure
//                 fusion, same units as the firmware's fLDMAXAOA etc.)
//   alpha_0     — per-flap zero-lift body angle (typically negative, e.g. -3.7)
//                 stored as fAlpha0 in OnSpeedConfig::SuFlaps
//   alpha_stall — per-flap stall body angle (positive, e.g. 10.3)
//                 stored as fAlphaStall in OnSpeedConfig::SuFlaps
//   stallwarn   — per-flap stall-warning body angle setpoint (fSTALLWARNAOA)
//                 used by the defensive ceiling when alpha_stall is uncalibrated
//   ias_valid   — true when IAS is above the audio mute floor; false on the
//                 ground or at very low speed.  Returns 0.0 when false.
//
// Returns: percent-of-stall in [0.0, 99.9].  See PercentLift.h for the
// full contract including the alpha_0 floor and uncalibrated-stall fallback.
// ---------------------------------------------------------------------------
static float compute_percent_lift(
    float aoaDeg,
    float alpha_0,
    float alpha_stall,
    float stallwarn,
    bool  ias_valid)
{
    OnSpeedConfig::SuFlaps flapCfg;
    flapCfg.fAlpha0      = alpha_0;
    flapCfg.fAlphaStall  = alpha_stall;
    flapCfg.fSTALLWARNAOA = stallwarn;
    // Other SuFlaps fields default to 0.0 from the constructor and are
    // not consulted by ComputePercentLift.

    return ComputePercentLift(aoaDeg, flapCfg, ias_valid);
}

// ---------------------------------------------------------------------------
// compute_anchors (Step 1)
//
// Computes the six display percent anchors (pipPctLift, tonesOnPctLift,
// onSpeedFastPctLift, onSpeedSlowPctLift, stallWarnPctLift, flapsDeg)
// for a given flap configuration, active flap index, and raw ADC reading.
//
// Parameters (JS-side convention mirrors the Replay tool's existing usage):
//   flap_setpoints  — JS object with per-detent AOA setpoints for the ACTIVE
//                     detent (used to fill SuFlaps for the snapped band edges).
//                     Keys: ldmaxAoa, onSpeedFastAoa, onSpeedSlowAoa,
//                           stallWarnAoa, alpha0, alphaStall, degrees,
//                           potPosition.
//   all_flaps       — JS Array of flap objects (same shape as flap_setpoints),
//                     one per configured detent, ordered clean→deployed.
//                     Used for the interpolated pip and flapsDeg.
//   active_index    — integer index of the active (snapped) flap detent.
//   raw_adc         — raw lever-pot ADC reading (uint16_t range 0..4095).
//
// Returns: JS object with integer fields:
//   { pipPctLift, tonesOnPctLift, onSpeedFastPctLift,
//     onSpeedSlowPctLift, stallWarnPctLift, flapsDeg }
//
// Design choice: val-based loose interop matches how the Replay tool
// currently passes config data — plain JS objects, no class registration.
// ---------------------------------------------------------------------------
static val compute_anchors(
    val all_flaps,
    int active_index,
    int raw_adc)
{
    // Build the flapEntries array from the JS all_flaps array.
    const int entry_count = all_flaps["length"].as<int>();
    std::vector<OnSpeedConfig::SuFlaps> entries;
    entries.resize(static_cast<size_t>(entry_count));

    for (int i = 0; i < entry_count; ++i) {
        val f = all_flaps[i];
        OnSpeedConfig::SuFlaps& s = entries[static_cast<size_t>(i)];
        s.iDegrees        = f["degrees"].as<int>();
        s.iPotPosition    = f["potPosition"].as<int>();
        s.fLDMAXAOA       = f["ldmaxAoa"].as<float>();
        s.fONSPEEDFASTAOA = f["onSpeedFastAoa"].as<float>();
        s.fONSPEEDSLOWAOA = f["onSpeedSlowAoa"].as<float>();
        s.fSTALLWARNAOA   = f["stallWarnAoa"].as<float>();
        s.fAlpha0         = f["alpha0"].as<float>();
        s.fAlphaStall     = f["alphaStall"].as<float>();
    }

    // Validate active_index. Negative or out-of-range values are programmer
    // errors — a JS caller passing flaps.indexOf(notFound) // -1 should
    // surface as an exception, not silently produce clean-detent anchors.
    // The empty-array case is legitimate (uncalibrated); skip the check then.
    if (!entries.empty() &&
        (active_index < 0 ||
         static_cast<size_t>(active_index) >= entries.size())) {
        throw std::out_of_range(
            "compute_anchors: active_index " +
            std::to_string(active_index) + " out of range [0, " +
            std::to_string(entries.size()) + ")");
    }
    size_t activeIdx = entries.empty() ? 0 : static_cast<size_t>(active_index);

    // The anchor computation always uses iasValid=true: indexer geometry
    // must stay stable across the audio mute threshold; anchors don't gate
    // on live-AOA validity.  Mirrors DisplaySerial.cpp and DataServer.cpp.
    const DisplayPctAnchors anchors = ComputeDisplayPctAnchors(
        static_cast<uint16_t>(raw_adc),
        entries.empty() ? nullptr : entries.data(),
        entries.size(),
        activeIdx,
        /*iasValid=*/true);

    val out = val::object();
    out.set("pipPctLift",         anchors.pipPctLift);
    out.set("tonesOnPctLift",     anchors.tonesOnPctLift);
    out.set("onSpeedFastPctLift", anchors.onSpeedFastPctLift);
    out.set("onSpeedSlowPctLift", anchors.onSpeedSlowPctLift);
    out.set("stallWarnPctLift",   anchors.stallWarnPctLift);
    out.set("flapsDeg",           anchors.flapsDeg);
    return out;
}

// ---------------------------------------------------------------------------
// parse_config (Step 1)
//
// Parses a V1 (Gen2-era <CONFIG>) or V2 (<CONFIG2>) OnSpeed config XML
// string and returns a JS object with the parsed fields.
//
// The V1 XML preprocessing trick (<3DAUDIO> → <_3DAUDIO>) is handled
// inside ConfigV1Parse.cpp, which uses FindTagValue to locate tags by
// string search — the digit-prefixed tags that break browsers' DOMParser
// are handled transparently because tinyxml2 (used for V2) and the
// V1 string-search parser both tolerate them.  The JS-side regex rewrite
// is therefore NOT needed here; the C++ handles it correctly.
//
// Returns: JS object with scalar fields and a `flaps` array, each element
// being a JS object with the per-detent setpoints.  Field names mirror
// the host_main parse_config JSON output and the Replay tool's config.js
// conventions.
//
// On parse error, returns { error: "description" }.
// ---------------------------------------------------------------------------
static val parse_config(const std::string& xml_text)
{
    OnSpeedConfig cfg;
    cfg.LoadDefaults();

    if (IsV1Format(xml_text)) {
        const auto st = ParseV1(xml_text, cfg);
        if (st != V1ParseStatus::Ok) {
            val err = val::object();
            err.set("error", std::string("V1 parse error: ") + V1ParseStatusToString(st));
            return err;
        }
    } else {
        const auto st = ParseXml(xml_text, cfg);
        if (st != XmlParseStatus::Ok) {
            val err = val::object();
            err.set("error", std::string("V2 parse error: ") + XmlParseStatusToString(st));
            return err;
        }
    }

    // Build the JS result object — field names match host_main's JSON
    // output and the Replay tool's existing config.js conventions.
    val out = val::object();
    out.set("aoaSmoothing",          cfg.iAoaSmoothing);
    out.set("pressureSmoothing",     cfg.iPressureSmoothing);
    out.set("muteUnderIas",          cfg.iMuteAudioUnderIAS);
    out.set("iasDisplayThresholdKt", cfg.iIasDisplayThresholdKt);
    out.set("dataSource",         std::string(cfg.suDataSrc.toCStr()));
    out.set("volumeControl",      cfg.bVolumeControl);
    out.set("defaultVolume",      cfg.iDefaultVolume);
    out.set("audio3D",            cfg.bAudio3D);
    out.set("overGWarning",       cfg.bOverGWarning);
    out.set("efisType",           cfg.sEfisType);
    out.set("serialOutFormat",    cfg.sSerialOutFormat);
    out.set("pitchBias",          cfg.fPitchBias);
    out.set("rollBias",           cfg.fRollBias);
    out.set("oatRecoveryFactor",  cfg.fOatRecoveryFactor);
    out.set("gxBias",             cfg.fGxBias);
    out.set("gyBias",             cfg.fGyBias);
    out.set("gzBias",             cfg.fGzBias);
    out.set("pstaticBias",        cfg.fPStaticBias);
    out.set("loadLimitPositive",  cfg.fLoadLimitPositive);
    out.set("loadLimitNegative",  cfg.fLoadLimitNegative);
    out.set("iAhrsAlgorithm",     cfg.iAhrsAlgorithm);
    out.set("sdLogging",          cfg.bSdLogging);
    out.set("boomConvertData",    cfg.bBoomConvertData);
    out.set("logRate",            cfg.iLogRate);
    out.set("vno",                cfg.iVno);
    out.set("acGrossWeight",      cfg.iAcGrossWeight);
    out.set("acBestGlideIAS",     cfg.fAcBestGlideIAS);
    out.set("acVfe",              cfg.fAcVfe);

    // flaps — array of per-detent objects, ordered as stored (clean→deployed
    // by convention).  Field names match the aoaconfig JSON mock and the
    // Replay tool's config.js flapsByDeg conventions.
    val flapsArr = val::array();
    for (size_t fi = 0; fi < cfg.aFlaps.size(); ++fi) {
        const auto& f = cfg.aFlaps[fi];
        val flap = val::object();
        flap.set("degrees",       f.iDegrees);
        flap.set("potPosition",   f.iPotPosition);
        flap.set("ldmaxAoa",      f.fLDMAXAOA);
        flap.set("onSpeedFastAoa",f.fONSPEEDFASTAOA);
        flap.set("onSpeedSlowAoa",f.fONSPEEDSLOWAOA);
        flap.set("stallWarnAoa",  f.fSTALLWARNAOA);
        flap.set("stallAoa",      f.fSTALLAOA);
        flap.set("manAoa",        f.fMANAOA);
        flap.set("alpha0",        f.fAlpha0);
        flap.set("alphaStall",    f.fAlphaStall);
        flap.set("kFit",          f.fKFit);
        // AOA polynomial curve. Without it, ConfigFromVal restores the
        // SuFlaps default (all-zero coefficients) and CurveCalc returns
        // the constant term (0) for every input, so engine AOA reads as
        // 0 regardless of pressures.
        val aoaCurve = val::object();
        aoaCurve.set("type", static_cast<int>(f.AoaCurve.iCurveType));
        val aoaCoeffs = val::array();
        for (size_t k = 0; k < onspeed::MAX_CURVE_COEFF; ++k) {
            aoaCoeffs.set(static_cast<int>(k), f.AoaCurve.afCoeff[k]);
        }
        aoaCurve.set("coeff", aoaCoeffs);
        flap.set("aoaCurve",      aoaCurve);
        flapsArr.set(fi, flap);
    }
    out.set("flaps", flapsArr);

    return out;
}

// ---------------------------------------------------------------------------
// LogReplayEngineHandle (Step 2)
//
// Wraps onspeed::replay::LogReplayEngine for JS callers.  JS holds an
// opaque object; embind manages its lifetime — calling `engine.delete()`
// on the JS side invokes the C++ destructor and frees heap memory.
//
// Ownership of cfg_ is internal: we take a copy of the parsed config
// passed in so the JS caller doesn't need to keep the config object alive.
//
// Constructor parameters (JS side):
//   cfgVal              — JS object returned by parse_config().  Each field
//                         is read out via val::as<T>() into OnSpeedConfig.
//                         The flaps array is read element-by-element.
//   logSampleRateHz     — 50 or 208.  Passed through to RateAdjustedAccelEma
//                         and used to size the synth circular buffer
//                         (synthHalfWindowTicks_ = kSynthHalfWindowSec × hz).
//   flapsRawAdcAvailable— true when the log's flapsRawADC column is present.
//                         When false, step() buffers rows and returns null
//                         during the lag period; flush() drains the tail.
//
// step(rowVal):
//   Takes a plain JS object with the log row fields.
//   Reads the relevant fields into a LogRow and calls engine_.step().
//
//   When flapsRawAdcAvailable is true: always returns a JS result object.
//
//   When flapsRawAdcAvailable is false: returns JS null for the first
//   synthHalfWindowTicks_ rows (the circular buffer is filling); thereafter
//   returns the synth result for the row synthHalfWindowTicks_ ticks in the
//   past.  JS callers must skip null returns and call flush() after the
//   input stream ends.
//
// flush():
//   When flapsRawAdcAvailable is false, drains the remaining buffered tail
//   rows and returns them as a JS Array of result objects.  Call once after
//   the last step() to collect the rows held in the half-window delay.
//   When flapsRawAdcAvailable is true, returns an empty Array.
//
// reset():
//   Forwards to engine_.reset().  Call between independent replay sessions
//   to avoid stale EMA state from a prior log contaminating the first rows.
// ---------------------------------------------------------------------------

// Fill an OnSpeedConfig from a JS parse_config result object.
// Mirrors the field names defined in parse_config() above.
static OnSpeedConfig ConfigFromVal(val cfgVal)
{
    OnSpeedConfig cfg;
    cfg.LoadDefaults();

    // Scalar fields — each fallsback to the default if missing.
    auto getInt = [&](const char* key, int def) -> int {
        val v = cfgVal[key];
        return (v.typeOf().as<std::string>() == "number") ? v.as<int>() : def;
    };
    auto getFloat = [&](const char* key, float def) -> float {
        val v = cfgVal[key];
        return (v.typeOf().as<std::string>() == "number") ? v.as<float>() : def;
    };

    auto getBool = [&](const char* key, bool def) -> bool {
        val v = cfgVal[key];
        return (v.typeOf().as<std::string>() == "boolean") ? v.as<bool>() : def;
    };
    auto getString = [&](const char* key, const std::string& def) -> std::string {
        val v = cfgVal[key];
        return (v.typeOf().as<std::string>() == "string") ? v.as<std::string>() : def;
    };

    cfg.iAoaSmoothing          = getInt("aoaSmoothing",          cfg.iAoaSmoothing);
    cfg.iPressureSmoothing     = getInt("pressureSmoothing",     cfg.iPressureSmoothing);
    cfg.iMuteAudioUnderIAS     = getInt("muteUnderIas",          cfg.iMuteAudioUnderIAS);
    cfg.iIasDisplayThresholdKt = getInt("iasDisplayThresholdKt", cfg.iIasDisplayThresholdKt);
    cfg.fPitchBias          = getFloat("pitchBias",         cfg.fPitchBias);
    cfg.fRollBias           = getFloat("rollBias",          cfg.fRollBias);
    {
        float fRf = getFloat("oatRecoveryFactor", cfg.fOatRecoveryFactor);
        if (fRf >= 0.0f && fRf <= 1.0f) {
            cfg.fOatRecoveryFactor = fRf;
        }
    }
    cfg.fGxBias             = getFloat("gxBias",            cfg.fGxBias);
    cfg.fGyBias             = getFloat("gyBias",            cfg.fGyBias);
    cfg.fGzBias             = getFloat("gzBias",            cfg.fGzBias);
    cfg.fPStaticBias        = getFloat("pstaticBias",       cfg.fPStaticBias);
    cfg.fLoadLimitPositive  = getFloat("loadLimitPositive", cfg.fLoadLimitPositive);
    cfg.fLoadLimitNegative  = getFloat("loadLimitNegative", cfg.fLoadLimitNegative);
    cfg.fAcBestGlideIAS     = getFloat("acBestGlideIAS",   cfg.fAcBestGlideIAS);
    cfg.fAcVfe              = getFloat("acVfe",             cfg.fAcVfe);
    cfg.iLogRate            = getInt  ("logRate",           cfg.iLogRate);
    cfg.iAhrsAlgorithm      = getInt  ("iAhrsAlgorithm",   cfg.iAhrsAlgorithm);
    cfg.iDefaultVolume      = getInt  ("defaultVolume",     cfg.iDefaultVolume);
    cfg.iVno                = getInt  ("vno",               cfg.iVno);
    cfg.iAcGrossWeight      = getInt  ("acGrossWeight",     cfg.iAcGrossWeight);
    cfg.bVolumeControl      = getBool ("volumeControl",     cfg.bVolumeControl);
    cfg.bAudio3D            = getBool ("audio3D",           cfg.bAudio3D);
    cfg.bOverGWarning       = getBool ("overGWarning",      cfg.bOverGWarning);
    cfg.bSdLogging          = getBool ("sdLogging",         cfg.bSdLogging);
    cfg.bBoomConvertData    = getBool ("boomConvertData",   cfg.bBoomConvertData);
    cfg.sEfisType           = getString("efisType",         cfg.sEfisType);
    cfg.sSerialOutFormat    = getString("serialOutFormat",  cfg.sSerialOutFormat);
    {
        val v = cfgVal["dataSource"];
        if (v.typeOf().as<std::string>() == "string")
            cfg.suDataSrc.fromStrSet(v.as<std::string>());
    }

    // Flaps array.
    val flapsArr = cfgVal["flaps"];
    if (flapsArr.typeOf().as<std::string>() == "object" &&
        !flapsArr.isNull() && !flapsArr.isUndefined())
    {
        const int n = flapsArr["length"].as<int>();
        cfg.aFlaps.clear();
        cfg.aFlaps.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            val f = flapsArr[i];
            OnSpeedConfig::SuFlaps& s = cfg.aFlaps[static_cast<size_t>(i)];
            s.iDegrees        = f["degrees"].as<int>();
            s.iPotPosition    = f["potPosition"].as<int>();
            s.fLDMAXAOA       = f["ldmaxAoa"].as<float>();
            s.fONSPEEDFASTAOA = f["onSpeedFastAoa"].as<float>();
            s.fONSPEEDSLOWAOA = f["onSpeedSlowAoa"].as<float>();
            s.fSTALLWARNAOA   = f["stallWarnAoa"].as<float>();
            s.fAlpha0         = f["alpha0"].as<float>();
            s.fAlphaStall     = f["alphaStall"].as<float>();

            val kFitVal = f["kFit"];
            if (!kFitVal.isUndefined() && !kFitVal.isNull()) {
                s.fKFit = kFitVal.as<float>();
            }

            // Required for non-zero engine AOA. SuFlaps' default ctor
            // leaves AoaCurve coefficients all-zero, so a missing
            // aoaCurve here silently zeros AOA across the engine.
            val curveVal = f["aoaCurve"];
            if (!curveVal.isUndefined() && !curveVal.isNull()) {
                val typeVal = curveVal["type"];
                if (!typeVal.isUndefined() && !typeVal.isNull()) {
                    s.AoaCurve.iCurveType =
                        static_cast<uint8_t>(typeVal.as<int>());
                }
                val coeffVal = curveVal["coeff"];
                if (!coeffVal.isUndefined() && !coeffVal.isNull()) {
                    const int n = coeffVal["length"].as<int>();
                    for (int k = 0;
                         k < n && k < static_cast<int>(onspeed::MAX_CURVE_COEFF);
                         ++k)
                    {
                        s.AoaCurve.afCoeff[k] = coeffVal[k].as<float>();
                    }
                }
            }
        }
    }

    return cfg;
}

// Build a JS result object from a ReplayStepResult.
// Field names mirror the host_main CSV output so JS callers can
// share the same field-access code regardless of whether results
// come from WASM or a parsed host_main CSV.
static val StepResultToVal(const onspeed::replay::ReplayStepResult& r)
{
    val out = val::object();

    out.set("iasKt",              r.iasKt);
    out.set("paltFt",             r.paltFt);
    out.set("iasValid",           r.iasValid);
    out.set("aoaDeg",             r.aoa);
    out.set("coeffP",             r.coeffP);
    out.set("flapsPos",           r.flapsPos);
    out.set("flapsIndex",         r.flapsIndex);
    out.set("flapsRawAdc",        static_cast<int>(r.flapsRawAdc));
    out.set("flapsRawAdcPresent", r.flapsRawAdcPresent);
    out.set("pitchDeg",           r.pitchDeg);
    out.set("rollDeg",            r.rollDeg);
    out.set("flightPathDeg",      r.flightPathDeg);
    out.set("kalmanVsiMps",       r.kalmanVSI);
    out.set("imuForwardG",        r.imuForwardG);
    out.set("imuLateralG",        r.imuLateralG);
    out.set("imuVerticalG",       r.imuVerticalG);
    out.set("imuRollRateDps",     r.imuRollRateDps);
    out.set("imuPitchRateDps",    r.imuPitchRateDps);
    out.set("imuYawRateDps",      r.imuYawRateDps);
    out.set("accelLatSmoothed",   r.accelLatSmoothed);
    out.set("accelVertSmoothed",  r.accelVertSmoothed);
    out.set("accelFwdSmoothed",   r.accelFwdSmoothed);
    out.set("gOnsetRate",         r.gOnsetRate);
    out.set("turnRateDps",        r.turnRateDps);
    out.set("oatC",               r.oatC);
    out.set("dataMark",           r.dataMark);

    return out;
}

// Translate a JS row object into a LogRow. Shared by LogReplayEngineHandle
// and LogReplayTaskHandle so both bindings see the same field shape; a
// future field added to the LogRow contract changes one place.
static onspeed::LogRow LogRowFromVal(val rowVal)
{
    onspeed::LogRow row;

    // Pressure fields.
    row.pfwdSmoothed = rowVal["pfwdSmoothed"].as<float>();
    row.p45Smoothed  = rowVal["p45Smoothed"].as<float>();
    row.pStaticMbar  = rowVal["pStaticMbar"].as<float>();
    row.paltFt       = rowVal["paltFt"].as<float>();
    row.iasKt        = rowVal["iasKt"].as<float>();

    // IAS validity. LogReplayTaskHandle ignores this and re-derives it
    // hysteretically; LogReplayEngineHandle propagates it directly.
    row.iasValid     = rowVal["iasValid"].as<bool>();

    // Flap state.
    row.flapsPos             = rowVal["flapsPos"].as<int>();
    val rawAdc = rowVal["flapsRawAdc"];
    val rawAdcPresent = rowVal["flapsRawAdcPresent"];
    row.flapsRawAdcPresent   = !rawAdcPresent.isUndefined() && !rawAdcPresent.isNull()
                               && rawAdcPresent.as<bool>();
    if (row.flapsRawAdcPresent && !rawAdc.isUndefined() && !rawAdc.isNull())
        row.flapsRawAdc = static_cast<uint16_t>(rawAdc.as<int>());

    // IMU axes.
    row.imuVerticalG    = rowVal["imuVerticalG"].as<float>();
    row.imuLateralG     = rowVal["imuLateralG"].as<float>();
    row.imuForwardG     = rowVal["imuForwardG"].as<float>();
    row.imuRollRateDps  = rowVal["imuRollRateDps"].as<float>();
    row.imuPitchRateDps = rowVal["imuPitchRateDps"].as<float>();
    row.imuYawRateDps   = rowVal["imuYawRateDps"].as<float>();

    // AHRS.
    row.pitchDeg      = rowVal["pitchDeg"].as<float>();
    row.rollDeg       = rowVal["rollDeg"].as<float>();
    row.flightPathDeg = rowVal["flightPathDeg"].as<float>();
    row.vsiFpm        = rowVal["vsiFpm"].as<float>();

    // OAT — optional; defaults to 0 (no-OAT-sensor convention).
    {
        val v = rowVal["oatCelsius"];
        row.oatCelsius = (v.typeOf().as<std::string>() == "number")
                         ? v.as<float>() : 0.0f;
    }

    // Data mark.
    row.dataMark = rowVal["dataMark"].as<int>();

    return row;
}

class LogReplayEngineHandle {
public:
    LogReplayEngineHandle(val cfgVal, int logSampleRateHz, bool flapsRawAdcAvailable)
        : cfg_(ConfigFromVal(cfgVal))
        , engine_(cfg_, logSampleRateHz, flapsRawAdcAvailable)
    {}

    // Process one log row.  rowVal is a plain JS object with fields matching
    // the SD log CSV columns used by the replay pipeline.  Returns a plain JS
    // object with all ReplayStepResult fields.
    val step(val rowVal)
    {
        const onspeed::LogRow row = LogRowFromVal(rowVal);
        std::optional<onspeed::replay::ReplayStepResult> result = engine_.step(row);
        if (!result.has_value()) return val::null();
        return StepResultToVal(*result);
    }

    // Drain remaining buffered tail rows after the input stream ends.
    // When flapsRawAdcAvailable is false, returns up to synthHalfWindowTicks_
    // results for the final rows held in the lag buffer.
    // When flapsRawAdcAvailable is true, returns an empty Array.
    val flush()
    {
        std::vector<onspeed::replay::ReplayStepResult> tail = engine_.flush();
        val arr = val::array();
        for (size_t i = 0; i < tail.size(); ++i) {
            arr.set(i, StepResultToVal(tail[i]));
        }
        return arr;
    }

    void reset() { engine_.reset(); }

private:
    // cfg_ must outlive engine_ because engine_ holds a const reference.
    OnSpeedConfig cfg_;
    onspeed::replay::LogReplayEngine engine_;
};

// ---------------------------------------------------------------------------
// LogReplayTaskHandle (issue #514)
//
// Wraps onspeed::replay::LogReplayTask, the C++-side single-source
// pipeline that takes a LogRow and produces 77-byte wire bytes ready
// for the M5 firmware sim. Eliminates the JS-side rowObjAt /
// buildDisplayInputs hand-derivations: the JS layer becomes pure glue
// (file pickers, time control, render).
//
// Public surface mirrors LogReplayEngineHandle for symmetry:
//   new Module.LogReplayTask(cfgVal, logSampleRateHz, flapsRawAdcAvailable)
//   task.processRow(rowVal)  -> Uint8Array (77 bytes) or zero-length
//                                array during synth-path lag
//   task.flush()             -> Array<Uint8Array> tail
//   task.reset()
//   task.delete()
// ---------------------------------------------------------------------------
class LogReplayTaskHandle {
public:
    LogReplayTaskHandle(val cfgVal, int logSampleRateHz, bool flapsRawAdcAvailable)
        : task_(ConfigFromVal(cfgVal), logSampleRateHz, flapsRawAdcAvailable)
    {}

    // Process one row and return the wire bytes. Empty Uint8Array on
    // synth-path lag (engine returns nullopt for the first
    // kSynthHalfWindowTicks rows on pre-flapsRawADC logs).
    val processRow(val rowVal)
    {
        const onspeed::LogRow row = LogRowFromVal(rowVal);
        const std::vector<uint8_t> bytes = task_.processRow(row);
        val Uint8Array = val::global("Uint8Array");
        if (bytes.empty()) {
            return Uint8Array.new_(0);
        }
        return Uint8Array.new_(typed_memory_view(bytes.size(), bytes.data()));
    }

    // Drain trailing rows held in the synth circular buffer. Returns a
    // JS Array of Uint8Array frames in arrival order. Empty array on
    // modern logs (flapsRawAdcAvailable=true).
    val flush()
    {
        const std::vector<std::vector<uint8_t>> tail = task_.flush();
        val Uint8Array = val::global("Uint8Array");
        val arr = val::array();
        for (size_t i = 0; i < tail.size(); ++i) {
            arr.set(i, Uint8Array.new_(typed_memory_view(
                tail[i].size(), tail[i].data())));
        }
        return arr;
    }

    void reset() { task_.reset(); }

    // Diagnostic accessor: return the engine's most recent
    // ReplayStepResult as a plain JS object. Used by ?debug=1 in the
    // replay tool to compare the C++ engine's aoa to the JS engine's
    // aoaDeg side by side.
    val lastStep() const {
        return StepResultToVal(task_.lastStep());
    }

    // Diagnostic: cfg.aFlaps[*].iDegrees in storage order, as a JS
    // Array<number>. Used to verify the cfg round-trip preserved
    // flap detent ordering.
    val cfgFlapsDegrees() const {
        const std::vector<int> v = task_.cfgFlapsDegrees();
        val arr = val::array();
        for (size_t i = 0; i < v.size(); ++i) arr.set(i, v[i]);
        return arr;
    }

private:
    onspeed::replay::LogReplayTask task_;
};

// ---------------------------------------------------------------------------
// build_display_frame
//
// Encodes a `DisplayBuildInputs`-shaped JS object into the 77-byte v4.23
// `#1` display-serial wire frame. Returns the bytes as a JS Uint8Array.
//
// This is the single source of truth for the wire format on the JS side:
// callers (the M5-replay-WASM Node test, future browser bridges) get the
// exact bytes the firmware would emit for the same inputs, so any future
// wire-format change shows up everywhere at once. No JS hand-port.
//
// Field names match `onspeed_core/proto/DisplaySerial.h::DisplayBuildInputs`.
// Missing fields default to zero (matching the C++ in-class initializers),
// matching the convention used by parse_config / step.
// ---------------------------------------------------------------------------
static val build_display_frame(val inputsVal)
{
    auto getFloat = [&](const char* key, float def) -> float {
        val v = inputsVal[key];
        return (v.typeOf().as<std::string>() == "number") ? v.as<float>() : def;
    };
    auto getInt = [&](const char* key, int def) -> int {
        val v = inputsVal[key];
        return (v.typeOf().as<std::string>() == "number") ? v.as<int>() : def;
    };
    auto getBool = [&](const char* key, bool def) -> bool {
        val v = inputsVal[key];
        return (v.typeOf().as<std::string>() == "boolean") ? v.as<bool>() : def;
    };

    onspeed::proto::DisplayBuildInputs in;
    in.pitchDeg           = getFloat("pitchDeg",           0.0f);
    in.rollDeg            = getFloat("rollDeg",            0.0f);
    in.iasKt              = getFloat("iasKt",              0.0f);
    in.iasValid           = getBool ("iasValid",           true);
    in.paltFt             = getFloat("paltFt",             0.0f);
    in.turnRateDps        = getFloat("turnRateDps",        0.0f);
    in.lateralG           = getFloat("lateralG",           0.0f);
    // Wire field is verticalG × 10 already rounded; JS callers pass the
    // raw G value via "verticalG" and we apply the ×10 rounding here so
    // the JS shape matches the firmware's input convention (raw G, not
    // pre-scaled). Mirrors X-Plane plugin's DataRefAdapter.cpp:
    // `in.verticalGScaled10 = std::round(vG * 10.0f);`
    in.verticalGScaled10  = std::round(getFloat("verticalG", 0.0f) * 10.0f);
    in.percentLiftPct     = getFloat("percentLiftPct",     0.0f);
    // vsiFpm10 is the wire's pre-divided value (vsi_fpm/10). JS passes
    // raw fpm via "vsiFpm" and we divide+floor here, mirroring the firmware
    // producer in DisplaySerial.cpp:
    //   inputs.vsiFpm10 = ClampInt((int)floorf(mps2fpm(KalmanVSI)/10.0f), ...);
    in.vsiFpm10 = static_cast<int>(std::floor(getFloat("vsiFpm", 0.0f) / 10.0f));
    in.oatC               = getInt  ("oatC",               0);
    in.flightPathDeg      = getFloat("flightPathDeg",      0.0f);
    in.flapsDeg           = getInt  ("flapsDeg",           0);
    in.tonesOnPctLift     = getInt  ("tonesOnPctLift",     0);
    in.onSpeedFastPctLift = getInt  ("onSpeedFastPctLift", 0);
    in.onSpeedSlowPctLift = getInt  ("onSpeedSlowPctLift", 0);
    in.stallWarnPctLift   = getInt  ("stallWarnPctLift",   0);
    in.flapsMinDeg        = getInt  ("flapsMinDeg",        0);
    in.flapsMaxDeg        = getInt  ("flapsMaxDeg",        0);
    in.gOnsetRate         = getFloat("gOnsetRate",         0.0f);
    in.spinRecoveryCue    = getInt  ("spinRecoveryCue",    0);
    in.dataMark           = getInt  ("dataMark",           0);
    in.pipPctLift         = getInt  ("pipPctLift",         0);

    uint8_t buf[onspeed::proto::kDisplayFrameSizeBytes];
    const size_t n = onspeed::proto::BuildDisplayFrame(in, buf, sizeof(buf));
    if (n != onspeed::proto::kDisplayFrameSizeBytes) {
        throw std::runtime_error(
            "build_display_frame: BuildDisplayFrame returned " +
            std::to_string(n) + " (expected " +
            std::to_string(onspeed::proto::kDisplayFrameSizeBytes) + ")");
    }

    // Build a JS Uint8Array by constructing it from a typed_memory_view
    // copy. Constructing `new Uint8Array(memoryView)` on the JS side
    // copies the bytes into a fresh JS-owned ArrayBuffer, so the local
    // stack-allocated `buf` going out of scope after this function
    // returns is safe — the JS caller holds independent storage.
    val Uint8Array = val::global("Uint8Array");
    return Uint8Array.new_(typed_memory_view(n, buf));
}

// ---------------------------------------------------------------------------
// parse_display_frame
//
// Inverse of build_display_frame. Decodes a 77-byte v4.23 #1 frame
// back into the DisplayFrame struct as a plain JS object. Returns
// null on malformed bytes (wrong length, wrong magic, bad CRC,
// wrong terminator).
//
// Used by the replay tool's diagnostic mode to verify wire-encoded
// values match what the engine and task pipelines intended to emit.
// ---------------------------------------------------------------------------
static val parse_display_frame(val bytesVal)
{
    // Accept Uint8Array or Array of bytes.
    const int n = bytesVal["length"].as<int>();
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        buf[static_cast<size_t>(i)] =
            static_cast<uint8_t>(bytesVal[i].as<int>());
    }
    auto opt = onspeed::proto::ParseDisplayFrame(buf.data(), buf.size());
    if (!opt.has_value()) return val::null();
    const onspeed::proto::DisplayFrame& f = opt.value();

    val out = val::object();
    out.set("pitchDeg",            f.pitchDeg);
    out.set("rollDeg",             f.rollDeg);
    out.set("iasKt",               f.iasKt);
    out.set("iasIsValid",          f.iasIsValid);
    out.set("paltFt",              f.paltFt);
    out.set("turnRateDps",         f.turnRateDps);
    out.set("lateralG",            f.lateralG);
    out.set("verticalG",           f.verticalG);
    out.set("percentLiftPct",      f.percentLiftPct);
    out.set("vsiFpm",              f.vsiFpm);
    out.set("oatC",                f.oatC);
    out.set("flightPathDeg",       f.flightPathDeg);
    out.set("flapsDeg",            f.flapsDeg);
    out.set("tonesOnPctLift",      f.tonesOnPctLift);
    out.set("onSpeedFastPctLift",  f.onSpeedFastPctLift);
    out.set("onSpeedSlowPctLift",  f.onSpeedSlowPctLift);
    out.set("stallWarnPctLift",    f.stallWarnPctLift);
    out.set("flapsMinDeg",         f.flapsMinDeg);
    out.set("flapsMaxDeg",         f.flapsMaxDeg);
    out.set("gOnsetRate",          f.gOnsetRate);
    out.set("spinRecoveryCue",     f.spinRecoveryCue);
    out.set("dataMark",            f.dataMark);
    out.set("pipPctLift",          f.pipPctLift);
    return out;
}

// ---------------------------------------------------------------------------
// tone_calc / tone_calc_muted (PR 1.5)
//
// Pure tone-decision logic from `onspeed_core/audio/ToneCalc.{h,cpp}` —
// the same code the firmware runs to decide what tone the pilot hears.
// Inputs: current AOA (degrees) + the four per-flap thresholds. Returns
// `{ enTone: 'None'|'Low'|'High', pulseFreq, volumeMult }`.
//
// Unblocks docs-site tone-sim replacement (#509) and a future audio-
// synthesis PR. Audio synthesis itself is NOT in scope here — only the
// tone-decision binding.
// ---------------------------------------------------------------------------

static const char* ToneTypeStr(EnToneType t)
{
    switch (t) {
        case EnToneType::None: return "None";
        case EnToneType::Low:  return "Low";
        case EnToneType::High: return "High";
    }
    __builtin_unreachable();
}

static val tone_calc(float aoaDeg,
                     float ldmaxAoa,
                     float fastAoa,
                     float slowAoa,
                     float stallWarnAoa)
{
    ToneThresholds th{ldmaxAoa, fastAoa, slowAoa, stallWarnAoa};
    ToneResult r = calculateTone(aoaDeg, th);
    val out = val::object();
    out.set("enTone",     std::string(ToneTypeStr(r.enTone)));
    out.set("pulseFreq",  r.fPulseFreq);
    out.set("volumeMult", r.fVolumeMult);
    return out;
}

static val tone_calc_muted(float aoaDeg,
                           float iasKt,
                           float stallWarnAoa,
                           int   muteUnderIas)
{
    ToneResult r = calculateToneMuted(aoaDeg, iasKt, stallWarnAoa, muteUnderIas);
    val out = val::object();
    out.set("enTone",     std::string(ToneTypeStr(r.enTone)));
    out.set("pulseFreq",  r.fPulseFreq);
    out.set("volumeMult", r.fVolumeMult);
    return out;
}

EMSCRIPTEN_BINDINGS(onspeed_core_module) {
    // Step 0: single export to prove the pipeline.
    function("compute_percent_lift", &compute_percent_lift);

    // Step 1: display anchors and config parsing.
    function("compute_anchors", &compute_anchors);
    function("parse_config",    &parse_config);

    // Step 2: streaming log-replay engine.
    class_<LogReplayEngineHandle>("LogReplayEngine")
        .constructor<val, int, bool>()
        .function("step",  &LogReplayEngineHandle::step)
        .function("flush", &LogReplayEngineHandle::flush)
        .function("reset", &LogReplayEngineHandle::reset);

    // Issue #514: single-source CSV-row → wire-bytes pipeline. Wraps
    // the engine + iasAlive hysteresis + DisplayBuildInputs fill +
    // BuildDisplayFrame in one C++ entry point so the JS replay tool
    // gets bit-identical wire output to the firmware path.
    class_<LogReplayTaskHandle>("LogReplayTask")
        .constructor<val, int, bool>()
        .function("processRow",        &LogReplayTaskHandle::processRow)
        .function("flush",             &LogReplayTaskHandle::flush)
        .function("reset",             &LogReplayTaskHandle::reset)
        .function("lastStep",          &LogReplayTaskHandle::lastStep)
        .function("cfgFlapsDegrees",   &LogReplayTaskHandle::cfgFlapsDegrees);

    // Bulldog round-1 fix C1: expose the canonical wire-frame builder so
    // the M5-replay-WASM Node test can drive frames without a JS hand-port.
    function("build_display_frame", &build_display_frame);

    // Inverse of build_display_frame, for the replay tool's diagnostic
    // mode. Returns the parsed DisplayFrame as a JS object, or null on
    // malformed input.
    function("parse_display_frame", &parse_display_frame);

    // PR 1.5: expose ToneCalc decision logic so JS callers (docs-site
    // tone-sim, future audio-synthesis PR) drive the same tone decisions
    // the firmware does.  Pure passthrough — no drift seam.
    function("tone_calc",       &tone_calc);
    function("tone_calc_muted", &tone_calc_muted);
}
