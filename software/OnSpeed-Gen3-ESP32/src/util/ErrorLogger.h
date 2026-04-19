#pragma once

#include <cstdarg>

//#define SENSORDEBUG                             // show sensor debug
//#define EFISDATADEBUG                           // show efis data debug
//#define BOOMDATADEBUG                           // show boom data debug
//#define TONEDEBUG                               // show tone related debug info
//#define SDCARDDEBUG                             // show SD writing debug info
//#define VOLUMEDEBUG                             // show audio volume info
//#define VNDEBUG                                 // show VN-300 debug info
//#define AXISDEBUG                               // show accelerometer axis configuration
//#define IMUTEMPDEBUG
//#define AGEDEBUG                                // debug data age (boom,efis [ms])

class MsgLog
    {

public:
    MsgLog();

public:
    // Logging levels
    enum EnLevel { EnDebug, EnWarning, EnError, EnOff };

    // Modules which can be individually controlled.
    enum EnModule {
        EnMain,
        EnAHRS,
        EnAudio,
        EnBoom,
        EnConfig,
        EnWebServer,
        EnDataServer,
        EnDisplay,
        EnEfis,
        EnPressure,
        EnIMU,
        EnReplay,
        EnDisk,
        EnSensors,
        EnSwitch,
        EnVolume,
        EnVN300,
        ModuleCount // This must be the last element
        };

    struct SuLogInfo {
        EnLevel         enLevel;
        const char    * szDescription;
        } asuModule[ModuleCount];

    // Methods
public:
    void Set(EnModule enModule, EnLevel enLevel);
    bool Set(const char * szModule, EnLevel enLevel);
    bool Test(EnModule enModule, EnLevel enLevel);
    const char * szLevelName(EnLevel enLevel);

    void print(EnModule enModule, EnLevel enLevel, const char * szLogMsg);
    void println(EnModule enModule, EnLevel enLevel, const char * szLogMsg);
    void printf(EnModule enModule, EnLevel enLevel, const char * szFmt, ...);

    size_t  print(const char * szLogMsg);
    size_t  println(const char * szLogMsg);
    size_t  printf(const char * szFmt, ...);

    void    flush();
    };

