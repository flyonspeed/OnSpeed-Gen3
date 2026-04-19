
//#include <Arduino.h>

#include "Globals.h"

#ifdef SUPPORT_LITTLEFS
// Undefine SdFat's FILE_READ/FILE_WRITE before including LittleFS which redefines them
#undef FILE_READ
#undef FILE_WRITE
#include <LittleFS.h>
#endif

#include <config/ConfigV1Parse.h>
#include <config/ConfigXmlEmit.h>
#include <config/ConfigXmlParse.h>

#include "src/io/EfisSerialPort.h"


// ============================================================================
// Note: the constructor, LoadDefaults(), and the data struct live in
// onspeed_core/config/OnSpeedConfig.{h,cpp}.  XML parse/emit lives in
// onspeed_core/config/ConfigXml{Parse,Emit}.{h,cpp}.  V1 (Gen2-era CSV)
// parse lives in onspeed_core/config/ConfigV1Parse.{h,cpp}.  Only
// sketch-side bits remain here: SD/flash file I/O and the post-parse
// side effects on global state (EFIS type cache, volume, IMU/AHRS reinit).
// ============================================================================

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

// Apply post-parse side effects that touch global state — factored out so
// both the V2 (core) parse and the V1 (legacy, still here) path can call
// the same helper after updating the member fields.
static void ApplyPostParseSideEffects(FOSConfig& cfg)
{
    if (!cfg.bVolumeControl)
        g_AudioPlay.SetVolume(cfg.iDefaultVolume);

    if      (cfg.sEfisType == "VN-300")    g_EfisSerial.enType = EfisSerialPort::EnVN300;
    else if (cfg.sEfisType == "ADVANCED")  g_EfisSerial.enType = EfisSerialPort::EnDynonSkyview;
    else if (cfg.sEfisType == "DYNOND10")  g_EfisSerial.enType = EfisSerialPort::EnDynonD10;
    else if (cfg.sEfisType == "GARMING5")  g_EfisSerial.enType = EfisSerialPort::EnGarminG5;
    else if (cfg.sEfisType == "GARMING3X") g_EfisSerial.enType = EfisSerialPort::EnGarminG3X;
    else if (cfg.sEfisType == "MGL")       g_EfisSerial.enType = EfisSerialPort::EnMglBinary;
    else                                   g_EfisSerial.enType = EfisSerialPort::EnNone;

    if (g_pIMU != nullptr)
        {
        g_pIMU->ConfigAxes();
        g_AHRS.Init(kImuSampleRateHz);
        }
}

// ----------------------------------------------------------------------------
// Main config functions
// ----------------------------------------------------------------------------

// Find and load a valid configuration. First load default compiled in values.
// Then try loading a configuration stored in flash. Lastly, load a configuration
// file from the SD card.

void FOSConfig::LoadConfig()
{
    bool    bStatus = false;

    // Load default config
    LoadDefaultConfiguration();

#ifdef SUPPORT_LITTLEFS
    // Load configuration from flash
    bStatus = LoadConfigurationFileFromFlash(szDefaultConfigFilename);
    if (bStatus)
        bConfigLoaded = true;
#endif

    // Load configuration from SD card
    if (g_SdFileSys.bSdAvailable && g_SdFileSys.exists(szDefaultConfigFilename))
        {
        // Load config from file
        g_Log.printf("Loading %s configuration\n", szDefaultConfigFilename);
        bStatus = LoadConfigurationFile(szDefaultConfigFilename);
        if (bStatus)
            bConfigLoaded = true;
        }

    // Log what happened
    if (bConfigLoaded)
        g_Log.println(MsgLog::EnConfig, MsgLog::EnDebug, "Configuration loaded.");
    else
        g_Log.println(MsgLog::EnConfig, MsgLog::EnDebug, "Default configuration loaded.");

}

// ----------------------------------------------------------------------------

// Load / save SD card files

