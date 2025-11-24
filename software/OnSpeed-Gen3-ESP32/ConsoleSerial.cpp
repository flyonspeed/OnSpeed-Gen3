
#include <string>
#include <cstring>

#include "freertos/idf_additions.h"

#include "Globals.h"

#ifdef SUPPORT_LITTLEFS
#include <LittleFS.h>
#endif

#include "Helpers.h"
#include "ConsoleSerial.h"


std::string Base64_Decode(std::string sEncodedString);

// ----------------------------------------------------------------------------

ConsoleSerialIO::ConsoleSerialIO()
{
}

// ----------------------------------------------------------------------------

// Tie into the USB serial I/O. USB serial should alread be open.

void ConsoleSerialIO::Init()
{
    // USB serial is the main arduino serial port (aka Serial0)
    pSerial = &Serial;

    CmdBufferIndex = 0;

    // Start in enabled mode
    Enable(true);

}

// ----------------------------------------------------------------------------

// Enable / disable reading of console data

void ConsoleSerialIO::Enable(bool bEnable)
{
    // If being enabled then flush the input buffer
    if (!bEnabled && bEnable)
        g_Log.flush();

    bEnabled = bEnable;
}

// ----------------------------------------------------------------------------

// Note: the g_Log routines are generally preferred because they automatically
// wrap the serial output in a semaphore for safely. There are a few places in
// this module, however, where it is advantageous to use the serial output 
// directly and wrap it in a semaphore. For example, in this help printout it
// is useful to wrap all println() in a semaphore to make sure they stay together.

void ConsoleSerialIO::DisplayConsoleHelp()
    {
    if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100))) 
        {
        pSerial->println("");
        pSerial->println("Commands accepted via console:\n");
        pSerial->println("HELP                 - Display this command list");
        pSerial->println("LOG [ENABLE|DISABLE] - Enable / disable data logging to disk");
        pSerial->println("MSG                  - Enable / disable debugging messages.");
        pSerial->println("LIST                 - List files on SD card");
        pSerial->println("DELETE filename      - Delete file on SD card");
        pSerial->println("PRINT filename       - Display file contents");
        pSerial->println("FORMAT               - Format SD card");
        pSerial->println("BIAS                 - Calculate pressure sensor bias");
        pSerial->println("REBOOT               - Reboot system");
        pSerial->println("WIFIREFLASH          - Allow reflashing Wifi chip via USB cable");
        pSerial->println("SENSORS              - Show current pressure and acceleration");
        pSerial->println("FLAPS                - Show current flap position value");
        pSerial->println("VOLUME               - Show current volume potentiometer value");
        pSerial->println("CONFIG               - Show current configuration values");
        pSerial->println("AUDIOTEST            - Left & right audio test");
        pSerial->println("TASKS                - Show info about running tasks");
        pSerial->println("COOKIE");
        pSerial->println("");

        xSemaphoreGive(xSerialLogMutex);
        }
    }

const char szHtmlHeader[] PROGMEM = R"#(
MSG                             - This help message
MSG *                           - Show all available module names
MSG module                      - Show message log level
MSG module debug|warning|error  - Set message log level
)#";

// ----------------------------------------------------------------------------

