
//#include <Arduino.h>

#include "tinyxml2.h"

#include "Globals.h"

#ifdef SUPPORT_LITTLEFS
#include <LittleFS.h>
#endif

#include "Config.h"
#include "EfisSerial.h"

using namespace tinyxml2;


// ============================================================================

FOSConfig::FOSConfig()
    {
    LoadDefaultConfiguration();
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

    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100))) 
        {
        hConfigFile = g_SdFileSys.open(szFilename, O_READ);
        if (hConfigFile) 
            {
            while (hConfigFile.available())
                sConfig += char(hConfigFile.read());                                                                                      

            // Close the file:
            hConfigFile.close();

            xSemaphoreGive(xWriteMutex);

            g_Log.printf(MsgLog::EnConfig, MsgLog::EnDebug, "Read file '%s' from SD card\n", szFilename);

            bStatus = LoadConfigFromString(sConfig);                
            } // end if config file open OK

        else 
            g_Log.printf(MsgLog::EnConfig, MsgLog::EnDebug, "Error reading %s from SD card", szFilename);

        xSemaphoreGive(xWriteMutex);
        } // end if semaphore OK

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

// Load default configuration

bool FOSConfig::LoadDefaultConfiguration()
{
    // ALL config items should be initialized to reasonable values here.

    iAoaSmoothing       = 20;
    iPressureSmoothing  = 15;
    iMuteAudioUnderIAS  = 30;

    suDataSrc.enSrc     = SuDataSource::EnSensors;
    //suDataSrc.enSrc     = SuDataSource::EnTestPot;
    //suDataSrc.enSrc     = SuDataSource::EnRangeSweep;
    //suDataSrc.enSrc     = SuDataSource::EnReplay;

    sReplayLogFileName  = "";

    // Flap positions
    // Note that default flap values are set in the constructor in the header file
    aFlaps.clear();
    SuFlaps             suFlaps;

    suFlaps.AoaCurve.iCurveType = 1;    // Default to polynomial curve
#if 0
    for (int iCoeffIdx = 0; iCoeffIdx < MAX_CURVE_COEFF; iCoeffIdx++)
        suFlaps.AoaCurve.afCoeff[iCoeffIdx] = 0.0;
#else
        // These approximate values give reasonable results in Vac's plane
        suFlaps.AoaCurve.afCoeff[0] =  0.0;  // x^3
        suFlaps.AoaCurve.afCoeff[1] =  8.0;  // x^2
        suFlaps.AoaCurve.afCoeff[2] = 24.0;  // x^1
        suFlaps.AoaCurve.afCoeff[3] =  4.5;  // x^0
#endif
    suFlaps.fLDMAXAOA       =  8.0;
    suFlaps.fONSPEEDFASTAOA = 11.0;
    suFlaps.fONSPEEDSLOWAOA = 14.0;
    suFlaps.fSTALLWARNAOA   = 16.0;
    aFlaps.push_back(suFlaps);

    // Volume
    bVolumeControl      = false;
    iVolumeHighAnalog   = 4095;
    iVolumeLowAnalog    =    0;
    iDefaultVolume      =  100;
//    iVolumePercent      =   25;
    
    bAudio3D            = false;
    bOverGWarning       = false;

    // CAS curve
    CasCurve.iCurveType = 1;
    CasCurve.afCoeff[0] = 0.0;  // x^3
    CasCurve.afCoeff[1] = 0.0;  // x^2
    CasCurve.afCoeff[2] = 1.0;  // x^1
    CasCurve.afCoeff[3] = 0.0;  // x^0
    bCasCurveEnabled    = false;

    sPortsOrientation   = "FORWARD";
    sBoxtopOrientation  = "UP";

    // Calibration data source
    sCalSource          = "ONSPEED";

    // Biases
    iPFwdBias           = 2048;
    iP45Bias            = 2048;
    fPStaticBias        = 0.0;
    fGxBias             = 0.0;
    fGyBias             = 0.0;
    fGzBias             = 0.0;
    fPitchBias          = 0.0;
    fRollBias           = 0.0;

    // Serial inputs
    bReadBoom           = false;
    bReadEfisData       = false;
    sEfisType           = "VN-300";

    // serial output
    sSerialOutFormat    = "ONSPEED";

    // load limit
    fLoadLimitPositive  =  4.0;
    fLoadLimitNegative  = -2.0;

    // vno chime
    iVno                = 150;
    uVnoChimeInterval   = 3;
    bVnoChimeEnabled    = false;

    // SD card logging
    bSdLogging          = false;

    return true;
}

