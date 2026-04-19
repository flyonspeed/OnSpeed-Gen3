// test_config_v1.cpp — unit tests for onspeed::config::ParseV1 / IsV1Format.
//
// Coverage goals:
//   - IsV1Format detects V1 (<CONFIG>) vs V2 (<CONFIG2>) correctly.
//   - ParseV1 on a realistic V1 fixture pins known field values so future
//     parser bugs can't silently break Gen2-era SD-card migrations.
//   - Empty / malformed / non-V1 inputs return the right status.
//   - AUDIT #009: PStaticBias V1->V2 sign-convention normalisation is
//     preserved verbatim.  V1 input `+X` and V2 XML `-X` must produce the
//     same in-memory `fPStaticBias` value.  This is the test that pins
//     the invariant across both parsers permanently.
//
// Fixture note: the sample V1 config (kSampleV1 below) is HAND-CRAFTED to
// match the Gen2-era format the sketch's original parser consumed.  It
// mirrors the tag vocabulary observed in the sketch's pre-extraction
// LoadConfigFromString (software/OnSpeed-Gen3-ESP32/src/config/Config.cpp,
// branch `extraction/phase-3-1-config`, pre-Task-3) — see git log
// commit b0acca3 for the V1 PStaticBias sign-convention write-up.  No real
// Gen2 V1 config is checked into the repo; if one surfaces (e.g. from the
// archive of pre-Gen3 installations) it should replace this fixture.

#include <unity.h>

#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

#include <config/ConfigV1Parse.h>
#include <config/ConfigXmlParse.h>
#include <config/OnSpeedConfig.h>
#include <util/OnSpeedTypes.h>

using namespace onspeed;
using onspeed::config::OnSpeedConfig;
using onspeed::config::SuDataSource;
using onspeed::config::IsV1Format;
using onspeed::config::ParseV1;
using onspeed::config::ParseXml;
using onspeed::config::V1ParseStatus;
using onspeed::config::V1ParseStatusToString;
using onspeed::config::XmlParseStatus;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Fixtures — hand-crafted to match the Gen2-era V1 tag vocabulary.
// ============================================================================

// Sample V1 config mirroring the Gen2 firmware output format.  Values
// chosen to be distinctive enough to detect parser bugs (e.g. field-swap
// regressions).  PStaticBias is +0.5 — see test_v1_pstatic_negation_preserved.
static constexpr const char* kSampleV1 = R"V1(<CONFIG>
<AOA_SMOOTHING>15</AOA_SMOOTHING>
<PRESSURE_SMOOTHING>12</PRESSURE_SMOOTHING>
<MUTE_AUDIO_UNDER_IAS>35</MUTE_AUDIO_UNDER_IAS>
<DATASOURCE>SENSORS</DATASOURCE>
<REPLAYLOGFILENAME></REPLAYLOGFILENAME>
<FLAPDEGREES>0,10,30</FLAPDEGREES>
<FLAPPOTPOSITIONS>3900,2300,100</FLAPPOTPOSITIONS>
<SETPOINT_LDMAXAOA>4.1,2.3,-1.12</SETPOINT_LDMAXAOA>
<SETPOINT_ONSPEEDFASTAOA>4.08,2.69,3.79</SETPOINT_ONSPEEDFASTAOA>
<SETPOINT_ONSPEEDSLOWAOA>4.95,4.1,5.23</SETPOINT_ONSPEEDSLOWAOA>
<SETPOINT_STALLWARNAOA>7.98,7.6,9.24</SETPOINT_STALLWARNAOA>
<SETPOINT_STALLAOA>0,0,0</SETPOINT_STALLAOA>
<SETPOINT_ALPHA0>-0.5,-1.2,-2.0</SETPOINT_ALPHA0>
<SETPOINT_ALPHASTALL>8.0,7.8,9.5</SETPOINT_ALPHASTALL>
<AOA_CURVE_FLAPS0>0,-6.14,26.41,4.9,1</AOA_CURVE_FLAPS0>
<AOA_CURVE_FLAPS1>0,-43.78,39.64,4.39,1</AOA_CURVE_FLAPS1>
<AOA_CURVE_FLAPS2>0,-13.57,39.87,2.16,1</AOA_CURVE_FLAPS2>
<VOLUMECONTROL>0</VOLUMECONTROL>
<VOLUME_HIGH_ANALOG>1023</VOLUME_HIGH_ANALOG>
<VOLUME_LOW_ANALOG>1</VOLUME_LOW_ANALOG>
<VOLUME_DEFAULT>25</VOLUME_DEFAULT>
<3DAUDIO>0</3DAUDIO>
<OVERGWARNING>1</OVERGWARNING>
<CAS_CURVE>0,0,0.97,4.64,1</CAS_CURVE>
<CAS_ENABLED>1</CAS_ENABLED>
<PORTS_ORIENTATION>DOWN</PORTS_ORIENTATION>
<BOX_TOP_ORIENTATION>FORWARD</BOX_TOP_ORIENTATION>
<EFISTYPE>ADVANCED</EFISTYPE>
<CALWIZ_SOURCE>ONSPEED</CALWIZ_SOURCE>
<PFWD_BIAS>2048</PFWD_BIAS>
<P45_BIAS>2047</P45_BIAS>
<PSTATIC_BIAS>0.5</PSTATIC_BIAS>
<GX_BIAS>0.054</GX_BIAS>
<GY_BIAS>0.142</GY_BIAS>
<GZ_BIAS>0.248</GZ_BIAS>
<PITCH_BIAS>4.41</PITCH_BIAS>
<ROLL_BIAS>-0.82</ROLL_BIAS>
<BOOM>0</BOOM>
<BOOMCHECKSUM>1</BOOMCHECKSUM>
<SERIALEFISDATA>1</SERIALEFISDATA>
<OATSENSOR>0</OATSENSOR>
<SERIALOUTFORMAT>ONSPEED</SERIALOUTFORMAT>
<LOADLIMITPOSITIVE>2.5</LOADLIMITPOSITIVE>
<LOADLIMITNEGATIVE>-1.0</LOADLIMITNEGATIVE>
<VNO>158</VNO>
<VNO_CHIME_INTERVAL>180</VNO_CHIME_INTERVAL>
<VNO_CHIME_ENABLED>1</VNO_CHIME_ENABLED>
<SDLOGGING>1</SDLOGGING>
</CONFIG>
)V1";