void ConsoleSerialIO::Read()
    {
    char    cSerialCmdChar;         // usb serial command character
    char    szListfileFileName[25]; // file name for sd card file listing operations

    // Look for serial command
    if (pSerial->available() > 0)
        {

        if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100))) 
            {
            cSerialCmdChar = pSerial->read();
            xSemaphoreGive(xSerialLogMutex);
            }

        // Not end of line so store char and keep reading
        if (cSerialCmdChar != char(10) &&      // LF
            cSerialCmdChar != char(13) &&      // CR
            CmdBufferIndex < CONSOLE_BUFFER_SIZE - 1)
            {
            // Swallow the ! character
            if (cSerialCmdChar != '!')
                {
                CmdBuffer[CmdBufferIndex  ] = cSerialCmdChar;
                CmdBuffer[CmdBufferIndex+1] = '\0'; // Make sure it is null terminate, just because.
                CmdBufferIndex++;

#if 1
                // Display input character, maybe handle backspace someday
                g_Log.printf("%c", cSerialCmdChar);
#endif
                } // end swallow '!'
            } // end if not CR/LF

        // End of line found so process command
        else
            {
#if 1
            g_Log.println("");
#endif
            // Begin tokenizing the command string
            char * szCmdToken = strtok(CmdBuffer, " ");

            // No command case
            if (szCmdToken == NULL)
                {
                }

            // LIST
            // ----
            else if (strncasecmp(szCmdToken, "LIST", 4) == 0)
                {
                const char * szFileListFormat = "%15s  %10.1f kB\n";

                // Show files on the flash file system
#ifdef SUPPORT_LITTLEFS
                if (g_bFlashFS) 
                    {
                    File hFlashRoot = LittleFS.open("/", "r");

                    if (hFlashRoot && hFlashRoot.isDirectory()) 
                        {
                        g_Log.println("Flash Files");
                        // Iterate through files
                        File hFile = hFlashRoot.openNextFile();
                        while (hFile) 
                            {
                            g_Log.printf(szFileListFormat, hFile.name(), (hFile.size() + 50) / 1000.0);
                            hFile = hFlashRoot.openNextFile();
                            } 
                        } // end if open root dir OK
                    } // end if flash enabled
#endif

                g_Log.println("SD Card Files");
                // Show files on the SD card
                if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100))) 
                    {
                    SdFileSys::SuFileInfoList   suFileList;

                    if (g_SdFileSys.FileList(&suFileList))
                        for (int iIdx=0; iIdx<suFileList.size(); iIdx++)
                            g_Log.printf(szFileListFormat, suFileList[iIdx].szFileName, (suFileList[iIdx].uFileSize + 50) / 1000.0);
                    else
                        g_Log.println(MsgLog::EnDisk, MsgLog::EnError, "LIST - SD card missing or unreadable");

                    xSemaphoreGive(xWriteMutex);
                    }
                else
                    g_Log.println(MsgLog::EnMain, MsgLog::EnWarning, "LIST - xWriteMutex fail");
                } // end if LIST

            // DELETE
            // ------
            else if (strncasecmp(szCmdToken, "DELETE", 6) == 0)
                {
                if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100))) 
                    {
                    // Delete file
                    szCmdToken = strtok(NULL, " ");
                    if (szCmdToken != NULL)
                        {
                        g_Log.printf("\nDelete '%s' ", szCmdToken);
                        if (g_SdFileSys.remove(szCmdToken)) 
                            g_Log.println("SUCCESS");
                        else
                            g_Log.println("FAIL");
                        }
                    xSemaphoreGive(xWriteMutex);
                    }
                else
                    g_Log.println(MsgLog::EnMain, MsgLog::EnWarning, "DELETE - xWriteMutex fail");
                } // end if DELETE

            // PRINT
            // -----
            else if (strncasecmp(szCmdToken, "PRINT", 5) == 0)
                {
                FsFile  hListFile;

                if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100))) 
                    {
                    // Print file contents
                    szCmdToken = strtok(NULL, " ");
                    if (szCmdToken != NULL)
                        {
                        hListFile = g_SdFileSys.open(szCmdToken, O_READ);

                        if (hListFile) 
                            {
                            g_Log.println("");
                            g_Log.print(szCmdToken);
                            g_Log.println(":");

                            // Read from the file until there's nothing else in it:
                            if (xSemaphoreTake(xSerialLogMutex, pdMS_TO_TICKS(100))) 
                                {
                                while (hListFile.available()) 
                                    {
                                    pSerial->flush();
                                    pSerial->write(hListFile.read());
                                    }
                                xSemaphoreGive(xSerialLogMutex);
                                }
                            // Close the file:
                            hListFile.close();
                            g_Log.println("\nDONE.");
                            } 
                        else 
                            {
                            // if the file didn't open, print an error:
                            g_Log.print("Error opening ");
                            g_Log.println(szCmdToken);
                            }
                        } // end if file name not NULL
                    xSemaphoreGive(xWriteMutex);
                    }
                else
                    g_Log.println(MsgLog::EnMain, MsgLog::EnWarning, "PRINT - xWriteMutex fail");
                } // end if PRINT

            // LOG
            // ---
            else if (strncasecmp(szCmdToken, "LOG", 3) == 0)
                {
                szCmdToken = strtok(NULL, " ");

                if (szCmdToken == NULL)
                    {
                    g_Log.printf("Logging %s\n", g_Config.bSdLogging ? "ENABLED" : "DISABLED");
                    }

                else if (strncasecmp(szCmdToken, "ENABLE", 6) == 0)
                    {
                    if (g_Config.bSdLogging == false)
                        {
                        g_Config.bSdLogging = true;
                        g_Config.SaveConfigurationToFile();
                        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100))) 
                            {
                            g_LogSensor.Open();
                            xSemaphoreGive(xWriteMutex);
                            }
                        g_Log.println("Logging ENABLED");
                        }
                    } // end ENABLE

                else if (strncasecmp(szCmdToken, "DISABLE", 7) == 0)
                    {
                    if (g_Config.bSdLogging == true)
                        {
                        g_Config.bSdLogging = false;
                        g_Config.SaveConfigurationToFile();
                        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100))) 
                            {
                            g_LogSensor.Close();
                            xSemaphoreGive(xWriteMutex);
                            }
                        g_Log.println("Logging DISABLED");
                        }
                    } // end DISABLE

                else
                    {
                    g_Log.printf("Logging %s\n", g_Config.bSdLogging ? "ENABLED" : "DISABLED");
                    }

                } // end if LOG

            // MSG
            // ---
            else if (strncasecmp(szCmdToken, "MSG", 3) == 0)
                {
                char * szModName = strtok(NULL, " ");

                if (szModName == NULL)
                    {
                    g_Log.println("  Module    Level");
                    g_Log.println("---------- -------");
                    for (int iModIdx = 0; iModIdx < g_Log.ModuleCount; iModIdx++)
                        g_Log.printf("%-10s %s\n", 
                            g_Log.asuModule[iModIdx].szDescription, 
                            g_Log.szLevelName(g_Log.asuModule[iModIdx].enLevel));
                    }

                else
                    {
                    char * szLevel = strtok(NULL, " ");
                    if (szLevel != NULL)
                        {
                        bool    bStatus;

                        if      (strcasecmp(szLevel, "DEBUG")   == 0) bStatus = g_Log.Set(szModName, MsgLog::EnDebug);
                        else if (strcasecmp(szLevel, "WARNING") == 0) bStatus = g_Log.Set(szModName, MsgLog::EnWarning);
                        else if (strcasecmp(szLevel, "ERROR")   == 0) bStatus = g_Log.Set(szModName, MsgLog::EnError);
                        else g_Log.printf("Level '%s' not recognized\n", szLevel);

                        if (bStatus == false)
                            g_Log.printf("Unable to set '%s' to '%s'\n", szModName, szLevel);
                        }
                    } // end module name not null

                } // end if MSG

            // FORMAT
            // ------
            else if (strncasecmp(szCmdToken, "FORMAT", 6) == 0)
                {

                if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100))) 
                    {
                    bool bOrigSdLogging = g_Config.bSdLogging;

                    g_Config.bSdLogging = false; // turn off sdLogging
                    if (bOrigSdLogging) 
                        {
                        g_LogSensor.Close();
                        }

                    // Format the SD card
                    g_SdFileSys.Format(pSerial);

                    if (bOrigSdLogging)
                        {
                        // Reinitialize card
                        g_Config.bSdLogging = true; // if logging was on before FORMAT turn it back on
                        g_LogSensor.Open();
                        }

                    xSemaphoreGive(xWriteMutex);

                    // Put the configuration file back onto the card
                    // Semaphore is handled in the function call
                    g_Config.SaveConfigurationToFile();
                    }
                else
                    g_Log.println(MsgLog::EnMain, MsgLog::EnWarning, "FORMAT - xWriteMutex fail");

                } // end if FORMAT

            // BIAS
            // ----
            else if (strncasecmp(szCmdToken, "BIAS", 4) == 0)
                {
                g_Log.println("Getting Pressure sensor bias...");

                // Get Pfwd bias
                long PfwdTotal = 0;
                long P45Total  = 0;
                for (int i=1;i<=1000;i++)
                    {
                    xSemaphoreTake(xSensorMutex, portMAX_DELAY);
                    PfwdTotal += g_pPitot->ReadPressureCounts();
                    P45Total  += g_pAOA->ReadPressureCounts();
                    xSemaphoreGive(xSensorMutex);
                    vTaskDelay(10);
                    }

                g_Log.printf("Pfwd bias = %d\n", PfwdTotal/1000);
                g_Log.printf("P45  bias = %d\n", P45Total/1000);
                } // end if BIAS

            else if (strncasecmp(szCmdToken, "WIFIREFLASH", 11) == 0)
                {
                g_Log.println("wifi reflash mode not supported");
                } // end if WIFIREFLASH

            // REBOOT
            // ------
            else if (strncasecmp(szCmdToken, "REBOOT", 6) == 0)
                {
                g_Log.println("Serial reboot request. Rebooting...");
                delay(2000);
                _softRestart();
                }

            // SENSORS
            // -------
            else if (strncasecmp(szCmdToken, "SENSORS", 7) == 0)
                {
                if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(100))) 
                    {
                    uint16_t    uPitotCounts  = g_pPitot->ReadPressureCounts();
                    uint16_t    uAoaCounts    = g_pAOA->ReadPressureCounts();
                    uint16_t    uStaticCounts = g_pStatic->ReadPressureCounts();

                    g_Log.printf("\nPorts %s  Box Top %s\r\n", g_Config.sPortsOrientation.c_str(), g_Config.sBoxtopOrientation.c_str());
                    g_Log.printf("Axis - Forward %s  Lateral %s  Vertical %s\r\n", 
                            g_pIMU->sForwardGloadAxis.c_str(), g_pIMU->sLateralGloadAxis.c_str(), g_pIMU->sVerticalGloadAxis.c_str());

                    g_Log.printf("Accel IMU  X : %5.2f  Y : %5.2f  Z : %5.2f\r\n", g_pIMU->fAccelX,         g_pIMU->fAccelY,         g_pIMU->fAccelZ);
                    g_Log.printf("Accel A/C  X : %5.2f  Y : %5.2f  Z : %5.2f\r\n", g_pIMU->Ax,              g_pIMU->Ay,              g_pIMU->Az);
                    g_Log.printf("Smoothed   X : %5.2f  Y : %5.2f  Z : %5.2f\r\n", g_AHRS.AccelFwdSmoothed, g_AHRS.AccelLatSmoothed, g_AHRS.AccelVertSmoothed);
                    g_Log.printf("Comp       X : %5.2f  Y : %5.2f  Z : %5.2f\r\n", g_AHRS.AccelFwdComp,     g_AHRS.AccelLatComp,     g_AHRS.AccelVertComp);

                    g_Log.printf("Pitch        : %5.2f\r\n", g_AHRS.SmoothedPitch);
                    g_Log.printf("Roll         : %5.2f\r\n", g_AHRS.SmoothedRoll);

                    g_Log.printf("PFwd         : %6.3f PSI %4d Counts\r\n",          g_pPitot->ReadPressurePSI(uPitotCounts), uPitotCounts);
                    g_Log.printf("PAoA         : %6.3f PSI %4d Counts\r\n",          g_pAOA->ReadPressurePSI(uAoaCounts),     uAoaCounts);
                    g_Log.printf("PStatic      : %6.3f PSI %4d Counts %7.2f mb\r\n", g_pStatic->ReadPressurePSI(uStaticCounts), uStaticCounts, g_pStatic->ReadPressureMillibars(uStaticCounts));
                    g_Log.printf("Palt         : %6.1f ft\r\n", g_Sensors.Palt);

                    xSemaphoreGive(xSensorMutex);
                    }
                else
                    g_Log.printf(MsgLog::EnMain, MsgLog::EnWarning, "SENSORS - Could not obtain the sensor mutex\n");
                } 

            // FLAPS
            // -----
            else if (strncasecmp(szCmdToken, "FLAPS", 5) == 0)
                {
                if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(100))) 
                    {
                    g_Flaps.Update();
                    xSemaphoreGive(xSensorMutex);
                    g_Log.printf("Current flap : %5u Raw  %d Position\n", g_Flaps.uValue, g_Flaps.iPosition);
                    }
                else
                    g_Log.printf(MsgLog::EnMain, MsgLog::EnWarning, "FLAPS - Could not obtain the sensor mutex\n");
                } 

            // VOLUME
            // ------
            else if (strncasecmp(szCmdToken, "VOLUME", 6) == 0)
                {
                int     iVolPos;
                int     iVolumePercent;

                if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(100))) 
                    {
                    iVolPos = ReadVolume();
                    xSemaphoreGive(xSensorMutex);
                    }
                else
                    g_Log.printf(MsgLog::EnMain, MsgLog::EnWarning, "VOLUME - Could not obtain the sensor mutex\n");

                iVolumePercent = mapfloat(iVolPos, g_Config.iVolumeLowAnalog, g_Config.iVolumeHighAnalog, 0, 100);
                g_Log.printf("Volume Control %s\n", g_Config.bVolumeControl ? "ENABLED" : "DISABLED");
                g_Log.printf("Volume Pot : Raw %5d Mapped %d%%\n", iVolPos, iVolumePercent);
                g_Log.printf("Current Volume :  %.0f%%  Left Gain %.2f Right Gain %.2f\n", 
                    g_AudioPlay.fVolume*100, g_AudioPlay.fLeftGain, g_AudioPlay.fRightGain);
                }

            // CONFIG
            // ------
            else if (strncasecmp(szCmdToken, "CONFIG", 6) == 0)
                {
                String sConfigString = "";
                sConfigString = g_Config.ConfigurationToString();
                g_Log.println("Current configuration: ");
                g_Log.print(sConfigString.c_str());
                g_Log.flush();
                }
                                                                                                                      
            // AUDIOTEST
            // ---------
            else if (strncasecmp(szCmdToken, "AUDIOTEST", 9) == 0)
                {
                g_AudioPlay.AudioTest();
                g_Log.printf("AUDIOTEST Complete\n");
                }

            // TASKS
            // -----
            else if (strncasecmp(szCmdToken, "TASKS", 5) == 0)
                {
                PrintTaskInfo(xTaskReadSensors);
                PrintTaskInfo(xTaskAudioPlay);
                PrintTaskInfo(xTaskWriteLog);
                PrintTaskInfo(xTaskCheckSwitch);
                PrintTaskInfo(xTaskDisplaySerial);
                PrintTaskInfo(xTaskGLimit);
                PrintTaskInfo(xTaskVolume);
                PrintTaskInfo(xTaskVnoChime);
                PrintTaskInfo(xTask3dAudio);
                PrintTaskInfo(xTaskHeartbeat);
                PrintTaskInfo(xTaskLogReplay);
                PrintTaskInfo(xTaskTestPot);
                PrintTaskInfo(xTaskRangeSweep);
                } // end TASKS

            // HELP
            // ----
            else if (strncasecmp(szCmdToken, "HELP", 4) == 0)
                {
                DisplayConsoleHelp();
                } // end HELP

            // COOKIE
            // ------
            else if (strncasecmp(szCmdToken, "COOKIE", 5) == 0)
                {
                g_Log.println("");
                g_Log.println(Base64_Decode(
                    "VGhpcyBlZmZvcnQgaXMgZGVkaWNhdGVkIHRvIG15IGRlYXIsIHN3ZWV0IE1p"
                    "bm55LiBUaHJvdWdoIHlvdXIgbG9zcyBtYXkgbWFueSBsaXZlcyBiZSBzYXZl"
                    "ZC4gQm9iYnkKSmVvbmcgTWluIEtpbQoxOTY3IC0gMjAyNQoK").c_str());
                g_Log.println("");
                }

            g_Log.print("# ");

            // reset cmdBuffer
            memset(CmdBuffer,0,sizeof(CmdBuffer));
            CmdBufferIndex = 0;
            }

        } // if serial.available
    } // end ReadUSBSerial()

