// test_config_xml.cpp — unit tests for onspeed::config::ParseXml / EmitXml.
//
// Coverage goals:
//   - Parse Sam's real N720AK config (V2) and pin several known field values
//     so future parser bugs can't silently break flight calibrations.
//   - Round-trip: parse -> emit -> parse -> every field equal.
//   - Defaults-for-missing behaviour: sparse XML parses without errors.
//   - Error paths: malformed XML, missing root, V1 <CONFIG> rejected.
//   - Audit #013: >MAX_AOA_CURVES flap entries return TooManyFlaps and
//     the in-memory aFlaps array is truncated to MAX_AOA_CURVES.
//   - Audit #009: the V2 <PSTATIC> sign convention is pinned verbatim
//     — the value in the XML is assigned directly to fPStaticBias.
//   - Enum parsing: DATASOURCE and SERIALOUTFORMAT round-trip correctly.
//   - Bool parsing: "true"/"false"/"1"/"0" all accepted.
//   - Float parsing: scientific notation accepted.
//   - Nested structures: flap AoaCurve, CAS curve, VOLUME, BIAS, AIRCRAFT.
//   - Unknown tags are silently ignored.

#include <unity.h>

#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

#include <config/ConfigXmlEmit.h>
#include <config/ConfigXmlParse.h>
#include <config/OnSpeedConfig.h>
#include <util/OnSpeedTypes.h>

using namespace onspeed;
using onspeed::config::OnSpeedConfig;
using onspeed::config::SuDataSource;
using onspeed::config::ParseXml;
using onspeed::config::EmitXml;
using onspeed::config::XmlParseStatus;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Fixtures — inlined as raw string literals.
// ============================================================================

// Sam's N720AK config (2_11_26_config.cfg) copied verbatim.
static constexpr const char* kN720akV2 = R"XML(<CONFIG2>
    <AOA_SMOOTHING>15</AOA_SMOOTHING>
    <PRESSURE_SMOOTHING>15</PRESSURE_SMOOTHING>
    <DATASOURCE>SENSORS</DATASOURCE>
    <REPLAYLOGFILENAME></REPLAYLOGFILENAME>
    <FLAP_POSITION>
        <DEGREES>0</DEGREES>
        <POT_VALUE>3908</POT_VALUE>
        <LDMAXAOA>4.0999999</LDMAXAOA>
        <ONSPEEDFASTAOA>4.0799999</ONSPEEDFASTAOA>
        <ONSPEEDSLOWAOA>4.9499998</ONSPEEDSLOWAOA>
        <STALLWARNAOA>7.98</STALLWARNAOA>
        <STALLAOA>0</STALLAOA>
        <MANAOA>0</MANAOA>
        <AOA_CURVE>
            <TYPE>1</TYPE>
            <X3>0</X3>
            <X2>-6.1382999</X2>
            <X1>26.4076</X1>
            <X0>4.9001002</X0>
        </AOA_CURVE>
    </FLAP_POSITION>
    <FLAP_POSITION>
        <DEGREES>16</DEGREES>
        <POT_VALUE>2332</POT_VALUE>
        <LDMAXAOA>2.3</LDMAXAOA>
        <ONSPEEDFASTAOA>2.6900001</ONSPEEDFASTAOA>
        <ONSPEEDSLOWAOA>4.0999999</ONSPEEDSLOWAOA>
        <STALLWARNAOA>7.5999999</STALLWARNAOA>
        <STALLAOA>0</STALLAOA>
        <MANAOA>0</MANAOA>
        <AOA_CURVE>
            <TYPE>1</TYPE>
            <X3>0</X3>
            <X2>-43.778</X2>
            <X1>39.641998</X1>
            <X0>4.3887</X0>
        </AOA_CURVE>
    </FLAP_POSITION>
    <FLAP_POSITION>
        <DEGREES>33</DEGREES>
        <POT_VALUE>8</POT_VALUE>
        <LDMAXAOA>-1.12</LDMAXAOA>
        <ONSPEEDFASTAOA>3.79</ONSPEEDFASTAOA>
        <ONSPEEDSLOWAOA>5.23</ONSPEEDSLOWAOA>
        <STALLWARNAOA>9.2399998</STALLWARNAOA>
        <STALLAOA>0</STALLAOA>
        <MANAOA>0</MANAOA>
        <AOA_CURVE>
            <TYPE>1</TYPE>
            <X3>0</X3>
            <X2>-13.5712</X2>
            <X1>39.873299</X1>
            <X0>2.1568999</X0>
        </AOA_CURVE>
    </FLAP_POSITION>
    <VOLUME>
        <ENABLED>false</ENABLED>
        <HIGH_ANALOG>1023</HIGH_ANALOG>
        <LOW_ANALOG>1</LOW_ANALOG>
        <DEFAULT>20</DEFAULT>
        <ENABLE_3DAUDIO>false</ENABLE_3DAUDIO>
        <MUTE_UNDER_IAS>35</MUTE_UNDER_IAS>
    </VOLUME>
    <OVERGWARNING>true</OVERGWARNING>
    <CAS_CURVE>
        <TYPE>1</TYPE>
        <X3>0</X3>
        <X2>0</X2>
        <X1>0.96969998</X1>
        <X0>4.6448998</X0>
        <ENABLED>true</ENABLED>
    </CAS_CURVE>
    <ORIENTATION>
        <PORTS>DOWN</PORTS>
        <BOX_TOP>FORWARD</BOX_TOP>
    </ORIENTATION>
    <BOOM>false</BOOM>
    <SERIALEFISDATA>true</SERIALEFISDATA>
    <EFISTYPE>ADVANCED</EFISTYPE>
    <SERIALOUTFORMAT>ONSPEED</SERIALOUTFORMAT>
    <CALWIZ_SOURCE>ONSPEED</CALWIZ_SOURCE>
    <BIAS>
        <PFWD>2048</PFWD>
        <P45>2047</P45>
        <PSTATIC>-0.65692139</PSTATIC>
        <GX>0.054132082</GX>
        <GY>0.14275716</GY>
        <GZ>0.24882813</GZ>
        <PITCH>4.4122477</PITCH>
        <ROLL>-0.81594318</ROLL>
    </BIAS>
    <LOAD_LIMIT>
        <POSITIVE>2.5</POSITIVE>
        <NEGATIVE>-1</NEGATIVE>
    </LOAD_LIMIT>
    <VNO>
        <SPEED>158</SPEED>
        <CHIME_INTERVAL>180</CHIME_INTERVAL>
        <CHIME_ENABLED>true</CHIME_ENABLED>
    </VNO>
    <SDLOGGING>true</SDLOGGING>
