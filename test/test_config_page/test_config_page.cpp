// test_config_page.cpp
//
// Semantic-equivalence tests for `onspeed::web::RenderConfigPage`.
//
// The renderer iterates `kSchema` / `kFlapSchema` and emits an HTML5 form
// whose <input name=> / <select name=> attributes match what the sketch's
// HandleConfig() handler produces today.  These tests pin:
//
//   - Every schema field appears as an <input name=> or <select name=> in
//     the rendered HTML.
//   - The current cfg value is round-tripped into the input's value=
//     attribute (or the matching <option selected> for selects).
//   - HTML escaping protects against XSS via malicious config strings.
//   - Per-flap fields render once per cfg.aFlaps entry, with the index
//     suffix scheme HandleConfigSave() expects.

#include <unity.h>

#include <cstring>
#include <string>
#include <string_view>

#include <config/OnSpeedConfig.h>
#include <web/ConfigPage.h>
#include <web/WebSchema.h>

using onspeed::config::OnSpeedConfig;
using onspeed::config::SuDataSource;
using onspeed::web::HtmlEscape;
using onspeed::web::kFlapSchema;
using onspeed::web::kFlapSchemaCount;
using onspeed::web::kSchema;
using onspeed::web::kSchemaCount;
using onspeed::web::RenderConfigPage;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool Contains(const std::string& haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string::npos;
}