// Minimal V1 config — just the root wrapper, no fields.  Parse should
// succeed and leave every field at whatever the caller passed in.
static constexpr const char* kMinimalV1 = R"V1(<CONFIG>
</CONFIG>
)V1";

// V2 XML document — must be rejected by IsV1Format and ParseV1.
static constexpr const char* kV2Doc = R"V1(<CONFIG2>
    <AOA_SMOOTHING>20</AOA_SMOOTHING>
</CONFIG2>
)V1";

// Totally malformed / garbage input — no V1 markers at all.
static constexpr const char* kGarbage = "this is not a config file at all";

// Empty / whitespace.
static constexpr const char* kWhitespace = "   \n\t  \n";

// ============================================================================
// IsV1Format — format detection.
// ============================================================================

void test_is_v1_format_detects_v1(void)
{
    TEST_ASSERT_TRUE(IsV1Format(kSampleV1));
    TEST_ASSERT_TRUE(IsV1Format(kMinimalV1));
    // Inline tag-only version also matches.
    TEST_ASSERT_TRUE(IsV1Format("<CONFIG><AOA_SMOOTHING>5</AOA_SMOOTHING></CONFIG>"));
}

void test_is_v1_format_rejects_v2_xml(void)
{
    // V2 CONFIG2 root must not be mistaken for V1.
    TEST_ASSERT_FALSE(IsV1Format(kV2Doc));
}

void test_is_v1_format_rejects_garbage(void)
{
    TEST_ASSERT_FALSE(IsV1Format(kGarbage));
    TEST_ASSERT_FALSE(IsV1Format(""));
    TEST_ASSERT_FALSE(IsV1Format("<CONFIG>"));           // open only
    TEST_ASSERT_FALSE(IsV1Format("</CONFIG>"));          // close only
}

// ============================================================================
// ParseV1 — empty / malformed / root-missing error paths.
// ============================================================================

void test_parse_empty_returns_empty_status(void)
{
    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Empty),
                      static_cast<int>(ParseV1("", cfg)));
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Empty),
                      static_cast<int>(ParseV1(kWhitespace, cfg)));
}

void test_parse_non_v1_returns_missing_root(void)
{
    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::MissingRoot),
                      static_cast<int>(ParseV1(kV2Doc, cfg)));
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::MissingRoot),
                      static_cast<int>(ParseV1(kGarbage, cfg)));
}