</CONFIG2>
)XML";

// Minimal valid V2 config — just the root element, no fields.  Parse should
// succeed and leave every field at whatever the caller passed in (typically
// defaults).
static constexpr const char* kMinimalV2 = R"XML(<CONFIG2>
</CONFIG2>
)XML";

// Intentionally malformed XML — unclosed tag.
static constexpr const char* kMalformed = R"XML(<CONFIG2>
    <AOA_SMOOTHING>20
</CONFIG2>
)XML";

// Missing the CONFIG2 root (uses V1 CONFIG instead) — ParseXml should reject
// this so the sketch shim can fall back to the V1 path.
static constexpr const char* kV1Root = R"XML(<CONFIG>
    <AOA_SMOOTHING>20</AOA_SMOOTHING>
</CONFIG>
)XML";

// Audit #013: 7 FLAP_POSITION entries (MAX_AOA_CURVES = 5).  Parser must
// truncate to MAX_AOA_CURVES and return TooManyFlaps.
static constexpr const char* kFlapOverflow = R"XML(<CONFIG2>
    <AOA_SMOOTHING>20</AOA_SMOOTHING>
    <FLAP_POSITION>
        <DEGREES>0</DEGREES>
        <LDMAXAOA>1.0</LDMAXAOA>
    </FLAP_POSITION>
    <FLAP_POSITION>
        <DEGREES>10</DEGREES>
        <LDMAXAOA>2.0</LDMAXAOA>
    </FLAP_POSITION>
    <FLAP_POSITION>
        <DEGREES>20</DEGREES>
        <LDMAXAOA>3.0</LDMAXAOA>
    </FLAP_POSITION>
    <FLAP_POSITION>
        <DEGREES>30</DEGREES>
        <LDMAXAOA>4.0</LDMAXAOA>
    </FLAP_POSITION>
    <FLAP_POSITION>
        <DEGREES>40</DEGREES>
        <LDMAXAOA>5.0</LDMAXAOA>
    </FLAP_POSITION>
    <FLAP_POSITION>
        <DEGREES>50</DEGREES>
        <LDMAXAOA>6.0</LDMAXAOA>
    </FLAP_POSITION>
    <FLAP_POSITION>
        <DEGREES>60</DEGREES>
        <LDMAXAOA>7.0</LDMAXAOA>
    </FLAP_POSITION>
</CONFIG2>
)XML";

// ============================================================================
// Equality helper — test-only, compares every config field including nested
// arrays.  Intentionally lives here (not in core) since it's test infra.
// ============================================================================

static bool CurvesEqual(const onspeed::SuCalibrationCurve& a,
                        const onspeed::SuCalibrationCurve& b)
{
    if (a.iCurveType != b.iCurveType) return false;
    for (int i = 0; i < onspeed::MAX_CURVE_COEFF; ++i)
        if (std::fabs(a.afCoeff[i] - b.afCoeff[i]) > 1e-5f)
            return false;
    return true;
}

static bool FlapsEqual(const OnSpeedConfig::SuFlaps& a,
                       const OnSpeedConfig::SuFlaps& b)
{
    if (a.iDegrees        != b.iDegrees)        return false;
    if (a.iPotPosition    != b.iPotPosition)    return false;
    if (std::fabs(a.fLDMAXAOA       - b.fLDMAXAOA)       > 1e-5f) return false;
    if (std::fabs(a.fONSPEEDFASTAOA - b.fONSPEEDFASTAOA) > 1e-5f) return false;
    if (std::fabs(a.fONSPEEDSLOWAOA - b.fONSPEEDSLOWAOA) > 1e-5f) return false;
    if (std::fabs(a.fSTALLWARNAOA   - b.fSTALLWARNAOA)   > 1e-5f) return false;
    if (std::fabs(a.fSTALLAOA       - b.fSTALLAOA)       > 1e-5f) return false;
    if (std::fabs(a.fMANAOA         - b.fMANAOA)         > 1e-5f) return false;
    if (std::fabs(a.fAlpha0         - b.fAlpha0)         > 1e-5f) return false;
    if (std::fabs(a.fAlphaStall     - b.fAlphaStall)     > 1e-5f) return false;
    if (std::fabs(a.fKFit           - b.fKFit)           > 1e-5f) return false;
    if (!CurvesEqual(a.AoaCurve, b.AoaCurve)) return false;
    return true;
}

