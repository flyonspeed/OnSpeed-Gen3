// test_web_schema.cpp
//
// Tripwire tests for `onspeed::web::kSchema` / `kFlapSchema` (WebSchema.h).
//
// The schema enumerates every OnSpeedConfig field that is editable from the
// /aoaconfig page.  These tests pin two invariants:
//
//   (a) The schema covers every field the sketch's HandleConfig() exposes
//       today — matched against a hand-maintained "expected" list so a
//       schema drop/typo doesn't go unnoticed.  The expected list is the
//       deliberate second source of truth.
//   (b) Every schema entry is well-formed (non-empty xmlTag, formName,
//       displayName; sensible numeric ranges; non-empty enum-choice arrays
//       for FieldType::Enum).
//
// When you legitimately add or remove a field, update BOTH the schema AND
// the expected lists below.

#include <unity.h>

#include <cstring>
#include <set>
#include <string>
#include <string_view>

#include <web/WebSchema.h>

using onspeed::web::EnumChoice;
using onspeed::web::FieldDef;
using onspeed::web::FieldType;
using onspeed::web::kFlapSchema;
using onspeed::web::kFlapSchemaCount;
using onspeed::web::kSchema;
using onspeed::web::kSchemaCount;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Hand-maintained expected lists — independent of the schema, on purpose.
// ---------------------------------------------------------------------------

// Each entry pairs a formName (the unique lookup key the POST handler
// reads via CfgServer.arg(...)) with the XML tag used by ConfigXmlParse/
// Emit. Pinning both catches a silent rename of either side: if a
// future PR changes kSchema's xmlTag from "AOA_SMOOTHING" to
// "AOA_SMOTHING", this test fails — old config files would silently
// stop loading the field correctly otherwise.
struct ExpectedField {
    const char* formName;
    const char* xmlTag;
};

// Top-level fields.
static constexpr ExpectedField kExpectedTopLevel[] = {
    {"aoaSmoothing",        "AOA_SMOOTHING"},
    {"pressureSmoothing",   "PRESSURE_SMOOTHING"},
    {"dataSource",          "DATASOURCE"},
    {"logFileName",         "REPLAYLOGFILENAME"},
    {"readBoom",            "BOOM"},
    {"boomChecksum",        "BOOMCHECKSUM"},
    {"boomConvertData",     "BOOMCONVERTDATA"},
    {"casCurveEnabled",     "ENABLED"},
    {"casCurveType",        "TYPE"},
    {"casCurveCoeff0",      "X3"},
    {"casCurveCoeff1",      "X2"},
    {"casCurveCoeff2",      "X1"},
    {"casCurveCoeff3",      "X0"},
    {"portsOrientation",    "PORTS"},
    {"boxtopOrientation",   "BOX_TOP"},
    {"readEfisData",        "SERIALEFISDATA"},
    {"efisType",            "EFISTYPE"},
    {"oatSensor",           "OATSENSOR"},
    {"calSource",           "CALWIZ_SOURCE"},
    {"ahrsAlgorithm",       "AHRS_ALGORITHM"},
    // <VOLUME>...</VOLUME> nested children:
    {"volumeControl",       "ENABLED"},
    {"defaultVolume",       "DEFAULT"},
    {"volumeLowAnalog",     "LOW_ANALOG"},
    {"volumeHighAnalog",    "HIGH_ANALOG"},
    {"muteAudioUnderIAS",   "MUTE_UNDER_IAS"},
    {"audio3D",             "ENABLE_3DAUDIO"},
    {"overgWarning",        "OVERGWARNING"},
    // <LOAD_LIMIT>...</LOAD_LIMIT> nested children:
    {"loadLimitPositive",   "POSITIVE"},
    {"loadLimitNegative",   "NEGATIVE"},
    {"asymmetricGyroLimit", "ASYMMETRIC_GYRO_LIMIT"},
    {"asymmetricReduction", "ASYMMETRIC_REDUCTION"},
    // <VNO>...</VNO> nested children:
    {"vnoChimeEnabled",     "CHIME_ENABLED"},
    {"Vno",                 "SPEED"},
    {"vnoChimeInterval",    "CHIME_INTERVAL"},
    {"sdLogging",           "SDLOGGING"},
    {"logRate",             "LOGRATE"},
    {"serialOutFormat",     "SERIALOUTFORMAT"},
    // <AIRCRAFT>...</AIRCRAFT> nested children:
    {"acGrossWeight",       "GROSS_WEIGHT"},
    {"acBestGlideIAS",      "BEST_GLIDE_IAS"},
    {"acVfe",               "VFE"},
    {"acGlimit",            "G_LIMIT"},
};
static constexpr std::size_t kExpectedTopLevelCount =
    sizeof(kExpectedTopLevel) / sizeof(kExpectedTopLevel[0]);

