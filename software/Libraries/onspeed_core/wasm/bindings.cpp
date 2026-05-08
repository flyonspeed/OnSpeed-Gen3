// bindings.cpp — Emscripten embind exports for onspeed_core.
//
// Step 0 (PR #462): one export to prove the pipeline (compute_percent_lift).
// Step 1 (this PR): extend with compute_anchors and parse_config.
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

#include <string>
#include <vector>

#include <aoa/DisplayPctAnchors.h>
#include <aoa/PercentLift.h>
#include <config/ConfigV1Parse.h>
#include <config/ConfigXmlParse.h>
#include <config/OnSpeedConfig.h>

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
    out.set("aoaSmoothing",       cfg.iAoaSmoothing);
    out.set("pressureSmoothing",  cfg.iPressureSmoothing);
    out.set("muteUnderIas",       cfg.iMuteAudioUnderIAS);
    out.set("dataSource",         std::string(cfg.suDataSrc.toCStr()));
    out.set("volumeControl",      cfg.bVolumeControl);
    out.set("defaultVolume",      cfg.iDefaultVolume);
    out.set("audio3D",            cfg.bAudio3D);
    out.set("overGWarning",       cfg.bOverGWarning);
    out.set("efisType",           cfg.sEfisType);
    out.set("serialOutFormat",    cfg.sSerialOutFormat);
    out.set("pitchBias",          cfg.fPitchBias);
    out.set("rollBias",           cfg.fRollBias);
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
        flapsArr.set(fi, flap);
    }
    out.set("flaps", flapsArr);

    return out;
}

EMSCRIPTEN_BINDINGS(onspeed_core_module) {
    // Step 0: single export to prove the pipeline.
    function("compute_percent_lift", &compute_percent_lift);

    // Step 1: display anchors and config parsing.
    function("compute_anchors", &compute_anchors);
    function("parse_config",    &parse_config);
}