static bool ConfigsEqual(const OnSpeedConfig& a, const OnSpeedConfig& b)
{
    if (a.iAoaSmoothing      != b.iAoaSmoothing)      return false;
    if (a.iPressureSmoothing != b.iPressureSmoothing) return false;
    if (a.iMuteAudioUnderIAS != b.iMuteAudioUnderIAS) return false;
    if (a.suDataSrc.enSrc    != b.suDataSrc.enSrc)    return false;
    if (a.sReplayLogFileName != b.sReplayLogFileName) return false;

    if (a.aFlaps.size() != b.aFlaps.size()) return false;
    for (std::size_t i = 0; i < a.aFlaps.size(); ++i)
        if (!FlapsEqual(a.aFlaps[i], b.aFlaps[i])) return false;

    if (a.bVolumeControl     != b.bVolumeControl)     return false;
    if (a.iVolumeHighAnalog  != b.iVolumeHighAnalog)  return false;
    if (a.iVolumeLowAnalog   != b.iVolumeLowAnalog)   return false;
    if (a.iDefaultVolume     != b.iDefaultVolume)     return false;
    if (a.bAudio3D           != b.bAudio3D)           return false;
    if (a.bOverGWarning      != b.bOverGWarning)      return false;

    if (!CurvesEqual(a.CasCurve, b.CasCurve))         return false;
    if (a.bCasCurveEnabled   != b.bCasCurveEnabled)   return false;

    if (a.sPortsOrientation  != b.sPortsOrientation)  return false;
    if (a.sBoxtopOrientation != b.sBoxtopOrientation) return false;
    if (a.sEfisType          != b.sEfisType)          return false;

    if (a.sCalSource         != b.sCalSource)         return false;
    if (a.bCalSourceEfis     != b.bCalSourceEfis)     return false;

    if (a.iPFwdBias          != b.iPFwdBias)          return false;
    if (a.iP45Bias           != b.iP45Bias)           return false;
    if (std::fabs(a.fPStaticBias - b.fPStaticBias) > 1e-5f) return false;
    if (std::fabs(a.fGxBias      - b.fGxBias)      > 1e-5f) return false;
    if (std::fabs(a.fGyBias      - b.fGyBias)      > 1e-5f) return false;
    if (std::fabs(a.fGzBias      - b.fGzBias)      > 1e-5f) return false;
    if (std::fabs(a.fPitchBias   - b.fPitchBias)   > 1e-5f) return false;
    if (std::fabs(a.fRollBias    - b.fRollBias)    > 1e-5f) return false;

    if (a.iAhrsAlgorithm     != b.iAhrsAlgorithm)     return false;

    if (a.bReadBoom          != b.bReadBoom)          return false;
    if (a.bReadEfisData      != b.bReadEfisData)      return false;
    if (a.bOatSensor         != b.bOatSensor)         return false;
    if (a.bBoomChecksum      != b.bBoomChecksum)      return false;
    if (a.bBoomConvertData   != b.bBoomConvertData)   return false;

    if (a.sSerialOutFormat   != b.sSerialOutFormat)   return false;
    if (a.enSerialOutFormat  != b.enSerialOutFormat)  return false;

    if (std::fabs(a.fLoadLimitPositive   - b.fLoadLimitPositive)   > 1e-5f) return false;
    if (std::fabs(a.fLoadLimitNegative   - b.fLoadLimitNegative)   > 1e-5f) return false;
    if (std::fabs(a.fAsymmetricGyroLimit - b.fAsymmetricGyroLimit) > 1e-5f) return false;
    if (std::fabs(a.fAsymmetricReduction - b.fAsymmetricReduction) > 1e-5f) return false;

    if (a.iLogRate           != b.iLogRate)           return false;

    if (a.iVno               != b.iVno)               return false;
    if (a.uVnoChimeInterval  != b.uVnoChimeInterval)  return false;
    if (a.bVnoChimeEnabled   != b.bVnoChimeEnabled)   return false;

    if (a.bSdLogging         != b.bSdLogging)         return false;

    if (a.iAcGrossWeight     != b.iAcGrossWeight)     return false;
    if (std::fabs(a.fAcBestGlideIAS - b.fAcBestGlideIAS) > 1e-5f) return false;
    if (std::fabs(a.fAcVfe          - b.fAcVfe)          > 1e-5f) return false;
    if (std::fabs(a.fAcGlimit       - b.fAcGlimit)       > 1e-5f) return false;
    if (std::fabs(a.fAcNegGlimit    - b.fAcNegGlimit)    > 1e-5f) return false;
    if (std::fabs(a.fCustomAcGlimit    - b.fCustomAcGlimit)    > 1e-5f) return false;
    if (std::fabs(a.fCustomAcNegGlimit - b.fCustomAcNegGlimit) > 1e-5f) return false;

    return true;
}

// ============================================================================
// Parsing Sam's N720AK config — pin known values so parser regressions
// surface as test failures, not silent flight-calibration breakage.
// ============================================================================