static int CountOccurrences(const std::string& haystack,
                            std::string_view needle)
{
    if (needle.empty()) return 0;
    int count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

// Returns true if `name="foo"` appears in either an <input> or <select> tag
// in `html`.  Doesn't validate surrounding context — sufficient because the
// renderer does not emit unrelated `name=` attributes.
static bool HasInputOrSelectNamed(const std::string& html,
                                  std::string_view formName)
{
    std::string needle = "name=\"";
    needle.append(formName.data(), formName.size());
    needle.append("\"");
    return html.find(needle) != std::string::npos;
}

// ---------------------------------------------------------------------------
// (1) HtmlEscape unit tests
// ---------------------------------------------------------------------------

void test_html_escape_passthrough(void)
{
    TEST_ASSERT_EQUAL_STRING("hello world", HtmlEscape("hello world").c_str());
}

void test_html_escape_special_chars(void)
{
    TEST_ASSERT_EQUAL_STRING("&lt;script&gt;",
                             HtmlEscape("<script>").c_str());
    TEST_ASSERT_EQUAL_STRING("a &amp; b",
                             HtmlEscape("a & b").c_str());
    TEST_ASSERT_EQUAL_STRING("&quot;hi&quot;",
                             HtmlEscape("\"hi\"").c_str());
    TEST_ASSERT_EQUAL_STRING("it&#39;s",
                             HtmlEscape("it's").c_str());
}

// ---------------------------------------------------------------------------
// (2) Schema-field coverage in rendered HTML
// ---------------------------------------------------------------------------

void test_render_default_config_emits_all_top_level_fields(void)
{
    OnSpeedConfig cfg;  // ctor calls LoadDefaults().
    std::string html = RenderConfigPage(cfg);
    for (std::size_t i = 0; i < kSchemaCount; ++i) {
        const char* formName = kSchema[i].formName;
        if (!HasInputOrSelectNamed(html, formName)) {
            std::string msg =
                "rendered HTML missing input/select for top-level field: ";
            msg += formName;
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }
}

void test_render_default_config_emits_all_flap_fields(void)
{
    OnSpeedConfig cfg;  // 1 flap from defaults.
    TEST_ASSERT_EQUAL_size_t(1u, cfg.aFlaps.size());

    std::string html = RenderConfigPage(cfg);

    // For the lone flap, every kFlapSchema entry must show up with
    // either the "<base>0" or "aoaCurve0Type"/"aoaCurve0CoeffN" form name.
    for (std::size_t i = 0; i < kFlapSchemaCount; ++i) {
        const std::string base = kFlapSchema[i].formName;
        std::string expectedName;
        if (base == "aoaCurveType") {
            expectedName = "aoaCurve0Type";
        } else if (base.rfind("aoaCurveCoeff", 0) == 0) {
            // aoaCurveCoeff<n> -> aoaCurve0Coeff<n>
            std::string suffix = base.substr(std::strlen("aoaCurveCoeff"));
            expectedName = "aoaCurve0Coeff" + suffix;
        } else {
            expectedName = base + "0";
        }

        if (!HasInputOrSelectNamed(html, expectedName)) {
            std::string msg =
                "rendered HTML missing per-flap field: ";
            msg += expectedName;
            TEST_FAIL_MESSAGE(msg.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// (3) Values round-trip from cfg into input value="..." / option selected.
// ---------------------------------------------------------------------------

void test_int_field_value_roundtrips(void)
{
    OnSpeedConfig cfg;
    cfg.iAcGrossWeight = 1850;
    cfg.iVno           = 175;
    cfg.iDefaultVolume = 73;
    std::string html = RenderConfigPage(cfg);

    TEST_ASSERT_TRUE_MESSAGE(
        Contains(html, "name=\"acGrossWeight\" type=\"text\" value=\"1850\""),
        "expected acGrossWeight=1850 in rendered HTML");
    TEST_ASSERT_TRUE_MESSAGE(
        Contains(html, "name=\"Vno\" type=\"text\" value=\"175\""),
        "expected Vno=175 in rendered HTML");
    TEST_ASSERT_TRUE_MESSAGE(
        Contains(html, "name=\"defaultVolume\" type=\"text\" value=\"73\""),
        "expected defaultVolume=73 in rendered HTML");
}

void test_float_field_value_roundtrips(void)
{
    OnSpeedConfig cfg;
    cfg.fLoadLimitPositive = 4.4f;
    cfg.fAcVfe             = 110.0f;
    std::string html = RenderConfigPage(cfg);

    // We render floats with %g, which trims trailing zeros — match either
    // "4.4" or any representation that includes "4.4".
    TEST_ASSERT_TRUE_MESSAGE(
        Contains(html, "name=\"loadLimitPositive\" type=\"text\" value=\"4.4\""),
        "expected loadLimitPositive=4.4 in rendered HTML");
    TEST_ASSERT_TRUE_MESSAGE(
        Contains(html, "name=\"acVfe\" type=\"text\" value=\"110\""),
        "expected acVfe=110 in rendered HTML");
}

void test_bool_field_renders_selected_option(void)
{
    OnSpeedConfig cfg;
    cfg.bOverGWarning = true;
    cfg.bOatSensor    = false;
    std::string html = RenderConfigPage(cfg);

    // overgWarning <select>: the "1" option must carry " selected".
    auto selectedFor = [&](std::string_view name,
                           std::string_view wireValue) -> bool {
        // Find the <select name="...">  block, then within it look for
        // <option value="<wireValue>" selected>.
        std::string startMarker =
            std::string("name=\"") + std::string(name) + "\"";
        std::size_t selStart = html.find(startMarker);
        if (selStart == std::string::npos) return false;
        std::size_t selEnd = html.find("</select>", selStart);
        if (selEnd == std::string::npos) return false;
        std::string body = html.substr(selStart, selEnd - selStart);
        std::string optMarker =
            std::string("value=\"") + std::string(wireValue) + "\" selected";
        return body.find(optMarker) != std::string::npos;
    };

    TEST_ASSERT_TRUE_MESSAGE(selectedFor("overgWarning", "1"),
                             "overgWarning=true should select option \"1\"");
    TEST_ASSERT_TRUE_MESSAGE(selectedFor("oatSensor", "0"),
                             "oatSensor=false should select option \"0\"");
}

void test_enum_field_selects_current_value(void)
{
    OnSpeedConfig cfg;
    cfg.suDataSrc.enSrc = SuDataSource::EnReplay;
    cfg.sEfisType       = "GARMING3X";
    std::string html = RenderConfigPage(cfg);

    TEST_ASSERT_TRUE(
        Contains(html, "value=\"REPLAYLOGFILE\" selected"));
    TEST_ASSERT_TRUE(
        Contains(html, "value=\"GARMING3X\" selected"));
}

// ---------------------------------------------------------------------------
// (4) XSS escape: malicious string config value must be neutralised.
// ---------------------------------------------------------------------------

void test_xss_payload_escaped_in_string_value(void)
{
    OnSpeedConfig cfg;
    // sReplayLogFileName is rendered as a String <input> whose value
    // attribute echoes the cfg field verbatim — exactly the spot where
    // XSS would land if escaping were missing.
    cfg.sReplayLogFileName = "<script>alert(1)</script>";
    std::string html = RenderConfigPage(cfg);

    // The literal <script> tag must NOT appear in the output (it would
    // execute in the browser).
    TEST_ASSERT_FALSE_MESSAGE(
        Contains(html, "<script>alert(1)</script>"),
        "raw <script> tag leaked into rendered HTML — XSS vulnerability");

    // The escaped form should appear instead.
    TEST_ASSERT_TRUE_MESSAGE(
        Contains(html, "&lt;script&gt;alert(1)&lt;/script&gt;"),
        "expected escaped <script> in rendered HTML");
}

void test_xss_payload_escaped_in_log_filename(void)
{
    OnSpeedConfig cfg;
    cfg.sReplayLogFileName = "\" onmouseover=\"alert(1)";
    std::string html = RenderConfigPage(cfg);

    // The renderer always closes the value attribute with `"`, so an
    // unescaped quote in the value would let the attacker break out and
    // inject `onmouseover=`.  Assert the raw payload is NOT present.
    TEST_ASSERT_FALSE_MESSAGE(
        Contains(html, "\" onmouseover=\"alert(1)"),
        "unescaped quote leaked; attribute injection possible");
    // Quote must be escaped as &quot;
    TEST_ASSERT_TRUE(Contains(html, "&quot; onmouseover=&quot;alert(1)"));
}

// ---------------------------------------------------------------------------
// (5) Per-flap rendering: N flaps -> N copies of each per-flap input.
// ---------------------------------------------------------------------------

void test_three_flaps_render_three_input_sets(void)
{
    OnSpeedConfig cfg;
    cfg.aFlaps.clear();
    for (int i = 0; i < 3; ++i) {
        OnSpeedConfig::SuFlaps f;
        f.iDegrees    = 10 * (i + 1);   // 10, 20, 30
        f.fLDMAXAOA   = 5.0f + i;       // 5, 6, 7
        cfg.aFlaps.push_back(f);
    }
    std::string html = RenderConfigPage(cfg);

    // All three flapDegrees<idx> names must be present.
    TEST_ASSERT_TRUE(HasInputOrSelectNamed(html, "flapDegrees0"));
    TEST_ASSERT_TRUE(HasInputOrSelectNamed(html, "flapDegrees1"));
    TEST_ASSERT_TRUE(HasInputOrSelectNamed(html, "flapDegrees2"));
    TEST_ASSERT_FALSE(HasInputOrSelectNamed(html, "flapDegrees3"));

    // And each carries the right value.
    TEST_ASSERT_TRUE(Contains(html, "name=\"flapDegrees0\" type=\"text\" value=\"10\""));
    TEST_ASSERT_TRUE(Contains(html, "name=\"flapDegrees1\" type=\"text\" value=\"20\""));
    TEST_ASSERT_TRUE(Contains(html, "name=\"flapDegrees2\" type=\"text\" value=\"30\""));

    // 3 occurrences of LDMAXAOA inputs.
    TEST_ASSERT_EQUAL_INT(3, CountOccurrences(html, "name=\"flapLDMAXAOA"));
}

void test_per_flap_curve_form_names_use_index_infix(void)
{
    OnSpeedConfig cfg;
    cfg.aFlaps.clear();
    OnSpeedConfig::SuFlaps f;
    f.AoaCurve.iCurveType  = 1;
    f.AoaCurve.afCoeff[2]  = 7.5f;
    cfg.aFlaps.push_back(f);
    cfg.aFlaps.push_back(f);  // 2 flaps

    std::string html = RenderConfigPage(cfg);

    // aoaCurve<idx>Type — verify infix scheme matches HandleConfigSave().
    TEST_ASSERT_TRUE(HasInputOrSelectNamed(html, "aoaCurve0Type"));
    TEST_ASSERT_TRUE(HasInputOrSelectNamed(html, "aoaCurve1Type"));
    // aoaCurve<idx>CoeffN
    TEST_ASSERT_TRUE(HasInputOrSelectNamed(html, "aoaCurve0Coeff0"));
    TEST_ASSERT_TRUE(HasInputOrSelectNamed(html, "aoaCurve1Coeff3"));
    // Coeff2 has the value we set.
    TEST_ASSERT_TRUE(Contains(html, "name=\"aoaCurve0Coeff2\" type=\"text\" value=\"7.5\""));
}

// ---------------------------------------------------------------------------
// (6) Whole-document sanity: doctype, form action, save button.
// ---------------------------------------------------------------------------

void test_rendered_document_structure(void)
{
    OnSpeedConfig cfg;
    std::string html = RenderConfigPage(cfg);

    TEST_ASSERT_TRUE(Contains(html, "<!DOCTYPE html>"));
    TEST_ASSERT_TRUE(Contains(html, "action=\"aoaconfigsave\""));
    TEST_ASSERT_TRUE(Contains(html, "method=\"POST\""));
    TEST_ASSERT_TRUE(Contains(html, "name=\"saveSettingsButton\""));
    TEST_ASSERT_TRUE(Contains(html, "</html>"));
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_html_escape_passthrough);
    RUN_TEST(test_html_escape_special_chars);
    RUN_TEST(test_render_default_config_emits_all_top_level_fields);
    RUN_TEST(test_render_default_config_emits_all_flap_fields);
    RUN_TEST(test_int_field_value_roundtrips);
    RUN_TEST(test_float_field_value_roundtrips);
    RUN_TEST(test_bool_field_renders_selected_option);
    RUN_TEST(test_enum_field_selects_current_value);
    RUN_TEST(test_xss_payload_escaped_in_string_value);
    RUN_TEST(test_xss_payload_escaped_in_log_filename);
    RUN_TEST(test_three_flaps_render_three_input_sets);
    RUN_TEST(test_per_flap_curve_form_names_use_index_infix);
    RUN_TEST(test_rendered_document_structure);
    return UNITY_END();
}
