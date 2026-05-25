
//#include <Arduino.h>

#include "src/Globals.h"

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
EfisSerialPort::EnEfisType EfisTypeFromConfigString(const std::string& s)
{
    if      (s == "VN-300")    return EfisSerialPort::EnVN300;
    else if (s == "ADVANCED")  return EfisSerialPort::EnDynonSkyview;
    else if (s == "DYNOND10")  return EfisSerialPort::EnDynonD10;
    else if (s == "GARMING5")  return EfisSerialPort::EnGarminG5;
    else if (s == "GARMING3X") return EfisSerialPort::EnGarminG3X;
    else if (s == "MGL")       return EfisSerialPort::EnMglBinary;
    else                       return EfisSerialPort::EnNone;
}

static void ApplyPostParseSideEffects(FOSConfig& cfg)
{
    // Defend against a parsed config that left aFlaps empty (corrupt
    // XML, hand-edited file with all FLAP_POSITION entries deleted,
    // etc.). Every downstream reader dereferences aFlaps[iIndex]
    // without bounds checks, so we guarantee one zeroed entry here.
    if (onspeed::config::EnsureAtLeastOneFlap(cfg.aFlaps))
        g_Log.println(MsgLog::EnConfig, MsgLog::EnWarning,
            "Parsed config had no flap positions; "
            "inserted a zeroed default. Calibrate before flying.");

    if (!cfg.bVolumeControl)
        g_AudioPlay.SetVolume(cfg.iDefaultVolume);

    // Route through RequestTypeChange so any runtime config reload
    // (e.g. console "load" command, web config-file upload) triggers
    // the deferred parser + UART reinit in Read().  The boot path also
    // exercises this code, but the .ino calls g_EfisSerial.Init()
    // directly right after LoadConfig — Init() clears pendingType_ so
    // the first Read() afterward is a no-op.
    g_EfisSerial.RequestTypeChange(EfisTypeFromConfigString(cfg.sEfisType));

    if (g_pIMU != nullptr)
        {
        g_pIMU->ConfigAxes();
        g_AHRS.Init(g_imuSampleRateHz);
        }
}

// ----------------------------------------------------------------------------
// Main config functions
// ----------------------------------------------------------------------------

// Find and load a valid configuration.
//
// Source-of-truth contract: SD card is authoritative. Flash is the
// backup that only matters when the SD card has no config file (fresh
// card, or post-format). The pilot edits via the web UI which writes
// SD first then mirrors to flash on success; the LittleFS copy never
// holds state that the SD card hasn't also accepted.
//
// Load order:
//   1. Compiled-in defaults (always run first so every field is
//      initialized to a known sane value).
//   2. SD card (if config file exists and load succeeds).
//   3. Flash (ONLY if SD didn't provide a config — file missing or
//      load failed).
//
// The previous order (flash first, then SD overwrites) meant that
// a transient SD load failure (mutex timeout, malformed field at row
// N) left fields 1..N-1 sourced from SD and N..end from flash —
// silent mixed-state. The new order keeps the box on a single
// coherent config source.
//
// Partial-parse caveat: today, LoadConfigurationFile parses
// field-by-field; a malformed value mid-file leaves the partial
// state already loaded. A future improvement would parse into a
// staging struct and commit only on success, giving full transaction
// semantics. For now, a loud warning is emitted if the SD load
// returns false, and the pilot can re-save from the web UI.
void FOSConfig::LoadConfig()
{
    bool    bSdLoaded    = false;
    bool    bFlashLoaded = false;

    // Load default config first so every field has a known value
    // even if every persistent source fails.
    LoadDefaultConfiguration();

    // Try SD card first — it's the source of truth.
    if (g_SdFileSys.bSdAvailable && g_SdFileSys.exists(szDefaultConfigFilename))
        {
        g_Log.printf("Loading %s configuration from SD\n", szDefaultConfigFilename);
        bSdLoaded = LoadConfigurationFile(szDefaultConfigFilename);
        if (!bSdLoaded)
            g_Log.println(MsgLog::EnConfig, MsgLog::EnError,
                "SD config file exists but failed to load (mutex timeout or "
                "parse error); falling back to flash backup. Re-save from the "
                "web UI to refresh SD.");
        }

#ifdef SUPPORT_LITTLEFS
    // Flash backup — only if SD didn't provide a config.
    if (!bSdLoaded)
        {
        bFlashLoaded = LoadConfigurationFileFromFlash(szDefaultConfigFilename);
        if (bFlashLoaded)
            g_Log.println(MsgLog::EnConfig, MsgLog::EnWarning,
                "Loaded configuration from flash backup (SD config "
                "missing or failed to load).");
        }
#endif

    bConfigLoaded = (bSdLoaded || bFlashLoaded);

    // Log what happened
    if (bSdLoaded)
        g_Log.println(MsgLog::EnConfig, MsgLog::EnDebug, "Configuration loaded from SD.");
    else if (bFlashLoaded)
        g_Log.println(MsgLog::EnConfig, MsgLog::EnDebug, "Configuration loaded from flash.");
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

    // 5s timeout — matches the other SD-mutex takers. At boot this
    // path normally runs uncontended (writer task starts after
    // LoadConfig() returns), so the wait is almost never measurable.
    // But if we ever do lose the race, the consequence is "boot
    // silently falls back to whatever LittleFS has," which can leave
    // the box running with stale config without any pilot-visible
    // signal. Failing slow is preferable to failing silent.
    if (!xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(5000)))
        {
        g_Log.println(MsgLog::EnConfig, MsgLog::EnError,
            "LoadConfigurationFile: xWriteMutex timeout — "
            "SD config NOT loaded; falling back to flash/defaults. "
            "Reboot or save config to recover.");
        return false;
        }

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
    // Outer wrapper now just defers to the named-file save below.
    // Flash mirror is written by that path AFTER the SD save succeeds —
    // ensures flash never holds a newer state than SD. Previously,
    // flash was written unconditionally first, so a failed SD save
    // (or a partial-form save that wrote garbage to SD) left flash
    // and SD in different states. Now they're always in lockstep.
    return SaveConfigurationToFile(szDefaultConfigFilename);
}


