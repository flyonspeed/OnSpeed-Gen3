// ConfigFunctions

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <vector>

// #include "Globals.h"

#define SUPPORT_CONFIG_V1

#define MAX_AOA_CURVES    5 // maximum number of AOA curves (flap/gear positions)
#define MAX_CURVE_COEFF   4 // 4 coefficients=3rd degree polynomial function for calibration curves

    // Data Structures
    // ---------------

    // These are mostly config item definitions but they are used in some other places
    // so they are defined outside of th config class.

#ifdef SUPPORT_CONFIG_V1
    typedef struct  {
      int Count;
      int Items[MAX_AOA_CURVES];
    } SuIntArray;

    typedef struct  {
      int       Count;
      float     Items[MAX_AOA_CURVES];
    } SuFloatArray;
#endif

    typedef struct  {
      float     afCoeff[MAX_CURVE_COEFF]; // 3rd degree polynomial
      uint8_t   iCurveType; // 1 - polynomial, 2 - logarithmic (linear is 1 degree polynomial), 3 - exponential
    } SuCalibrationCurve;


// ============================================================================

struct SuDataSource
{
    enum EnDataSource
    {
        EnSensors,
        EnReplay,
        EnTestPot,
        EnRangeSweep,
        EnUnknown
    } enSrc;

    const char * toCStr()
        {
        return toCStr(enSrc);
        }

    const char * toCStr(EnDataSource enDataSource)
        {
        if      (enDataSource == EnDataSource::EnSensors)    return "SENSORS";
        else if (enDataSource == EnDataSource::EnReplay)     return "REPLAYLOGFILE";
        else if (enDataSource == EnDataSource::EnTestPot)    return "TESTPOT";
        else if (enDataSource == EnDataSource::EnRangeSweep) return "RANGESWEEP";
        else if (enDataSource == EnDataSource::EnUnknown)    return "UNKNOWN";
        else                                                 return "UNKNOWN";
        }

    void fromStrSet(String  sDataSource)
        {
        enSrc = fromStr(sDataSource);
        }

     EnDataSource fromStr(String  sDataSource)
        {
        if      (sDataSource == "SENSORS")        return EnDataSource::EnSensors;
        else if (sDataSource == "REPLAYLOGFILE")  return EnDataSource::EnReplay;
        else if (sDataSource == "TESTPOT")        return EnDataSource::EnTestPot;
        else if (sDataSource == "RANGESWEEP")     return EnDataSource::EnRangeSweep;
        else                                      return EnDataSource::EnUnknown;
        }
};


// ============================================================================

class FOSConfig
{
public:
    FOSConfig();

    // Config data
    // -----------
public:
    // These are the config items that are saved to persistent memory and/or disk
    int             iAoaSmoothing;
    int             iPressureSmoothing;
    int             iMuteAudioUnderIAS;
    SuDataSource    suDataSrc;
    String          sReplayLogFileName;

    struct SuFlaps
    {
        SuFlaps()
            {
            iDegrees        = 0;
            iPotPosition    = 0;
            fLDMAXAOA       = 0.0;
            fONSPEEDFASTAOA = 0.0;
            fONSPEEDSLOWAOA = 0.0;
            fSTALLWARNAOA   = 0.0;
            fSTALLAOA       = 0.0;
            fMANAOA         = 0.0;
            }
        int      iDegrees;
        int      iPotPosition;
        float    fLDMAXAOA;
        float    fONSPEEDFASTAOA;
        float    fONSPEEDSLOWAOA;
        float    fSTALLWARNAOA;
        float    fSTALLAOA;
        float    fMANAOA;

        SuCalibrationCurve  AoaCurve;
    };

    // A resizable array of flap position related values.
    std::vector<SuFlaps>    aFlaps;

    // Volume
    bool            bVolumeControl;
    int             iVolumeHighAnalog;
    int             iVolumeLowAnalog;
    int             iDefaultVolume;     // Percent from 0 to 100
//  int             iVolumePercent;     // Percent from 0 to 100
    bool            bAudio3D;           // 3D audio enabled
    bool            bOverGWarning;

    // CAS curve
    SuCalibrationCurve  CasCurve;         // calibrated airspeed curve (polynomial)
    bool                bCasCurveEnabled;

    // Box orientation
    String          sPortsOrientation;
    String          sBoxtopOrientation;
    String          sEfisType;

    // calibration data source
    String          sCalSource;

    // biases
    int             iPFwdBias;      // Counts
    int             iP45Bias;       // Counts
    float           fPStaticBias;   // millibars
    float           fGxBias;
    float           fGyBias;
    float           fGzBias;
    float           fPitchBias;
    float           fRollBias;

    // serial inputs
    bool            bReadBoom;
    bool            bReadEfisData;

    // serial output
    String          sSerialOutFormat;
//    String          sSerialOutPort;

    // load limit
    float           fLoadLimitPositive;
    float           fLoadLimitNegative;

    // vno chime
    int             iVno;                 // aircraft Vno in kts;
    unsigned        uVnoChimeInterval;    // chime interval in seconds
    bool            bVnoChimeEnabled;

    // SD card logging
//    bool            bSdLoggingConfig;
    bool            bSdLogging;

    // Other config data
    char            szDefaultConfigFilename[14] = "onspeed2.cfg";
    bool            bConfigLoaded;

  // Methods
  // -------
public:
    bool                LoadConfigurationFile(char* szFilename);
    bool                SaveConfigurationToFile();
    bool                SaveConfigurationToFile(char* szFilename);

#ifdef SUPPORT_LITTLEFS
    bool                LoadConfigurationFileFromFlash(char* szFilename);
    bool                SaveConfigurationToFlash();
    bool                SaveConfigurationToFlash(char* szFilename);
#endif

    bool                LoadDefaultConfiguration();
    void                LoadConfig();

    bool                ToBoolean(String sBool);
    float               ToFloat(String sFloat);
    String              ToString(float fFloat);
#ifdef SUPPORT_CONFIG_V1
  SuIntArray          ParseIntCSV(String sConfig);
  SuFloatArray        ParseFloatCSV(String sConfig, int limit=MAX_AOA_CURVES);
  SuCalibrationCurve  ParseCurveCSV(String sConfig);
  String              GetConfigValue(String sConfig,String configName);
  String              MakeConfig(String configName, String configValue);
  String              Curve2String(SuCalibrationCurve  sConfig);
  String              Array2String(SuFloatArray       afConfig);
  String              Array2String(SuIntArray         aiConfig);
#endif

  String              ConfigurationToString();
  bool                LoadConfigFromString(String sConfig);
  //void                AddCRC(String &sConfig);

  //float               array2float(byte buffer[], int startIndex);
  //long double         array2double(byte buffer[], int startIndex);
  //void                configChecksum(String &sConfig, String &checksumString);

}; // end class FOSConfig
#endif // _CONFIG_H_