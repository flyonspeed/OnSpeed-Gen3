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

// Top-level fields, identified by formName (the unique lookup key).
static const char* const kExpectedTopLevelFormNames[] = {
    "aoaSmoothing",
    "pressureSmoothing",
    "dataSource",
    "logFileName",
    "readBoom",
    "boomChecksum",
    "boomConvertData",
    "casCurveEnabled",
    "casCurveType",
    "casCurveCoeff0",
    "casCurveCoeff1",
    "casCurveCoeff2",
    "casCurveCoeff3",
    "portsOrientation",
    "boxtopOrientation",
    "readEfisData",
    "efisType",
    "oatSensor",
    "calSource",
    "ahrsAlgorithm",
    "volumeControl",
    "defaultVolume",
    "volumeLowAnalog",
    "volumeHighAnalog",
    "muteAudioUnderIAS",
    "audio3D",
    "overgWarning",
    "loadLimitPositive",
    "loadLimitNegative",
    "asymmetricGyroLimit",
    "asymmetricReduction",
    "vnoChimeEnabled",
    "Vno",
    "vnoChimeInterval",
    "sdLogging",
    "logRate",
    "serialOutFormat",
    "acGrossWeight",
    "acBestGlideIAS",
    "acVfe",
    "acGlimit",
};
static constexpr std::size_t kExpectedTopLevelCount =
    sizeof(kExpectedTopLevelFormNames) / sizeof(kExpectedTopLevelFormNames[0]);

// Per-flap fields.
static const char* const kExpectedFlapFormNames[] = {
    "flapDegrees",
    "flapPotPositions",
    "flapLDMAXAOA",
    "flapONSPEEDFASTAOA",
    "flapONSPEEDSLOWAOA",
    "flapSTALLWARNAOA",
    "flapSTALLAOA",
    "flapMANAOA",
    "flapKFit",
    "flapAlpha0",
    "flapAlphaStall",
    "aoaCurveType",
    "aoaCurveCoeff0",
    "aoaCurveCoeff1",
    "aoaCurveCoeff2",
    "aoaCurveCoeff3",
};
static constexpr std::size_t kExpectedFlapCount =
    sizeof(kExpectedFlapFormNames) / sizeof(kExpectedFlapFormNames[0]);

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

static bool ExpectedContainsFormName(const char* const* expected,
                                     std::size_t count,
                                     std::string_view formName)
{
    for (std::size_t i = 0; i < count; ++i) {
        if (formName == expected[i]) return true;
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
                                              kExpectedTopLevelFormNames[i]);
        if (!present) {
            std::string msg = "missing top-level field in kSchema: ";
            msg += kExpectedTopLevelFormNames[i];
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }
}

void test_every_expected_flap_field_in_schema(void)
{
    for (std::size_t i = 0; i < kExpectedFlapCount; ++i) {
        bool present = SchemaContainsFormName(kFlapSchema, kFlapSchemaCount,
                                              kExpectedFlapFormNames[i]);
        if (!present) {
            std::string msg = "missing per-flap field in kFlapSchema: ";
            msg += kExpectedFlapFormNames[i];
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }
}

// (3) No extra fields in the schema beyond the expected list.
void test_no_extra_fields_in_top_level_schema(void)
{
    for (std::size_t i = 0; i < kSchemaCount; ++i) {
        bool present = ExpectedContainsFormName(kExpectedTopLevelFormNames,
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
        bool present = ExpectedContainsFormName(kExpectedFlapFormNames,
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
void test_known_xml_tags_pinned(void)
{
    auto findByForm = [](const FieldDef* schema, std::size_t count,
                         std::string_view formName) -> const FieldDef* {
        for (std::size_t i = 0; i < count; ++i) {
            if (formName == schema[i].formName) return &schema[i];
        }
        return nullptr;
    };

    const FieldDef* p;

    p = findByForm(kSchema, kSchemaCount, "aoaSmoothing");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("AOA_SMOOTHING", p->xmlTag);

    p = findByForm(kSchema, kSchemaCount, "overgWarning");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("OVERGWARNING", p->xmlTag);

    p = findByForm(kSchema, kSchemaCount, "ahrsAlgorithm");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("AHRS_ALGORITHM", p->xmlTag);

    p = findByForm(kFlapSchema, kFlapSchemaCount, "flapLDMAXAOA");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("LDMAXAOA", p->xmlTag);

    p = findByForm(kFlapSchema, kFlapSchemaCount, "flapAlphaStall");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("ALPHASTALL", p->xmlTag);

    p = findByForm(kFlapSchema, kFlapSchemaCount, "aoaCurveCoeff0");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("X3", p->xmlTag);
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
    RUN_TEST(test_known_xml_tags_pinned);
    return UNITY_END();
}