// ----------------------------------------------------------------------------

bool FOSConfig::SaveConfigurationToFile(char* szFilename)
    {
    bool    bStatus = false;
    String  sConfig;

    // Convert the configuration to XML
    sConfig = ConfigurationToString();

    // Save XML string to config file. Retry the mutex-take up to 4
    // times with backoff. At 208 Hz the writer is frequently holding
    // xWriteMutex (sync, write, sidecar) and any single 1-sec wait
    // can lose the race. Each retry waits 1 sec and tries fresh —
    // total worst-case is ~5 sec, which is acceptable for a user-
    // initiated config save. Keeps the producer running throughout
    // (no g_bPause needed — paused_drops would silently lose IMU
    // samples and we've worked hard to make sure that doesn't happen).
    FsFile  hConfigFile;
    static const int kMaxAttempts = 4;
    int attempt = 0;
    // Distinguish mutex-busy (retry helps) from filesystem-open-failed
    // (retry won't help — FS full, card unmounted, permissions weirdness).
    // For FS failures we break out early with a precise error rather than
    // burning all 4 retries and emitting a misleading "mutex timeout".
    bool bFsOpenFailed = false;
    while (attempt < kMaxAttempts && !bStatus)
        {
        attempt++;
        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
            {
            hConfigFile = g_SdFileSys.open(szFilename, O_WRITE | O_CREAT | O_TRUNC);
            if (hConfigFile)
                {
                hConfigFile.print(sConfig);
                hConfigFile.close();
                bStatus = true;
                xSemaphoreGive(xWriteMutex);
                }
            else
                {
                // Mutex acquired but file open failed — not a contention
                // issue. Break out and report accurately.
                xSemaphoreGive(xWriteMutex);
                bFsOpenFailed = true;
                break;
                }
            }
        else if (attempt < kMaxAttempts)
            {
            // Yield to the writer for a short bit before retrying so
            // the next take has a real chance of winning the race.
            g_Log.printf(MsgLog::EnConfig, MsgLog::EnWarning,
                         "Config save: xWriteMutex busy, retry %d/%d\n",
                         attempt, kMaxAttempts);
            vTaskDelay(pdMS_TO_TICKS(250));
            }
        }

    if (bStatus == false)
        {
        if (bFsOpenFailed)
            g_Log.printf(MsgLog::EnConfig, MsgLog::EnError,
                         "Could not save config file '%s' (SD open() failed — "
                         "card full, FS error, or card removed?)\n",
                         szFilename);
        else
            g_Log.println(MsgLog::EnConfig, MsgLog::EnError,
                          "Could not save config file (xWriteMutex timeout after retries)");
        }
    else
        g_Log.printf(MsgLog::EnConfig, MsgLog::EnWarning,
                     "Saved config file to SD card (attempt %d/%d)\n",
                     attempt, kMaxAttempts);

#ifdef SUPPORT_LITTLEFS
    // Mirror to flash ONLY on SD-save success. If SD failed, flash
    // keeps the previous successfully-saved version so that on next
    // boot we recover the last good state rather than an in-memory
    // snapshot that never made it to SD. This is the "SD is the
    // source of truth, flash is its backup" contract.
    if (bStatus)
        SaveConfigurationToFlash(szFilename);
#endif

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
        // Legacy Gen2-era flat-tag config format.  Always supported
        // for pilots upgrading from Gen2 SD cards; CONFIG2 below is
        // the format the firmware emits on save.
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