bool FOSConfig::LoadConfigurationFile(char* szFilename)
    {
    String  sConfig = "";
    bool    bStatus = false;

    // If config file exists on SD card load it
    FsFile  hConfigFile;

    if (!xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100)))
        return false;

    hConfigFile = g_SdFileSys.open(szFilename, O_READ);
    if (!hConfigFile)
        {
        g_Log.printf(MsgLog::EnConfig, MsgLog::EnDebug, "Error reading %s from SD card", szFilename);
        xSemaphoreGive(xWriteMutex);
        return false;
        }

    while (hConfigFile.available())
        sConfig += char(hConfigFile.read());

    // Close the file:
    hConfigFile.close();

    xSemaphoreGive(xWriteMutex);

    g_Log.printf(MsgLog::EnConfig, MsgLog::EnDebug, "Read file '%s' from SD card\n", szFilename);

    bStatus = LoadConfigFromString(sConfig);

    return bStatus;
    }

// ----------------------------------------------------------------------------

bool FOSConfig::SaveConfigurationToFile()
{
#ifdef SUPPORT_LITTLEFS
    // Save it to flash also
    SaveConfigurationToFlash(szDefaultConfigFilename);
#endif

    return SaveConfigurationToFile(szDefaultConfigFilename);
}


// ----------------------------------------------------------------------------

bool FOSConfig::SaveConfigurationToFile(char* szFilename)
    {
    bool    bStatus = false;
    String  sConfig;

#ifdef SUPPORT_LITTLEFS
    // Save it to flash also
    SaveConfigurationToFlash(szFilename);
#endif

    // Convert the configuration to XML
    sConfig = ConfigurationToString();

    // Save XML string to config file
    FsFile  hConfigFile;

    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
        {
        hConfigFile = g_SdFileSys.open(szFilename, O_WRITE | O_CREAT | O_TRUNC);
        if (hConfigFile)
            {
            hConfigFile.print(sConfig);
            hConfigFile.close();
            bStatus = true;
            }

        xSemaphoreGive(xWriteMutex);
        }

    if (bStatus == false)
        g_Log.println(MsgLog::EnConfig, MsgLog::EnError, "Could not save config file");
    else
        g_Log.println(MsgLog::EnConfig, MsgLog::EnDebug, "Saved config file to SD card");

    return bStatus;
    }

// ----------------------------------------------------------------------------

// Load / save flash file system files

#ifdef SUPPORT_LITTLEFS

// Load a configuration file from flash memory

bool FOSConfig::LoadConfigurationFileFromFlash(char* szFilename)
    {
    String  sConfig = "";
    bool    bStatus = false;

    // Load configuration from flash
    if (g_bFlashFS)
        {
        String  sFilename;
        sFilename  = "/";
        sFilename += szFilename;

        File hFlashFile = LittleFS.open(sFilename, "r");

        if (hFlashFile)
            {
            sConfig = hFlashFile.readString();
            hFlashFile.close();

            g_Log.printf(MsgLog::EnConfig, MsgLog::EnDebug, "Read config file '%s' from flash\n", szFilename);

            bStatus = LoadConfigFromString(sConfig);
            } // end if config file open OK

        else
            g_Log.printf(MsgLog::EnConfig, MsgLog::EnError, "Error opening '%s' from flash for reading\n", szFilename);
        } // end if flash file system enabled

    return bStatus;
    }

// ----------------------------------------------------------------------------

bool FOSConfig::SaveConfigurationToFlash()
    {
    return SaveConfigurationToFlash(szDefaultConfigFilename);
    }

// ----------------------------------------------------------------------------

bool FOSConfig::SaveConfigurationToFlash(char* szFilename)
    {
    String  sFilename;
    String  sConfig;

    // Convert the configuration to XML
    sConfig = ConfigurationToString();

    sFilename  = "/";
    sFilename += szFilename;

    File hFlashFile = LittleFS.open(sFilename, "w");
    if (hFlashFile)
        {
        hFlashFile.print(sConfig);
        hFlashFile.close();
        }
    else
        {
        g_Log.println(MsgLog::EnConfig, MsgLog::EnError, "Could not save config file to flash");
        return false;
        }

    g_Log.println(MsgLog::EnConfig, MsgLog::EnDebug, "Saved config file to flash");
    return true;
    }

#endif

