
#include "src/Globals.h"

#include <cstdio>

// ----------------------------------------------------------------------------
// Internal helpers
// ----------------------------------------------------------------------------

namespace {

// Max characters for one formatted module+level log line. Larger than
// the typical line length so a vsnprintf truncation rarely happens;
// truncation is harmless (NUL-terminated, output reflects what fit).
constexpr size_t kFormattedMax = 256;

const char *LevelPrefix(MsgLog::EnLevel enLevel)
    {
    switch (enLevel)
        {
        case MsgLog::EnError:   return "ERROR   ";
        case MsgLog::EnWarning: return "WARNING ";
        case MsgLog::EnDebug:   return "DEBUG   ";
        default:                return "        ";
        }
    }

}  // namespace

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

const char * MsgLog::szLevelName(EnLevel enLevel)
    {
    if      (enLevel == EnDebug)    return "DEBUG";
    else if (enLevel == EnWarning)  return "WARNING";
    else if (enLevel == EnError)    return "ERROR";
    else if (enLevel == EnOff)      return "OFF";
    else                            return "UNKNOWN";
    }

// ----------------------------------------------------------------------------

// Build "<level> <module> - <body>[\n]" into a stack buffer, write
// to Serial, and (if the module/level passes the SD threshold)
// forward the same bytes to g_DebugLog. The dual-sink ordering is
// intentional: a slow SD layer can't backpressure the Serial print
// because we format once and the debug push is non-blocking.
void MsgLog::print(EnModule enModule, EnLevel enLevel, const char * szLogMsg)
    {
    if (enLevel < asuModule[enModule].enLevel)
        return;

    char   szBuf[kFormattedMax];
    int    n = snprintf(szBuf, sizeof(szBuf), "%s%s - %s",
                        LevelPrefix(enLevel),
                        asuModule[enModule].szDescription,
                        szLogMsg);
    if (n < 0) return;
    if (static_cast<size_t>(n) >= sizeof(szBuf))
        n = static_cast<int>(sizeof(szBuf) - 1);

    if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100)))
        {
        Serial.write(reinterpret_cast<const uint8_t *>(szBuf),
                     static_cast<size_t>(n));
        if (enLevel >= m_enSdThreshold)
            g_DebugLog.Write(szBuf, static_cast<size_t>(n));
        xSemaphoreGive(xSerialLogMutex);
        }
    }

// ----------------------------------------------------------------------------

void MsgLog::println(EnModule enModule, EnLevel enLevel, const char * szLogMsg)
    {
    if (enLevel < asuModule[enModule].enLevel)
        return;

    char   szBuf[kFormattedMax];
    int    n = snprintf(szBuf, sizeof(szBuf), "%s%s - %s\n",
                        LevelPrefix(enLevel),
                        asuModule[enModule].szDescription,
                        szLogMsg);
    if (n < 0) return;
    if (static_cast<size_t>(n) >= sizeof(szBuf))
        {
        // Truncated: ensure the buffer still ends with '\n' so the
        // .dbg file stays line-delimited even on overlong inputs.
        n = static_cast<int>(sizeof(szBuf) - 1);
        szBuf[n - 1] = '\n';
        }

    if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100)))
        {
        Serial.write(reinterpret_cast<const uint8_t *>(szBuf),
                     static_cast<size_t>(n));
        if (enLevel >= m_enSdThreshold)
            g_DebugLog.Write(szBuf, static_cast<size_t>(n));
        xSemaphoreGive(xSerialLogMutex);
        }
    }

// ----------------------------------------------------------------------------

void MsgLog::printf(EnModule enModule, EnLevel enLevel, const char * szFmt, ...)
    {
    if (enLevel < asuModule[enModule].enLevel)
        return;

    char    szBuf[kFormattedMax];
    int     nHdr = snprintf(szBuf, sizeof(szBuf), "%s%s - ",
                            LevelPrefix(enLevel),
                            asuModule[enModule].szDescription);
    if (nHdr < 0) return;
    if (static_cast<size_t>(nHdr) >= sizeof(szBuf))
        nHdr = static_cast<int>(sizeof(szBuf) - 1);

    va_list args;
    va_start(args, szFmt);
    int nBody = vsnprintf(szBuf + nHdr, sizeof(szBuf) - nHdr, szFmt, args);
    va_end(args);
    if (nBody < 0) nBody = 0;

    int n = nHdr + nBody;
    if (static_cast<size_t>(n) >= sizeof(szBuf))
        n = static_cast<int>(sizeof(szBuf) - 1);

    if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100)))
        {
        Serial.write(reinterpret_cast<const uint8_t *>(szBuf),
                     static_cast<size_t>(n));
        if (enLevel >= m_enSdThreshold)
            g_DebugLog.Write(szBuf, static_cast<size_t>(n));
        xSemaphoreGive(xSerialLogMutex);
        }
    }

// ----------------------------------------------------------------------------

// These routines always print to the console. Use these instead of the regular
// serial print routines because they need to be wrapped in a semaphore so
// they don't interfer with error logging routines above.

size_t  MsgLog::print(const char * szLogMsg)
    {
    size_t  iChars = 0;

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
    size_t  iChars = 0;

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
    size_t  iChars = 0;

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