void test_parse_minimal_v1_ok(void)
{
    OnSpeedConfig cfg;
    cfg.iAoaSmoothing = 77;           // pre-set so we can verify preservation
    cfg.fLoadLimitPositive = 42.0f;

    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kMinimalV1, cfg)));

    // Missing tags: caller's pre-parse values for scalar-only fields are
    // preserved (ToInt/ToFloat return 0 for empty tags, but since the
    // *tags themselves* are absent, the assignment is from "", yielding 0).
    // Document current behaviour: field is reset to 0, not preserved.
    TEST_ASSERT_EQUAL_INT(0, cfg.iAoaSmoothing);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, cfg.fLoadLimitPositive);
}

// ============================================================================
// ParseV1 — known-value checks.
// ============================================================================

void test_parse_v1_scalars(void)
{
    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kSampleV1, cfg)));

    TEST_ASSERT_EQUAL_INT(15, cfg.iAoaSmoothing);
    TEST_ASSERT_EQUAL_INT(12, cfg.iPressureSmoothing);
    TEST_ASSERT_EQUAL_INT(35, cfg.iMuteAudioUnderIAS);
    TEST_ASSERT_EQUAL(SuDataSource::EnSensors, cfg.suDataSrc.enSrc);
    TEST_ASSERT_EQUAL_STRING("", cfg.sReplayLogFileName.c_str());

    // Volume
    TEST_ASSERT_FALSE(cfg.bVolumeControl);
    TEST_ASSERT_EQUAL_INT(1023, cfg.iVolumeHighAnalog);
    TEST_ASSERT_EQUAL_INT(1,    cfg.iVolumeLowAnalog);
    TEST_ASSERT_EQUAL_INT(25,   cfg.iDefaultVolume);

    // 3D audio / over-G
    TEST_ASSERT_FALSE(cfg.bAudio3D);
    TEST_ASSERT_TRUE (cfg.bOverGWarning);

    // Orientation / EFIS
    TEST_ASSERT_EQUAL_STRING("DOWN",     cfg.sPortsOrientation.c_str());
    TEST_ASSERT_EQUAL_STRING("FORWARD",  cfg.sBoxtopOrientation.c_str());
    TEST_ASSERT_EQUAL_STRING("ADVANCED", cfg.sEfisType.c_str());

    // Cal source
    TEST_ASSERT_EQUAL_STRING("ONSPEED", cfg.sCalSource.c_str());
    TEST_ASSERT_FALSE(cfg.bCalSourceEfis);

    // Biases (PStatic handled in its own test)
    TEST_ASSERT_EQUAL_INT(2048, cfg.iPFwdBias);
    TEST_ASSERT_EQUAL_INT(2047, cfg.iP45Bias);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.054f, cfg.fGxBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.142f, cfg.fGyBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.248f, cfg.fGzBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.41f,  cfg.fPitchBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.82f, cfg.fRollBias);

    // Serial input toggles
    TEST_ASSERT_FALSE(cfg.bReadBoom);
    TEST_ASSERT_TRUE (cfg.bBoomChecksum);
    TEST_ASSERT_TRUE (cfg.bReadEfisData);
    TEST_ASSERT_FALSE(cfg.bOatSensor);

    // Serial output format — cache is derived from string.
    TEST_ASSERT_EQUAL_STRING("ONSPEED", cfg.sSerialOutFormat.c_str());
    TEST_ASSERT_EQUAL(OnSpeedConfig::EnSerialFmtOnSpeed, cfg.enSerialOutFormat);

    // Load limits
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  2.5f, cfg.fLoadLimitPositive);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, cfg.fLoadLimitNegative);

    // Vno
    TEST_ASSERT_EQUAL_INT(158, cfg.iVno);
    TEST_ASSERT_EQUAL_UINT32(180u, cfg.uVnoChimeInterval);
    TEST_ASSERT_TRUE(cfg.bVnoChimeEnabled);

    // SD logging
    TEST_ASSERT_TRUE(cfg.bSdLogging);
}

