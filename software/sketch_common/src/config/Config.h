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

#include "src/io/EfisSerialPort.h"

// Translate a configured EFIS-type string (as written in onspeed2.cfg
// under <EFISTYPE>) into the EfisSerialPort enum the driver consumes.
// Recognised strings: "VN-300", "ADVANCED" (Dynon SkyView), "DYNOND10",
// "GARMING5", "GARMING3X", "MGL"; anything else returns EnNone.
//
// Two callers in the firmware: ApplyPostParseSideEffects() at config-
// load time, and HandleConfigSave() when the pilot saves a new EFIS
// type via the web UI (so the driver picks up the change without a
// reboot).
EfisSerialPort::EnEfisType EfisTypeFromConfigString(const std::string& s);

// Re-export core types at global scope so legacy call sites that reference
// SuFlaps / SuDataSource / EnDataSource / SuIntArray / SuFloatArray without
// any namespace qualifier keep compiling.
using onspeed::SuCalibrationCurve;
using onspeed::MAX_AOA_CURVES;
using onspeed::MAX_CURVE_COEFF;
using onspeed::config::SuDataSource;
using onspeed::config::SuIntArray;
using onspeed::config::SuFloatArray;

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

    // String conversion helpers — still used by the web server for
    // rendering config values in HTML.  The V1 CSV-field helpers
    // (ParseIntCSV, ParseFloatCSV, ParseCurveCSV, GetConfigValue, etc.)
    // moved into onspeed_core/config/ConfigV1Parse as part of PR 3.1
    // Task 3 and are no longer exposed sketch-side.
    bool   ToBoolean(String sBool);
    float  ToFloat(String sFloat);
    String ToString(float fFloat);

    String ConfigurationToString();
    bool   LoadConfigFromString(String sConfig);
};
#endif  // _CONFIG_H_
