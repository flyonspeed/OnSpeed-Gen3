// ConfigV1Parse.cpp — see ConfigV1Parse.h for the public interface.
//
// This is a verbatim port of the <CONFIG> branch of FOSConfig::
// LoadConfigFromString() (software/sketch_common/src/config/Config.cpp
// on branch `extraction/phase-3-1-config`, before this task) with the
// following adaptations:
//
//   1. Arduino String -> std::string / std::string_view.
//   2. Arduino atoi/atof -> std::strtol / std::strtof with proper
//      end-pointer checks.
//   3. All platform side effects (g_AudioPlay.SetVolume, g_EfisSerial.enType,
//      g_pIMU->ConfigAxes, g_AHRS.Init, g_Log.println) are removed — the
//      sketch shim does them after this function returns Ok.
//   4. The PStaticBias V1->V2 sign normalisation is preserved verbatim
//      (audit #009) — V1 stored bias for `Pstatic + bias` (addition);
//      Gen3 uses `Pstatic - bias` (subtraction); we negate on load.

#include <config/ConfigV1Parse.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>

#include <util/OnSpeedTypes.h>  // MAX_AOA_CURVES, MAX_CURVE_COEFF

namespace onspeed::config {

// ----------------------------------------------------------------------------
// String helpers — pure C++, no Arduino String.
// ----------------------------------------------------------------------------

namespace {

// Find the inner text of `<TAG>...</TAG>` and return it as a string_view
// into `text`.  Returns empty view if either delimiter is absent.
//
// Mirrors the sketch's GetConfigValue but operates on string_view.  The
// V1 format is flat — no nested tags of the same name — so a simple
// linear find() pair is correct.
std::string_view FindTagValue(std::string_view text, std::string_view tag)
{
    // Build "<TAG>" and "</TAG>" — short stack strings, no allocation
    // pressure even on the embedded target.
    std::string sOpen;
    sOpen.reserve(tag.size() + 2);
    sOpen.push_back('<');
    sOpen.append(tag.data(), tag.size());
    sOpen.push_back('>');

    std::string sClose;
    sClose.reserve(tag.size() + 3);
    sClose.append("</");
    sClose.append(tag.data(), tag.size());
    sClose.push_back('>');

    const std::size_t openPos = text.find(sOpen);
    if (openPos == std::string_view::npos) return {};
    const std::size_t valStart = openPos + sOpen.size();

    const std::size_t closePos = text.find(sClose, valStart);
    if (closePos == std::string_view::npos) return {};

    return text.substr(valStart, closePos - valStart);
}

// V1 ToBoolean — matches sketch behaviour:
//   true iff the integer prefix is 1 OR the trimmed text equals
//   "YES", "ENABLED", or "ON".
bool ToBoolean(std::string_view value)
{
    // Try integer prefix first — matches Arduino String::toInt() semantics
    // of returning 0 for non-numeric.
    if (!value.empty()) {
        // strtol over a NUL-terminated copy (string_view isn't guaranteed
        // to be null-terminated).
        std::string s(value);
        char* end = nullptr;
        long n = std::strtol(s.c_str(), &end, 10);
        if (end != s.c_str() && n == 1)
            return true;
    }
    return value == "YES" || value == "ENABLED" || value == "ON";
}

int ToInt(std::string_view value)
{
    if (value.empty()) return 0;
    std::string s(value);
    char* end = nullptr;
    long n = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return 0;
    return static_cast<int>(n);
}

float ToFloat(std::string_view value)
{
    if (value.empty()) return 0.0f;
    std::string s(value);
    char* end = nullptr;
    float f = std::strtof(s.c_str(), &end);
    if (end == s.c_str()) return 0.0f;
    return f;
}

// Read a comma-separated integer list into a fixed-capacity SuIntArray.
// Stops after MAX_AOA_CURVES values; trailing garbage is ignored.
SuIntArray ParseIntCSV(std::string_view text)
{
    SuIntArray result{};
    result.Count = 0;

    std::size_t pos = 0;
    for (int i = 0; i < onspeed::MAX_AOA_CURVES; ++i) {
        if (pos >= text.size()) break;
        const std::size_t comma = text.find(',', pos);
        if (comma == std::string_view::npos) {
            // Last value
            result.Items[i] = ToInt(text.substr(pos));
            result.Count = i + 1;
            return result;
        }
        result.Items[i] = ToInt(text.substr(pos, comma - pos));
        result.Count = i + 1;
        pos = comma + 1;
    }
    return result;
}

// Read a comma-separated float list, padding with zeros to `limit` entries
// if the source has fewer values.  Mirrors the sketch's ParseFloatCSV.
SuFloatArray ParseFloatCSV(std::string_view text,
                           int limit = onspeed::MAX_AOA_CURVES)
{
    SuFloatArray result{};
    result.Count = 0;

    if (limit > onspeed::MAX_AOA_CURVES)
        limit = onspeed::MAX_AOA_CURVES;

    std::size_t pos = 0;
    for (int i = 0; i < limit; ++i) {
        if (pos >= text.size() || text.substr(pos).empty()) {
            // No more text — match sketch behaviour: pad with zeros.
            result.Items[i] = 0.0f;
            result.Count = i + 1;
            continue;
        }
        const std::size_t comma = text.find(',', pos);
        if (comma == std::string_view::npos) {
            result.Items[i] = ToFloat(text.substr(pos));
            result.Count = i + 1;
            return result;
        }
        result.Items[i] = ToFloat(text.substr(pos, comma - pos));
        result.Count = i + 1;
        pos = comma + 1;
    }
    return result;
}

// Read a curve definition: comma-separated coefficients followed by a
// final integer curve type.
//
// Format:  <coeff0>,<coeff1>,...,<coeffN>,<curveType>
onspeed::SuCalibrationCurve ParseCurveCSV(std::string_view text)
{
    onspeed::SuCalibrationCurve result{};
    result.iCurveType = 0;
    for (int i = 0; i < onspeed::MAX_CURVE_COEFF; ++i)
        result.afCoeff[i] = 0.0f;

    std::size_t pos = 0;
    for (int i = 0; i <= onspeed::MAX_CURVE_COEFF; ++i) {
        if (pos >= text.size()) return result;
        const std::size_t comma = text.find(',', pos);
        if (comma == std::string_view::npos) {
            // Last value is curveType (matches sketch).
            result.iCurveType = static_cast<uint8_t>(ToInt(text.substr(pos)));
            return result;
        }
        // Bounds check: only the first MAX_CURVE_COEFF positions are
        // coefficients.  Anything past that is malformed and ignored —
        // mirrors the sketch's loop-bound behaviour.
        if (i < onspeed::MAX_CURVE_COEFF)
            result.afCoeff[i] = ToFloat(text.substr(pos, comma - pos));
        pos = comma + 1;
    }
    return result;
}

// True iff `s` contains nothing but ASCII whitespace.
bool IsWhitespaceOnly(std::string_view s)
{
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c)))
            return false;
    return true;
}

}  // namespace

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

