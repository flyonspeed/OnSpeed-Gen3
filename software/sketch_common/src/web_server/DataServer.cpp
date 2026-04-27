
#include <Arduino.h>

#include <WiFi.h>               // https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi
#include <WiFiClient.h>
#include <WiFiAP.h>

#include <WebSocketsServer.h>   // https://github.com/Links2004/arduinoWebSockets version 2.1.3

#include <algorithm>

#include "src/Globals.h"

#include <aoa/DisplayPctAnchors.h>
#include <aoa/PercentLift.h>

using onspeed::rad2deg;
using onspeed::kts2mps;
using onspeed::m2ft;
using onspeed::mps2fpm;
using onspeed::aoa::ComputeDisplayPctAnchors;
using onspeed::aoa::ComputePercentLift;
using onspeed::aoa::DisplayPctAnchors;
using onspeed::fpm2mps;
using onspeed::safeAsin;

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
        lNextMillis = millis() + kDisplaySerialPeriodMs;
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
	    case WStype_PING:
	    case WStype_PONG:
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
    float fWifiOAT;
    // Installation-corrected body-vertical acceleration (G). 1G level, 2G in a
    // 60-deg bank. Matches the value GLimitDecision uses for over-G warnings.
    float fVerticalGload = g_AHRS.AccelVertCorr;

    // AOA is gated by the user-tunable `iMuteAudioUnderIAS` threshold,
    // not `bIasAlive`.  Rationale: `iMuteAudioUnderIAS` is a UX knob
    // (pilot sets where they want AOA callouts to appear) — it is
    // configured independently of sensor validity.  `bIasAlive` is the
    // sensor-level validity flag (20/15 kt fixed).  Audio mute uses
    // `iMuteAudioUnderIAS` with its own +5 kt hysteresis; keep the web
    // UI's "show AOA?" decision aligned with that UX choice rather than
    // surfacing AOA 10 kt earlier than the pilot configured.
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
    if (g_Config.bCalSourceEfis)
    {

        // efis or VN-300 data
        if (g_EfisSerial.enType == EfisSerialPort::EnVN300)
        {
            // use Vectornav data
            fWifiPitch = g_EfisSerial.suVN300.Pitch;
            fWifiRoll  = g_EfisSerial.suVN300.Roll;

            if (g_AHRS.fTAS > 0)
            {
                // TAS is being updated in an interrupt
                fWifiFlightpath = rad2deg(safeAsin(-g_EfisSerial.suVN300.VelNedDown/g_AHRS.fTAS)); // vnVelNedDown is reversed (positive when descending)
            }
            else
                fWifiFlightpath = 0;

            fWifiVSI = mps2fpm(-g_EfisSerial.suVN300.VelNedDown); // fpm
            fWifiIAS = g_Sensors.IAS;
        } // end enType = EnVN300

        else
        {
            //use parsed efis data
            fWifiPitch = g_EfisSerial.suEfis.Pitch;
            fWifiRoll  = g_EfisSerial.suEfis.Roll;
            if (g_EfisSerial.suEfis.TAS > 0)
            {
                // Use EFIS VSI (matches the EFIS pitch/roll above) instead of
                // KalmanVSI to avoid mixing data sources in the same JSON payload.
                const float fEfisVsiMps = fpm2mps(g_EfisSerial.suEfis.VSI);
                fWifiFlightpath = rad2deg(safeAsin(fEfisVsiMps / kts2mps(g_EfisSerial.suEfis.TAS)));
            }

            else
                if (g_AHRS.fTAS > 0)
                {
                    fWifiFlightpath = rad2deg(safeAsin(g_AHRS.KalmanVSI/g_AHRS.fTAS)); // convert efiVSI from fpm to m/s
                }
                else
                    fWifiFlightpath=0;

            // kalmanVSI is being updated in an interrupt
            fWifiVSI = mps2fpm(g_AHRS.KalmanVSI);
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
        fWifiVSI        = mps2fpm(g_AHRS.KalmanVSI); // fpm

        fWifiIAS = g_Sensors.IAS;
    } // end internal cal source

    // OAT: prefer EFIS data, fall back to internal sensor
    if (g_Config.bCalSourceEfis)
        fWifiOAT = g_EfisSerial.suEfis.OAT;
    else if (g_Config.bOatSensor)
        fWifiOAT = g_Sensors.OatC;
    else
        fWifiOAT = 0.0f;

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
    // WebSocket schema mirrors the display-serial wire's percent-anchor
    // contract, so a future shared indexer renderer can run identically
    // off either transport.  Body-angle AOA / DerivedAOA stay because
    // the LiveView shows them numerically (and a debugging consumer
    // wanting to compare body angle to percent gets both); the per-flap
    // body-angle setpoints (LDmax, OnSpeedFast/Slow/Warn, alpha_0,
    // alpha_stall) are gone — every consumer renders against the
    // percent anchors instead.
    const char * szFormat =
        "{\"AOA\":%.2f,\"Pitch\":%.2f,\"Roll\":%.2f,\"IAS\":%.2f,\"PAlt\":%.2f,"
        "\"verticalGLoad\":%.2f,\"lateralGLoad\":%.2f,"
        "\"flapsPos\":%i,\"flapIndex\":%i,"
        "\"coeffP\":%.2f,\"dataMark\":%i,\"kalmanVSI\":%.2f,\"flightPath\":%.2f,"
        "\"PitchRate\":%.2f,\"DecelRate\":%.2f,\"OAT\":%.2f,\"DerivedAOA\":%.2f,"
        "\"percentLift\":%i,\"tonesOnPctLift\":%i,\"onSpeedFastPctLift\":%i,"
        "\"onSpeedSlowPctLift\":%i,\"stallWarnPctLift\":%i,\"pipPctLift\":%i}";

    // Ensure JSON never contains invalid numeric tokens like "nan"/"inf".
    fWifiAOA        = SafeJsonFloat(fWifiAOA, -100.0f);
    fWifiPitch      = SafeJsonFloat(fWifiPitch, 0.0f);
    fWifiRoll       = SafeJsonFloat(fWifiRoll, 0.0f);
    fWifiIAS        = SafeJsonFloat(fWifiIAS, 0.0f);
    fWifiVSI        = SafeJsonFloat(fWifiVSI, 0.0f);
    fWifiFlightpath = SafeJsonFloat(fWifiFlightpath, 0.0f);
    fVerticalGload  = SafeJsonFloat(fVerticalGload, 0.0f);
    fWifiOAT        = SafeJsonFloat(fWifiOAT, 0.0f);

    const float fPAltFt = SafeJsonFloat(m2ft(g_AHRS.KalmanAlt), 0.0f);
    const float fLatG   = SafeJsonFloat(g_AHRS.AccelLatCorr, 0.0f);
    const float fCoeffP = SafeJsonFloat(g_fCoeffP, 0.0f);
    const float fPitchRate  = SafeJsonFloat(g_AHRS.gPitch, 0.0f);
    const float fDecelRate  = SafeJsonFloat(g_Sensors.fDecelRate, 0.0f);
    const float fDerivedAOA = SafeJsonFloat(g_AHRS.DerivedAOA, 0.0f);

    // Snapshot the active flap entry plus the full flap vector once
    // under xAhrsMutex.  Two consumers below read this snapshot:
    //
    //   - ComputePercentLift on the snapped active detent — drives the
    //     live `percentLift` JSON field from current AOA, matching
    //     what the audio path uses.
    //
    //   - ComputeDisplayPctAnchors on the entire vector + the raw
    //     lever ADC — drives the four interpolated anchor percents
    //     (`tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`,
    //     `stallWarnPctLift`) that the LiveView's indexer uses to
    //     position the L/Dmax pip and OnSpeed band edges.  These slide
    //     smoothly between adjacent detents during flap deployment so
    //     the pip tracks the aerodynamic transition.
    //
    // HandleConfigSave on Core 0 swaps g_Config.aFlaps under the same
    // mutex; without this snapshot the WebSocket frame could mix
    // old-and-new setpoints from a mid-serialization swap, or index
    // past the new vector's end after a 4->2 shrink.  20 Hz cadence
    // has plenty of headroom for a 10 ms timeout; on timeout / OOB
    // the payload reports zeros and `flapIndex` is forced to 0 too,
    // which the JS liveview renders as "uncalibrated".
    FOSConfig::SuFlaps flapSnapshot{};
    int iSnapFlapIdx = 0;
    int iSnapFlapPos = -1;
    bool bSnapValid  = false;

    FOSConfig::SuFlaps aFlapsSnapshot[onspeed::MAX_AOA_CURVES]{};
    size_t   nFlapsSnapshot = 0;
    uint16_t uFlapsRawAdc   = 0;
    if (xSemaphoreTake(xAhrsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
        const size_t nFlaps = g_Config.aFlaps.size();
        const int    iIdx   = g_Flaps.iIndex;
        if (iIdx >= 0 && (size_t)iIdx < nFlaps)
            {
            flapSnapshot = g_Config.aFlaps[iIdx];
            iSnapFlapIdx = iIdx;
            iSnapFlapPos = g_Flaps.iPosition;
            bSnapValid   = true;
            }

        // Copy the full flap vector + raw lever ADC for the
        // interpolated-anchor pass.  Capped at MAX_AOA_CURVES; same
        // bound the config parser and flap detector enforce.
        const size_t nCopy = std::min<size_t>(nFlaps,
                                              static_cast<size_t>(onspeed::MAX_AOA_CURVES));
        for (size_t i = 0; i < nCopy; ++i)
            aFlapsSnapshot[i] = g_Config.aFlaps[i];
        nFlapsSnapshot = nCopy;
        // g_Flaps.uValue is a uint16_t written outside this mutex by
        // Flaps::Update() at 1 Hz from SensorIO; aligned 16-bit stores
        // are atomic on the ESP32-S3 so a torn read is impossible
        // regardless of mutex state.  Reading inside this window is for
        // snapshot consistency, not synchronization.
        uFlapsRawAdc   = g_Flaps.uValue;

        xSemaphoreGive(xAhrsMutex);
        }

    // Snapshot the live AOA / IAS once.  Without this snapshot we'd
    // read g_Sensors.AOA twice (once for fWifiAOA earlier, once for
    // ComputePercentLift below) and g_Sensors.IAS twice (mute gate
    // earlier, percent-lift IAS gate below) across an unguarded
    // multi-tick window, so the displayed numeric AOA and the
    // percent-lift bar could disagree by one sample tick.  Cheap
    // local snapshot keeps them coherent.
    const float fAoaSnap         = g_Sensors.AOA;
    const float fIasSnap         = g_Sensors.IAS;
    const bool bIasValidForOutput = (fIasSnap >= g_Config.iMuteAudioUnderIAS);

    // Live percent-lift reading — the active-detent calibration is
    // what the audio path uses, so this matches what the pilot hears.
    int iJsonPercentLift   = 0;
    if (bSnapValid)
        {
        iJsonPercentLift = ComputePercentLift(fAoaSnap, flapSnapshot, bIasValidForOutput);
        }

    // Display percent anchors:
    //   * tonesOnPctLift (the L/Dmax pip) is INTERPOLATED across the
    //     bracket containing the lever, so the LiveView pip slides
    //     smoothly during flap deployment.
    //   * onSpeedFast/Slow/StallWarn SNAP to the active detent so the
    //     donut/chevron screen positions stay in lockstep with the
    //     audio cues that fire at those same calibrated thresholds.
    //   * flapsDeg is INTERPOLATED so the numeric flap-angle readout
    //     in LiveView's corner slides smoothly with the lever.
    // Same contract the M5 wire ships, so a future shared indexer
    // renderer can run identically off either transport.  iasValid=true
    // keeps the indexer geometry stable across the audio mute threshold.
    DisplayPctAnchors anchors = ComputeDisplayPctAnchors(uFlapsRawAdc,
                                                         aFlapsSnapshot,
                                                         nFlapsSnapshot,
                                                         static_cast<size_t>(iSnapFlapIdx),
                                                         true);
    const int iJsonTonesOnPct    = anchors.tonesOnPctLift;
    const int iJsonFastPct       = anchors.onSpeedFastPctLift;
    const int iJsonSlowPct       = anchors.onSpeedSlowPctLift;
    const int iJsonStallWarnPct  = anchors.stallWarnPctLift;
    const int iJsonPipPct        = anchors.pipPctLift;

    // Use the interpolated flap angle so the WebSocket's numeric
    // "flapsPos" readout slides smoothly during deployment (matching
    // the M5 wire's behavior).  Falls back to the snapped detent
    // position when the calibration is empty (anchors.flapsDeg = 0)
    // or the snapshot was invalid — same convention as DisplaySerial.
    const int iJsonFlapsPos      = (nFlapsSnapshot > 0)
                                       ? anchors.flapsDeg
                                       : iSnapFlapPos;

    // szFormat is a compile-time constant split across lines for readability.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
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
        iJsonFlapsPos,
        iSnapFlapIdx,
        fCoeffP,
        g_iDataMark,
        fWifiVSI,
        fWifiFlightpath,
        fPitchRate,
        fDecelRate,
        fWifiOAT,
        fDerivedAOA,
        iJsonPercentLift,
        iJsonTonesOnPct,
        iJsonFastPct,
        iJsonSlowPct,
        iJsonStallWarnPct,
        iJsonPipPct);
#pragma GCC diagnostic pop

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