void test_parse_n720ak_known_values(void)
{
    OnSpeedConfig cfg;  // ctor calls LoadDefaults
    XmlParseStatus st = ParseXml(kN720akV2, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok), static_cast<int>(st));

    // Scalars
    TEST_ASSERT_EQUAL_INT(15, cfg.iAoaSmoothing);
    TEST_ASSERT_EQUAL_INT(15, cfg.iPressureSmoothing);
    TEST_ASSERT_EQUAL(SuDataSource::EnSensors, cfg.suDataSrc.enSrc);

    // Three flaps present, sorted by degrees (0, 16, 33).
    TEST_ASSERT_EQUAL_size_t(3u, cfg.aFlaps.size());
    TEST_ASSERT_EQUAL_INT(0,  cfg.aFlaps[0].iDegrees);
    TEST_ASSERT_EQUAL_INT(16, cfg.aFlaps[1].iDegrees);
    TEST_ASSERT_EQUAL_INT(33, cfg.aFlaps[2].iDegrees);

    // Flap 0 — LDMAX, POT_VALUE, AOA curve X1 (distinctive non-default).
    TEST_ASSERT_EQUAL_INT(3908, cfg.aFlaps[0].iPotPosition);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.0999999f, cfg.aFlaps[0].fLDMAXAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 7.98f,      cfg.aFlaps[0].fSTALLWARNAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 26.4076f,   cfg.aFlaps[0].AoaCurve.afCoeff[2]);  // X1
    TEST_ASSERT_EQUAL_UINT8(1u,                 cfg.aFlaps[0].AoaCurve.iCurveType);

    // Flap 2 — more distinctive values.
    TEST_ASSERT_EQUAL_INT(8, cfg.aFlaps[2].iPotPosition);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.12f, cfg.aFlaps[2].fLDMAXAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 39.873299f, cfg.aFlaps[2].AoaCurve.afCoeff[2]);

    // Volume block
    TEST_ASSERT_FALSE(cfg.bVolumeControl);
    TEST_ASSERT_EQUAL_INT(1023, cfg.iVolumeHighAnalog);
    TEST_ASSERT_EQUAL_INT(20,   cfg.iDefaultVolume);
    TEST_ASSERT_EQUAL_INT(35,   cfg.iMuteAudioUnderIAS);

    // Top-level bools
    TEST_ASSERT_TRUE (cfg.bOverGWarning);
    TEST_ASSERT_FALSE(cfg.bReadBoom);
    TEST_ASSERT_TRUE (cfg.bReadEfisData);
    TEST_ASSERT_TRUE (cfg.bSdLogging);

    // Orientation
    TEST_ASSERT_EQUAL_STRING("DOWN",    cfg.sPortsOrientation.c_str());
    TEST_ASSERT_EQUAL_STRING("FORWARD", cfg.sBoxtopOrientation.c_str());

    // EFIS
    TEST_ASSERT_EQUAL_STRING("ADVANCED", cfg.sEfisType.c_str());
    TEST_ASSERT_EQUAL_STRING("ONSPEED",  cfg.sSerialOutFormat.c_str());
    TEST_ASSERT_EQUAL(OnSpeedConfig::EnSerialFmtOnSpeed, cfg.enSerialOutFormat);

    // Cal source
    TEST_ASSERT_EQUAL_STRING("ONSPEED", cfg.sCalSource.c_str());
    TEST_ASSERT_FALSE(cfg.bCalSourceEfis);

    // Biases — note fPStaticBias pinned in its own test.
    TEST_ASSERT_EQUAL_INT(2048, cfg.iPFwdBias);
    TEST_ASSERT_EQUAL_INT(2047, cfg.iP45Bias);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  4.4122477f, cfg.fPitchBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.81594318f, cfg.fRollBias);

    // Load limits
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  2.5f, cfg.fLoadLimitPositive);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, cfg.fLoadLimitNegative);

    // Vno
    TEST_ASSERT_EQUAL_INT(158, cfg.iVno);
    TEST_ASSERT_EQUAL_UINT32(180u, cfg.uVnoChimeInterval);
    TEST_ASSERT_TRUE(cfg.bVnoChimeEnabled);

    // CAS curve (ENABLED=true, X1=0.96969998, X0=4.6448998)
    TEST_ASSERT_TRUE(cfg.bCasCurveEnabled);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.96969998f, cfg.CasCurve.afCoeff[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.6448998f,  cfg.CasCurve.afCoeff[3]);
}

// ============================================================================
// Round-trip: parse -> emit -> parse -> struct-equal.  The emit may re-order
// whitespace, so we compare the two parsed structs, not the byte strings.
// ============================================================================

void test_parse_emit_roundtrip_n720ak(void)
{
    OnSpeedConfig cfg1;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(kN720akV2, cfg1)));

    std::string xml = EmitXml(cfg1);
    TEST_ASSERT_TRUE(!xml.empty());
    TEST_ASSERT_TRUE(xml.find("<CONFIG2>") != std::string::npos);

    OnSpeedConfig cfg2;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, cfg2)));

    TEST_ASSERT_TRUE(ConfigsEqual(cfg1, cfg2));
}

void test_parse_defaults_roundtrip(void)
{
    OnSpeedConfig cfg1;  // defaults
    std::string xml = EmitXml(cfg1);

    OnSpeedConfig cfg2;
    cfg2.iAoaSmoothing = -999;  // mutate so we see the emit/parse actually ran
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, cfg2)));

    TEST_ASSERT_TRUE(ConfigsEqual(cfg1, cfg2));
}

// ============================================================================
// Minimal / missing / malformed / V1 documents.
// ============================================================================

void test_parse_minimal_uses_defaults_for_missing(void)
{
    OnSpeedConfig cfg;
    cfg.iAoaSmoothing = 77;       // pre-fill to confirm sparse-parse keeps it
    cfg.fLoadLimitPositive = 9.5f;

    XmlParseStatus st = ParseXml(kMinimalV2, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok), static_cast<int>(st));

    // Missing tags: caller's pre-parse values are preserved.
    TEST_ASSERT_EQUAL_INT(77, cfg.iAoaSmoothing);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 9.5f, cfg.fLoadLimitPositive);
}

void test_parse_malformed_returns_error(void)
{
    OnSpeedConfig cfg;
    XmlParseStatus st = ParseXml(kMalformed, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Malformed),
                      static_cast<int>(st));
}

void test_parse_v1_root_rejected(void)
{
    OnSpeedConfig cfg;
    XmlParseStatus st = ParseXml(kV1Root, cfg);
    // The V1 legacy path handles <CONFIG>; this V2 parser rejects it with
    // MissingRoot so the sketch shim can fall back to the V1 parser.
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::MissingRoot),
                      static_cast<int>(st));
}

// ============================================================================
// AUDIT #013: flap count > MAX_AOA_CURVES is bounded to MAX_AOA_CURVES.
// Before the fix, a 7-entry flap list flowed through to the emitter and
// generated unbounded XML.  Now we return TooManyFlaps and truncate in-place.
// ============================================================================

void test_parse_flap_overflow_safely(void)
{
    OnSpeedConfig cfg;
    XmlParseStatus st = ParseXml(kFlapOverflow, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::TooManyFlaps),
                      static_cast<int>(st));

    // aFlaps is truncated to MAX_AOA_CURVES, NOT zero-filled.  The caller
    // gets a usable config of the first MAX_AOA_CURVES flap entries.
    TEST_ASSERT_EQUAL_size_t(
        static_cast<std::size_t>(onspeed::MAX_AOA_CURVES),
        cfg.aFlaps.size());

    // First 5 entries, sorted by degrees: 0, 10, 20, 30, 40.
    TEST_ASSERT_EQUAL_INT(0,  cfg.aFlaps[0].iDegrees);
    TEST_ASSERT_EQUAL_INT(10, cfg.aFlaps[1].iDegrees);
    TEST_ASSERT_EQUAL_INT(20, cfg.aFlaps[2].iDegrees);
    TEST_ASSERT_EQUAL_INT(30, cfg.aFlaps[3].iDegrees);
    TEST_ASSERT_EQUAL_INT(40, cfg.aFlaps[4].iDegrees);

    // And the emit path must also respect the bound — no unbounded output.
    std::string xml = EmitXml(cfg);
    std::size_t flapCount = 0;
    std::size_t pos = 0;
    while ((pos = xml.find("<FLAP_POSITION>", pos)) != std::string::npos) {
        ++flapCount;
        ++pos;
    }
    TEST_ASSERT_EQUAL_size_t(
        static_cast<std::size_t>(onspeed::MAX_AOA_CURVES),
        flapCount);
}

