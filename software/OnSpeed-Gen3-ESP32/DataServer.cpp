
#include <Arduino.h>

#include <WiFi.h>               // https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi
#include <WiFiClient.h>
#include <WiFiAP.h>

#include <WebSocketsServer.h>   // https://github.com/Links2004/arduinoWebSockets version 2.1.3

#include <ArduinoJson.h>

#include "Globals.h"
#include "DataServer.h"

// wifi data variables
//char crc_buffer[250];
//volatile byte CRC=0;
//volatile float wifiAOA;
//volatile float alphaVA=0.00;
//volatile float wifiPitch=0;
//volatile float wifiRoll=0;
//volatile float wifiFlightpath=0;
//volatile float wifiVSI=0;
//volatile float wifiIAS=0;
//volatile int calSourceID;
//volatile float accelSumSq;
//volatile float verticalGload;

// WebSocket server for live data display
WebSocketsServer    DataServer = WebSocketsServer(81);

// Function prototypes
// -------------------
void    DataServerEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
size_t  UpdateLiveDataJson(char * pOut, size_t uOutSize);

unsigned long   lNextMillis = 0;

// ----------------------------------------------------------------------------

void DataServerPoll()
    {
    char            szLiveDataJson[512];

    DataServer.loop();

    // Send to websocket if there are any clients waiting
    if (millis() > lNextMillis)
        {
        if (DataServer.connectedClients(false) > 0)
            {
            size_t uLen = UpdateLiveDataJson(szLiveDataJson, sizeof(szLiveDataJson));
            if (uLen > 0)
                DataServer.broadcastTXT(szLiveDataJson, uLen);
            }
        lNextMillis = millis() + 100;
        }
    }


// ----------------------------------------------------------------------------

void DataServerInit()
    {
    lNextMillis = 0;

    // Start websockets (live data display)
    DataServer.begin();
    DataServer.onEvent(DataServerEvent);
    }

// ----------------------------------------------------------------------------

// There really isn't much to do on events. There is some debug code but in production
// it should all be commented out.

void DataServerEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
    {

    switch(type)
        {
        case WStype_DISCONNECTED:
            g_Log.printf(MsgLog::EnDataServer, MsgLog::EnDebug, "[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED:
            {
            IPAddress ip = DataServer.remoteIP(num);
            g_Log.printf(MsgLog::EnDataServer, MsgLog::EnDebug, "[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

		    // send message to client
		    // DataServer.sendTXT(num, "Connected");
            }
            break;
        case WStype_TEXT:
            g_Log.printf(MsgLog::EnDataServer, MsgLog::EnDebug, "[%u] Got Text: %s\n", num, payload);

            // send message to client
            // DataServer.sendTXT(num, "message here");

            // send data to all connected clients
            // DataServer.broadcastTXT("message here");
            break;
        case WStype_BIN:
	    case WStype_ERROR:
	    case WStype_FRAGMENT_TEXT_START:
	    case WStype_FRAGMENT_BIN_START:
	    case WStype_FRAGMENT:
	    case WStype_FRAGMENT_FIN:
	        break;
        }

    } // end DataServerEvent()

// ----------------------------------------------------------------------------

static inline bool IsFiniteFloat(float v)
    {
    return !isnan(v) && !isinf(v);
    }

static inline float SafeJsonFloat(float v, float fallback)
    {
    return IsFiniteFloat(v) ? v : fallback;
    }

size_t UpdateLiveDataJson(char * pOut, size_t uOutSize)
    {
    if (pOut == nullptr || uOutSize == 0)
        return 0;

#if 1
    float fWifiAOA;
    float fWifiPitch;
    float fWifiRoll;
    float fWifiFlightpath;
    float fWifiVSI;
    float fWifiIAS;
    float fAccelSumSq;
    float fVerticalGload;

    fAccelSumSq    = g_pIMU->Ax*g_pIMU->Ax + g_pIMU->Ay*g_pIMU->Ay + g_pIMU->Az*g_pIMU->Az;
    fVerticalGload = sqrt(abs(fAccelSumSq));
    fVerticalGload = round(fVerticalGload * 10.0) / 10.0; // round to 1 decimal place

    if (g_pIMU->Az < 0)
        fVerticalGload *= -1;

    if (isnan(g_Sensors.AOA) || g_Sensors.IAS < g_Config.iMuteAudioUnderIAS)
    {
        fWifiAOA = -100;
    }
    else
    {
        // protect AOA from interrupts overwriting it
        fWifiAOA = g_Sensors.AOA;
    }

    // Pitch, Roll, VSI, Flightpath
    if (g_Config.sCalSource == "EFIS")
    {

        // efis or VN-300 data
        if (g_EfisSerial.enType == EfisSerialIO::EnVN300)
        {
            // use Vectornav data
            fWifiPitch = g_EfisSerial.suVN300.Pitch;
            fWifiRoll  = g_EfisSerial.suVN300.Roll;

            if (g_AHRS.fTAS > 0)
            {
                // TAS is being updated in an interrupt
                fWifiFlightpath = RAD2DEG(asin(-g_EfisSerial.suVN300.VelNedDown/g_AHRS.fTAS)); // vnVelNedDown is reversed (positive when descending)
            }
            else
                fWifiFlightpath = 0;

            fWifiVSI = MPS2FPM(-g_EfisSerial.suVN300.VelNedDown); // fpm
            fWifiIAS = g_Sensors.IAS;
        } // end enType = EnVN300

        else
        {
            //use parsed efis data
            fWifiPitch = g_EfisSerial.suEfis.Pitch;
            fWifiRoll  = g_EfisSerial.suEfis.Roll;
            if (g_EfisSerial.suEfis.TAS > 0)
            {
                fWifiFlightpath = RAD2DEG(asin(g_AHRS.KalmanVSI/KTS2MPS(g_EfisSerial.suEfis.TAS))); // convert efiVSI from fpm to m/s
            }

            else
                if (g_AHRS.fTAS > 0)
                {
                    fWifiFlightpath = RAD2DEG(asin(g_AHRS.KalmanVSI/g_AHRS.fTAS)); // convert efiVSI from fpm to m/s
                }
                else
                    fWifiFlightpath=0;

            // kalmanVSI is being updated in an interrupt
            fWifiVSI = MPS2FPM(g_AHRS.KalmanVSI);
            fWifiIAS = g_EfisSerial.suEfis.IAS;
        } // end if enType != EnVN300

    } // end if cal source is efis

    // Internal cal source
    else
    {
        // Internal data
        fWifiPitch      = g_AHRS.SmoothedPitch;      // degrees
        fWifiRoll       = g_AHRS.SmoothedRoll;       // degrees
        fWifiFlightpath = g_AHRS.FlightPath;         // degrees
        fWifiVSI        = MPS2FPM(g_AHRS.KalmanVSI); // fpm

        // Send efisIAS if spherical probe is in use otherwise use OnspeedIAS.
 #ifdef SPHERICAL_PROBE
//// This logic can't be fully right. What if there is a sperical probe and
//// a VN300? The VN300 doesn't do IAS but there isn't an EFIS to provide IAS
//// because the VN300 is using that serial port.
        if (g_EfisSerial.enType == EfisSerialIO::EnVN300)
            fWifiIAS = g_Sensors.IAS;
        else
            fWifiIAS = g_EfisSerial.suEfis.IAS;
 #else
        fWifiIAS = g_Sensors.IAS;
 #endif
    } // end internal cal source

#else   // Dummy data
    static float fWifiAOA = 0.0;
    static float fWifiPitch = 0.0;
    static float fWifiRoll  = 0.0;
//    float wifiFlightpath=0;
//    float wifiVSI=0;
//    float wifiIAS=0;
//    int   calSourceID;

    static float fWifiPitchInc = 0.2;
    static float fWifiRollInc  = 0.5;


    fWifiAOA > 15.0 ? fWifiAOA = 0.0 : fWifiAOA += 0.1;

    if ((fWifiPitch >= 10.0) || (fWifiPitch <= -10.0)) fWifiPitchInc *= -1.0;
    fWifiPitch += fWifiPitchInc;

    if ((fWifiRoll >= 30.0) || (fWifiRoll <= -30.0)) fWifiRollInc *= -1.0;
    fWifiRoll += fWifiRollInc;

#endif

    // Build a compact JSON payload into a fixed-size buffer to avoid heap churn
    // and prevent buffer overruns on unexpected values.
    const char * szFormat =
        "{\"AOA\":%.2f,\"Pitch\":%.2f,\"Roll\":%.2f,\"IAS\":%.2f,\"PAlt\":%.2f,"
        "\"verticalGLoad\":%.2f,\"lateralGLoad\":%.2f,\"LDmax\":%.2f,\"OnspeedFast\":%.2f,"
        "\"OnspeedSlow\":%.2f,\"OnspeedWarn\":%.2f,\"flapsPos\":%i,\"flapIndex\":%i,"
        "\"coeffP\":%.2f,\"dataMark\":%i,\"kalmanVSI\":%.2f,\"flightPath\":%.2f,"
        "\"PitchRate\":%.2f,\"DecelRate\":%.2f}";

    // Ensure JSON never contains invalid numeric tokens like "nan"/"inf".
    fWifiAOA        = SafeJsonFloat(fWifiAOA, -100.0f);
    fWifiPitch      = SafeJsonFloat(fWifiPitch, 0.0f);
    fWifiRoll       = SafeJsonFloat(fWifiRoll, 0.0f);
    fWifiIAS        = SafeJsonFloat(fWifiIAS, 0.0f);
    fWifiVSI        = SafeJsonFloat(fWifiVSI, 0.0f);
    fWifiFlightpath = SafeJsonFloat(fWifiFlightpath, 0.0f);
    fVerticalGload  = SafeJsonFloat(fVerticalGload, 0.0f);

    const float fPAltFt = SafeJsonFloat(M2FT(g_AHRS.KalmanAlt), 0.0f);
    const float fLatG   = SafeJsonFloat(g_AHRS.AccelLatCorr, 0.0f);
    const float fCoeffP = SafeJsonFloat((float)g_fCoeffP, 0.0f);
    const float fPitchRate = SafeJsonFloat(g_AHRS.gPitch, 0.0f);
    const float fDecelRate = SafeJsonFloat(g_Sensors.fDecelRate, 0.0f);

    int iChars = snprintf(
        pOut,
        uOutSize,
        szFormat,
        fWifiAOA,
        fWifiPitch,
        fWifiRoll,
        fWifiIAS,
        fPAltFt,
        fVerticalGload,
        fLatG,
        SafeJsonFloat(g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA, 0.0f),
        SafeJsonFloat(g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA, 0.0f),
        SafeJsonFloat(g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDSLOWAOA, 0.0f),
        SafeJsonFloat(g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA, 0.0f),
        g_Flaps.iPosition,
        g_Flaps.iIndex,
        fCoeffP,
        g_iDataMark,
        fWifiVSI,
        fWifiFlightpath,
        fPitchRate,
        fDecelRate);

    if (iChars < 0)
        return 0;

    // Truncated; return a minimal valid JSON object rather than invalid data.
    if ((size_t)iChars >= uOutSize)
        {
        if (uOutSize >= 3)
            {
            pOut[0] = '{';
            pOut[1] = '}';
            pOut[2] = '\0';
            return 2;
            }
        pOut[0] = '\0';
        return 0;
        }

    return (size_t)iChars;
    }