void test_parse_v1_flap_arrays(void)
{
    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kSampleV1, cfg)));

    // Three flap entries (0, 10, 30), sorted by degrees.
    TEST_ASSERT_EQUAL_size_t(3u, cfg.aFlaps.size());
    TEST_ASSERT_EQUAL_INT( 0, cfg.aFlaps[0].iDegrees);
    TEST_ASSERT_EQUAL_INT(10, cfg.aFlaps[1].iDegrees);
    TEST_ASSERT_EQUAL_INT(30, cfg.aFlaps[2].iDegrees);

    // Pot positions
    TEST_ASSERT_EQUAL_INT(3900, cfg.aFlaps[0].iPotPosition);
    TEST_ASSERT_EQUAL_INT(2300, cfg.aFlaps[1].iPotPosition);
    TEST_ASSERT_EQUAL_INT( 100, cfg.aFlaps[2].iPotPosition);

    // Sampled setpoints (first and last flap).
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  4.1f,  cfg.aFlaps[0].fLDMAXAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.12f, cfg.aFlaps[2].fLDMAXAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  7.98f, cfg.aFlaps[0].fSTALLWARNAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  9.24f, cfg.aFlaps[2].fSTALLWARNAOA);

    // alpha_0 and alpha_stall propagate from the CSV lists.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.5f, cfg.aFlaps[0].fAlpha0);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -2.0f, cfg.aFlaps[2].fAlpha0);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  8.0f, cfg.aFlaps[0].fAlphaStall);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  9.5f, cfg.aFlaps[2].fAlphaStall);
}

void test_parse_v1_aoa_curves(void)
{
    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kSampleV1, cfg)));

    // <AOA_CURVE_FLAPS0>0,-6.14,26.41,4.9,1</>
    TEST_ASSERT_EQUAL_UINT8(1u, cfg.aFlaps[0].AoaCurve.iCurveType);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,   0.0f, cfg.aFlaps[0].AoaCurve.afCoeff[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f,  -6.14f, cfg.aFlaps[0].AoaCurve.afCoeff[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f,  26.41f, cfg.aFlaps[0].AoaCurve.afCoeff[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,   4.9f,  cfg.aFlaps[0].AoaCurve.afCoeff[3]);

    // <AOA_CURVE_FLAPS2>0,-13.57,39.87,2.16,1</>
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, -13.57f, cfg.aFlaps[2].AoaCurve.afCoeff[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f,  39.87f, cfg.aFlaps[2].AoaCurve.afCoeff[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,   2.16f, cfg.aFlaps[2].AoaCurve.afCoeff[3]);
}

void test_parse_v1_cas_curve(void)
{
    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kSampleV1, cfg)));

    TEST_ASSERT_EQUAL_UINT8(1u, cfg.CasCurve.iCurveType);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f,  cfg.CasCurve.afCoeff[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f,  cfg.CasCurve.afCoeff[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.97f, cfg.CasCurve.afCoeff[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 4.64f, cfg.CasCurve.afCoeff[3]);
    TEST_ASSERT_TRUE(cfg.bCasCurveEnabled);
}

// ============================================================================
// AUDIT #009: V1 PStaticBias is negated on load.  Gen2 stored bias for
// `Pstatic + bias` (addition); Gen3 uses `Pstatic - bias` (subtraction).
// The negation normalises the V1 value to the Gen3/V2 in-memory convention.
// ============================================================================

void test_v1_pstatic_negation_preserved(void)
{
    // Fixture has <PSTATIC_BIAS>0.5</PSTATIC_BIAS> — in-memory must be -0.5.
    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kSampleV1, cfg)));

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.5f, cfg.fPStaticBias);
    TEST_ASSERT_TRUE(cfg.fPStaticBias < 0.0f);

    // And a negative V1 input should end up positive in memory — sign
    // flipped, regardless of source sign.
    static constexpr const char* kNegativeV1 =
        "<CONFIG><PSTATIC_BIAS>-1.25</PSTATIC_BIAS></CONFIG>";
    OnSpeedConfig cfg2;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kNegativeV1, cfg2)));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.25f, cfg2.fPStaticBias);
}

