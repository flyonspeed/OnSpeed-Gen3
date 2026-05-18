// ConfigXmlEmit.cpp — see ConfigXmlEmit.h for the public interface.
//
// Verbatim port of FOSConfig::ConfigurationToString() (software/OnSpeed-
// Gen3-ESP32/src/config/Config.cpp, pre-PR-3.1-Task-2).  Tag order
// matches the legacy emitter byte-for-byte so uploaders comparing the
// round-trip XML to their local copies see no churn.

#include <config/ConfigXmlEmit.h>

#include <algorithm>
#include <cstddef>
#include <string>

#include <tinyxml2.h>

#include <util/OnSpeedTypes.h>

namespace onspeed::config {

using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;
using tinyxml2::XMLPrinter;

namespace {

// Create `name` as a new child of `parent`, return the new element.
inline XMLElement* AddElem(XMLElement* parent, const char* name)
{
    return parent->InsertNewChildElement(name);
}

// Add `name` as a child of `parent` with the given text value.  One helper
// per type so the call sites stay concise and tinyxml2's SetText overloads
// are dispatched correctly.
inline void AddInt(XMLElement* parent, const char* name, int value)
{
    XMLElement* e = parent->InsertNewChildElement(name);
    e->SetText(value);
}

inline void AddUnsigned(XMLElement* parent, const char* name, unsigned value)
{
    XMLElement* e = parent->InsertNewChildElement(name);
    e->SetText(value);
}

inline void AddFloat(XMLElement* parent, const char* name, float value)
{
    XMLElement* e = parent->InsertNewChildElement(name);
    e->SetText(value);
}

inline void AddBool(XMLElement* parent, const char* name, bool value)
{
    XMLElement* e = parent->InsertNewChildElement(name);
    e->SetText(value);
}

inline void AddString(XMLElement* parent, const char* name, const char* value)
{
    XMLElement* e = parent->InsertNewChildElement(name);
    e->SetText(value);
}

}  // namespace

std::string EmitXml(const OnSpeedConfig& cfg)
{
    XMLDocument doc;
    XMLElement* root = doc.NewElement("CONFIG2");
    doc.InsertEndChild(root);

    AddInt   (root, "AOA_SMOOTHING",      cfg.iAoaSmoothing);
    // Adaptive-EMA AOA smoothing (issue #566). Opt-in.
    AddBool  (root, "AOA_FILTER_ADAPTIVE",  cfg.bAoaFilterAdaptive);
    AddFloat (root, "AOA_FILTER_ALPHA_MIN", cfg.fAoaFilterAlphaMin);
    AddFloat (root, "AOA_FILTER_ALPHA_MAX", cfg.fAoaFilterAlphaMax);
    AddFloat (root, "AOA_FILTER_K_BOOST",   cfg.fAoaFilterKBoost);
    AddInt   (root, "PRESSURE_SMOOTHING", cfg.iPressureSmoothing);
    AddString(root, "DATASOURCE",         cfg.suDataSrc.toCStr());
    AddString(root, "REPLAYLOGFILENAME",  cfg.sReplayLogFileName.c_str());

    // AUDIT #013 defence-in-depth: bound the emit loop even if aFlaps
    // somehow contains more than MAX_AOA_CURVES entries.  ParseXml already
    // truncates at load, but this belt-and-suspenders check prevents a
    // buggy in-memory mutation (e.g. a web upload that bypasses ParseXml)
    // from generating unbounded XML.
    const std::size_t flapCount =
        std::min<std::size_t>(cfg.aFlaps.size(),
                              static_cast<std::size_t>(onspeed::MAX_AOA_CURVES));

    for (std::size_t i = 0; i < flapCount; ++i) {
        const OnSpeedConfig::SuFlaps& f = cfg.aFlaps[i];

        XMLElement* flap = AddElem(root, "FLAP_POSITION");
        AddInt  (flap, "DEGREES",        f.iDegrees);
        AddInt  (flap, "POT_VALUE",      f.iPotPosition);
        AddFloat(flap, "LDMAXAOA",       f.fLDMAXAOA);
        AddFloat(flap, "ONSPEEDFASTAOA", f.fONSPEEDFASTAOA);
        AddFloat(flap, "ONSPEEDSLOWAOA", f.fONSPEEDSLOWAOA);
        AddFloat(flap, "STALLWARNAOA",   f.fSTALLWARNAOA);
        AddFloat(flap, "STALLAOA",       f.fSTALLAOA);
        AddFloat(flap, "MANAOA",         f.fMANAOA);
        AddFloat(flap, "ALPHA0",         f.fAlpha0);
        AddFloat(flap, "ALPHASTALL",     f.fAlphaStall);
        AddFloat(flap, "KFIT",           f.fKFit);

        XMLElement* curve = AddElem(flap, "AOA_CURVE");
        AddInt  (curve, "TYPE", static_cast<int>(f.AoaCurve.iCurveType));
        AddFloat(curve, "X3",   f.AoaCurve.afCoeff[0]);
        AddFloat(curve, "X2",   f.AoaCurve.afCoeff[1]);
        AddFloat(curve, "X1",   f.AoaCurve.afCoeff[2]);
        AddFloat(curve, "X0",   f.AoaCurve.afCoeff[3]);
    }

    // <VOLUME>
    XMLElement* vol = AddElem(root, "VOLUME");
    AddBool(vol, "ENABLED",        cfg.bVolumeControl);
    AddInt (vol, "HIGH_ANALOG",    cfg.iVolumeHighAnalog);
    AddInt (vol, "LOW_ANALOG",     cfg.iVolumeLowAnalog);
    AddInt (vol, "DEFAULT",        cfg.iDefaultVolume);
    AddBool(vol, "ENABLE_3DAUDIO", cfg.bAudio3D);
    AddInt (vol, "MUTE_UNDER_IAS", cfg.iMuteAudioUnderIAS);

    AddBool(root, "OVERGWARNING", cfg.bOverGWarning);

    // <CAS_CURVE>
    XMLElement* cas = AddElem(root, "CAS_CURVE");
    AddInt  (cas, "TYPE",    static_cast<int>(cfg.CasCurve.iCurveType));
    AddFloat(cas, "X3",      cfg.CasCurve.afCoeff[0]);
    AddFloat(cas, "X2",      cfg.CasCurve.afCoeff[1]);
    AddFloat(cas, "X1",      cfg.CasCurve.afCoeff[2]);
    AddFloat(cas, "X0",      cfg.CasCurve.afCoeff[3]);
    AddBool (cas, "ENABLED", cfg.bCasCurveEnabled);

    // <ORIENTATION>
    XMLElement* orient = AddElem(root, "ORIENTATION");
    AddString(orient, "PORTS",   cfg.sPortsOrientation.c_str());
    AddString(orient, "BOX_TOP", cfg.sBoxtopOrientation.c_str());

    AddBool  (root, "BOOM",            cfg.bReadBoom);
    AddBool  (root, "BOOMCHECKSUM",    cfg.bBoomChecksum);
    AddBool  (root, "BOOMCONVERTDATA", cfg.bBoomConvertData);
    AddBool  (root, "SERIALEFISDATA",  cfg.bReadEfisData);
    AddString(root, "EFISTYPE",        cfg.sEfisType.c_str());
    AddBool  (root, "OATSENSOR",       cfg.bOatSensor);

    AddString(root, "SERIALOUTFORMAT", cfg.sSerialOutFormat.c_str());
    AddString(root, "CALWIZ_SOURCE",   cfg.sCalSource.c_str());

    // <BIAS>
    XMLElement* bias = AddElem(root, "BIAS");
    AddInt  (bias, "PFWD",    cfg.iPFwdBias);
    AddInt  (bias, "P45",     cfg.iP45Bias);
    AddFloat(bias, "PSTATIC", cfg.fPStaticBias);
    AddFloat(bias, "GX",      cfg.fGxBias);
    AddFloat(bias, "GY",      cfg.fGyBias);
    AddFloat(bias, "GZ",      cfg.fGzBias);
    AddFloat(bias, "PITCH",   cfg.fPitchBias);
    AddFloat(bias, "ROLL",    cfg.fRollBias);

    AddInt(root, "AHRS_ALGORITHM", cfg.iAhrsAlgorithm);

    // <LOAD_LIMIT>
    XMLElement* load = AddElem(root, "LOAD_LIMIT");
    AddFloat(load, "POSITIVE",              cfg.fLoadLimitPositive);
    AddFloat(load, "NEGATIVE",              cfg.fLoadLimitNegative);
    AddFloat(load, "ASYMMETRIC_GYRO_LIMIT", cfg.fAsymmetricGyroLimit);
    AddFloat(load, "ASYMMETRIC_REDUCTION",  cfg.fAsymmetricReduction);

    // <VNO>
    XMLElement* vno = AddElem(root, "VNO");
    AddInt     (vno, "SPEED",          cfg.iVno);
    AddUnsigned(vno, "CHIME_INTERVAL", cfg.uVnoChimeInterval);
    AddBool    (vno, "CHIME_ENABLED",  cfg.bVnoChimeEnabled);

    AddBool(root, "SDLOGGING", cfg.bSdLogging);
    AddInt (root, "LOGRATE",   cfg.iLogRate);

    // <AIRCRAFT>
    XMLElement* ac = AddElem(root, "AIRCRAFT");
    AddInt  (ac, "GROSS_WEIGHT",    cfg.iAcGrossWeight);
    AddFloat(ac, "BEST_GLIDE_IAS",  cfg.fAcBestGlideIAS);
    AddFloat(ac, "VFE",             cfg.fAcVfe);
    AddFloat(ac, "G_LIMIT",         cfg.fAcGlimit);
    AddFloat(ac, "NEG_G_LIMIT",     cfg.fAcNegGlimit);
    AddFloat(ac, "CUSTOM_G_LIMIT",     cfg.fCustomAcGlimit);
    AddFloat(ac, "CUSTOM_NEG_G_LIMIT", cfg.fCustomAcNegGlimit);

    XMLPrinter printer;
    doc.Print(&printer);
    return std::string(printer.CStr(), printer.CStrSize() - 1);
}

}  // namespace onspeed::config