// ----------------------------------------------------------------------------
// XML emit — delegates to onspeed_core/config/ConfigXmlEmit.
// ----------------------------------------------------------------------------

String FOSConfig::ConfigurationToString()
{
    // Cast to the base OnSpeedConfig — EmitXml only needs core fields.
    std::string sXml = onspeed::config::EmitXml(static_cast<const onspeed::config::OnSpeedConfig&>(*this));
    return String(sXml.c_str());
}


// ----------------------------------------------------------------------------
// XML parse — V2 and V1 both delegate to onspeed_core.  V1 is detected
// first because the V2 parser will reject documents whose root is
// <CONFIG> rather than <CONFIG2>.
// ----------------------------------------------------------------------------

bool FOSConfig::LoadConfigFromString(String sConfig)
{
    // V1 (Gen2-era) <CONFIG>...</CONFIG> — delegated to core.
    // ------------------------------------------------------
    std::string_view sv(sConfig.c_str(), sConfig.length());
    if (onspeed::config::IsV1Format(sv))
        {
#ifdef SUPPORT_CONFIG_V1
        onspeed::config::V1ParseStatus v1status =
            onspeed::config::ParseV1(sv, *this);

        if (v1status != onspeed::config::V1ParseStatus::Ok)
            {
            g_Log.println(MsgLog::EnConfig, MsgLog::EnWarning, "V1 config parse failed");
            return false;
            }

        g_Log.println(MsgLog::EnConfig, MsgLog::EnDebug, "Decoded V1 config string");

        ApplyPostParseSideEffects(*this);
        return true;
#else
        return false;
#endif
        }

    // CONFIG2 format — delegated to onspeed_core/config/ConfigXmlParse.
    // --------------
    else if (sConfig.indexOf("<CONFIG2>" ) >= 0 && sConfig.indexOf("</CONFIG2>" ) >= 0)
        {
        std::string xml(sConfig.c_str(), sConfig.length());
        onspeed::config::XmlParseStatus status = onspeed::config::ParseXml(xml, *this);

        // TooManyFlaps is a warning, not a hard failure: the first
        // MAX_AOA_CURVES entries were parsed successfully and the config
        // is usable.  Log it so the user knows and move on.
        if (status == onspeed::config::XmlParseStatus::TooManyFlaps)
            {
            g_Log.println(MsgLog::EnConfig, MsgLog::EnWarning,
                          "Config had too many FLAP_POSITION entries; truncated to MAX_AOA_CURVES");
            }
        else if (status != onspeed::config::XmlParseStatus::Ok)
            {
            g_Log.println(MsgLog::EnConfig, MsgLog::EnWarning, "CONFIG2 XML parse failed");
            return false;
            }

        g_Log.println(MsgLog::EnConfig, MsgLog::EnDebug, "Decoded V2 config string");

        ApplyPostParseSideEffects(*this);
        return true;
        }

    // Unknown or bad format string
    // ----------------------------
    else
        {
        g_Log.println(MsgLog::EnConfig, MsgLog::EnWarning, "Unknow config string format");
        return false;
        }
}


// ----------------------------------------------------------------------------
// String conversion helpers — still used by the web server for rendering
// config values in the HTML UI.  (The V1 CSV-field helpers — ParseIntCSV,
// ParseFloatCSV, ParseCurveCSV, GetConfigValue, MakeConfig, Curve2String,
// Array2String — moved into onspeed_core/config/ConfigV1Parse as part of
// Task 3 and are no longer needed sketch-side.)
// ----------------------------------------------------------------------------

bool FOSConfig::ToBoolean(String sBool)
{
    if (sBool.toInt()==1 || sBool=="YES" || sBool=="ENABLED" || sBool=="ON")
      return true;
    else
      return false;
}

// ----------------------------------------------------------------------------

float FOSConfig::ToFloat(String sString)
{
    return  atof(sString.c_str());
}

// ----------------------------------------------------------------------------

String FOSConfig::ToString(float fFloat)
{
    char szFloatBuffer[20];

    // Save config float values with 4 digit precision
    snprintf(szFloatBuffer, sizeof(szFloatBuffer), "%.4f", fFloat);
    return String (szFloatBuffer);
}
