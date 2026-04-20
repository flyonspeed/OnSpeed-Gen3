// WebSchema.h
//
// Constexpr schema describing every OnSpeedConfig field that is editable from
// the web configuration page.  Each FieldDef carries:
//
//   - xmlTag    — matches the <TAG> used by ConfigXmlParse/ConfigXmlEmit,
//                 i.e. the XML persistence key.  Uses UPPER_SNAKE_CASE.
//   - formName  — matches the <input name="..."> / <select name="...">
//                 attribute emitted by the current HandleConfig() sketch
//                 handler.  Uses lowerCamelCase.  Kept separate from xmlTag
//                 because the HTTP POST handler reads these names, and they
//                 historically diverged from the XML tag spelling (e.g.
//                 `aoaSmoothing` vs `AOA_SMOOTHING`).
//   - displayName — the <label> text the HTML emits.
//   - units     — unit suffix used in labels (may be "").
//   - type      — how the renderer converts cfg -> HTML value (Float/Int/...)
//   - minValue / maxValue — approximate client-side validation ranges.
//   - enumChoices / enumChoiceCount — only set for FieldType::Enum.  Each
//     EnumChoice carries the wire value (what appears in <option value=>)
//     and the label (what appears between <option>...</option>).
//   - isPerFlap — true for fields under OnSpeedConfig::SuFlaps, rendered
//                 once per entry of cfg.aFlaps.
//
// Platform-freeness: no Arduino, FreeRTOS, ESP-IDF, or <String> allowed.
// This header is consumed both by the renderer (onspeed_core/src/web/
// ConfigPage.cpp) and by the test_web_schema suite.

#ifndef ONSPEED_CORE_WEB_WEB_SCHEMA_H
#define ONSPEED_CORE_WEB_WEB_SCHEMA_H

#include <cstddef>
#include <cstdint>