// ============================================================================
// AUDIT #009: V2 <PSTATIC> sign convention is pinned verbatim.
// The N720AK fixture has <PSTATIC>-0.65692139</PSTATIC>.  The in-memory
// value of fPStaticBias must be exactly -0.65692139 — negative (the V2
// convention).  The V1 CSV parser negates on load because its legacy
// convention was the opposite sign; this test ensures future edits can't
// silently flip the V2 convention.
// ============================================================================

void test_pstaticbias_sign_convention_preserved(void)
{
    OnSpeedConfig cfg;
    XmlParseStatus st = ParseXml(kN720akV2, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok), static_cast<int>(st));

    // Value is stored with the same sign as the XML (-0.65692139).
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.65692139f, cfg.fPStaticBias);
    TEST_ASSERT_TRUE(cfg.fPStaticBias < 0.0f);

    // Emit must round-trip the sign.
    std::string xml = EmitXml(cfg);
    TEST_ASSERT_TRUE(xml.find("-0.6569") != std::string::npos);

    // And a positive PSTATIC in the XML must end up as positive in memory.
    static constexpr const char* kPositivePStatic = R"XML(<CONFIG2>
        <BIAS><PSTATIC>0.25</PSTATIC></BIAS>
    </CONFIG2>)XML";
    OnSpeedConfig cfg2;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(kPositivePStatic, cfg2)));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, cfg2.fPStaticBias);
}

// ============================================================================
// Enum parsing: DATASOURCE tag.
// ============================================================================

void test_enum_parsing_datasource(void)
{
    auto parseSource = [](const char* src) {
        std::string xml = std::string("<CONFIG2><DATASOURCE>") + src
                        + "</DATASOURCE></CONFIG2>";
        OnSpeedConfig cfg;
        XmlParseStatus st = ParseXml(xml, cfg);
        TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                          static_cast<int>(st));
        return cfg.suDataSrc.enSrc;
    };

    TEST_ASSERT_EQUAL(SuDataSource::EnSensors,    parseSource("SENSORS"));
    TEST_ASSERT_EQUAL(SuDataSource::EnReplay,     parseSource("REPLAYLOGFILE"));
    TEST_ASSERT_EQUAL(SuDataSource::EnTestPot,    parseSource("TESTPOT"));
    TEST_ASSERT_EQUAL(SuDataSource::EnRangeSweep, parseSource("RANGESWEEP"));
    TEST_ASSERT_EQUAL(SuDataSource::EnUnknown,    parseSource("GARBAGE"));
}

void test_enum_parsing_serialoutformat(void)
{
    auto parseFmt = [](const char* fmt) {
        std::string xml = std::string("<CONFIG2><SERIALOUTFORMAT>") + fmt
                        + "</SERIALOUTFORMAT></CONFIG2>";
        OnSpeedConfig cfg;
        TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                          static_cast<int>(ParseXml(xml, cfg)));
        return cfg.enSerialOutFormat;
    };

    TEST_ASSERT_EQUAL(OnSpeedConfig::EnSerialFmtG3X,     parseFmt("G3X"));
    TEST_ASSERT_EQUAL(OnSpeedConfig::EnSerialFmtOnSpeed, parseFmt("ONSPEED"));
    TEST_ASSERT_EQUAL(OnSpeedConfig::EnSerialFmtOther,   parseFmt("SOMETHING_ELSE"));
}

// ============================================================================
// Bool parsing: tinyxml2 accepts "true"/"false"/"1"/"0" via QueryBoolText.
// ============================================================================

void test_bool_parsing_true_false(void)
{
    auto parseBool = [](const char* val) {
        std::string xml = std::string("<CONFIG2><OVERGWARNING>") + val
                        + "</OVERGWARNING></CONFIG2>";
        OnSpeedConfig cfg;
        cfg.bOverGWarning = false;  // start from a known state
        XmlParseStatus st = ParseXml(xml, cfg);
        TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                          static_cast<int>(st));
        return cfg.bOverGWarning;
    };

    TEST_ASSERT_TRUE(parseBool("true"));
    TEST_ASSERT_TRUE(parseBool("1"));
    TEST_ASSERT_FALSE(parseBool("false"));
    TEST_ASSERT_FALSE(parseBool("0"));
}

// ============================================================================
// Float parsing: scientific notation must parse correctly.
// ============================================================================

void test_float_parsing_scientific(void)
{
    static constexpr const char* kSci = R"XML(<CONFIG2>
        <BIAS><GX>1.5e-3</GX></BIAS>
    </CONFIG2>)XML";

    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(kSci, cfg)));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0015f, cfg.fGxBias);
}

// ============================================================================
// Nested structure round-trips.
// ============================================================================

void test_volume_block_roundtrip(void)
{
    OnSpeedConfig a;
    a.bVolumeControl    = true;
    a.iVolumeHighAnalog = 4000;
    a.iVolumeLowAnalog  = 100;
    a.iDefaultVolume    = 75;
    a.bAudio3D          = true;
    a.iMuteAudioUnderIAS = 42;

    std::string xml = EmitXml(a);
    OnSpeedConfig b;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, b)));

    TEST_ASSERT_TRUE(b.bVolumeControl);
    TEST_ASSERT_EQUAL_INT(4000, b.iVolumeHighAnalog);
    TEST_ASSERT_EQUAL_INT(100,  b.iVolumeLowAnalog);
    TEST_ASSERT_EQUAL_INT(75,   b.iDefaultVolume);
    TEST_ASSERT_TRUE(b.bAudio3D);
    TEST_ASSERT_EQUAL_INT(42,   b.iMuteAudioUnderIAS);
}

