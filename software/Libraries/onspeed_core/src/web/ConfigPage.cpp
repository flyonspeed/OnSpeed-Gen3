// ConfigPage.cpp — see ConfigPage.h for the public interface.
//
// Walks `kSchema` / `kFlapSchema` from WebSchema.h and emits an HTML5 form
// whose <input name=> attributes exactly match what HandleConfig() in the
// sketch has historically produced.  Kept deliberately minimal — no
// CSS/JS — because PR 4.1 only lands foundations; PR 4.1b will wrap the
// schema-driven output in the existing production header/footer once
// benched.

#include <web/ConfigPage.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include <config/OnSpeedConfig.h>
#include <util/OnSpeedTypes.h>
#include <web/WebSchema.h>

namespace onspeed::web {

namespace {

// ---------------------------------------------------------------------------
// Small formatting helpers.
// ---------------------------------------------------------------------------

// Format a float the same way the sketch's g_Config.ToString(float) does
// enough of the time that schema-driven output looks right on screen: trim
// trailing zeros, cap at 6 significant digits.  For the round-trip POST
// handler, only the numeric value matters — the exact digit pattern does
// not, so we don't have to match the legacy format byte-for-byte.
std::string FormatFloat(float value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(value));
    return std::string(buf);
}

std::string FormatInt(int value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", value);
    return std::string(buf);
}

std::string FormatUnsigned(unsigned value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", value);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// cfg -> value-as-string resolver.
//
// Keyed on the formName (unique per field in the schema).  Returns the
// string that should go in the input/select's `value=` attribute (or the
// string compared against each <option value=> when rendering selects).
//
// Keeping this switch co-located with the schema is intentional: any time
// someone adds a new FieldDef, the compiler still happily builds, but the
// test_config_page suite will fail on the "every schema field renders with
// the expected value" check until the appropriate case is added here.
// ---------------------------------------------------------------------------

std::string TopLevelValue(const onspeed::config::OnSpeedConfig& cfg,
                          std::string_view formName)
{
    if (formName == "aoaSmoothing")         return FormatInt(cfg.iAoaSmoothing);
    if (formName == "pressureSmoothing")    return FormatInt(cfg.iPressureSmoothing);
    if (formName == "dataSource")           return cfg.suDataSrc.toCStr();
    if (formName == "logFileName")          return cfg.sReplayLogFileName;
    if (formName == "readBoom")             return cfg.bReadBoom        ? "1" : "0";
    if (formName == "boomChecksum")         return cfg.bBoomChecksum    ? "1" : "0";
    if (formName == "boomConvertData")      return cfg.bBoomConvertData ? "1" : "0";
    if (formName == "casCurveEnabled")      return cfg.bCasCurveEnabled ? "1" : "0";
    if (formName == "casCurveType")         return FormatInt(cfg.CasCurve.iCurveType);
    if (formName == "casCurveCoeff0")       return FormatFloat(cfg.CasCurve.afCoeff[0]);
    if (formName == "casCurveCoeff1")       return FormatFloat(cfg.CasCurve.afCoeff[1]);
    if (formName == "casCurveCoeff2")       return FormatFloat(cfg.CasCurve.afCoeff[2]);
    if (formName == "casCurveCoeff3")       return FormatFloat(cfg.CasCurve.afCoeff[3]);
    if (formName == "portsOrientation")     return cfg.sPortsOrientation;
    if (formName == "boxtopOrientation")    return cfg.sBoxtopOrientation;
    if (formName == "readEfisData")         return cfg.bReadEfisData ? "1" : "0";
    if (formName == "efisType")             return cfg.sEfisType;
    if (formName == "oatSensor")            return cfg.bOatSensor   ? "1" : "0";
    if (formName == "calSource")            return cfg.sCalSource;
    if (formName == "ahrsAlgorithm")        return FormatInt(cfg.iAhrsAlgorithm);
    if (formName == "volumeControl")        return cfg.bVolumeControl ? "1" : "0";
    if (formName == "defaultVolume")        return FormatInt(cfg.iDefaultVolume);
    if (formName == "volumeLowAnalog")      return FormatInt(cfg.iVolumeLowAnalog);
    if (formName == "volumeHighAnalog")     return FormatInt(cfg.iVolumeHighAnalog);
    if (formName == "muteAudioUnderIAS")    return FormatInt(cfg.iMuteAudioUnderIAS);
    if (formName == "audio3D")              return cfg.bAudio3D       ? "1" : "0";
    if (formName == "overgWarning")         return cfg.bOverGWarning  ? "1" : "0";
    if (formName == "loadLimitPositive")    return FormatFloat(cfg.fLoadLimitPositive);
    if (formName == "loadLimitNegative")    return FormatFloat(cfg.fLoadLimitNegative);
    if (formName == "asymmetricGyroLimit")  return FormatFloat(cfg.fAsymmetricGyroLimit);
    if (formName == "asymmetricReduction")  return FormatFloat(cfg.fAsymmetricReduction);
    if (formName == "vnoChimeEnabled")      return cfg.bVnoChimeEnabled ? "1" : "0";
    if (formName == "Vno")                  return FormatInt(cfg.iVno);
    if (formName == "vnoChimeInterval")     return FormatUnsigned(cfg.uVnoChimeInterval);
    if (formName == "sdLogging")            return cfg.bSdLogging ? "1" : "0";
    if (formName == "logRate")              return FormatInt(cfg.iLogRate);
    if (formName == "serialOutFormat")      return cfg.sSerialOutFormat;
    if (formName == "acGrossWeight")        return FormatInt(cfg.iAcGrossWeight);
    if (formName == "acBestGlideIAS")       return FormatFloat(cfg.fAcBestGlideIAS);
    if (formName == "acVfe")                return FormatFloat(cfg.fAcVfe);
    if (formName == "acGlimit")             return FormatFloat(cfg.fAcGlimit);
    return "";
}

std::string FlapValue(const onspeed::config::OnSpeedConfig::SuFlaps& flap,
                      std::string_view formName)
{
    if (formName == "flapDegrees")        return FormatInt(flap.iDegrees);
    if (formName == "flapPotPositions")   return FormatInt(flap.iPotPosition);
    if (formName == "flapLDMAXAOA")       return FormatFloat(flap.fLDMAXAOA);
    if (formName == "flapONSPEEDFASTAOA") return FormatFloat(flap.fONSPEEDFASTAOA);
    if (formName == "flapONSPEEDSLOWAOA") return FormatFloat(flap.fONSPEEDSLOWAOA);
    if (formName == "flapSTALLWARNAOA")   return FormatFloat(flap.fSTALLWARNAOA);
    if (formName == "flapSTALLAOA")       return FormatFloat(flap.fSTALLAOA);
    if (formName == "flapMANAOA")         return FormatFloat(flap.fMANAOA);
    if (formName == "flapKFit")           return FormatFloat(flap.fKFit);
    if (formName == "flapAlpha0")         return FormatFloat(flap.fAlpha0);
    if (formName == "flapAlphaStall")     return FormatFloat(flap.fAlphaStall);
    if (formName == "aoaCurveType")       return FormatInt(flap.AoaCurve.iCurveType);
    if (formName == "aoaCurveCoeff0")     return FormatFloat(flap.AoaCurve.afCoeff[0]);
    if (formName == "aoaCurveCoeff1")     return FormatFloat(flap.AoaCurve.afCoeff[1]);
    if (formName == "aoaCurveCoeff2")     return FormatFloat(flap.AoaCurve.afCoeff[2]);
    if (formName == "aoaCurveCoeff3")     return FormatFloat(flap.AoaCurve.afCoeff[3]);
    return "";
}

// Per-flap form-name helper: HandleConfig() appends the flap index to the
// base name for scalar fields (e.g. "flapDegrees0") but inserts it between
// prefix and suffix for the curve fields (e.g. "aoaCurve0Type",
// "aoaCurve0Coeff2").  We mirror both patterns here so the emitted HTML
// names match what HandleConfigSave() reads via CfgServer.arg().
std::string PerFlapName(std::string_view base, int idx) {
    const std::string sidx = FormatInt(idx);
    if (base == "aoaCurveType") {
        return "aoaCurve" + sidx + "Type";
    }
    // aoaCurveCoeff0..3 -> aoaCurve<idx>Coeff<n>
    static constexpr std::string_view kCoeffPrefix = "aoaCurveCoeff";
    if (base.substr(0, kCoeffPrefix.size()) == kCoeffPrefix) {
        std::string_view suffix = base.substr(kCoeffPrefix.size());
        std::string out;
        out.reserve(16);
        out += "aoaCurve";
        out += sidx;
        out += "Coeff";
        out += suffix;
        return out;
    }
    // Default: append index after the base, matching flapDegrees0, flapLDMAXAOA0, ...
    std::string out;
    out.reserve(base.size() + sidx.size());
    out.append(base.data(), base.size());
    out += sidx;
    return out;
}

// ---------------------------------------------------------------------------
// HTML fragment emitters.  All append to the supplied string buffer so we
// never allocate more than necessary.
// ---------------------------------------------------------------------------

void AppendEscaped(std::string& out, std::string_view input) {
    for (char c : input) {
        switch (c) {
            case '&':  out.append("&amp;");  break;
            case '<':  out.append("&lt;");   break;
            case '>':  out.append("&gt;");   break;
            case '"':  out.append("&quot;"); break;
            case '\'': out.append("&#39;");  break;
            default:   out.push_back(c);     break;
        }
    }
}

void EmitLabel(std::string& out, std::string_view id, std::string_view text,
               std::string_view units)
{
    out.append("<label for=\"");
    AppendEscaped(out, id);
    out.append("\">");
    AppendEscaped(out, text);
    if (!units.empty()) {
        out.push_back(' ');
        out.push_back('(');
        AppendEscaped(out, units);
        out.push_back(')');
    }
    out.append("</label>\n");
}

void EmitTextInput(std::string& out, std::string_view id,
                   std::string_view name, std::string_view value)
{
    out.append("<input id=\"");
    AppendEscaped(out, id);
    out.append("\" name=\"");
    AppendEscaped(out, name);
    out.append("\" type=\"text\" value=\"");
    AppendEscaped(out, value);
    out.append("\"/>\n");
}

void EmitSelect(std::string& out, std::string_view id, std::string_view name,
                const EnumChoice* choices, int choiceCount,
                std::string_view currentValue)
{
    out.append("<select id=\"");
    AppendEscaped(out, id);
    out.append("\" name=\"");
    AppendEscaped(out, name);
    out.append("\">\n");
    for (int i = 0; i < choiceCount; ++i) {
        out.append("  <option value=\"");
        AppendEscaped(out, choices[i].wireValue);
        out.append("\"");
        if (currentValue == choices[i].wireValue) {
            out.append(" selected");
        }
        out.append(">");
        AppendEscaped(out, choices[i].displayText);
        out.append("</option>\n");
    }
    out.append("</select>\n");
}

void EmitField(std::string& out, const FieldDef& def,
               std::string_view id, std::string_view name,
               const std::string& value)
{
    out.append("<div class=\"form-row\">\n");
    EmitLabel(out, id, def.displayName, def.units);
    switch (def.type) {
        case FieldType::Bool:
        case FieldType::Enum:
            EmitSelect(out, id, name, def.enumChoices, def.enumChoiceCount, value);
            break;
        case FieldType::Float:
        case FieldType::Int:
        case FieldType::UInt:
        case FieldType::String:
            EmitTextInput(out, id, name, value);
            break;
    }
    out.append("</div>\n");
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string HtmlEscape(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    AppendEscaped(out, input);
    return out;
}

std::string RenderConfigPage(const onspeed::config::OnSpeedConfig& cfg) {
    std::string out;
    out.reserve(16 * 1024);

    // Minimal HTML5 header.  PR 4.1b will swap this for the production
    // header that shares CSS with the rest of the sketch.
    out.append("<!DOCTYPE html>\n");
    out.append("<html lang=\"en\"><head><meta charset=\"utf-8\">");
    out.append("<title>OnSpeed Configuration</title></head><body>\n");

    out.append("<form id=\"id_configForm\" action=\"aoaconfigsave\" method=\"POST\">\n");

    // Top-level fields.
    for (std::size_t i = 0; i < kSchemaCount; ++i) {
        const FieldDef& def = kSchema[i];
        const std::string id = std::string("id_") + def.formName;
        const std::string value = TopLevelValue(cfg, def.formName);
        EmitField(out, def, id, def.formName, value);
    }

    // Per-flap fieldsets.
    for (std::size_t iFlap = 0; iFlap < cfg.aFlaps.size(); ++iFlap) {
        const auto& flap = cfg.aFlaps[iFlap];
        out.append("<fieldset class=\"flap-section\">\n");
        out.append("<legend>Flap Curve ");
        out.append(FormatInt(static_cast<int>(iFlap) + 1));
        out.append("</legend>\n");

        for (std::size_t j = 0; j < kFlapSchemaCount; ++j) {
            const FieldDef& def = kFlapSchema[j];
            const std::string name  = PerFlapName(def.formName,
                                                  static_cast<int>(iFlap));
            const std::string id    = std::string("id_") + name;
            const std::string value = FlapValue(flap, def.formName);
            EmitField(out, def, id, name, value);
        }

        out.append("</fieldset>\n");
    }

    // Form controls (Save button only for now — PR 4.1b brings the full
    // control set: upload, delete-flap, load-defaults, etc.).
    out.append(
        "<input class=\"blackbutton\" type=\"submit\" "
        "name=\"saveSettingsButton\" value=\"Save\"/>\n");

    out.append("</form>\n");
    out.append("</body></html>\n");
    return out;
}

}  // namespace onspeed::web
