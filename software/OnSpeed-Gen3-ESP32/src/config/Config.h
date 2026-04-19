// Config.h — sketch-side wrapper around onspeed::config::OnSpeedConfig.
//
// The config data struct + defaults loader lives in onspeed_core
// (software/Libraries/onspeed_core/src/config/OnSpeedConfig.h).  This header
// adds the sketch-owned XML parse/emit and SD/flash I/O methods on top of it
// via inheritance, keeping the legacy `FOSConfig` name so the 400+ existing
// `g_Config.xxx` call sites don't need to change.
//
// Task 2 of PR 3.1 will port the XML code into core, at which point this
// wrapper will collapse to just the SD/flash file I/O methods.

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <Arduino.h>
#include <vector>

#include <onspeed_core.h>
#include <config/OnSpeedConfig.h>
#include <util/OnSpeedTypes.h>  // Core types: SuCalibrationCurve, MAX_CURVE_COEFF, etc.

// Re-export core types at global scope so legacy call sites that reference
// SuFlaps / SuDataSource / EnDataSource / SuIntArray / SuFloatArray without
// any namespace qualifier keep compiling.
using onspeed::SuCalibrationCurve;
using onspeed::MAX_AOA_CURVES;
using onspeed::MAX_CURVE_COEFF;
using onspeed::config::SuDataSource;
#ifdef SUPPORT_CONFIG_V1
using onspeed::config::SuIntArray;
using onspeed::config::SuFloatArray;
#endif

// SuFlaps is a member of OnSpeedConfig; a global alias keeps the unqualified
// name available (e.g. the sort-lambda `[](SuFlaps a, SuFlaps b) ...` in
// Config.cpp).  FOSConfig also inherits it so `FOSConfig::SuFlaps` still works.
using SuFlaps = onspeed::config::OnSpeedConfig::SuFlaps;

// ============================================================================
// FOSConfig — adds Arduino/SD-card I/O methods to the core struct.
// All data fields are inherited from onspeed::config::OnSpeedConfig.
// ============================================================================

class FOSConfig : public onspeed::config::OnSpeedConfig
{
public:
    FOSConfig() = default;  // core constructor calls LoadDefaults()

    // Re-export the enum and static parser helper with Arduino String so the
    // legacy `FOSConfig::EnSerialFmtG3X` / `FOSConfig::ParseSerialFmt(String)`
    // call sites continue to compile.  Values mirror the core enum.
    using EnSerialFmt = onspeed::config::OnSpeedConfig::EnSerialFmt;
    static constexpr EnSerialFmt EnSerialFmtOther   = onspeed::config::OnSpeedConfig::EnSerialFmtOther;
    static constexpr EnSerialFmt EnSerialFmtG3X     = onspeed::config::OnSpeedConfig::EnSerialFmtG3X;
    static constexpr EnSerialFmt EnSerialFmtOnSpeed = onspeed::config::OnSpeedConfig::EnSerialFmtOnSpeed;

    static EnSerialFmt ParseSerialFmt(const String& s) {
        if (s == "G3X")     return EnSerialFmtG3X;
        if (s == "ONSPEED") return EnSerialFmtOnSpeed;
        return EnSerialFmtOther;
    }

    // ------------------------------------------------------------------------
    // Sketch-owned methods — XML parse/emit + SD/flash file I/O.
    // Task 2 will push the XML bits into core; SD/flash stay here.
    // ------------------------------------------------------------------------

    bool LoadConfigurationFile(char* szFilename);
    bool SaveConfigurationToFile();
    bool SaveConfigurationToFile(char* szFilename);

#ifdef SUPPORT_LITTLEFS
    bool LoadConfigurationFileFromFlash(char* szFilename);
    bool SaveConfigurationToFlash();
    bool SaveConfigurationToFlash(char* szFilename);
#endif

    // Back-compat alias for the core's LoadDefaults().
    bool LoadDefaultConfiguration() { return LoadDefaults(); }
    void LoadConfig();

    bool   ToBoolean(String sBool);
    float  ToFloat(String sFloat);
    String ToString(float fFloat);
#ifdef SUPPORT_CONFIG_V1
    SuIntArray          ParseIntCSV(String sConfig);
    SuFloatArray        ParseFloatCSV(String sConfig, int limit=MAX_AOA_CURVES);
    SuCalibrationCurve  ParseCurveCSV(String sConfig);
    String              GetConfigValue(String sConfig, String configName);
    String              MakeConfig(String configName, String configValue);
    String              Curve2String(SuCalibrationCurve sConfig);
    String              Array2String(SuFloatArray       afConfig);
    String              Array2String(SuIntArray         aiConfig);
#endif

    String ConfigurationToString();
    bool   LoadConfigFromString(String sConfig);
};
#endif  // _CONFIG_H_
