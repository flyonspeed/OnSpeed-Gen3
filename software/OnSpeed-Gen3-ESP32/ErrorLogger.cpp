
#include "Globals.h"
#include "ErrorLogger.h"

// ----------------------------------------------------------------------------

MsgLog::MsgLog()
    {

    asuModule[EnMain]       = { EnWarning, "Main"       };
    asuModule[EnAHRS]       = { EnWarning, "AHRS"       };
    asuModule[EnAudio]      = { EnWarning, "Audio"      };
    asuModule[EnBoom]       = { EnWarning, "Boom"       };
    asuModule[EnConfig]     = { EnWarning, "Config"     };
    asuModule[EnWebServer]  = { EnWarning, "WebServer"  };
    asuModule[EnDataServer] = { EnWarning, "DataServer" };
    asuModule[EnDisplay]    = { EnWarning, "Display"    };
    asuModule[EnEfis]       = { EnWarning, "EFIS"       };
    asuModule[EnPressure]   = { EnWarning, "Pressure"   };
    asuModule[EnIMU]        = { EnWarning, "IMU"        };
    asuModule[EnReplay]     = { EnWarning, "Replay"     };
    asuModule[EnDisk]       = { EnWarning, "Disk"       };
    asuModule[EnSensors]    = { EnWarning, "Sensors"    };
    asuModule[EnSwitch]     = { EnWarning, "Switch"     };
    asuModule[EnVolume]     = { EnWarning, "Volume"     };
    asuModule[EnVN300]      = { EnWarning, "VN300"      };

    }

// ----------------------------------------------------------------------------

void MsgLog::Set(EnModule enModule, EnLevel enLevel)
    {
    asuModule[enModule].enLevel = enLevel;
    }

// ----------------------------------------------------------------------------

// Set log level by module description string

bool MsgLog::Set(const char * szModule, EnLevel enLevel)
    {
    bool    bStatus = false;

    for (int iModIdx = 0; iModIdx < ModuleCount; iModIdx++)
        if (strcasecmp(szModule, asuModule[iModIdx].szDescription) == 0)
            {
            asuModule[iModIdx].enLevel = enLevel;
            bStatus = true;
            break;
            }

    return bStatus;
    }

// ----------------------------------------------------------------------------

// Return true if logging is equal to or greater than for this module

bool MsgLog::Test(EnModule enModule, EnLevel enLevel)
    {
    return enLevel >= asuModule[enModule].enLevel;
    }

// ----------------------------------------------------------------------------

char * MsgLog::szLevelName(EnLevel enLevel)
    {
    if      (enLevel == EnDebug)    return "DEBUG";
    else if (enLevel == EnWarning)  return "WARNING";
    else if (enLevel == EnError)    return "ERROR";
    else                            return "UNKNOWN";
    }

// ----------------------------------------------------------------------------

void MsgLog::print(EnModule enModule, EnLevel enLevel, const char * szLogMsg)
    {
    if (enLevel >= asuModule[enModule].enLevel)
        {
        if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100)))
            {
            if      (enLevel == EnError)   Serial.print("ERROR   ");
            else if (enLevel == EnWarning) Serial.print("WARNING ");
            else if (enLevel == EnDebug)   Serial.print("DEBUG   ");
            else                           Serial.print("        ");
            Serial.printf("%s - ", asuModule[enModule].szDescription);
            Serial.print(szLogMsg);

            xSemaphoreGive(xSerialLogMutex);
            }
        }
    }

// ----------------------------------------------------------------------------

void MsgLog::println(EnModule enModule, EnLevel enLevel, const char * szLogMsg)
    {
    if (enLevel >= asuModule[enModule].enLevel)
        {
        if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100)))
            {
            if      (enLevel == EnError)   Serial.print("ERROR   ");
            else if (enLevel == EnWarning) Serial.print("WARNING ");
            else if (enLevel == EnDebug)   Serial.print("DEBUG   ");
            else                           Serial.print("        ");
            Serial.printf("%s - ", asuModule[enModule].szDescription);
            Serial.println(szLogMsg);

            xSemaphoreGive(xSerialLogMutex);
            }
        }
    }

// ----------------------------------------------------------------------------

void MsgLog::printf(EnModule enModule, EnLevel enLevel, const char * szFmt, ...)
    {
    if (enLevel >= asuModule[enModule].enLevel)
        {
        if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100)))
            {
            va_list args;
            va_start(args, szFmt);

            if      (enLevel == EnError)   Serial.print("ERROR   ");
            else if (enLevel == EnWarning) Serial.print("WARNING ");
            else if (enLevel == EnDebug)   Serial.print("DEBUG   ");
            else                           Serial.print("        ");
            Serial.printf("%s - ", asuModule[enModule].szDescription);
            Serial.vprintf(szFmt, args);

            va_end(args);

            xSemaphoreGive(xSerialLogMutex);
            }
        }
    }

// ----------------------------------------------------------------------------

// These routines always print to the console. Use these instead of the regular
// serial print routines because they need to be wrapped in a semaphore so
// they don't interfer with error logging routines above.

size_t  MsgLog::print(const char * szLogMsg)
    {
    size_t  iChars;

    if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100)))
        {
        iChars = Serial.print(szLogMsg);
        xSemaphoreGive(xSerialLogMutex);
        }
    return iChars;
    }

// ----------------------------------------------------------------------------

size_t  MsgLog::println(const char * szLogMsg)
    {
    size_t  iChars;

    if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100)))
        {
        iChars = Serial.println(szLogMsg);
        xSemaphoreGive(xSerialLogMutex);
        }
    return iChars;
    }

// ----------------------------------------------------------------------------

size_t  MsgLog::printf(const char * szFmt, ...)
    {
    size_t  iChars;

    if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100)))
        {
        va_list args;
        va_start(args, szFmt);
        iChars = Serial.vprintf(szFmt, args);
        va_end(args);
        xSemaphoreGive(xSerialLogMutex);
        }
    return iChars;
    }

// ----------------------------------------------------------------------------

void MsgLog::flush()
    {
    if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100)))
        {
        Serial.flush();
        xSemaphoreGive(xSerialLogMutex);
        }
    }
