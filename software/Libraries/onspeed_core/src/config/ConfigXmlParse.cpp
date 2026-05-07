// ConfigXmlParse.cpp — see ConfigXmlParse.h for the public interface.
//
// This is a verbatim port of the <CONFIG2> branch of FOSConfig::
// LoadConfigFromString() (software/sketch_common/src/config/Config.cpp
// on branch `extraction/phase-3-1-config`, before this task) with three
// differences:
//
//   1. Arduino String -> std::string for cached string fields.
//   2. All platform side effects (g_AudioPlay.SetVolume, g_EfisSerial.enType,
//      g_pIMU->ConfigAxes, g_AHRS.Init, g_Log.println) are removed — the
//      sketch shim does them after this function returns Ok.
//   3. The flap loop is bounded to onspeed::MAX_AOA_CURVES (audit #013) —
//      extra FLAP_POSITION entries cause a TooManyFlaps return but the
//      first MAX_AOA_CURVES entries are still loaded.

#include <config/ConfigXmlParse.h>

#include <algorithm>
#include <cstddef>
#include <string>

#include <tinyxml2.h>

#include <util/OnSpeedTypes.h>  // MAX_AOA_CURVES, MAX_CURVE_COEFF

namespace onspeed::config {

using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;
using tinyxml2::XMLNode;
using tinyxml2::XML_SUCCESS;

// ----------------------------------------------------------------------------
// Per-element readers.  Each returns true iff the tag was found and parsed
// successfully; the caller leaves `value` unchanged on false.
// ----------------------------------------------------------------------------

static bool GetInt(const XMLElement* root, const char* name, int& value)
{
    if (root == nullptr) return false;
    const XMLElement* el = root->FirstChildElement(name);
    if (el == nullptr) return false;
    int tmp;
    if (el->QueryIntText(&tmp) != XML_SUCCESS) return false;
    value = tmp;
    return true;
}

// Overload so unsigned fields (uVnoChimeInterval) don't lose sign info.
// Matches the original sketch semantics — the old macro read into `int
// iTemp` then assigned to the unsigned field, so values above INT_MAX were
// already lost.  We preserve that behaviour by using QueryUnsignedText().
static bool GetUnsigned(const XMLElement* root, const char* name, unsigned& value)
{
    if (root == nullptr) return false;
    const XMLElement* el = root->FirstChildElement(name);
    if (el == nullptr) return false;
    unsigned tmp;
    if (el->QueryUnsignedText(&tmp) != XML_SUCCESS) return false;
    value = tmp;
    return true;
}

static bool GetFloat(const XMLElement* root, const char* name, float& value)
{
    if (root == nullptr) return false;
    const XMLElement* el = root->FirstChildElement(name);
    if (el == nullptr) return false;
    float tmp;
    if (el->QueryFloatText(&tmp) != XML_SUCCESS) return false;
    value = tmp;
    return true;
}

static bool GetBool(const XMLElement* root, const char* name, bool& value)
{
    if (root == nullptr) return false;
    const XMLElement* el = root->FirstChildElement(name);
    if (el == nullptr) return false;
    bool tmp;
    if (el->QueryBoolText(&tmp) != XML_SUCCESS) return false;
    value = tmp;
    return true;
}

static bool GetString(const XMLElement* root, const char* name, std::string& value)
{
    if (root == nullptr) return false;
    const XMLElement* el = root->FirstChildElement(name);
    if (el == nullptr) return false;
    const char* txt = el->GetText();
    if (txt == nullptr) {
        // Empty/null text node — leave `value` untouched. Matches the
        // historical XML_GET_STR macro behavior so a malformed config with
        // an empty tag (e.g. <EFISTYPE></EFISTYPE>) does not wipe the
        // in-memory value that LoadDefaults() set. Wiping would silently
        // disable EFIS, corrupt IMU axis orientation, and drop the
        // bCalSourceEfis cache, since ApplyPostParseSideEffects falls
        // through every branch when these strings are "".
        return false;
    }
    value = txt;
    return true;
}

// ----------------------------------------------------------------------------
// ParseXml — see header.
// ----------------------------------------------------------------------------

const char* XmlParseStatusToString(XmlParseStatus status)
{
    switch (status) {
        case XmlParseStatus::Ok:            return "Ok";
        case XmlParseStatus::MissingRoot:   return "MissingRoot";
        case XmlParseStatus::Malformed:     return "Malformed";
        case XmlParseStatus::TooManyFlaps:  return "TooManyFlaps";
    }
    __builtin_unreachable();
}

XmlParseStatus ParseXml(std::string_view xml, OnSpeedConfig& cfg)
{
    XMLDocument doc;
    // tinyxml2 only reads up to length when we pass it; no need for NUL
    // termination.  `Parse` returns XML_SUCCESS on a well-formed document.
    tinyxml2::XMLError err = doc.Parse(xml.data(), xml.size());
    if (err != XML_SUCCESS)
        return XmlParseStatus::Malformed;

    XMLElement* root = doc.FirstChildElement("CONFIG2");
    if (root == nullptr) {
        // V1 (<CONFIG>) is handled by a separate parser (Task 3).  We
        // reject non-CONFIG2 documents here so the sketch shim can fall
        // back to the V1 path.
        return XmlParseStatus::MissingRoot;
    }

    // ---------------- Scalar top-level fields ----------------------------

    GetInt(root, "AOA_SMOOTHING",      cfg.iAoaSmoothing);
    GetInt(root, "PRESSURE_SMOOTHING", cfg.iPressureSmoothing);

    std::string sDataSource;
    if (GetString(root, "DATASOURCE", sDataSource))
        cfg.suDataSrc.fromStrSet(sDataSource);

    GetString(root, "REPLAYLOGFILENAME", cfg.sReplayLogFileName);

    // ---------------- <FLAP_POSITION> array ------------------------------
    //
    // AUDIT #013: bound the loop to MAX_AOA_CURVES.  A malformed upload
    // with dozens of FLAP_POSITION entries used to walk into unbounded
    // emit territory on save; we truncate here and signal TooManyFlaps.

    int flapIdx = 0;
    bool tooManyFlaps = false;
    XMLElement* pFlaps = root->FirstChildElement("FLAP_POSITION");

    // Clear prior flaps only if the document contains any FLAP_POSITION
    // — matches old behaviour: a config with no flap tags leaves whatever
    // the caller's prior aFlaps value was.
    if (pFlaps != nullptr)
        cfg.aFlaps.clear();

    while (pFlaps != nullptr) {
        if (flapIdx >= onspeed::MAX_AOA_CURVES) {
            tooManyFlaps = true;
            break;
        }

        OnSpeedConfig::SuFlaps f;
        GetInt  (pFlaps, "DEGREES",        f.iDegrees);
        GetInt  (pFlaps, "POT_VALUE",      f.iPotPosition);
        GetFloat(pFlaps, "LDMAXAOA",       f.fLDMAXAOA);
        GetFloat(pFlaps, "ONSPEEDFASTAOA", f.fONSPEEDFASTAOA);
        GetFloat(pFlaps, "ONSPEEDSLOWAOA", f.fONSPEEDSLOWAOA);
        GetFloat(pFlaps, "STALLWARNAOA",   f.fSTALLWARNAOA);
        GetFloat(pFlaps, "STALLAOA",       f.fSTALLAOA);
        GetFloat(pFlaps, "MANAOA",         f.fMANAOA);
        GetFloat(pFlaps, "ALPHA0",         f.fAlpha0);
        GetFloat(pFlaps, "ALPHASTALL",     f.fAlphaStall);
        GetFloat(pFlaps, "KFIT",           f.fKFit);

        XMLElement* pCurve = pFlaps->FirstChildElement("AOA_CURVE");
        if (pCurve != nullptr) {
            int curveType = f.AoaCurve.iCurveType;
            if (GetInt(pCurve, "TYPE", curveType))
                f.AoaCurve.iCurveType = static_cast<uint8_t>(curveType);
            GetFloat(pCurve, "X3", f.AoaCurve.afCoeff[0]);
            GetFloat(pCurve, "X2", f.AoaCurve.afCoeff[1]);
            GetFloat(pCurve, "X1", f.AoaCurve.afCoeff[2]);
            GetFloat(pCurve, "X0", f.AoaCurve.afCoeff[3]);
        }

        cfg.aFlaps.push_back(f);
        ++flapIdx;
        pFlaps = pFlaps->NextSiblingElement("FLAP_POSITION");
    }

    // Sort flaps data array by flap degrees — matches old behaviour.
    std::sort(cfg.aFlaps.begin(), cfg.aFlaps.end(),
              [](const OnSpeedConfig::SuFlaps& a,
                 const OnSpeedConfig::SuFlaps& b) {
                  return a.iDegrees < b.iDegrees;
              });

    // ---------------- <VOLUME> -------------------------------------------

    XMLElement* pVol = root->FirstChildElement("VOLUME");
    if (pVol != nullptr) {
        GetBool(pVol, "ENABLED",        cfg.bVolumeControl);
        GetInt (pVol, "HIGH_ANALOG",    cfg.iVolumeHighAnalog);
        GetInt (pVol, "LOW_ANALOG",     cfg.iVolumeLowAnalog);
        GetInt (pVol, "DEFAULT",        cfg.iDefaultVolume);
        GetBool(pVol, "ENABLE_3DAUDIO", cfg.bAudio3D);
        GetInt (pVol, "MUTE_UNDER_IAS", cfg.iMuteAudioUnderIAS);
    }

    GetBool(root, "OVERGWARNING", cfg.bOverGWarning);

    // ---------------- <CAS_CURVE> ----------------------------------------

    XMLElement* pCas = root->FirstChildElement("CAS_CURVE");
    if (pCas != nullptr) {
        int curveType = cfg.CasCurve.iCurveType;
        if (GetInt(pCas, "TYPE", curveType))
            cfg.CasCurve.iCurveType = static_cast<uint8_t>(curveType);
        GetFloat(pCas, "X3",      cfg.CasCurve.afCoeff[0]);
        GetFloat(pCas, "X2",      cfg.CasCurve.afCoeff[1]);
        GetFloat(pCas, "X1",      cfg.CasCurve.afCoeff[2]);
        GetFloat(pCas, "X0",      cfg.CasCurve.afCoeff[3]);
        GetBool (pCas, "ENABLED", cfg.bCasCurveEnabled);
    }

    // ---------------- <ORIENTATION> --------------------------------------

    XMLElement* pOrient = root->FirstChildElement("ORIENTATION");
    if (pOrient != nullptr) {
        GetString(pOrient, "PORTS",   cfg.sPortsOrientation);
        GetString(pOrient, "BOX_TOP", cfg.sBoxtopOrientation);
    }

    // ---------------- Serial / boom / EFIS toggles -----------------------

    GetBool  (root, "BOOM",            cfg.bReadBoom);
    GetBool  (root, "BOOMCHECKSUM",    cfg.bBoomChecksum);
    GetBool  (root, "BOOMCONVERTDATA", cfg.bBoomConvertData);
    GetBool  (root, "SERIALEFISDATA",  cfg.bReadEfisData);
    GetString(root, "EFISTYPE",        cfg.sEfisType);
    GetBool  (root, "OATSENSOR",       cfg.bOatSensor);

    GetString(root, "SERIALOUTFORMAT", cfg.sSerialOutFormat);
    cfg.enSerialOutFormat = OnSpeedConfig::ParseSerialFmt(cfg.sSerialOutFormat);

    GetString(root, "CALWIZ_SOURCE", cfg.sCalSource);
    cfg.bCalSourceEfis = (cfg.sCalSource == "EFIS");

    // ---------------- <BIAS> ---------------------------------------------
    //
    // AUDIT #009: V2 <PSTATIC> is assigned verbatim.  The V1 CSV path
    // negates on load because the legacy convention was the opposite sign;
    // test_config_xml::test_pstaticbias_sign_convention_preserved pins
    // this V2 behaviour so future refactors can't silently flip the sign.

    XMLElement* pBias = root->FirstChildElement("BIAS");
    if (pBias != nullptr) {
        GetInt  (pBias, "PFWD",    cfg.iPFwdBias);
        GetInt  (pBias, "P45",     cfg.iP45Bias);
        GetFloat(pBias, "PSTATIC", cfg.fPStaticBias);
        GetFloat(pBias, "GX",      cfg.fGxBias);
        GetFloat(pBias, "GY",      cfg.fGyBias);
        GetFloat(pBias, "GZ",      cfg.fGzBias);
        GetFloat(pBias, "PITCH",   cfg.fPitchBias);
        GetFloat(pBias, "ROLL",    cfg.fRollBias);
    }

    // AHRS algorithm — scalar top-level.
    GetInt(root, "AHRS_ALGORITHM", cfg.iAhrsAlgorithm);

    // ---------------- <LOAD_LIMIT> ---------------------------------------

    XMLElement* pLoad = root->FirstChildElement("LOAD_LIMIT");
    if (pLoad != nullptr) {
        GetFloat(pLoad, "POSITIVE",                cfg.fLoadLimitPositive);
        GetFloat(pLoad, "NEGATIVE",                cfg.fLoadLimitNegative);
        GetFloat(pLoad, "ASYMMETRIC_GYRO_LIMIT",   cfg.fAsymmetricGyroLimit);
        GetFloat(pLoad, "ASYMMETRIC_REDUCTION",    cfg.fAsymmetricReduction);
    }

    // ---------------- <VNO> ----------------------------------------------

    XMLElement* pVno = root->FirstChildElement("VNO");
    if (pVno != nullptr) {
        GetInt     (pVno, "SPEED",          cfg.iVno);
        GetUnsigned(pVno, "CHIME_INTERVAL", cfg.uVnoChimeInterval);
        GetBool    (pVno, "CHIME_ENABLED",  cfg.bVnoChimeEnabled);
    }

    // ---------------- Scalars: SD logging, log rate ----------------------

    GetBool(root, "SDLOGGING", cfg.bSdLogging);
    GetInt (root, "LOGRATE",   cfg.iLogRate);

    // ---------------- <AIRCRAFT> -----------------------------------------

    XMLElement* pAc = root->FirstChildElement("AIRCRAFT");
    if (pAc != nullptr) {
        GetInt  (pAc, "GROSS_WEIGHT",   cfg.iAcGrossWeight);
        GetFloat(pAc, "BEST_GLIDE_IAS", cfg.fAcBestGlideIAS);
        GetFloat(pAc, "VFE",            cfg.fAcVfe);
        GetFloat(pAc, "G_LIMIT",        cfg.fAcGlimit);
        // <NEG_G_LIMIT> absent on configs written before the Custom-mode
        // pos/neg split landed.  Leaving cfg.fAcNegGlimit untouched
        // preserves the LoadDefaults seed (-1.76 G, the Utility negative
        // side) so an old config opens as a labeled category until the
        // pilot edits and re-saves.
        GetFloat(pAc, "NEG_G_LIMIT",    cfg.fAcNegGlimit);
    }

    if (tooManyFlaps)
        return XmlParseStatus::TooManyFlaps;
    return XmlParseStatus::Ok;
}

}  // namespace onspeed::config