void test_cas_curve_roundtrip(void)
{
    OnSpeedConfig a;
    a.CasCurve.iCurveType = 2;
    a.CasCurve.afCoeff[0] = 0.1f;
    a.CasCurve.afCoeff[1] = 0.2f;
    a.CasCurve.afCoeff[2] = 0.3f;
    a.CasCurve.afCoeff[3] = 0.4f;
    a.bCasCurveEnabled = true;

    std::string xml = EmitXml(a);
    OnSpeedConfig b;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, b)));

    TEST_ASSERT_EQUAL_UINT8(2u, b.CasCurve.iCurveType);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.1f, b.CasCurve.afCoeff[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.2f, b.CasCurve.afCoeff[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.3f, b.CasCurve.afCoeff[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.4f, b.CasCurve.afCoeff[3]);
    TEST_ASSERT_TRUE(b.bCasCurveEnabled);
}

void test_bias_block_roundtrip(void)
{
    OnSpeedConfig a;
    a.iPFwdBias = 1234;
    a.iP45Bias  = 5678;
    a.fPStaticBias = -0.123f;
    a.fGxBias = 0.01f;
    a.fGyBias = -0.02f;
    a.fGzBias = 0.03f;
    a.fPitchBias = 1.5f;
    a.fRollBias  = -2.5f;

    std::string xml = EmitXml(a);
    OnSpeedConfig b;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, b)));

    TEST_ASSERT_EQUAL_INT(1234, b.iPFwdBias);
    TEST_ASSERT_EQUAL_INT(5678, b.iP45Bias);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.123f, b.fPStaticBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  0.01f,  b.fGxBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.02f,  b.fGyBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  0.03f,  b.fGzBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  1.5f,   b.fPitchBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -2.5f,   b.fRollBias);
}

void test_aircraft_block_roundtrip(void)
{
    OnSpeedConfig a;
    a.iAcGrossWeight  = 2750;
    a.fAcBestGlideIAS = 87.5f;
    a.fAcVfe          = 100.0f;
    a.fAcGlimit       =  3.8f;
    a.fAcNegGlimit    = -1.52f;

    std::string xml = EmitXml(a);
    OnSpeedConfig b;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, b)));

    TEST_ASSERT_EQUAL_INT(2750, b.iAcGrossWeight);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 87.5f,  b.fAcBestGlideIAS);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, b.fAcVfe);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  3.80f, b.fAcGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.52f, b.fAcNegGlimit);
}

// Custom-mode pos/neg pair that does not match any named category.
// Round-trips both fields with their actual stored sign convention
// (negative G stored as a negative float).
void test_aircraft_custom_glimit_roundtrip(void)
{
    OnSpeedConfig a;
    a.fAcGlimit    =  5.5f;
    a.fAcNegGlimit = -2.5f;

    std::string xml = EmitXml(a);
    OnSpeedConfig b;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, b)));

    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  5.5f, b.fAcGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -2.5f, b.fAcNegGlimit);
}

// Old configs (written before the Custom-mode pos/neg split) lack the
// <NEG_G_LIMIT> element.  When fAcGlimit matches a named-category
// preset, the parser pairs it with that preset's negative so the UI
// opens at the same labeled category the pilot saw before the upgrade.
// Custom-storage fields, also absent in old configs, seed from the
// active fields so picking Custom for the first time after an upgrade
// shows what the pilot would expect.
void test_aircraft_glimit_legacy_no_neg_field(void)
{
    const char* kLegacyXml =
        "<?xml version=\"1.0\"?>\n"
        "<CONFIG2>\n"
        "  <AIRCRAFT>\n"
        "    <GROSS_WEIGHT>2700</GROSS_WEIGHT>\n"
        "    <BEST_GLIDE_IAS>87.5</BEST_GLIDE_IAS>\n"
        "    <VFE>96.0</VFE>\n"
        "    <G_LIMIT>4.4</G_LIMIT>\n"
        "  </AIRCRAFT>\n"
        "</CONFIG2>\n";

    OnSpeedConfig cfg;  // ctor calls LoadDefaults
    XmlParseStatus st = ParseXml(kLegacyXml, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(st));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  4.4f,  cfg.fAcGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.76f, cfg.fAcNegGlimit);
    // Custom-storage migration seeds from the active fields.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  4.4f,  cfg.fCustomAcGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.76f, cfg.fCustomAcNegGlimit);
}

// Old Aerobatic config: <G_LIMIT>=6.0 with no <NEG_G_LIMIT> migrates
// to the Aerobatic-paired negative (-3.0), preserving the named
// category on upgrade.  Without the matched-preset migration the
// parser would fall back to the LoadDefaults seed of -1.76 and the
// pilot's Aerobatic config would silently appear as Custom in the UI.
void test_aircraft_glimit_legacy_aerobatic_migrates(void)
{
    const char* kLegacyXml =
        "<?xml version=\"1.0\"?>\n"
        "<CONFIG2>\n"
        "  <AIRCRAFT>\n"
        "    <G_LIMIT>6.0</G_LIMIT>\n"
        "  </AIRCRAFT>\n"
        "</CONFIG2>\n";

    OnSpeedConfig cfg;
    XmlParseStatus st = ParseXml(kLegacyXml, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(st));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  6.0f,  cfg.fAcGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -3.0f,  cfg.fAcNegGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  6.0f,  cfg.fCustomAcGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -3.0f,  cfg.fCustomAcNegGlimit);
}

