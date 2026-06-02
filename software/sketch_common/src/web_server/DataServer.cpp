
#include <Arduino.h>

#include <WiFi.h>               // https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi
#include <WiFiClient.h>
#include <WiFiAP.h>

#include <WebSocketsServer.h>   // https://github.com/Links2004/arduinoWebSockets version 2.1.3

#include <algorithm>
#include <cstring>

#include "src/Globals.h"
#include "src/ahrs/AhrsSnapshot.h"
#include "src/ahrs/FlapSnapshot.h"

#include <aoa/DisplayPctAnchors.h>
#include <aoa/PercentLift.h>
#include <efis/OatSelect.h>

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

    // Enable WS-level heartbeat so the server detects dead peers in
    // ~20 s instead of waiting for lwIP TCP keepalive (default ~2 h
    // when unconfigured). Without this, an iOS Safari tab that drops
    // the foreground socket (screen sleep, app background) leaves a
    // zombie client slot occupied; broadcasts keep flowing into the
    // dead lwIP send buffer until it fills, starving the live clients.
    //
    // Values: pingInterval=15s, pongTimeout=3s, missedPongsBeforeDrop=2.
    // Timing: first ping at t=15s, pong timeout at t≈18s (count=1); the
    // arduinoWebSockets impl rewinds lastPing to force an immediate
    // re-ping after a miss (WebSockets.cpp::handleHBTimeout), so the
    // second timeout fires at t≈21s (count=2 -> disconnect). Net:
    // dead-peer detection in ~21 s rather than ~33 s, which is fine —
    // more aggressive cleanup is the desired direction.
    DataServer.enableHeartbeat(15000, 3000, 2);
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

    // Read the AHRS output fields as one coherent frame from the
    // lock-free snapshot (published once per AHRS::Process() iteration
    // by ImuReadTask).  Every g_AHRS.* read in this function now comes
    // from this single snapshot, so the whole JSON frame is internally
    // consistent — pitch, VSI, AOA, accel and gyro all from the same
    // AHRS iteration.  read() is wait-free and never blocks the producer.
    const onspeed::ahrs::AhrsSnapshotPayload ahrsSnap =
        onspeed::ahrs::g_AhrsSnapshot.read();

    // Installation-corrected body-vertical acceleration (G), EMA-smoothed
    // by AHRS::Process to suppress per-tick IMU jitter.  The M5 wire path
    // ships the same filtered value at DisplaySerial.cpp — same source
    // here so the LiveView G readout doesn't twitch where the M5's holds
    // steady.  GLimitDecision still reads the unsmoothed AccelVertCorr
    // for over-G warnings; the smoothing is purely a presentation choice.
    float fVerticalGload = ahrsSnap.accelVertFilteredG;

    // AOA / DerivedAOA emit JSON null when bIasAlive is false, matching
    // IAS / percentLift.  `bIasAlive` is the canonical sensor-level
    // air-data validity signal (rising-edge 20 kt, falling 15 kt,
    // hysteresis; backed by the pitot deadband so it can't latch alive
    // on sensor noise alone).  Display validity is a sensor-level fact,
    // not a UX choice; `iMuteAudioUnderIAS` keeps its job in the audio
    // path (Audio.cpp) and no longer drives display gates.
    //
    // The wsClient consumer guard checks `typeof === 'number'` to reject
    // null, then `> AOA_NA_SENTINEL` (-20) as belt-and-suspenders against
    // any future numeric-sentinel drift.  See issues #358 and #455.
    fWifiAOA = g_Sensors.AOA;

    // Pitch, Roll, VSI, Flightpath
    if (g_Config.bCalSourceEfis)
    {

        // efis or VN-300 data
        if (g_EfisSerial.enType == EfisSerialPort::EnVN300)
        {
            // Atomic snapshot — Pitch/Roll/VelNedDown all from same frame.
            EfisSerialPort::SuVN300Data vn;
            g_EfisSerial.SnapshotVn300(vn);

            fWifiPitch = vn.Pitch;
            fWifiRoll  = vn.Roll;

            if (ahrsSnap.tasMps > 0)
            {
                // TAS is being updated in an interrupt
                fWifiFlightpath = rad2deg(safeAsin(-vn.VelNedDown/ahrsSnap.tasMps)); // vnVelNedDown is reversed (positive when descending)
            }
            else
                fWifiFlightpath = 0;

            fWifiVSI = mps2fpm(-vn.VelNedDown); // fpm
            fWifiIAS = g_Sensors.IAS;
        } // end enType = EnVN300

        else
        {
            // Atomic snapshot — Pitch/Roll/TAS/VSI/IAS all from same frame.
            EfisSerialPort::SuEfisData ef;
            g_EfisSerial.SnapshotEfis(ef);

            fWifiPitch = ef.Pitch;
            fWifiRoll  = ef.Roll;
            if (ef.TAS > 0)
            {
                // Use EFIS VSI (matches the EFIS pitch/roll above) instead of
                // KalmanVSI to avoid mixing data sources in the same JSON payload.
                const float fEfisVsiMps = fpm2mps(ef.VSI);
                fWifiFlightpath = rad2deg(safeAsin(fEfisVsiMps / kts2mps(ef.TAS)));
            }

            else
                if (ahrsSnap.tasMps > 0)
                {
                    fWifiFlightpath = rad2deg(safeAsin(ahrsSnap.kalmanVsiMps/ahrsSnap.tasMps)); // convert efiVSI from fpm to m/s
                }
                else
                    fWifiFlightpath=0;

            // kalmanVSI is being updated in an interrupt
            fWifiVSI = mps2fpm(ahrsSnap.kalmanVsiMps);
            fWifiIAS = ef.IAS;
        } // end if enType != EnVN300

    } // end if cal source is efis

    // Internal cal source
    else
    {
        // Internal data
        fWifiPitch      = ahrsSnap.pitchDeg;          // degrees
        fWifiRoll       = ahrsSnap.rollDeg;           // degrees
        fWifiFlightpath = ahrsSnap.flightPathDeg;     // degrees
        fWifiVSI        = mps2fpm(ahrsSnap.kalmanVsiMps); // fpm

        fWifiIAS = g_Sensors.IAS;
    } // end internal cal source

    // OAT source selection — shared with the M5 display-serial path so
    // both surfaces report the same value.  See
    // onspeed_core/efis/OatSelect.h for the decision rule and the
    // gates that lock the EFIS branch out when the EFIS feed is
    // disabled or stale.
    // Snapshot to read OAT atomically (single field, but the snapshot
    // is cheap; consistent with the pattern used everywhere else).
    EfisSerialPort::SuEfisData efOat;
    g_EfisSerial.SnapshotEfis(efOat);
    fWifiOAT = onspeed::efis::SelectDisplayOatC(
        g_Config.bCalSourceEfis,
        g_Config.bReadEfisData,
        g_EfisSerial.IsDataFresh(2000),
        g_Config.bOatSensor,
        efOat.OAT,
        g_Sensors.OatC);

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
    // AOA, DerivedAOA, IAS, percentLift, and DecelRate use %s placeholders
    // (rather than %.2f / %.1f) so the producer can emit JSON `null` for
    // those fields when air data is not valid (bIasAlive=false).  Consumer's
    // fmt() helper collapses null/undefined/NaN to '—'; the wsClient's
    // aoaIsValid check rejects null via `typeof === 'number'`.
    const char * szFormat =
        "{\"AOA\":%s,\"Pitch\":%.2f,\"Roll\":%.2f,\"IAS\":%s,\"PAlt\":%.2f,"
        "\"verticalGLoad\":%.2f,\"lateralGLoad\":%.2f,"
        "\"flapsPos\":%i,\"flapIndex\":%i,"
        "\"flapsMinDeg\":%i,\"flapsMaxDeg\":%i,"
        "\"coeffP\":%.2f,\"dataMark\":%i,\"kalmanVSI\":%.2f,\"flightPath\":%.2f,"
        "\"PitchRate\":%.2f,\"DecelRate\":%s,\"OAT\":%.2f,\"DerivedAOA\":%s,"
        "\"gOnsetRate\":%.2f,"
        "\"percentLift\":%s,\"tonesOnPctLift\":%i,\"onSpeedFastPctLift\":%i,"
        "\"onSpeedSlowPctLift\":%i,\"stallWarnPctLift\":%i,\"pipPctLift\":%i}";

    // Ensure JSON never contains invalid numeric tokens like "nan"/"inf".
    fWifiPitch      = SafeJsonFloat(fWifiPitch, 0.0f);
    fWifiRoll       = SafeJsonFloat(fWifiRoll, 0.0f);
    fWifiIAS        = SafeJsonFloat(fWifiIAS, 0.0f);
    fWifiVSI        = SafeJsonFloat(fWifiVSI, 0.0f);
    fWifiFlightpath = SafeJsonFloat(fWifiFlightpath, 0.0f);
    fVerticalGload  = SafeJsonFloat(fVerticalGload, 0.0f);
    fWifiOAT        = SafeJsonFloat(fWifiOAT, 0.0f);

    const float fPAltFt = SafeJsonFloat(m2ft(ahrsSnap.kalmanAltMeters), 0.0f);
    // Lateral G: smoothed by AccelLatFilter, raw sign (positive = right
    // per the EKFQ body-axis convention).  The legacy /live AOA tab and
    // the new /indexer data-table both display this number as "Lat G"
    // unmodified, so the JSON layer carries the engineering convention.
    // The M5 wire-format builder applies its own negation (DisplaySerial.cpp:
    // 294,342) so the binary mirror still ships positive=leftward — that
    // is a wire-format-only flip and must not leak into the JSON channel.
    // The JS slip-ball consumer applies the same wire-format negation
    // locally so the ball deflects in the conventional direction
    // (rightward G → ball moves left, "step on the ball").
    const float fLatG   = SafeJsonFloat(ahrsSnap.accelLatFilteredG, 0.0f);
    const float fCoeffP = SafeJsonFloat(g_fCoeffP, 0.0f);
    const float fPitchRate  = SafeJsonFloat(ahrsSnap.gPitchDps, 0.0f);
    const float fDecelRate  = SafeJsonFloat(g_Sensors.fDecelRate, 0.0f);

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
        {
        // Lock-free read of the coherent flap frame (published by
        // Flaps::Update / HandleConfigSave).  One read() gives the whole
        // vector + active index/position + raw ADC from a single revision.
        // No xAhrsMutex.
        const onspeed::ahrs::FlapSnapshotPayload fs =
            onspeed::ahrs::g_FlapSnapshot.read();
        const size_t nFlaps = fs.nFlaps;
        const int    iIdx   = fs.iIndex;
        if (fs.bValid && iIdx >= 0 && (size_t)iIdx < nFlaps)
            {
            flapSnapshot = fs.aFlaps[iIdx];
            iSnapFlapIdx = iIdx;
            iSnapFlapPos = fs.iPosition;
            bSnapValid   = true;
            }

        // Copy the full flap vector + raw lever ADC for the
        // interpolated-anchor pass.  Capped at MAX_AOA_CURVES; same
        // bound the config parser and flap detector enforce.
        const size_t nCopy = std::min<size_t>(nFlaps,
                                              static_cast<size_t>(onspeed::MAX_AOA_CURVES));
        for (size_t i = 0; i < nCopy; ++i)
            aFlapsSnapshot[i] = fs.aFlaps[i];
        nFlapsSnapshot = nCopy;
        uFlapsRawAdc   = fs.uValue;
        }

    // Snapshot the live AOA / IAS once.  Without this snapshot we'd
    // read g_Sensors.AOA twice (once for fWifiAOA earlier, once for
    // ComputePercentLift below) and g_Sensors.IAS twice (mute gate
    // earlier, percent-lift IAS gate below) across an unguarded
    // multi-tick window, so the displayed numeric AOA and the
    // percent-lift bar could disagree by one sample tick.  Cheap
    // local snapshot keeps them coherent.
    const float fAoaSnap         = g_Sensors.AOA;
    // Match the AOA gate above and the M5 wire-format gate in
    // DisplaySerial.cpp: display validity rides on bIasAlive (sensor-
    // level), not the audio mute threshold (UX-level).  See #358.
    const bool bIasValidForOutput = g_Sensors.bIasAlive;

    // Live percent-lift reading — the active-detent calibration is
    // what the audio path uses, so this matches what the pilot hears.
    // Float in whole-percent units (0.0..99.9) so LiveView can render
    // the index bar at sub-percent fidelity (matches the wire-tenths
    // resolution available on the M5 wire).
    float fJsonPercentLiftPct = 0.0f;
    if (bSnapValid)
        {
        fJsonPercentLiftPct = ComputePercentLift(fAoaSnap, flapSnapshot, bIasValidForOutput);
        }

    // Display percent anchors (Vac, ld_max.pdf §8 — aerodynamic
    // references and operational cues must remain independent):
    //   * tonesOnPctLift SNAPS to the active detent's L/Dmax pct.
    //     LiveView's bottom chevron + the audio low-tone gate fire
    //     from this same threshold, in lockstep.  Operational cue.
    //   * onSpeedFast/Slow/StallWarn SNAP to the active detent so the
    //     donut/chevron screen positions stay in lockstep with the
    //     audio cues at those same calibrated thresholds.
    //   * pipPctLift INTERPOLATES linearly clean→fullflap (ignores
    //     intermediate detents).  LiveView reads this for the L/Dmax
    //     pip dot position so the pip slides smoothly with the lever.
    //     Visual aerodynamic reference.
    //   * flapsDeg interpolates per-bracket so the numeric flap-angle
    //     readout in LiveView's corner slides smoothly.
    // Same contract the M5 wire ships, so the M5 indexer and the web
    // LiveView render identically off either transport.  iasValid=true
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

    // Min/max flap angle across configured detents — Mode 0's flap
    // circle widget normalizes the triangle sweep over [min, max]
    // (flapWidget.js::flapWidgetFrac).  When no detents are configured,
    // emit 0/33 to match the prototype's defaults so the widget still
    // renders sensibly on an uncalibrated unit.
    int iJsonFlapsMinDeg = 0;
    int iJsonFlapsMaxDeg = 33;
    if (nFlapsSnapshot > 0)
        {
        iJsonFlapsMinDeg = aFlapsSnapshot[0].iDegrees;
        iJsonFlapsMaxDeg = aFlapsSnapshot[0].iDegrees;
        for (size_t i = 1; i < nFlapsSnapshot; ++i)
            {
            const int d = aFlapsSnapshot[i].iDegrees;
            if (d < iJsonFlapsMinDeg) iJsonFlapsMinDeg = d;
            if (d > iJsonFlapsMaxDeg) iJsonFlapsMaxDeg = d;
            }
        }

    // G-onset rate filtered in AHRS (250 ms tau).  Same value the
    // M5 wire format reads, so the M5 hardware indicator and the
    // LiveView's right-edge tape stay in lockstep.
    const float fGOnsetRate = SafeJsonFloat(ahrsSnap.gOnsetRate, 0.0f);

    // Format the AOA / DerivedAOA / IAS / percentLift values as strings
    // — JSON `null` when air data is invalid (so the consumer's fmt()
    // helper dashes them) or the formatted float otherwise.  AOA and
    // DerivedAOA also dash on a NaN source value (e.g. uninitialized
    // AHRS).  16-byte staging buffers hold the longest expected output.
    char szAoa[16];
    char szDerivedAoa[16];
    char szIas[16];
    char szPctLift[16];
    char szDecelRate[16];
    if (bIasValidForOutput && IsFiniteFloat(fWifiAOA))
        snprintf(szAoa, sizeof(szAoa), "%.2f", fWifiAOA);
    else
        std::strcpy(szAoa, "null");
    // Single read so the IsFiniteFloat check and the snprintf format the same value (TOCTOU-free).
    const float fDerivedAoaSnap = ahrsSnap.derivedAoaDeg;
    if (bIasValidForOutput && IsFiniteFloat(fDerivedAoaSnap))
        snprintf(szDerivedAoa, sizeof(szDerivedAoa), "%.2f", fDerivedAoaSnap);
    else
        std::strcpy(szDerivedAoa, "null");
    if (bIasValidForOutput)
        {
        snprintf(szIas,     sizeof(szIas),     "%.2f", fWifiIAS);
        snprintf(szPctLift, sizeof(szPctLift), "%.1f", fJsonPercentLiftPct);
        snprintf(szDecelRate, sizeof(szDecelRate), "%.2f", fDecelRate);
        }
    else
        {
        std::strcpy(szIas,        "null");
        std::strcpy(szPctLift,    "null");
        std::strcpy(szDecelRate,  "null");
        }

    // szFormat is a compile-time constant split across lines for readability.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    int iChars = snprintf(
        pOut,
        uOutSize,
        szFormat,
        szAoa,
        fWifiPitch,
        fWifiRoll,
        szIas,
        fPAltFt,
        fVerticalGload,
        fLatG,
        iJsonFlapsPos,
        iSnapFlapIdx,
        iJsonFlapsMinDeg,
        iJsonFlapsMaxDeg,
        fCoeffP,
        g_iDataMark,
        fWifiVSI,
        fWifiFlightpath,
        fPitchRate,
        szDecelRate,
        fWifiOAT,
        szDerivedAoa,
        fGOnsetRate,
        szPctLift,
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