// ----------------------------------------------------------------------------
// To / From string conversion functions
// ----------------------------------------------------------------------------

/*
You will note that in several places when polynomial coefficients are stored
it will seem kind of bass-ackwards. That is because in the data array that
holds the coefficients the highest array index holds the lowest power
coefficient. But in the XML I *really* want <X0> to correspond to the coefficient
for the X^0 term, <X1> to correspond to the coefficient for X^1 term, etc.
Someday I may go refactor the code to make the array indexes match the XML
config tags, but the user never sees these so I defer that for now.
*/

#define XML_INSERT(root, name)                          \
    XmlConfigNew = root->InsertNewChildElement(name);

#define XML_INSERT_SET(root, name, value)               \
    XmlConfigNew = root->InsertNewChildElement(name);   \
    XmlConfigNew->SetText(value);

String FOSConfig::ConfigurationToString()
{
    XMLPrinter      XmlPrint;
    XMLDocument     XmlConfigDoc;
    XMLElement    * XmlConfigRoot;
    XMLElement    * XmlConfigNew;
    String          sConfig = "";

    sConfig = "";

    XmlConfigRoot = XmlConfigDoc.NewElement("CONFIG2");
    XmlConfigDoc.InsertEndChild(XmlConfigRoot);

    XML_INSERT_SET(XmlConfigRoot, "AOA_SMOOTHING", iAoaSmoothing)
    XML_INSERT_SET(XmlConfigRoot, "PRESSURE_SMOOTHING", iPressureSmoothing)
    XML_INSERT_SET(XmlConfigRoot, "DATASOURCE", suDataSrc.toCStr())
    XML_INSERT_SET(XmlConfigRoot, "REPLAYLOGFILENAME", sReplayLogFileName.c_str())

    for (int iFlapIdx = 0; iFlapIdx < aFlaps.size(); iFlapIdx++)
        {
        XML_INSERT(XmlConfigRoot, "FLAP_POSITION")
        XMLElement * XmlConfigFlaps = XmlConfigNew;
        XML_INSERT_SET(XmlConfigFlaps, "DEGREES",        aFlaps[iFlapIdx].iDegrees)
        XML_INSERT_SET(XmlConfigFlaps, "POT_VALUE",      aFlaps[iFlapIdx].iPotPosition)
        XML_INSERT_SET(XmlConfigFlaps, "LDMAXAOA",       aFlaps[iFlapIdx].fLDMAXAOA)
        XML_INSERT_SET(XmlConfigFlaps, "ONSPEEDFASTAOA", aFlaps[iFlapIdx].fONSPEEDFASTAOA)
        XML_INSERT_SET(XmlConfigFlaps, "ONSPEEDSLOWAOA", aFlaps[iFlapIdx].fONSPEEDSLOWAOA)
        XML_INSERT_SET(XmlConfigFlaps, "STALLWARNAOA",   aFlaps[iFlapIdx].fSTALLWARNAOA)
        XML_INSERT_SET(XmlConfigFlaps, "STALLAOA",       aFlaps[iFlapIdx].fSTALLAOA)
        XML_INSERT_SET(XmlConfigFlaps, "MANAOA",         aFlaps[iFlapIdx].fMANAOA)

        XML_INSERT(XmlConfigFlaps, "AOA_CURVE")
        XMLElement * XmlConfigAoACurve = XmlConfigNew;
        XML_INSERT_SET(XmlConfigAoACurve, "TYPE", aFlaps[iFlapIdx].AoaCurve.iCurveType)
        XML_INSERT_SET(XmlConfigAoACurve, "X3",   aFlaps[iFlapIdx].AoaCurve.afCoeff[0])
        XML_INSERT_SET(XmlConfigAoACurve, "X2",   aFlaps[iFlapIdx].AoaCurve.afCoeff[1])
        XML_INSERT_SET(XmlConfigAoACurve, "X1",   aFlaps[iFlapIdx].AoaCurve.afCoeff[2])
        XML_INSERT_SET(XmlConfigAoACurve, "X0",   aFlaps[iFlapIdx].AoaCurve.afCoeff[3])
        }

    XML_INSERT(XmlConfigRoot, "VOLUME")
    XMLElement * XmlConfigVolume = XmlConfigNew;
    XML_INSERT_SET(XmlConfigVolume, "ENABLED",        bVolumeControl)
    XML_INSERT_SET(XmlConfigVolume, "HIGH_ANALOG",    iVolumeHighAnalog)
    XML_INSERT_SET(XmlConfigVolume, "LOW_ANALOG",     iVolumeLowAnalog)
    XML_INSERT_SET(XmlConfigVolume, "DEFAULT",        iDefaultVolume)
    XML_INSERT_SET(XmlConfigVolume, "ENABLE_3DAUDIO", bAudio3D)
    XML_INSERT_SET(XmlConfigVolume, "MUTE_UNDER_IAS", iMuteAudioUnderIAS)

    XML_INSERT_SET(XmlConfigRoot, "OVERGWARNING", bOverGWarning)

    XML_INSERT(XmlConfigRoot, "CAS_CURVE")
    XMLElement * XmlConfigCasCurve = XmlConfigNew;
    XML_INSERT_SET(XmlConfigCasCurve, "TYPE", CasCurve.iCurveType)
    XML_INSERT_SET(XmlConfigCasCurve, "X3",   CasCurve.afCoeff[0])
    XML_INSERT_SET(XmlConfigCasCurve, "X2",   CasCurve.afCoeff[1])
    XML_INSERT_SET(XmlConfigCasCurve, "X1",   CasCurve.afCoeff[2])
    XML_INSERT_SET(XmlConfigCasCurve, "X0",   CasCurve.afCoeff[3])
    XML_INSERT_SET(XmlConfigCasCurve, "ENABLED", bCasCurveEnabled)

    XML_INSERT(XmlConfigRoot, "ORIENTATION")
    XMLElement * XmlConfigOrientation = XmlConfigNew;
    XML_INSERT_SET(XmlConfigOrientation, "PORTS",   sPortsOrientation.c_str())
    XML_INSERT_SET(XmlConfigOrientation, "BOX_TOP", sBoxtopOrientation.c_str())

    XML_INSERT_SET(XmlConfigRoot, "BOOM",           bReadBoom)
    XML_INSERT_SET(XmlConfigRoot, "SERIALEFISDATA", bReadEfisData)
    XML_INSERT_SET(XmlConfigRoot, "EFISTYPE",       sEfisType.c_str())

    XML_INSERT_SET(XmlConfigRoot, "SERIALOUTFORMAT", sSerialOutFormat.c_str())
//    XML_INSERT_SET(XmlConfigRoot, "SERIALOUTPORT",   sSerialOutPort.c_str())

    XML_INSERT_SET(XmlConfigRoot, "CALWIZ_SOURCE", sCalSource.c_str())

    XML_INSERT(XmlConfigRoot, "BIAS")
    XMLElement * XmlConfigBias = XmlConfigNew;
    XML_INSERT_SET(XmlConfigBias, "PFWD",    iPFwdBias)
    XML_INSERT_SET(XmlConfigBias, "P45",     iP45Bias)
    XML_INSERT_SET(XmlConfigBias, "PSTATIC", fPStaticBias)
    XML_INSERT_SET(XmlConfigBias, "GX",      fGxBias)
    XML_INSERT_SET(XmlConfigBias, "GY",      fGyBias)
    XML_INSERT_SET(XmlConfigBias, "GZ",      fGzBias)
    XML_INSERT_SET(XmlConfigBias, "PITCH",   fPitchBias)
    XML_INSERT_SET(XmlConfigBias, "ROLL",    fRollBias)

    XML_INSERT(XmlConfigRoot, "LOAD_LIMIT")
    XMLElement * XmlConfigLoadLimit = XmlConfigNew;
    XML_INSERT_SET(XmlConfigLoadLimit, "POSITIVE", fLoadLimitPositive)
    XML_INSERT_SET(XmlConfigLoadLimit, "NEGATIVE", fLoadLimitNegative)

    XML_INSERT(XmlConfigRoot, "VNO")
    XMLElement * XmlConfigVNO = XmlConfigNew;
    XML_INSERT_SET(XmlConfigVNO, "SPEED",          iVno)
    XML_INSERT_SET(XmlConfigVNO, "CHIME_INTERVAL", uVnoChimeInterval)
    XML_INSERT_SET(XmlConfigVNO, "CHIME_ENABLED",  bVnoChimeEnabled)

    XML_INSERT_SET(XmlConfigRoot, "SDLOGGING", bSdLogging)

    XmlConfigDoc.Print(&XmlPrint);
    sConfig = XmlPrint.CStr();

    return sConfig;
}


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
        suDataSrc.fromStrSet(sDataSource);

        sReplayLogFileName  = GetConfigValue(sConfig,"REPLAYLOGFILENAME");

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

        afValues = ParseFloatCSV(GetConfigValue(sConfig,"SETPOINT_STALLAOA"),iFlapsArraySize); // STALLAOA is only availabel after calibration wizard run
        for (iIdx=0; (iIdx<aiValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].fSTALLAOA = afValues.Items[iIdx];

//        afValues = ParseFloatCSV(GetConfigValue(sConfig,"SETPOINT_MANAOA"),iFlapsArraySize); // MANAOA is only availabel after calibration wizard run
//        for (iIdx=0; (iIdx<aiValues.Count) && (iIdx<iFlapsArraySize); iIdx++) aFlaps[iIdx].fMANAOA = afValues.Items[iIdx];

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
        if (!bVolumeControl)
            g_AudioPlay.SetVolume(iDefaultVolume);

        bAudio3D            = ToBoolean(GetConfigValue(sConfig,"3DAUDIO"));
        bOverGWarning       = ToBoolean(GetConfigValue(sConfig,"OVERGWARNING"));

        //CAS curve
        CasCurve            = ParseCurveCSV(GetConfigValue(sConfig,"CAS_CURVE"));
        bCasCurveEnabled    = ToBoolean(GetConfigValue(sConfig,"CAS_ENABLED"));

        sPortsOrientation   = GetConfigValue(sConfig,"PORTS_ORIENTATION");
        sBoxtopOrientation  = GetConfigValue(sConfig,"BOX_TOP_ORIENTATION");
        sEfisType           = GetConfigValue(sConfig,"EFISTYPE");
        if      (sEfisType=="VN-300")    g_EfisSerial.enType = EfisSerialIO::EnVN300;        // iEfisID = 1; 
        else if (sEfisType=="ADVANCED")  g_EfisSerial.enType = EfisSerialIO::EnDynonSkyview; // iEfisID = 2; 
        else if (sEfisType=="DYNOND10")  g_EfisSerial.enType = EfisSerialIO::EnDynonD10;     // iEfisID = 3; 
        else if (sEfisType=="GARMING5")  g_EfisSerial.enType = EfisSerialIO::EnGarminG5;     // iEfisID = 4; 
        else if (sEfisType=="GARMING3X") g_EfisSerial.enType = EfisSerialIO::EnGarminG3X;    // iEfisID = 5; 
        else if (sEfisType=="MGL")       g_EfisSerial.enType = EfisSerialIO::EnMglBinary;    // iEfisID = 6; 
        else                             g_EfisSerial.enType = EfisSerialIO::EnNone;         // iEfisID = 0;

        // Calibration data source
        sCalSource           = GetConfigValue(sConfig,"CALWIZ_SOURCE");

        // Biases
        iPFwdBias           =  GetConfigValue(sConfig,"PFWD_BIAS").toInt();
        iP45Bias            =  GetConfigValue(sConfig,"P45_BIAS").toInt();
        fPStaticBias        = -ToFloat(GetConfigValue(sConfig,"PSTATIC_BIAS"));
        fGxBias             =  ToFloat(GetConfigValue(sConfig,"GX_BIAS"));
        fGyBias             =  ToFloat(GetConfigValue(sConfig,"GY_BIAS"));
        fGzBias             =  ToFloat(GetConfigValue(sConfig,"GZ_BIAS"));
        fPitchBias          =  ToFloat(GetConfigValue(sConfig,"PITCH_BIAS"));
        fRollBias           =  ToFloat(GetConfigValue(sConfig,"ROLL_BIAS"));

        // serial inputs
        bReadBoom           = ToBoolean(GetConfigValue(sConfig,"BOOM"));
        bReadEfisData       = ToBoolean(GetConfigValue(sConfig,"SERIALEFISDATA"));

        // serial output
        sSerialOutFormat    = GetConfigValue(sConfig,"SERIALOUTFORMAT");
//        sSerialOutPort      = GetConfigValue(sConfig,"SERIALOUTPORT");

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

        return true;
#else
        return false;
#endif
        }

    // CONFIG2 format
    // --------------