// Old Normal config: <G_LIMIT>=3.8 pairs with -1.52.
void test_aircraft_glimit_legacy_normal_migrates(void)
{
    const char* kLegacyXml =
        "<?xml version=\"1.0\"?>\n"
        "<CONFIG2>\n"
        "  <AIRCRAFT>\n"
        "    <G_LIMIT>3.8</G_LIMIT>\n"
        "  </AIRCRAFT>\n"
        "</CONFIG2>\n";

    OnSpeedConfig cfg;
    XmlParseStatus st = ParseXml(kLegacyXml, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(st));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  3.8f,  cfg.fAcGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.52f, cfg.fAcNegGlimit);
}

// Old config with a positive that doesn't match any named preset
// (e.g. a pilot who edited only the positive limit on Gen2): falls
// through to the LoadDefaults seed for the negative, and the UI
// renders this as Custom because the positive is off-preset.
void test_aircraft_glimit_legacy_unmatched_pos_uses_default_neg(void)
{
    const char* kLegacyXml =
        "<?xml version=\"1.0\"?>\n"
        "<CONFIG2>\n"
        "  <AIRCRAFT>\n"
        "    <G_LIMIT>2.5</G_LIMIT>\n"
        "  </AIRCRAFT>\n"
        "</CONFIG2>\n";

    OnSpeedConfig cfg;
    XmlParseStatus st = ParseXml(kLegacyXml, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(st));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  2.5f,   cfg.fAcGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.76f,  cfg.fAcNegGlimit);
}

// Round-trip the new Custom-storage fields independently of the
// active fields.  Picking Utility (active) while the pilot has Custom
// values typed (5.5 / -2.5) must persist all four through Save +
// Reload.
void test_aircraft_custom_glimit_storage_roundtrip(void)
{
    OnSpeedConfig a;
    a.fAcGlimit          =  4.4f;   // Utility active
    a.fAcNegGlimit       = -1.76f;
    a.fCustomAcGlimit    =  5.5f;   // pilot's typed Custom values
    a.fCustomAcNegGlimit = -2.5f;

    std::string xml = EmitXml(a);
    OnSpeedConfig b;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, b)));

    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  4.4f,  b.fAcGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.76f, b.fAcNegGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  5.5f,  b.fCustomAcGlimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -2.5f,  b.fCustomAcNegGlimit);
}

void test_vno_block_roundtrip(void)
{
    OnSpeedConfig a;
    a.iVno = 160;
    a.uVnoChimeInterval = 300u;
    a.bVnoChimeEnabled  = true;

    std::string xml = EmitXml(a);
    OnSpeedConfig b;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, b)));

    TEST_ASSERT_EQUAL_INT(160, b.iVno);
    TEST_ASSERT_EQUAL_UINT32(300u, b.uVnoChimeInterval);
    TEST_ASSERT_TRUE(b.bVnoChimeEnabled);
}

void test_load_limit_asymmetric_roundtrip(void)
{
    OnSpeedConfig a;
    a.fLoadLimitPositive   =  3.5f;
    a.fLoadLimitNegative   = -1.5f;
    a.fAsymmetricGyroLimit = 20.0f;
    a.fAsymmetricReduction = 0.5f;

    std::string xml = EmitXml(a);
    OnSpeedConfig b;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, b)));

    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  3.5f,  b.fLoadLimitPositive);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -1.5f,  b.fLoadLimitNegative);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 20.0f,  b.fAsymmetricGyroLimit);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  0.5f,  b.fAsymmetricReduction);
}

// ============================================================================
// Single flap entry with every field set, including AoaCurve.
// ============================================================================

void test_single_flap_entry_roundtrip(void)
{
    OnSpeedConfig a;
    a.aFlaps.clear();
    OnSpeedConfig::SuFlaps f;
    f.iDegrees        = 20;
    f.iPotPosition    = 1500;
    f.fLDMAXAOA       = 3.5f;
    f.fONSPEEDFASTAOA = 4.1f;
    f.fONSPEEDSLOWAOA = 5.2f;
    f.fSTALLWARNAOA   = 6.8f;
    f.fSTALLAOA       = 8.0f;
    f.fMANAOA         = 7.0f;
    f.fAlpha0         = -1.5f;
    f.fAlphaStall     = 9.0f;
    f.fKFit           = 1500.0f;
    f.AoaCurve.iCurveType = 1;
    f.AoaCurve.afCoeff[0] = 0.5f;
    f.AoaCurve.afCoeff[1] = 1.5f;
    f.AoaCurve.afCoeff[2] = 2.5f;
    f.AoaCurve.afCoeff[3] = 3.5f;
    a.aFlaps.push_back(f);

    std::string xml = EmitXml(a);
    OnSpeedConfig b;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, b)));

    TEST_ASSERT_EQUAL_size_t(1u, b.aFlaps.size());
    TEST_ASSERT_TRUE(FlapsEqual(f, b.aFlaps[0]));
}

// ============================================================================
// Unknown tag is silently ignored.
// ============================================================================

void test_unknown_tag_ignored(void)
{
    static constexpr const char* kUnknown = R"XML(<CONFIG2>
        <AOA_SMOOTHING>42</AOA_SMOOTHING>
        <SOMETHING_FROM_THE_FUTURE>whatever</SOMETHING_FROM_THE_FUTURE>
        <ANOTHER>
            <NESTED>true</NESTED>
        </ANOTHER>
    </CONFIG2>)XML";

    OnSpeedConfig cfg;
    XmlParseStatus st = ParseXml(kUnknown, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok), static_cast<int>(st));
    TEST_ASSERT_EQUAL_INT(42, cfg.iAoaSmoothing);
}

// ============================================================================
// status -> string mapping is stable.
// ============================================================================