namespace onspeed::web {

enum class FieldType : uint8_t {
    Float,    ///< float field, rendered as <input type="text" value="%g">
    Int,      ///< int field, rendered as <input type="text" value="%d">
    UInt,     ///< unsigned field — separate from Int because uVnoChimeInterval
              ///< was historically parsed with QueryUnsignedText().
    Bool,     ///< bool — rendered as a <select> with "1"/"0" options, matching
              ///< the existing HandleConfig() convention (NOT <input type=
              ///< "checkbox">; that would break the POST handler).
    Enum,     ///< string-valued enumeration with a fixed set of wire values.
    String,   ///< free-form std::string field.
};

struct EnumChoice {
    const char* wireValue;    ///< What goes in <option value="...">.
    const char* displayText;  ///< What goes between <option>...</option>.
};

struct FieldDef {
    const char*       xmlTag;
    const char*       formName;
    const char*       displayName;
    const char*       units;           // "" for none
    FieldType         type;
    float             minValue;        // ignored for Bool/Enum/String
    float             maxValue;        // ignored for Bool/Enum/String
    const EnumChoice* enumChoices;     // null for non-enum
    int               enumChoiceCount; // 0 for non-enum
    bool              isPerFlap;       // true for SuFlaps members
};

// ============================================================================
// Enum choice arrays (file scope so FieldDef entries can take their address
// at compile time).
// ============================================================================

inline constexpr EnumChoice kDataSourceChoices[] = {
    {"SENSORS",       "Sensors (default)"},
    {"TESTPOT",       "Test Potentiometer"},
    {"RANGESWEEP",    "Range Sweep"},
    {"REPLAYLOGFILE", "Replay Log File"},
};

inline constexpr EnumChoice kBoolChoices[] = {
    {"1", "Enabled"},
    {"0", "Disabled"},
};

// Boom convert data has different labels for 0/1 than the generic bool.
inline constexpr EnumChoice kBoomConvertChoices[] = {
    {"0", "Raw Counts"},
    {"1", "Converted (polynomial)"},
};

inline constexpr EnumChoice kCurveTypeChoices[] = {
    {"1", "Polynomial"},
    {"2", "Logarithmic"},
    {"3", "Exponential"},
};

inline constexpr EnumChoice kOrientationChoices[] = {
    {"UP",      "Up"},
    {"DOWN",    "Down"},
    {"LEFT",    "Left"},
    {"RIGHT",   "Right"},
    {"FORWARD", "Forward"},
    {"AFT",     "Aft"},
};

inline constexpr EnumChoice kEfisTypeChoices[] = {
    {"DYNOND10",  "Dynon D10/D100"},
    {"ADVANCED",  "SkyView/Advanced"},
    {"GARMING5",  "Garmin G5"},
    {"GARMING3X", "Garmin G3X"},
    {"VN-300",    "VectorNav VN-300 GNSS/INS"},
    {"VN-100",    "VectorNav VN-100 IMU/AHRS"},
    {"MGL",       "MGL iEFIS"},
};

inline constexpr EnumChoice kCalSourceChoices[] = {
    {"ONSPEED", "ONSPEED (internal IMU)"},
    {"EFIS",    "EFIS (via serial input)"},
};

inline constexpr EnumChoice kAhrsAlgorithmChoices[] = {
    {"0", "Madgwick (default)"},
    {"1", "EKF6"},
};

inline constexpr EnumChoice kLogRateChoices[] = {
    {"50",  "50 Hz (pressure rate)"},
    {"208", "208 Hz (IMU rate)"},
};

inline constexpr EnumChoice kSerialOutFormatChoices[] = {
    {"G3X",     "Garmin G3X"},
    {"ONSPEED", "OnSpeed"},
};

// Small helper so the FieldDef table stays readable.
#define ONSPEED_CHOICES(arr) (arr), \
    (static_cast<int>(sizeof(arr) / sizeof((arr)[0])))

// ============================================================================
// kSchema — top-level scalar fields.
//
// Order roughly matches the order the sketch's HandleConfig() emits them, so
// the renderer that walks this array produces a visually similar page.  Form
// names are taken from HandleConfig() verbatim; xmlTags are taken from
// ConfigXmlParse.cpp verbatim.
// ============================================================================

inline constexpr FieldDef kSchema[] = {
    // AOA / pressure smoothing
    {"AOA_SMOOTHING",      "aoaSmoothing",      "AOA Smoothing",              "", FieldType::Int,    0.0f,    100.0f, nullptr, 0, false},
    {"PRESSURE_SMOOTHING", "pressureSmoothing", "Pressure Smoothing",         "", FieldType::Int,    0.0f,    100.0f, nullptr, 0, false},

    // Data source / replay
    {"DATASOURCE",         "dataSource",        "Data Source - Operation mode", "", FieldType::Enum, 0.0f, 0.0f, ONSPEED_CHOICES(kDataSourceChoices), false},
    {"REPLAYLOGFILENAME",  "logFileName",       "Log file to replay",         "", FieldType::String, 0.0f, 0.0f, nullptr, 0, false},

    // Boom / serial
    {"BOOM",               "readBoom",          "Flight Test Boom",           "", FieldType::Bool,   0.0f, 0.0f, ONSPEED_CHOICES(kBoolChoices), false},
    {"BOOMCHECKSUM",       "boomChecksum",      "Boom Checksum",              "", FieldType::Bool,   0.0f, 0.0f, ONSPEED_CHOICES(kBoolChoices), false},
    {"BOOMCONVERTDATA",    "boomConvertData",   "Boom Data Conversion",       "", FieldType::Enum,   0.0f, 0.0f, ONSPEED_CHOICES(kBoomConvertChoices), false},

    // CAS curve
    {"CAS_CURVE_ENABLED",  "casCurveEnabled",   "Airspeed Calibration",       "", FieldType::Bool,   0.0f, 0.0f, ONSPEED_CHOICES(kBoolChoices), false},
    {"CAS_CURVE_TYPE",     "casCurveType",      "Airspeed Calibration Curve Type", "", FieldType::Enum, 0.0f, 0.0f, ONSPEED_CHOICES(kCurveTypeChoices), false},
    {"CAS_CURVE_COEFF0",   "casCurveCoeff0",    "CAS Coefficient 0",          "", FieldType::Float, -1e6f, 1e6f, nullptr, 0, false},
    {"CAS_CURVE_COEFF1",   "casCurveCoeff1",    "CAS Coefficient 1",          "", FieldType::Float, -1e6f, 1e6f, nullptr, 0, false},
    {"CAS_CURVE_COEFF2",   "casCurveCoeff2",    "CAS Coefficient 2",          "", FieldType::Float, -1e6f, 1e6f, nullptr, 0, false},
    {"CAS_CURVE_COEFF3",   "casCurveCoeff3",    "CAS Coefficient 3",          "", FieldType::Float, -1e6f, 1e6f, nullptr, 0, false},

    // Orientation
    {"PORTS",              "portsOrientation",  "Pressure ports orientation", "", FieldType::Enum,   0.0f, 0.0f, ONSPEED_CHOICES(kOrientationChoices), false},
    {"BOX_TOP",            "boxtopOrientation", "Box top Orientation",        "", FieldType::Enum,   0.0f, 0.0f, ONSPEED_CHOICES(kOrientationChoices), false},

    // EFIS
    {"SERIALEFISDATA",     "readEfisData",      "Serial EFIS data",           "", FieldType::Bool,   0.0f, 0.0f, ONSPEED_CHOICES(kBoolChoices), false},
    {"EFISTYPE",           "efisType",          "EFIS Type",                  "", FieldType::Enum,   0.0f, 0.0f, ONSPEED_CHOICES(kEfisTypeChoices), false},

    // Misc toggles
    {"OATSENSOR",          "oatSensor",         "Internal OAT Sensor (DS18B20)", "", FieldType::Bool, 0.0f, 0.0f, ONSPEED_CHOICES(kBoolChoices), false},
    {"CALWIZ_SOURCE",      "calSource",         "Calibration Data Source",    "", FieldType::Enum,   0.0f, 0.0f, ONSPEED_CHOICES(kCalSourceChoices), false},
    {"AHRS_ALGORITHM",     "ahrsAlgorithm",     "AHRS Algorithm",             "", FieldType::Enum,   0.0f, 0.0f, ONSPEED_CHOICES(kAhrsAlgorithmChoices), false},

    // Volume / audio
    {"VOLUME_ENABLED",     "volumeControl",     "Volume Potentiometer",       "",   FieldType::Bool, 0.0f,     0.0f,   ONSPEED_CHOICES(kBoolChoices), false},
    {"VOLUME_DEFAULT",     "defaultVolume",     "Volume %",                   "%",  FieldType::Int,  0.0f,     100.0f, nullptr, 0, false},
    {"VOLUME_LOW_ANALOG",  "volumeLowAnalog",   "Low Vol. value",             "",   FieldType::Int,  0.0f,     5000.0f, nullptr, 0, false},
    {"VOLUME_HIGH_ANALOG", "volumeHighAnalog",  "High Vol. value",            "",   FieldType::Int,  0.0f,     5000.0f, nullptr, 0, false},
    {"MUTE_UNDER_IAS",     "muteAudioUnderIAS", "Mute below IAS",             "kts", FieldType::Int, 0.0f,     500.0f, nullptr, 0, false},
    {"ENABLE_3DAUDIO",     "audio3D",           "3D Audio",                   "",   FieldType::Bool, 0.0f,     0.0f,   ONSPEED_CHOICES(kBoolChoices), false},

    // G-limit / load
    {"OVERGWARNING",           "overgWarning",        "OverG audio warning",            "",      FieldType::Bool,  0.0f,  0.0f, ONSPEED_CHOICES(kBoolChoices), false},
    {"POSITIVE",               "loadLimitPositive",   "Positive G limit",               "G",     FieldType::Float, 0.0f,  20.0f, nullptr, 0, false},
    {"NEGATIVE",               "loadLimitNegative",   "Negative G limit",               "G",     FieldType::Float, -20.0f, 0.0f,  nullptr, 0, false},
    {"ASYMMETRIC_GYRO_LIMIT",  "asymmetricGyroLimit", "Asymmetric gyro threshold",      "deg/s", FieldType::Float, 0.0f,  360.0f, nullptr, 0, false},
    {"ASYMMETRIC_REDUCTION",   "asymmetricReduction", "Asymmetric G reduction factor",  "",      FieldType::Float, 0.1f,  1.0f,  nullptr, 0, false},

    // Vno chime
    {"CHIME_ENABLED",      "vnoChimeEnabled",   "Vno warning chime",          "",    FieldType::Bool, 0.0f,   0.0f,   ONSPEED_CHOICES(kBoolChoices), false},
    {"SPEED",              "Vno",               "Aircraft Vno",               "kts", FieldType::Int,  0.0f,   500.0f, nullptr, 0, false},
    {"CHIME_INTERVAL",     "vnoChimeInterval",  "Chime interval",             "sec", FieldType::UInt, 0.0f,   3600.0f, nullptr, 0, false},

    // Logging
    {"SDLOGGING",          "sdLogging",         "SD Card Logging",            "",  FieldType::Bool, 0.0f, 0.0f, ONSPEED_CHOICES(kBoolChoices), false},
    {"LOGRATE",            "logRate",           "Logging Rate",               "",  FieldType::Enum, 0.0f, 0.0f, ONSPEED_CHOICES(kLogRateChoices), false},

    // Serial output
    {"SERIALOUTFORMAT",    "serialOutFormat",   "Serial out format",          "",  FieldType::Enum, 0.0f, 0.0f, ONSPEED_CHOICES(kSerialOutFormatChoices), false},

    // Aircraft parameters
    {"GROSS_WEIGHT",       "acGrossWeight",     "Max gross weight",           "lbs",   FieldType::Int,   0.0f,    100000.0f, nullptr, 0, false},
    {"BEST_GLIDE_IAS",     "acBestGlideIAS",    "Best glide at max gross wt", "KIAS",  FieldType::Float, 0.0f,    500.0f, nullptr, 0, false},
    {"VFE",                "acVfe",             "Vfe - max flap speed",       "KIAS",  FieldType::Float, 0.0f,    500.0f, nullptr, 0, false},
    {"G_LIMIT",            "acGlimit",          "Custom G limit",             "G",     FieldType::Float, 0.0f,    20.0f,  nullptr, 0, false},
};

// ============================================================================
// kFlapSchema — per-flap nested fields (under SuFlaps / AoaCurve).
//
// The renderer applies these once per entry in cfg.aFlaps, appending the flap
// index to both `formName` (e.g. flapDegrees0, flapDegrees1, ...) and the
// HTML `id` attribute.  xmlTag values match ConfigXmlParse's child tag names
// inside <FLAP_POSITION>.
// ============================================================================

inline constexpr FieldDef kFlapSchema[] = {
    {"DEGREES",        "flapDegrees",        "Flap Degrees",         "deg", FieldType::Int,   -180.0f, 180.0f, nullptr, 0, true},
    {"POT_VALUE",      "flapPotPositions",   "Sensor Value",         "",    FieldType::Int,   0.0f,    8192.0f, nullptr, 0, true},
    {"LDMAXAOA",       "flapLDMAXAOA",       "L/Dmax",               "deg", FieldType::Float, -30.0f,  60.0f, nullptr, 0, true},
    {"ONSPEEDFASTAOA", "flapONSPEEDFASTAOA", "OnSpeed Fast",         "deg", FieldType::Float, -30.0f,  60.0f, nullptr, 0, true},
    {"ONSPEEDSLOWAOA", "flapONSPEEDSLOWAOA", "OnSpeed Slow",         "deg", FieldType::Float, -30.0f,  60.0f, nullptr, 0, true},
    {"STALLWARNAOA",   "flapSTALLWARNAOA",   "Stall Warning",        "deg", FieldType::Float, -30.0f,  60.0f, nullptr, 0, true},
    {"STALLAOA",       "flapSTALLAOA",       "Stall AOA",            "deg", FieldType::Float, -30.0f,  60.0f, nullptr, 0, true},
    {"MANAOA",         "flapMANAOA",         "Maneuvering AOA",      "deg", FieldType::Float, -30.0f,  60.0f, nullptr, 0, true},
    {"KFIT",           "flapKFit",           "K (lift sensitivity)", "deg*kt^2", FieldType::Float, -1e9f, 1e9f, nullptr, 0, true},
    {"ALPHA0",         "flapAlpha0",         "Alpha-0 (zero-lift)",  "deg", FieldType::Float, -30.0f,  30.0f, nullptr, 0, true},
    {"ALPHASTALL",     "flapAlphaStall",     "Alpha-Stall (from fit)", "deg", FieldType::Float, -30.0f,  60.0f, nullptr, 0, true},

    // AOA curve (nested <AOA_CURVE> inside <FLAP_POSITION>).  Form names
    // follow HandleConfig's `aoaCurve<idx>Type`, `aoaCurve<idx>Coeff0` etc.
    // pattern, so the renderer emits e.g. `aoaCurve0Type`, `aoaCurve0Coeff0`.
    {"TYPE",           "aoaCurveType",       "AOA Curve Type",       "",    FieldType::Enum, 0.0f, 0.0f, ONSPEED_CHOICES(kCurveTypeChoices), true},
    {"X3",             "aoaCurveCoeff0",     "AOA Curve Coeff 0",    "",    FieldType::Float, -1e6f, 1e6f, nullptr, 0, true},
    {"X2",             "aoaCurveCoeff1",     "AOA Curve Coeff 1",    "",    FieldType::Float, -1e6f, 1e6f, nullptr, 0, true},
    {"X1",             "aoaCurveCoeff2",     "AOA Curve Coeff 2",    "",    FieldType::Float, -1e6f, 1e6f, nullptr, 0, true},
    {"X0",             "aoaCurveCoeff3",     "AOA Curve Coeff 3",    "",    FieldType::Float, -1e6f, 1e6f, nullptr, 0, true},
};

#undef ONSPEED_CHOICES

inline constexpr std::size_t kSchemaCount     = sizeof(kSchema)     / sizeof(kSchema[0]);
inline constexpr std::size_t kFlapSchemaCount = sizeof(kFlapSchema) / sizeof(kFlapSchema[0]);

}  // namespace onspeed::web

#endif  // ONSPEED_CORE_WEB_WEB_SCHEMA_H