void test_v1_v2_pstatic_normalize_to_same_memory(void)
{
    // This is the audit #009 fix in test form.  V1 stores the bias with
    // Gen2's sign convention (add); V2 stores it with Gen3's convention
    // (subtract).  Both parsers must yield the same in-memory value for
    // equivalent physical biases.
    //
    // Physical bias value:  -0.75 (Gen3/V2 convention — subtract this).
    // V2 XML:               <PSTATIC>-0.75</PSTATIC>        -> fPStaticBias = -0.75
    // V1 CSV:               <PSTATIC_BIAS>0.75</PSTATIC_BIAS> (Gen2 stored +X)
    //                       -> fPStaticBias = -ToFloat("0.75") = -0.75
    // Both end up with the SAME in-memory value.

    static constexpr const char* kV1 =
        "<CONFIG><PSTATIC_BIAS>0.75</PSTATIC_BIAS></CONFIG>";
    static constexpr const char* kV2 = R"XML(<CONFIG2>
        <BIAS><PSTATIC>-0.75</PSTATIC></BIAS>
    </CONFIG2>)XML";

    OnSpeedConfig v1cfg;
    OnSpeedConfig v2cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kV1, v1cfg)));
    TEST_ASSERT_EQUAL(static_cast<int>(XmlParseStatus::Ok),
                      static_cast<int>(ParseXml(kV2, v2cfg)));

    // Both parsers arrive at the same in-memory value.
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, v1cfg.fPStaticBias, v2cfg.fPStaticBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.75f, v1cfg.fPStaticBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.75f, v2cfg.fPStaticBias);
}

// ============================================================================
// Boolean variants — YES/ENABLED/ON and 1 should all parse as true.
// ============================================================================

void test_parse_v1_boolean_variants(void)
{
    static constexpr const char* kBools = R"V1(<CONFIG>
<OVERGWARNING>YES</OVERGWARNING>
<SDLOGGING>ENABLED</SDLOGGING>
<BOOM>ON</BOOM>
<BOOMCHECKSUM>1</BOOMCHECKSUM>
<OATSENSOR>NO</OATSENSOR>
<3DAUDIO>0</3DAUDIO>
<VOLUMECONTROL>false</VOLUMECONTROL>
</CONFIG>)V1";

    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kBools, cfg)));

    TEST_ASSERT_TRUE (cfg.bOverGWarning);    // YES
    TEST_ASSERT_TRUE (cfg.bSdLogging);       // ENABLED
    TEST_ASSERT_TRUE (cfg.bReadBoom);        // ON
    TEST_ASSERT_TRUE (cfg.bBoomChecksum);    // 1
    TEST_ASSERT_FALSE(cfg.bOatSensor);       // NO
    TEST_ASSERT_FALSE(cfg.bAudio3D);         // 0
    TEST_ASSERT_FALSE(cfg.bVolumeControl);   // false (unknown -> false)
}

// ============================================================================
// Data source enum parsing.
// ============================================================================

void test_parse_v1_datasource_variants(void)
{
    auto parse = [](const char* val) {
        std::string txt = std::string("<CONFIG><DATASOURCE>") + val
                        + "</DATASOURCE></CONFIG>";
        OnSpeedConfig cfg;
        TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                          static_cast<int>(ParseV1(txt, cfg)));
        return cfg.suDataSrc.enSrc;
    };

    TEST_ASSERT_EQUAL(SuDataSource::EnSensors,    parse("SENSORS"));
    TEST_ASSERT_EQUAL(SuDataSource::EnReplay,     parse("REPLAYLOGFILE"));
    TEST_ASSERT_EQUAL(SuDataSource::EnTestPot,    parse("TESTPOT"));
    TEST_ASSERT_EQUAL(SuDataSource::EnRangeSweep, parse("RANGESWEEP"));
    TEST_ASSERT_EQUAL(SuDataSource::EnUnknown,    parse("BOGUS"));
}

// ============================================================================
// Cal source cache is derived from sCalSource.
// ============================================================================

void test_parse_v1_calsource_efis_cache(void)
{
    static constexpr const char* kEfis =
        "<CONFIG><CALWIZ_SOURCE>EFIS</CALWIZ_SOURCE></CONFIG>";
    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kEfis, cfg)));
    TEST_ASSERT_TRUE(cfg.bCalSourceEfis);
    TEST_ASSERT_EQUAL_STRING("EFIS", cfg.sCalSource.c_str());

    static constexpr const char* kOnSpeed =
        "<CONFIG><CALWIZ_SOURCE>ONSPEED</CALWIZ_SOURCE></CONFIG>";
    OnSpeedConfig cfg2;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kOnSpeed, cfg2)));
    TEST_ASSERT_FALSE(cfg2.bCalSourceEfis);
}

// ============================================================================
// Flap sorting — V1 parser sorts by DEGREES, matching V2.
// ============================================================================