void test_status_to_string(void)
{
    TEST_ASSERT_EQUAL_STRING("Ok",
        onspeed::config::XmlParseStatusToString(XmlParseStatus::Ok));
    TEST_ASSERT_EQUAL_STRING("Malformed",
        onspeed::config::XmlParseStatusToString(XmlParseStatus::Malformed));
    TEST_ASSERT_EQUAL_STRING("MissingRoot",
        onspeed::config::XmlParseStatusToString(XmlParseStatus::MissingRoot));
    TEST_ASSERT_EQUAL_STRING("TooManyFlaps",
        onspeed::config::XmlParseStatusToString(XmlParseStatus::TooManyFlaps));
}

// ============================================================================
// Empty text node leaves the prior value untouched (matches historical
// XML_GET_STR macro behavior). Critical: in the production load flow the
// caller runs LoadDefaults() first; an empty <EFISTYPE></EFISTYPE> in the
// XML must NOT clobber the default to "" because that disables EFIS reads
// silently in ApplyPostParseSideEffects.
// ============================================================================

void test_empty_text_tag_preserves_prior_value(void)
{
    static constexpr const char* kEmpty = R"XML(<CONFIG2>
        <REPLAYLOGFILENAME></REPLAYLOGFILENAME>
        <EFISTYPE></EFISTYPE>
        <PORTS></PORTS>
    </CONFIG2>)XML";

    OnSpeedConfig cfg;
    cfg.sReplayLogFileName = "stale.csv";
    cfg.sEfisType          = "VN-300";
    cfg.sPortsOrientation  = "FORWARD";
    XmlParseStatus st = ParseXml(kEmpty, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok), static_cast<int>(st));
    // Empty tag must not wipe the prior value — would silently disable EFIS,
    // corrupt IMU axis lookup, and drop the bCalSourceEfis cache.
    TEST_ASSERT_EQUAL_STRING("stale.csv", cfg.sReplayLogFileName.c_str());
    TEST_ASSERT_EQUAL_STRING("VN-300",    cfg.sEfisType.c_str());
    TEST_ASSERT_EQUAL_STRING("FORWARD",   cfg.sPortsOrientation.c_str());
}

// ============================================================================
// Exactly MAX_AOA_CURVES flap entries is the boundary — no TooManyFlaps.
// ============================================================================

void test_parse_exactly_max_flaps_ok(void)
{
    std::string xml = "<CONFIG2>";
    for (int i = 0; i < onspeed::MAX_AOA_CURVES; ++i) {
        xml += "<FLAP_POSITION><DEGREES>";
        xml += std::to_string(i * 5);
        xml += "</DEGREES></FLAP_POSITION>";
    }
    xml += "</CONFIG2>";

    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(xml, cfg)));
    TEST_ASSERT_EQUAL_size_t(static_cast<std::size_t>(onspeed::MAX_AOA_CURVES),
                             cfg.aFlaps.size());
}

// ============================================================================
// Ahrs algorithm scalar is parsed.
// ============================================================================

void test_ahrs_algorithm_parse(void)
{
    static constexpr const char* kEkf = R"XML(<CONFIG2>
        <AHRS_ALGORITHM>1</AHRS_ALGORITHM>
    </CONFIG2>)XML";

    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(kEkf, cfg)));
    TEST_ASSERT_EQUAL_INT(1, cfg.iAhrsAlgorithm);
}

// ============================================================================
// Cal source -> bCalSourceEfis cache is derived, not stored — make sure it
// tracks sCalSource on load.
// ============================================================================

void test_cal_source_efis_cache(void)
{
    static constexpr const char* kEfis = R"XML(<CONFIG2>
        <CALWIZ_SOURCE>EFIS</CALWIZ_SOURCE>
    </CONFIG2>)XML";
    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(kEfis, cfg)));
    TEST_ASSERT_TRUE(cfg.bCalSourceEfis);

    static constexpr const char* kOnSpeed = R"XML(<CONFIG2>
        <CALWIZ_SOURCE>ONSPEED</CALWIZ_SOURCE>
    </CONFIG2>)XML";
    OnSpeedConfig cfg2;
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(kOnSpeed, cfg2)));
    TEST_ASSERT_FALSE(cfg2.bCalSourceEfis);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_parse_n720ak_known_values);
    RUN_TEST(test_parse_emit_roundtrip_n720ak);
    RUN_TEST(test_parse_defaults_roundtrip);
    RUN_TEST(test_parse_minimal_uses_defaults_for_missing);
    RUN_TEST(test_parse_malformed_returns_error);
    RUN_TEST(test_parse_v1_root_rejected);
    RUN_TEST(test_parse_flap_overflow_safely);
    RUN_TEST(test_pstaticbias_sign_convention_preserved);
    RUN_TEST(test_enum_parsing_datasource);
    RUN_TEST(test_enum_parsing_serialoutformat);
    RUN_TEST(test_bool_parsing_true_false);
    RUN_TEST(test_float_parsing_scientific);
    RUN_TEST(test_volume_block_roundtrip);
    RUN_TEST(test_cas_curve_roundtrip);
    RUN_TEST(test_bias_block_roundtrip);
    RUN_TEST(test_aircraft_block_roundtrip);
    RUN_TEST(test_aircraft_custom_glimit_roundtrip);
    RUN_TEST(test_aircraft_glimit_legacy_no_neg_field);
    RUN_TEST(test_aircraft_glimit_legacy_aerobatic_migrates);
    RUN_TEST(test_aircraft_glimit_legacy_normal_migrates);
    RUN_TEST(test_aircraft_glimit_legacy_unmatched_pos_uses_default_neg);
    RUN_TEST(test_aircraft_custom_glimit_storage_roundtrip);
    RUN_TEST(test_vno_block_roundtrip);
    RUN_TEST(test_load_limit_asymmetric_roundtrip);
    RUN_TEST(test_single_flap_entry_roundtrip);
    RUN_TEST(test_unknown_tag_ignored);
    RUN_TEST(test_status_to_string);
    RUN_TEST(test_empty_text_tag_preserves_prior_value);
    RUN_TEST(test_parse_exactly_max_flaps_ok);
    RUN_TEST(test_ahrs_algorithm_parse);
    RUN_TEST(test_cal_source_efis_cache);

    return UNITY_END();
}
