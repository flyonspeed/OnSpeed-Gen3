
//#include <Arduino.h>

#include "Globals.h"

#ifdef SUPPORT_LITTLEFS
// Undefine SdFat's FILE_READ/FILE_WRITE before including LittleFS which redefines them
#undef FILE_READ
#undef FILE_WRITE
#include <LittleFS.h>
#endif

#include <config/ConfigXmlEmit.h>
#include <config/ConfigXmlParse.h>

#include "src/io/EfisSerialPort.h"


// ============================================================================
// Note: the constructor, LoadDefaults(), and the data struct live in
// onspeed_core/config/OnSpeedConfig.{h,cpp}.  XML parse/emit lives in
// onspeed_core/config/ConfigXml{Parse,Emit}.{h,cpp}.  Only sketch-side
// bits remain here: SD/flash file I/O, the V1 legacy CSV parser (Task 3
// will push that into core too), and the post-parse side effects on
// global state (EFIS type cache, volume, IMU/AHRS reinit).
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
// XML parse — V2 delegates to core; V1 (legacy CSV tags) stays here until
// Task 3 lifts it too.
// ----------------------------------------------------------------------------

bool FOSConfig::LoadConfigFromString(String sConfig)
{
    // Original CONFIG format
    // ----------------------
    if (sConfig.indexOf("<CONFIG>" ) >= 0 && sConfig.indexOf("</CONFIG>" ) >= 0)
        {
#ifdef SUPPORT_CONFIG_V1
        int             iFlapsArraySize;
        int             iIdx;
        String          sDataSource;
        SuIntArray      aiValues;
        SuFloatArray    afValues;

        iAoaSmoothing       = GetConfigValue(sConfig,"AOA_SMOOTHING").toInt();
        iPressureSmoothing  = GetConfigValue(sConfig,"PRESSURE_SMOOTHING").toInt();
        iMuteAudioUnderIAS  = GetConfigValue(sConfig,"MUTE_AUDIO_UNDER_IAS").toInt();

        sDataSource  = GetConfigValue(sConfig,"DATASOURCE");
        suDataSrc.fromStrSet(sDataSource.c_str());

        sReplayLogFileName  = GetConfigValue(sConfig,"REPLAYLOGFILENAME").c_str();

        // Flap position related values
        aiValues = ParseIntCSV(GetConfigValue(sConfig,"FLAPDEGREES"));
        iFlapsArraySize = aiValues.Count;
        aFlaps.resize(iFlapsArraySize);
        for (iIdx=0; (iIdx<aiValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].iDegrees = aiValues.Items[iIdx];

        aiValues = ParseIntCSV(GetConfigValue(sConfig,"FLAPPOTPOSITIONS"));
        for (iIdx=0; (iIdx<aiValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].iPotPosition = aiValues.Items[iIdx];

        afValues = ParseFloatCSV(GetConfigValue(sConfig,"SETPOINT_LDMAXAOA"));
        for (iIdx=0; (iIdx<aiValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].fLDMAXAOA = afValues.Items[iIdx];

        afValues = ParseFloatCSV(GetConfigValue(sConfig,"SETPOINT_ONSPEEDFASTAOA"));
        for (iIdx=0; (iIdx<aiValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].fONSPEEDFASTAOA = afValues.Items[iIdx];

        afValues = ParseFloatCSV(GetConfigValue(sConfig,"SETPOINT_ONSPEEDSLOWAOA"));
        for (iIdx=0; (iIdx<aiValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].fONSPEEDSLOWAOA = afValues.Items[iIdx];

        afValues = ParseFloatCSV(GetConfigValue(sConfig,"SETPOINT_STALLWARNAOA"));
        for (iIdx=0; (iIdx<aiValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].fSTALLWARNAOA = afValues.Items[iIdx];

        afValues = ParseFloatCSV(GetConfigValue(sConfig,"SETPOINT_STALLAOA"),iFlapsArraySize); // STALLAOA is only available after calibration wizard run
        for (iIdx=0; (iIdx<aiValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].fSTALLAOA = afValues.Items[iIdx];

//        afValues = ParseFloatCSV(GetConfigValue(sConfig,"SETPOINT_MANAOA"),iFlapsArraySize); // MANAOA is only available after calibration wizard run
//        for (iIdx=0; (iIdx<aiValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].fMANAOA = afValues.Items[iIdx];

        afValues = ParseFloatCSV(GetConfigValue(sConfig,"SETPOINT_ALPHA0"),iFlapsArraySize);
        for (iIdx=0; (iIdx<afValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].fAlpha0 = afValues.Items[iIdx];

        afValues = ParseFloatCSV(GetConfigValue(sConfig,"SETPOINT_ALPHASTALL"),iFlapsArraySize);
        for (iIdx=0; (iIdx<afValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].fAlphaStall = afValues.Items[iIdx];

        // aoa curves: AOA_CURVE_FLAPS0, AOA_CURVE_FLAPS1,...
        for (iIdx=0; iIdx<iFlapsArraySize; iIdx++)
            {
            SuCalibrationCurve  aAoaCurve;
            aAoaCurve = ParseCurveCSV(GetConfigValue(sConfig,"AOA_CURVE_FLAPS"+String(iIdx)));
            aFlaps[iIdx].AoaCurve.iCurveType = aAoaCurve.iCurveType;
            for (int iIdxCoeff=0; iIdxCoeff<MAX_CURVE_COEFF; iIdxCoeff++)
                aFlaps[iIdx].AoaCurve.afCoeff[iIdxCoeff] = aAoaCurve.afCoeff[iIdxCoeff];
            }

        // Sort flaps data array by flap degrees
        std::sort(aFlaps.begin(), aFlaps.end(),
                [](SuFlaps a, SuFlaps b) { return a.iDegrees < b.iDegrees; } );


        // Volume
        bVolumeControl      = ToBoolean(GetConfigValue(sConfig,"VOLUMECONTROL"));
        iVolumeHighAnalog   = GetConfigValue(sConfig,"VOLUME_HIGH_ANALOG").toInt();
        iVolumeLowAnalog    = GetConfigValue(sConfig,"VOLUME_LOW_ANALOG").toInt();
        iDefaultVolume      = GetConfigValue(sConfig,"VOLUME_DEFAULT").toInt();

        bAudio3D            = ToBoolean(GetConfigValue(sConfig,"3DAUDIO"));
        bOverGWarning       = ToBoolean(GetConfigValue(sConfig,"OVERGWARNING"));

        //CAS curve
        CasCurve            = ParseCurveCSV(GetConfigValue(sConfig,"CAS_CURVE"));
        bCasCurveEnabled    = ToBoolean(GetConfigValue(sConfig,"CAS_ENABLED"));

        sPortsOrientation   = GetConfigValue(sConfig,"PORTS_ORIENTATION").c_str();
        sBoxtopOrientation  = GetConfigValue(sConfig,"BOX_TOP_ORIENTATION").c_str();
        sEfisType           = GetConfigValue(sConfig,"EFISTYPE").c_str();

        // Calibration data source
        sCalSource           = GetConfigValue(sConfig,"CALWIZ_SOURCE").c_str();
        bCalSourceEfis       = (sCalSource == "EFIS");

        // Biases
        iPFwdBias           =  GetConfigValue(sConfig,"PFWD_BIAS").toInt();
        iP45Bias            =  GetConfigValue(sConfig,"P45_BIAS").toInt();
        // Negate: Gen2 V1 configs stored bias for Pstatic+bias (addition),
        // but Gen3 uses Pstatic-bias (subtraction). Flip sign on load.
        fPStaticBias        = -ToFloat(GetConfigValue(sConfig,"PSTATIC_BIAS"));
        fGxBias             =  ToFloat(GetConfigValue(sConfig,"GX_BIAS"));
        fGyBias             =  ToFloat(GetConfigValue(sConfig,"GY_BIAS"));
        fGzBias             =  ToFloat(GetConfigValue(sConfig,"GZ_BIAS"));
        fPitchBias          =  ToFloat(GetConfigValue(sConfig,"PITCH_BIAS"));
        fRollBias           =  ToFloat(GetConfigValue(sConfig,"ROLL_BIAS"));

        // serial inputs
        bReadBoom           = ToBoolean(GetConfigValue(sConfig,"BOOM"));
        bBoomChecksum       = ToBoolean(GetConfigValue(sConfig,"BOOMCHECKSUM"));
        bReadEfisData       = ToBoolean(GetConfigValue(sConfig,"SERIALEFISDATA"));
        bOatSensor          = ToBoolean(GetConfigValue(sConfig,"OATSENSOR"));

        // serial output
        sSerialOutFormat    = GetConfigValue(sConfig,"SERIALOUTFORMAT").c_str();
        enSerialOutFormat   = OnSpeedConfig::ParseSerialFmt(sSerialOutFormat);

        // Load limit
        fLoadLimitPositive  = GetConfigValue(sConfig,"LOADLIMITPOSITIVE").toFloat();
        fLoadLimitNegative  = GetConfigValue(sConfig,"LOADLIMITNEGATIVE").toFloat();

        // Vno chime
        iVno                = GetConfigValue(sConfig,"VNO").toInt();
        uVnoChimeInterval   = GetConfigValue(sConfig,"VNO_CHIME_INTERVAL").toInt();
        bVnoChimeEnabled    = ToBoolean(GetConfigValue(sConfig,"VNO_CHIME_ENABLED"));

        // SD card logging
        bSdLogging          = ToBoolean(GetConfigValue(sConfig,"SDLOGGING"));

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
// Utility functions (used by the V1 legacy path above; Task 3 lifts them
// into core with ConfigV1Parse).
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

// ----------------------------------------------------------------------------
#ifdef SUPPORT_CONFIG_V1

SuIntArray FOSConfig::ParseIntCSV(String sConfig)
{
    int         commaIndex=0;
    SuIntArray  result;
    for (int i=0; i < MAX_AOA_CURVES; i++)
    {
        commaIndex=sConfig.indexOf(',');
        if (commaIndex>0)
        {
            result.Items[i]=sConfig.substring(0,commaIndex).toInt();
            result.Count=i+1;
            sConfig=sConfig.substring(commaIndex+1,sConfig.length());
        }
        else
        {
            // last value
            result.Items[i]=sConfig.toInt();
            result.Count=i+1;
            return result;
        }
    }

    return result;
}

// ----------------------------------------------------------------------------

SuFloatArray FOSConfig::ParseFloatCSV(String sConfig, int limit)
{
    int           commaIndex=0;
    SuFloatArray  result;
    for (int i=0; i < limit; i++)
    {
        if (sConfig=="")
        {
            result.Items[i]=0;
            result.Count=i+1;
        }
        else
        {
            commaIndex=sConfig.indexOf(',');
            if (commaIndex>0)
            {
                result.Items[i]=ToFloat(sConfig.substring(0,commaIndex));
                result.Count=i+1;
                sConfig=sConfig.substring(commaIndex+1,sConfig.length());
            }
            else
            {
                // last value
                result.Items[i]=ToFloat(sConfig);
                result.Count=i+1;
                return result;
            }
        }

    }
    return result;
}

// ----------------------------------------------------------------------------

SuCalibrationCurve FOSConfig::ParseCurveCSV(String sConfig)
{
    int                 iCommaIndex=0;
    SuCalibrationCurve  sResult;
    for (int i=0; i <= MAX_CURVE_COEFF; i++)
    {
        iCommaIndex = sConfig.indexOf(',');
        if (iCommaIndex>0)
        {
            sResult.afCoeff[i] = ToFloat(sConfig.substring(0, iCommaIndex));
            sConfig=sConfig.substring(iCommaIndex+1, sConfig.length());
        }
        else
        {
            // last value is curveType
            sResult.iCurveType = sConfig.toInt();
            return sResult;
        }
    }
    return sResult;
}

// ----------------------------------------------------------------------------

String FOSConfig::GetConfigValue(String sConfig,String configName)
{
    // parse a config value from config string
    int     iStartIndex;
    int     iEndIndex;

    iStartIndex = sConfig.indexOf("<"+configName+">");
    iEndIndex   = sConfig.indexOf("</"+configName+">");

    if (iStartIndex < 0 || iEndIndex < 0)
        return ""; // value not found in config

    return sConfig.substring(iStartIndex+configName.length()+2, iEndIndex);
}

// ----------------------------------------------------------------------------

String FOSConfig::MakeConfig(String configName, String configValue)
{
    String sResult = "<";
    sResult.concat(configName);
    sResult.concat(">");
    sResult.concat(configValue);
    sResult.concat("</");
    sResult.concat(configName);
    sResult.concat(">\n");
    return sResult;
    //return "<"+configName+">"+configValue+"</"+configName+">\n";
}

// ----------------------------------------------------------------------------

String FOSConfig::Curve2String(SuCalibrationCurve sConfig)
{
    String sResult = "";
    for (int i=0; i < MAX_CURVE_COEFF; i++)
    {
        sResult.concat(ToString(sConfig.afCoeff[i]));
        sResult.concat(",");
    }
    // Add curveType to the end
    sResult += String(sConfig.iCurveType);
    return sResult;
}

// ----------------------------------------------------------------------------

String FOSConfig::Array2String(SuFloatArray afConfig)
{
    String sResult = "";
    for (int i=0; i < afConfig.Count; i++)
        {
        sResult.concat(ToString(afConfig.Items[i]));
        if (i < afConfig.Count-1)
            sResult.concat(",");
        }
    return sResult;
}

// ----------------------------------------------------------------------------

String FOSConfig::Array2String(SuIntArray aiConfig)
{
    String sResult = "";
    for (int i=0; i < aiConfig.Count; i++)
    {
        sResult.concat(aiConfig.Items[i]);
        if (i < aiConfig.Count-1)
            sResult.concat(",");
    }
    return sResult;
}

#endif