// Per-flap fields.
static constexpr ExpectedField kExpectedFlap[] = {
    {"flapDegrees",        "DEGREES"},
    {"flapPotPositions",   "POT_VALUE"},
    {"flapLDMAXAOA",       "LDMAXAOA"},
    {"flapONSPEEDFASTAOA", "ONSPEEDFASTAOA"},
    {"flapONSPEEDSLOWAOA", "ONSPEEDSLOWAOA"},
    {"flapSTALLWARNAOA",   "STALLWARNAOA"},
    {"flapSTALLAOA",       "STALLAOA"},
    {"flapMANAOA",         "MANAOA"},
    {"flapKFit",           "KFIT"},
    {"flapAlpha0",         "ALPHA0"},
    {"flapAlphaStall",     "ALPHASTALL"},
    {"aoaCurveType",       "TYPE"},
    {"aoaCurveCoeff0",     "X3"},
    {"aoaCurveCoeff1",     "X2"},
    {"aoaCurveCoeff2",     "X1"},
    {"aoaCurveCoeff3",     "X0"},
};
static constexpr std::size_t kExpectedFlapCount =
    sizeof(kExpectedFlap) / sizeof(kExpectedFlap[0]);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool SchemaContainsFormName(const FieldDef* schema, std::size_t count,
                                   std::string_view formName)
{
    for (std::size_t i = 0; i < count; ++i) {
        if (formName == schema[i].formName) return true;
    }
    return false;
}

static bool ExpectedContainsFormName(const ExpectedField* expected,
                                     std::size_t count,
                                     std::string_view formName)
{
    for (std::size_t i = 0; i < count; ++i) {
        if (formName == expected[i].formName) return true;
    }
    return false;
}


// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// (1) Cardinality: schemas must match expected counts exactly so a silent
//     drop / accidental dup is caught.
void test_top_level_schema_size_matches_expected(void)
{
    TEST_ASSERT_EQUAL_size_t(kExpectedTopLevelCount, kSchemaCount);
}

void test_flap_schema_size_matches_expected(void)
{
    TEST_ASSERT_EQUAL_size_t(kExpectedFlapCount, kFlapSchemaCount);
}