void test_parse_v1_flaps_sorted_by_degrees(void)
{
    // Pass flaps in unsorted order — 33, 0, 16 — then verify sort.
    static constexpr const char* kUnsorted = R"V1(<CONFIG>
<FLAPDEGREES>33,0,16</FLAPDEGREES>
<FLAPPOTPOSITIONS>100,3900,2300</FLAPPOTPOSITIONS>
<SETPOINT_LDMAXAOA>-1.12,4.1,2.3</SETPOINT_LDMAXAOA>
</CONFIG>)V1";

    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kUnsorted, cfg)));

    TEST_ASSERT_EQUAL_size_t(3u, cfg.aFlaps.size());
    TEST_ASSERT_EQUAL_INT( 0, cfg.aFlaps[0].iDegrees);
    TEST_ASSERT_EQUAL_INT(16, cfg.aFlaps[1].iDegrees);
    TEST_ASSERT_EQUAL_INT(33, cfg.aFlaps[2].iDegrees);

    // After sort, the pot values must travel WITH their flap entry:
    // degrees=0 -> potPos=3900, degrees=16 -> 2300, degrees=33 -> 100.
    TEST_ASSERT_EQUAL_INT(3900, cfg.aFlaps[0].iPotPosition);
    TEST_ASSERT_EQUAL_INT(2300, cfg.aFlaps[1].iPotPosition);
    TEST_ASSERT_EQUAL_INT( 100, cfg.aFlaps[2].iPotPosition);

    // LDMAX values also move with their flap.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  4.1f,  cfg.aFlaps[0].fLDMAXAOA);  // was idx 1
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  2.3f,  cfg.aFlaps[1].fLDMAXAOA);  // was idx 2
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.12f, cfg.aFlaps[2].fLDMAXAOA);  // was idx 0
}

// ============================================================================
// V1ParseStatusToString mapping.
// ============================================================================

void test_status_to_string(void)
{
    TEST_ASSERT_EQUAL_STRING("Ok",
        V1ParseStatusToString(V1ParseStatus::Ok));
    TEST_ASSERT_EQUAL_STRING("Empty",
        V1ParseStatusToString(V1ParseStatus::Empty));
    TEST_ASSERT_EQUAL_STRING("MissingRoot",
        V1ParseStatusToString(V1ParseStatus::MissingRoot));
}

// ============================================================================
// Unknown / missing tags don't cause failures — fields just stay at their
// parse-time zero-or-empty default.
// ============================================================================

void test_parse_v1_missing_tags_are_zero(void)
{
    static constexpr const char* kSparse = R"V1(<CONFIG>
<AOA_SMOOTHING>42</AOA_SMOOTHING>
</CONFIG>)V1";

    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL(static_cast<int>(V1ParseStatus::Ok),
                      static_cast<int>(ParseV1(kSparse, cfg)));

    // Present scalar parses correctly.
    TEST_ASSERT_EQUAL_INT(42, cfg.iAoaSmoothing);

    // Absent tags all read as empty strings -> zero-valued fields.
    TEST_ASSERT_EQUAL_INT(0, cfg.iPressureSmoothing);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, cfg.fPStaticBias);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, cfg.fPitchBias);
    TEST_ASSERT_EQUAL_size_t(0u, cfg.aFlaps.size());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_is_v1_format_detects_v1);
    RUN_TEST(test_is_v1_format_rejects_v2_xml);
    RUN_TEST(test_is_v1_format_rejects_garbage);

    RUN_TEST(test_parse_empty_returns_empty_status);
    RUN_TEST(test_parse_non_v1_returns_missing_root);
    RUN_TEST(test_parse_minimal_v1_ok);

    RUN_TEST(test_parse_v1_scalars);
    RUN_TEST(test_parse_v1_flap_arrays);
    RUN_TEST(test_parse_v1_aoa_curves);
    RUN_TEST(test_parse_v1_cas_curve);

    RUN_TEST(test_v1_pstatic_negation_preserved);
    RUN_TEST(test_v1_v2_pstatic_normalize_to_same_memory);

    RUN_TEST(test_parse_v1_boolean_variants);
    RUN_TEST(test_parse_v1_datasource_variants);
    RUN_TEST(test_parse_v1_calsource_efis_cache);
    RUN_TEST(test_parse_v1_flaps_sorted_by_degrees);

    RUN_TEST(test_status_to_string);
    RUN_TEST(test_parse_v1_missing_tags_are_zero);

    return UNITY_END();
}