#define XML_GET_INT(root, name, value)                          \
    {                                                           \
    int          iTemp;                                         \
    XMLElement * XmlElement = root->FirstChildElement(name);    \
    if ((XmlElement                       != nullptr) &&        \
        (XmlElement->QueryIntText(&iTemp) == XML_SUCCESS))      \
        value = iTemp;                                          \
    }

#define XML_GET_FLOAT(root, name, value)                        \
    {                                                           \
    float        fTemp;                                         \
    XMLElement * XmlElement = root->FirstChildElement(name);    \
    if ((XmlElement                         != nullptr) &&      \
        (XmlElement->QueryFloatText(&fTemp) == XML_SUCCESS))    \
        value = fTemp;                                          \
    }

#define XML_GET_BOOL(root, name, value)                         \
    {                                                           \
    bool         bTemp;                                         \
    XMLElement * XmlElement = root->FirstChildElement(name);    \
    if ((XmlElement                        != nullptr) &&       \
        (XmlElement->QueryBoolText(&bTemp) == XML_SUCCESS))     \
        value = bTemp;                                          \
    }

#define XML_GET_STR(root, name, value)                          \
    {                                                           \
    const char * szTemp;                                        \
    XMLElement * XmlElement = root->FirstChildElement(name);    \
    if ((XmlElement                       != nullptr) &&        \
        ((szTemp = XmlElement->GetText()) != nullptr))          \
        value = szTemp;                                         \
    }

    else if (sConfig.indexOf("<CONFIG2>" ) >= 0 && sConfig.indexOf("</CONFIG2>" ) >= 0)
        {
        XMLDocument     XmlDoc;
        XMLNode       * XmlRootNode;
        String          sDataSource;

        if(XmlDoc.Parse(sConfig.c_str()) != XML_SUCCESS)
            return false; 

        XmlRootNode = XmlDoc.FirstChild();
        String     sConfigRoot = XmlRootNode->Value();

        XML_GET_INT(XmlRootNode, "AOA_SMOOTHING",      iAoaSmoothing)
        XML_GET_INT(XmlRootNode, "PRESSURE_SMOOTHING", iPressureSmoothing)

        XML_GET_STR(XmlRootNode, "DATASOURCE",         sDataSource)
        suDataSrc.fromStrSet(sDataSource);

        XML_GET_STR(XmlRootNode, "REPLAYLOGFILENAME", sReplayLogFileName)

        int          iFlapIdx  = -1;
        XMLElement * pXmlFlaps = XmlRootNode->FirstChildElement("FLAP_POSITION");

        // If there is flaps info in the config string then clear out any previous
        // flaps information.
        if (pXmlFlaps != NULL)
            aFlaps.clear();

        while (pXmlFlaps != NULL)
            {
            iFlapIdx++;

            SuFlaps     suFlaps;

            XML_GET_INT  (pXmlFlaps, "DEGREES",        suFlaps.iDegrees)
            XML_GET_INT  (pXmlFlaps, "POT_VALUE",      suFlaps.iPotPosition)
            XML_GET_FLOAT(pXmlFlaps, "LDMAXAOA",       suFlaps.fLDMAXAOA)
            XML_GET_FLOAT(pXmlFlaps, "ONSPEEDFASTAOA", suFlaps.fONSPEEDFASTAOA)
            XML_GET_FLOAT(pXmlFlaps, "ONSPEEDSLOWAOA", suFlaps.fONSPEEDSLOWAOA)
            XML_GET_FLOAT(pXmlFlaps, "STALLWARNAOA",   suFlaps.fSTALLWARNAOA)
            XML_GET_FLOAT(pXmlFlaps, "STALLAOA",       suFlaps.fSTALLAOA)
            XML_GET_FLOAT(pXmlFlaps, "MANAOA",         suFlaps.fMANAOA)

            XMLElement * pXmlAoaCurve = pXmlFlaps->FirstChildElement("AOA_CURVE");
            if (pXmlAoaCurve != NULL)
                {
                XML_GET_INT  (pXmlAoaCurve, "TYPE",       suFlaps.AoaCurve.iCurveType)
                XML_GET_FLOAT(pXmlAoaCurve, "X3",         suFlaps.AoaCurve.afCoeff[0])
                XML_GET_FLOAT(pXmlAoaCurve, "X2",         suFlaps.AoaCurve.afCoeff[1])
                XML_GET_FLOAT(pXmlAoaCurve, "X1",         suFlaps.AoaCurve.afCoeff[2])
                XML_GET_FLOAT(pXmlAoaCurve, "X0",         suFlaps.AoaCurve.afCoeff[3])
                }

            // Save the new values to the flaps data array
            if (iFlapIdx < aFlaps.size())   aFlaps[iFlapIdx] = suFlaps;
            else                            aFlaps.push_back(suFlaps);

            pXmlFlaps = pXmlFlaps->NextSiblingElement("FLAP_POSITION");
            } // end for each FLAP_POSITION section

        // This is in case the new set of flaps data is smaller
        aFlaps.resize(iFlapIdx+1);

        // Sort flaps data array by flap degrees
        std::sort(aFlaps.begin(), aFlaps.end(), 
                [](SuFlaps a, SuFlaps b) { return a.iDegrees < b.iDegrees; } );

        XMLElement * pXmlVolume = XmlRootNode->FirstChildElement("VOLUME");
        if (pXmlVolume != NULL)
            {
            XML_GET_BOOL (pXmlVolume, "ENABLED",        bVolumeControl)
            XML_GET_INT  (pXmlVolume, "HIGH_ANALOG",    iVolumeHighAnalog)
            XML_GET_INT  (pXmlVolume, "LOW_ANALOG",     iVolumeLowAnalog)
            XML_GET_INT  (pXmlVolume, "DEFAULT",        iDefaultVolume)
            XML_GET_BOOL (pXmlVolume, "ENABLE_3DAUDIO", bAudio3D)
            XML_GET_INT  (pXmlVolume, "MUTE_UNDER_IAS", iMuteAudioUnderIAS)
            }
        if (!bVolumeControl)
            g_AudioPlay.SetVolume(iDefaultVolume);

        XML_GET_BOOL (XmlRootNode, "OVERGWARNING", bOverGWarning)

        XMLElement * pXmlCasCurve = XmlRootNode->FirstChildElement("CAS_CURVE");
        if (pXmlCasCurve != NULL)
            {
            XML_GET_INT  (pXmlCasCurve, "TYPE",       CasCurve.iCurveType)
            XML_GET_FLOAT(pXmlCasCurve, "X3",         CasCurve.afCoeff[0])
            XML_GET_FLOAT(pXmlCasCurve, "X2",         CasCurve.afCoeff[1])
            XML_GET_FLOAT(pXmlCasCurve, "X1",         CasCurve.afCoeff[2])
            XML_GET_FLOAT(pXmlCasCurve, "X0",         CasCurve.afCoeff[3])
            XML_GET_BOOL (pXmlCasCurve, "ENABLED",    bCasCurveEnabled)
            }

        XMLElement * pXmlOrientation = XmlRootNode->FirstChildElement("ORIENTATION");
        if (pXmlOrientation != NULL)
            {
            XML_GET_STR(pXmlOrientation, "PORTS",     sPortsOrientation)
            XML_GET_STR(pXmlOrientation, "BOX_TOP",   sBoxtopOrientation)
            }

        // Serial inputs
        XML_GET_BOOL(XmlRootNode, "BOOM",             bReadBoom)
        XML_GET_BOOL(XmlRootNode, "SERIALEFISDATA",   bReadEfisData)
        XML_GET_STR(XmlRootNode,  "EFISTYPE",         sEfisType)
        if      (sEfisType=="VN-300")    g_EfisSerial.enType = EfisSerialIO::EnVN300;        // iEfisID = 1; 
        else if (sEfisType=="ADVANCED")  g_EfisSerial.enType = EfisSerialIO::EnDynonSkyview; // iEfisID = 2; 
        else if (sEfisType=="DYNOND10")  g_EfisSerial.enType = EfisSerialIO::EnDynonD10;     // iEfisID = 3; 
        else if (sEfisType=="GARMING5")  g_EfisSerial.enType = EfisSerialIO::EnGarminG5;     // iEfisID = 4; 
        else if (sEfisType=="GARMING3X") g_EfisSerial.enType = EfisSerialIO::EnGarminG3X;    // iEfisID = 5; 
        else if (sEfisType=="MGL")       g_EfisSerial.enType = EfisSerialIO::EnMglBinary;    // iEfisID = 6; 
        else                             g_EfisSerial.enType = EfisSerialIO::EnNone;         // iEfisID = 0;

        // Serial output
        XML_GET_STR(XmlRootNode, "SERIALOUTFORMAT",   sSerialOutFormat)
//        XML_GET_STR(XmlRootNode, "SERIALOUTPORT",     sSerialOutPort)

        XML_GET_STR(XmlRootNode, "CALWIZ_SOURCE",         sCalSource)

        XMLElement * pXmlBias = XmlRootNode->FirstChildElement("BIAS");
        if (pXmlBias != NULL)
            {
            XML_GET_INT  (pXmlBias, "PFWD",     iPFwdBias)
            XML_GET_INT  (pXmlBias, "PFWD",     iP45Bias)
            XML_GET_FLOAT(pXmlBias, "PSTATIC",  fPStaticBias)
            XML_GET_FLOAT(pXmlBias, "GX",       fGxBias)
            XML_GET_FLOAT(pXmlBias, "GY",       fGyBias)
            XML_GET_FLOAT(pXmlBias, "GZ",       fGzBias)
            XML_GET_FLOAT(pXmlBias, "PITCH",    fPitchBias)
            XML_GET_FLOAT(pXmlBias, "ROLL",     fRollBias)
            }

        XMLElement * pXmlLoad = XmlRootNode->FirstChildElement("LOAD_LIMIT");
        if (pXmlLoad != NULL)
            {
            XML_GET_FLOAT(pXmlLoad, "POSITIVE",  fLoadLimitPositive)
            XML_GET_FLOAT(pXmlLoad, "NEGATIVE",  fLoadLimitNegative)
            }

        XMLElement * pXmlVno = XmlRootNode->FirstChildElement("VNO");
        if (pXmlVno != NULL)
            {
            XML_GET_INT  (pXmlVno, "SPEED",           iVno)
            XML_GET_INT  (pXmlVno, "CHIME_INTERVAL",  uVnoChimeInterval)
            XML_GET_BOOL (pXmlVno, "CHIME_ENABLED",   bVnoChimeEnabled)
            }

        XML_GET_BOOL(XmlRootNode, "SDLOGGING",        bSdLogging)

        g_Log.println(MsgLog::EnConfig, MsgLog::EnDebug, "Decoded V2 config string");

        return true;
        }

    // Unknown or bad format string
    // ----------------------------
    else
        {
        g_Log.println(MsgLog::EnConfig, MsgLog::EnWarning, "Unknow config string format");
        return false;
        }

    // Configure anything that needs to be configured based on new config settings
    // ---------------------------------------------------------------------------

    // Configure accelerometer axes
    g_pIMU->ConfigAxes();
    g_AHRS.Init(IMU_SAMPLE_RATE);

}


// ----------------------------------------------------------------------------
// Utility functions
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
    sprintf(szFloatBuffer,"%.4f",fFloat);
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