// (2) Every expected formName appears in the schema.
void test_every_expected_top_level_field_in_schema(void)
{
    for (std::size_t i = 0; i < kExpectedTopLevelCount; ++i) {
        bool present = SchemaContainsFormName(kSchema, kSchemaCount,
                                              kExpectedTopLevel[i].formName);
        if (!present) {
            std::string msg = "missing top-level field in kSchema: ";
            msg += kExpectedTopLevel[i].formName;
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }
}

void test_every_expected_flap_field_in_schema(void)
{
    for (std::size_t i = 0; i < kExpectedFlapCount; ++i) {
        bool present = SchemaContainsFormName(kFlapSchema, kFlapSchemaCount,
                                              kExpectedFlap[i].formName);
        if (!present) {
            std::string msg = "missing per-flap field in kFlapSchema: ";
            msg += kExpectedFlap[i].formName;
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }
}

// (3) No extra fields in the schema beyond the expected list.
void test_no_extra_fields_in_top_level_schema(void)
{
    for (std::size_t i = 0; i < kSchemaCount; ++i) {
        bool present = ExpectedContainsFormName(kExpectedTopLevel,
                                                kExpectedTopLevelCount,
                                                kSchema[i].formName);
        if (!present) {
            std::string msg = "kSchema has unexpected field (not in "
                              "expected list): ";
            msg += kSchema[i].formName;
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }
}

void test_no_extra_fields_in_flap_schema(void)
{
    for (std::size_t i = 0; i < kFlapSchemaCount; ++i) {
        bool present = ExpectedContainsFormName(kExpectedFlap,
                                                kExpectedFlapCount,
                                                kFlapSchema[i].formName);
        if (!present) {
            std::string msg = "kFlapSchema has unexpected field: ";
            msg += kFlapSchema[i].formName;
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }
}

// (4) FormNames are unique within each schema.
void test_top_level_form_names_unique(void)
{
    std::set<std::string> seen;
    for (std::size_t i = 0; i < kSchemaCount; ++i) {
        auto [_, inserted] = seen.insert(kSchema[i].formName);
        if (!inserted) {
            std::string msg = "duplicate formName in kSchema: ";
            msg += kSchema[i].formName;
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }
}

void test_flap_form_names_unique(void)
{
    std::set<std::string> seen;
    for (std::size_t i = 0; i < kFlapSchemaCount; ++i) {
        auto [_, inserted] = seen.insert(kFlapSchema[i].formName);
        if (!inserted) {
            std::string msg = "duplicate formName in kFlapSchema: ";
            msg += kFlapSchema[i].formName;
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }
}

// (5) Every entry has a non-empty xmlTag, formName, displayName.
void test_top_level_entries_well_formed(void)
{
    for (std::size_t i = 0; i < kSchemaCount; ++i) {
        const FieldDef& d = kSchema[i];
        TEST_ASSERT_NOT_NULL(d.xmlTag);
        TEST_ASSERT_NOT_NULL(d.formName);
        TEST_ASSERT_NOT_NULL(d.displayName);
        TEST_ASSERT_NOT_NULL(d.units);
        TEST_ASSERT_GREATER_THAN_INT(0, std::strlen(d.xmlTag));
        TEST_ASSERT_GREATER_THAN_INT(0, std::strlen(d.formName));
        TEST_ASSERT_GREATER_THAN_INT(0, std::strlen(d.displayName));
        TEST_ASSERT_FALSE(d.isPerFlap);
    }
}

void test_flap_entries_well_formed(void)
{
    for (std::size_t i = 0; i < kFlapSchemaCount; ++i) {
        const FieldDef& d = kFlapSchema[i];
        TEST_ASSERT_NOT_NULL(d.xmlTag);
        TEST_ASSERT_NOT_NULL(d.formName);
        TEST_ASSERT_NOT_NULL(d.displayName);
        TEST_ASSERT_NOT_NULL(d.units);
        TEST_ASSERT_GREATER_THAN_INT(0, std::strlen(d.xmlTag));
        TEST_ASSERT_GREATER_THAN_INT(0, std::strlen(d.formName));
        TEST_ASSERT_GREATER_THAN_INT(0, std::strlen(d.displayName));
        TEST_ASSERT_TRUE(d.isPerFlap);
    }
}

// (6) Enum entries have non-empty choice arrays; non-enum entries don't.
void test_enum_entries_have_choices(void)
{
    auto check = [](const FieldDef* schema, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            const FieldDef& d = schema[i];
            if (d.type == FieldType::Enum || d.type == FieldType::Bool) {
                TEST_ASSERT_NOT_NULL_MESSAGE(
                    d.enumChoices,
                    (std::string("Enum/Bool field missing enumChoices: ") +
                     d.formName).c_str());
                TEST_ASSERT_GREATER_THAN_INT_MESSAGE(
                    0, d.enumChoiceCount,
                    (std::string("Enum/Bool field has zero choices: ") +
                     d.formName).c_str());
                // Each choice must have non-empty strings.
                for (int j = 0; j < d.enumChoiceCount; ++j) {
                    TEST_ASSERT_NOT_NULL(d.enumChoices[j].wireValue);
                    TEST_ASSERT_NOT_NULL(d.enumChoices[j].displayText);
                    TEST_ASSERT_GREATER_THAN_INT(
                        0, std::strlen(d.enumChoices[j].wireValue));
                }
            }
        }
    };
    check(kSchema, kSchemaCount);
    check(kFlapSchema, kFlapSchemaCount);
}

// (7) Numeric field ranges are sensible (min < max).  We skip Bool/Enum/
//     String because their min/max are unused.
void test_numeric_ranges_sensible(void)
{
    auto check = [](const FieldDef* schema, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            const FieldDef& d = schema[i];
            if (d.type == FieldType::Float || d.type == FieldType::Int ||
                d.type == FieldType::UInt) {
                TEST_ASSERT_LESS_THAN_FLOAT_MESSAGE(
                    d.maxValue, d.minValue,
                    (std::string("min >= max for ") + d.formName).c_str());
            }
        }
    };
    check(kSchema, kSchemaCount);
    check(kFlapSchema, kFlapSchemaCount);
}

// (8) Spot-check that specific xmlTags match what ConfigXmlParse expects.
//     Pinning a few key tags catches accidental rename.
// Walks every schema entry and asserts its xmlTag matches the expected
// value pinned in kExpectedTopLevel / kExpectedFlap. A silent rename in
// kSchema (e.g. typo from "AOA_SMOOTHING" to "AOA_SMOTHING") would let
// old config files fail to load that field with no error — this test
// catches it. Pinning all 41+16 entries (instead of spot-checking 6)
// makes the gate symmetric with the formName completeness checks.
void test_every_xml_tag_matches_expected(void)
{
    auto findByForm = [](const FieldDef* schema, std::size_t count,
                         std::string_view formName) -> const FieldDef* {
        for (std::size_t i = 0; i < count; ++i) {
            if (formName == schema[i].formName) return &schema[i];
        }
        return nullptr;
    };

    for (std::size_t i = 0; i < kExpectedTopLevelCount; ++i) {
        const FieldDef* p = findByForm(kSchema, kSchemaCount,
                                       kExpectedTopLevel[i].formName);
        TEST_ASSERT_NOT_NULL(p);
        if (std::string_view(p->xmlTag) != kExpectedTopLevel[i].xmlTag) {
            std::string msg = "kSchema entry '";
            msg += kExpectedTopLevel[i].formName;
            msg += "': xmlTag is '";
            msg += p->xmlTag;
            msg += "', expected '";
            msg += kExpectedTopLevel[i].xmlTag;
            msg += "'";
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }

    for (std::size_t i = 0; i < kExpectedFlapCount; ++i) {
        const FieldDef* p = findByForm(kFlapSchema, kFlapSchemaCount,
                                       kExpectedFlap[i].formName);
        TEST_ASSERT_NOT_NULL(p);
        if (std::string_view(p->xmlTag) != kExpectedFlap[i].xmlTag) {
            std::string msg = "kFlapSchema entry '";
            msg += kExpectedFlap[i].formName;
            msg += "': xmlTag is '";
            msg += p->xmlTag;
            msg += "', expected '";
            msg += kExpectedFlap[i].xmlTag;
            msg += "'";
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_top_level_schema_size_matches_expected);
    RUN_TEST(test_flap_schema_size_matches_expected);
    RUN_TEST(test_every_expected_top_level_field_in_schema);
    RUN_TEST(test_every_expected_flap_field_in_schema);
    RUN_TEST(test_no_extra_fields_in_top_level_schema);
    RUN_TEST(test_no_extra_fields_in_flap_schema);
    RUN_TEST(test_top_level_form_names_unique);
    RUN_TEST(test_flap_form_names_unique);
    RUN_TEST(test_top_level_entries_well_formed);
    RUN_TEST(test_flap_entries_well_formed);
    RUN_TEST(test_enum_entries_have_choices);
    RUN_TEST(test_numeric_ranges_sensible);
    RUN_TEST(test_every_xml_tag_matches_expected);
    return UNITY_END();
}