// ----------------------------------------------------------------------------

// Task info is kinda thin but I hope to have more later
// TaskHandle_t is actually a pointer. All the task handles were initialized
// to NULL. If the value is still NULL then the task hasn't been created.

void ConsoleSerialIO::PrintTaskInfo(TaskHandle_t  xTask)
    {
    if (xTask != NULL)
        {
        char           * szTaskName      = pcTaskGetName(xTask);
        BaseType_t       iFreeStackSpace = uxTaskGetStackHighWaterMark(xTask);

        g_Log.printf("%-16s  %d min stack\n", szTaskName, iFreeStackSpace);
        }
    }


// ----------------------------------------------------------------------------

static const char from_base64[] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  62, 255,  62, 255,  63,
                                     52,  53,  54,  55,  56,  57,  58,  59,  60,  61, 255, 255, 255, 255, 255, 255,
                                    255,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
                                     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25, 255, 255, 255, 255,  63,
                                    255,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
                                     41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51, 255, 255, 255, 255, 255};

std::string Base64_Decode(std::string sEncodedString)
{
    // Make sure string length is a multiple of 4
    while ((sEncodedString.size() % 4) != 0)
        sEncodedString.push_back('=');

    size_t iEncodedSize = sEncodedString.size();
    //std::vector<char>   sAsciiStr;
    //sAsciiStr.reserve(3*iEncodedSize/4);
    std::string         sAsciiStr;

    for (size_t i=0; i<iEncodedSize; i += 4)
    {
        // Get values for each group of four base 64 characters
        char    b4[4];
        b4[0] = (sEncodedString[i+0] <= 'z') ? from_base64[sEncodedString[i+0]] : 0xff;
        b4[1] = (sEncodedString[i+1] <= 'z') ? from_base64[sEncodedString[i+1]] : 0xff;
        b4[2] = (sEncodedString[i+2] <= 'z') ? from_base64[sEncodedString[i+2]] : 0xff;
        b4[3] = (sEncodedString[i+3] <= 'z') ? from_base64[sEncodedString[i+3]] : 0xff;

        // Transform into a group of three bytes
        char    b3[3];
        b3[0] = ((b4[0] & 0x3f) << 2) + ((b4[1] & 0x30) >> 4);
        b3[1] = ((b4[1] & 0x0f) << 4) + ((b4[2] & 0x3c) >> 2);
        b3[2] = ((b4[2] & 0x03) << 6) + ((b4[3] & 0x3f) >> 0);

        // Add the byte to the return value if it isn't part of an '=' character (indicated by 0xff)
        if (b4[1] != 0xff) sAsciiStr.push_back(b3[0]);
        if (b4[2] != 0xff) sAsciiStr.push_back(b3[1]);
        if (b4[3] != 0xff) sAsciiStr.push_back(b3[2]);
    }

    return sAsciiStr;
}