const char* V1ParseStatusToString(V1ParseStatus status)
{
    switch (status) {
        case V1ParseStatus::Ok:           return "Ok";
        case V1ParseStatus::Empty:        return "Empty";
        case V1ParseStatus::MissingRoot:  return "MissingRoot";
    }
    return "Unknown";
}

bool IsV1Format(std::string_view configText)
{
    // V2 wins if both somehow appear — caller should route to V2.
    if (configText.find("<CONFIG2>") != std::string_view::npos)
        return false;
    return configText.find("<CONFIG>")  != std::string_view::npos
        && configText.find("</CONFIG>") != std::string_view::npos;
}

V1ParseStatus ParseV1(std::string_view text, OnSpeedConfig& cfg)
{
    if (text.empty() || IsWhitespaceOnly(text))
        return V1ParseStatus::Empty;

    if (!IsV1Format(text))
        return V1ParseStatus::MissingRoot;

    // ---------------- Scalar top-level fields -----------------------------

    cfg.iAoaSmoothing      = ToInt(FindTagValue(text, "AOA_SMOOTHING"));
    cfg.iPressureSmoothing = ToInt(FindTagValue(text, "PRESSURE_SMOOTHING"));
    cfg.iMuteAudioUnderIAS = ToInt(FindTagValue(text, "MUTE_AUDIO_UNDER_IAS"));

    {
        std::string sDataSource(FindTagValue(text, "DATASOURCE"));
        cfg.suDataSrc.fromStrSet(sDataSource);
    }

    cfg.sReplayLogFileName = std::string(FindTagValue(text, "REPLAYLOGFILENAME"));

    // ---------------- Flap arrays ----------------------------------------
    //
    // V1 stored each flap-related field as a separate comma-separated tag:
    //   <FLAPDEGREES>0,16,33</FLAPDEGREES>
    //   <SETPOINT_LDMAXAOA>4.1,2.3,-1.12</SETPOINT_LDMAXAOA>
    //   ...
    // and each flap got its own AOA_CURVE_FLAPS<N> tag.

    SuIntArray aiFlapDegrees = ParseIntCSV(FindTagValue(text, "FLAPDEGREES"));
    const int iFlapsArraySize = aiFlapDegrees.Count;

    cfg.aFlaps.clear();
    cfg.aFlaps.resize(static_cast<std::size_t>(iFlapsArraySize));
    for (int i = 0; i < iFlapsArraySize; ++i)
        cfg.aFlaps[i].iDegrees = aiFlapDegrees.Items[i];

    SuIntArray aiPotPos = ParseIntCSV(FindTagValue(text, "FLAPPOTPOSITIONS"));
    for (int i = 0; (i < aiPotPos.Count) && (i < iFlapsArraySize); ++i)
        cfg.aFlaps[i].iPotPosition = aiPotPos.Items[i];

    {
        SuFloatArray af = ParseFloatCSV(FindTagValue(text, "SETPOINT_LDMAXAOA"));
        for (int i = 0; (i < af.Count) && (i < iFlapsArraySize); ++i)
            cfg.aFlaps[i].fLDMAXAOA = af.Items[i];
    }
    {
        SuFloatArray af = ParseFloatCSV(FindTagValue(text, "SETPOINT_ONSPEEDFASTAOA"));
        for (int i = 0; (i < af.Count) && (i < iFlapsArraySize); ++i)
            cfg.aFlaps[i].fONSPEEDFASTAOA = af.Items[i];
    }
    {
        SuFloatArray af = ParseFloatCSV(FindTagValue(text, "SETPOINT_ONSPEEDSLOWAOA"));
        for (int i = 0; (i < af.Count) && (i < iFlapsArraySize); ++i)
            cfg.aFlaps[i].fONSPEEDSLOWAOA = af.Items[i];
    }
    {
        SuFloatArray af = ParseFloatCSV(FindTagValue(text, "SETPOINT_STALLWARNAOA"));
        for (int i = 0; (i < af.Count) && (i < iFlapsArraySize); ++i)
            cfg.aFlaps[i].fSTALLWARNAOA = af.Items[i];
    }
    {
        // STALLAOA may be missing in pre-calibration configs — sketch passes
        // iFlapsArraySize as the limit so the missing positions get padded
        // to zero.  We do the same.
        SuFloatArray af = ParseFloatCSV(FindTagValue(text, "SETPOINT_STALLAOA"),
                                        iFlapsArraySize);
        for (int i = 0; (i < af.Count) && (i < iFlapsArraySize); ++i)
            cfg.aFlaps[i].fSTALLAOA = af.Items[i];
    }
    {
        SuFloatArray af = ParseFloatCSV(FindTagValue(text, "SETPOINT_ALPHA0"),
                                        iFlapsArraySize);
        for (int i = 0; (i < af.Count) && (i < iFlapsArraySize); ++i)
            cfg.aFlaps[i].fAlpha0 = af.Items[i];
    }
    {
        SuFloatArray af = ParseFloatCSV(FindTagValue(text, "SETPOINT_ALPHASTALL"),
                                        iFlapsArraySize);
        for (int i = 0; (i < af.Count) && (i < iFlapsArraySize); ++i)
            cfg.aFlaps[i].fAlphaStall = af.Items[i];
    }

    // Per-flap AOA curve: AOA_CURVE_FLAPS0, AOA_CURVE_FLAPS1, ...
    for (int i = 0; i < iFlapsArraySize; ++i) {
        std::string tag = "AOA_CURVE_FLAPS" + std::to_string(i);
        onspeed::SuCalibrationCurve curve = ParseCurveCSV(FindTagValue(text, tag));
        cfg.aFlaps[i].AoaCurve.iCurveType = curve.iCurveType;
        for (int j = 0; j < onspeed::MAX_CURVE_COEFF; ++j)
            cfg.aFlaps[i].AoaCurve.afCoeff[j] = curve.afCoeff[j];
    }

    // Sort flaps by degrees to match V2 behaviour and downstream expectations.
    std::sort(cfg.aFlaps.begin(), cfg.aFlaps.end(),
              [](const OnSpeedConfig::SuFlaps& a,
                 const OnSpeedConfig::SuFlaps& b) {
                  return a.iDegrees < b.iDegrees;
              });

    // ---------------- Volume --------------------------------------------

    cfg.bVolumeControl    = ToBoolean(FindTagValue(text, "VOLUMECONTROL"));
    cfg.iVolumeHighAnalog = ToInt    (FindTagValue(text, "VOLUME_HIGH_ANALOG"));
    cfg.iVolumeLowAnalog  = ToInt    (FindTagValue(text, "VOLUME_LOW_ANALOG"));
    cfg.iDefaultVolume    = ToInt    (FindTagValue(text, "VOLUME_DEFAULT"));

    cfg.bAudio3D      = ToBoolean(FindTagValue(text, "3DAUDIO"));
    cfg.bOverGWarning = ToBoolean(FindTagValue(text, "OVERGWARNING"));

    // ---------------- CAS curve -----------------------------------------

    cfg.CasCurve         = ParseCurveCSV(FindTagValue(text, "CAS_CURVE"));
    cfg.bCasCurveEnabled = ToBoolean   (FindTagValue(text, "CAS_ENABLED"));

    // ---------------- Orientation / EFIS --------------------------------

    cfg.sPortsOrientation  = std::string(FindTagValue(text, "PORTS_ORIENTATION"));
    cfg.sBoxtopOrientation = std::string(FindTagValue(text, "BOX_TOP_ORIENTATION"));
    cfg.sEfisType          = std::string(FindTagValue(text, "EFISTYPE"));

    // ---------------- Calibration source --------------------------------

    cfg.sCalSource     = std::string(FindTagValue(text, "CALWIZ_SOURCE"));
    cfg.bCalSourceEfis = (cfg.sCalSource == "EFIS");

    // ---------------- Biases --------------------------------------------
    //
    // AUDIT #009: PStaticBias is *negated* on load.  Gen2 V1 configs stored
    // the bias for use as `Pstatic + bias`; Gen3 uses `Pstatic - bias`.
    // Negating here normalises the V1 value to the Gen3/V2 in-memory
    // convention so the same physical bias produces the same sensor
    // behaviour regardless of which config format the user loaded from.
    // test_config_v1::test_v1_v2_pstatic_normalize_to_same_memory pins this
    // — V1 input `+X` and V2 input `-X` both yield the same in-memory value.

    cfg.iPFwdBias    =  ToInt  (FindTagValue(text, "PFWD_BIAS"));
    cfg.iP45Bias     =  ToInt  (FindTagValue(text, "P45_BIAS"));
    cfg.fPStaticBias = -ToFloat(FindTagValue(text, "PSTATIC_BIAS"));
    cfg.fGxBias      =  ToFloat(FindTagValue(text, "GX_BIAS"));
    cfg.fGyBias      =  ToFloat(FindTagValue(text, "GY_BIAS"));
    cfg.fGzBias      =  ToFloat(FindTagValue(text, "GZ_BIAS"));
    cfg.fPitchBias   =  ToFloat(FindTagValue(text, "PITCH_BIAS"));
    cfg.fRollBias    =  ToFloat(FindTagValue(text, "ROLL_BIAS"));

    // ---------------- Serial inputs / outputs ---------------------------

    cfg.bReadBoom     = ToBoolean(FindTagValue(text, "BOOM"));
    cfg.bBoomChecksum = ToBoolean(FindTagValue(text, "BOOMCHECKSUM"));
    cfg.bReadEfisData = ToBoolean(FindTagValue(text, "SERIALEFISDATA"));
    cfg.bOatSensor    = ToBoolean(FindTagValue(text, "OATSENSOR"));

    cfg.sSerialOutFormat  = std::string(FindTagValue(text, "SERIALOUTFORMAT"));
    cfg.enSerialOutFormat = OnSpeedConfig::ParseSerialFmt(cfg.sSerialOutFormat);

    // ---------------- Load limit / Vno / SD logging ---------------------

    cfg.fLoadLimitPositive = ToFloat(FindTagValue(text, "LOADLIMITPOSITIVE"));
    cfg.fLoadLimitNegative = ToFloat(FindTagValue(text, "LOADLIMITNEGATIVE"));

    cfg.iVno              = ToInt  (FindTagValue(text, "VNO"));
    cfg.uVnoChimeInterval = static_cast<unsigned>(
                                 ToInt(FindTagValue(text, "VNO_CHIME_INTERVAL")));
    cfg.bVnoChimeEnabled  = ToBoolean(FindTagValue(text, "VNO_CHIME_ENABLED"));

    cfg.bSdLogging = ToBoolean(FindTagValue(text, "SDLOGGING"));

    return V1ParseStatus::Ok;
}

}  // namespace onspeed::config
