
#include <Arduino.h>

#include <algorithm>

#include "src/Globals.h"
#include "../web_server/DataServer.h"
#include <aoa/DisplayPctAnchors.h>
#include <aoa/PercentLift.h>
#include <efis/OatSelect.h>
#include <proto/DisplaySerial.h>

using onspeed::m2ft;
using onspeed::mps2fpm;
using onspeed::aoa::ComputeDisplayPctAnchors;
using onspeed::aoa::ComputePercentLift;
using onspeed::aoa::DisplayPctAnchors;
using onspeed::proto::DisplayBuildInputs;
using onspeed::proto::BuildDisplayFrame;
using onspeed::proto::kDisplayFrameSizeBytes;


static inline bool IsFiniteFloat(float v)
{
    return !isnan(v) && !isinf(v);
}

static inline int ClampInt(int value, int minValue, int maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static inline unsigned ClampUInt(unsigned value, unsigned minValue, unsigned maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static inline int SafeScaledInt(float value, float scale, int minValue, int maxValue)
{
    if (!IsFiniteFloat(value))
        return 0;

    // Preserve original behavior (truncate toward zero) but clamp to the
    // fixed-width protocol field size to prevent message length changes.
    long scaled = (long)(value * scale);
    if (scaled < minValue) scaled = minValue;
    if (scaled > maxValue) scaled = maxValue;
    return (int)scaled;
}

static inline unsigned SafeScaledUInt(float value, float scale, unsigned minValue, unsigned maxValue)
{
    if (!IsFiniteFloat(value))
        return minValue;

    long scaled = (long)(value * scale);
    if (scaled < (long)minValue) scaled = (long)minValue;
    if (scaled > (long)maxValue) scaled = (long)maxValue;
    return (unsigned)scaled;
}

// ----------------------------------------------------------------------------

// FreeRTOS task for writing display data

void WriteDisplayDataTask(void * pvParams)
    {
    BaseType_t      xWasDelayed;
    TickType_t      xLastWakeTime = xTaskGetTickCount();
    static unsigned long uLastLateLogMs = 0;

    while (true)
        {
        // No delay happening is a design error so flag it if it happens
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kDisplaySerialPeriodMs));
        if (xWasDelayed == pdFALSE)
            {
            // If this task runs late, don't "catch up" by running back-to-back and
            // bursting serial data at the display. Re-align to the current tick
            // period instead.
            xLastWakeTime = xLAST_TICK_TIME(kDisplaySerialPeriodMs);
            unsigned long uNow = millis();
            if ((uNow - uLastLateLogMs) > 1000)
                {
                g_Log.println(MsgLog::EnDisplay, MsgLog::EnWarning, "WriteDisplayDataTask Late");
                uLastLateLogMs = uNow;
                }
            }

        g_DisplaySerial.Write();
        }

    } // end WriteSerialDataTask()


// ============================================================================

DisplaySerial::DisplaySerial()
{

}

// ----------------------------------------------------------------------------

void DisplaySerial::Init(Stream * pDispSerial)
{

    // In the original G2V3 implementation the panel output port could be
    // selected. In this implementation panel output is a fixed serial
    // port. But to easily allow run time configuration I have made the
    // serial port a pointer to the Stream base object.

    pSerial = pDispSerial;

}

// ----------------------------------------------------------------------------

void DisplaySerial::Write()
    {
    byte            SerialCRC = 0;

//    if (serialOutPort!="NONE" && millis()-serialoutLastUpdate>50) // update every 50ms, 20Hz

    char    serialOutString[200];

    float   fPercentLiftPct;      // 0.0..99.9, whole percent (wire encoder scales ×10 to tenths)
    float   fDisplayAOA;
    float   fDisplayIAS;
    int     iDisplayVerticalG;

    fDisplayIAS = g_Sensors.IAS;
    // Display-serial gate uses the sensor-level `bIasAlive` flag — the
    // canonical "air data is valid" signal (rising-edge 20 kt, falling
    // 15 kt, hysteresis; backed by the pitot deadband so it can't latch
    // alive on sensor noise alone).  Display validity is a sensor-level
    // fact, not a UX choice: the audio-mute threshold (iMuteAudioUnderIAS)
    // controls when tones play, not whether displayed values are
    // trustworthy.  See issue #358.
    const bool bIasValidForOutput = g_Sensors.bIasAlive;
    const float fIasForOutput = bIasValidForOutput ? fDisplayIAS : 0.0f;
    const float fPAltFt = m2ft(g_AHRS.KalmanAlt);

    const float fAccelVert = g_AHRS.AccelVertFilter.get();
    if (IsFiniteFloat(fAccelVert))
        // Round-to-nearest tenth so the M5 / indexer display matches the
        // LiveView's `verticalGLoad`.  Over-G alerting reads the unrounded
        // float in GLimitDecision (Housekeeping path), so the encoding
        // choice here is purely a presentation concern.
        iDisplayVerticalG = (int)lroundf(fAccelVert * 10.0f);
    else
        iDisplayVerticalG = 0;

    // G-onset rate: read the smoothed signal from AHRS, where the
    // GOnsetFilter lives now so DataServer (LiveView JSON) and this
    // wire-format consumer share one source.  See AHRS::Process().
    const float fGOnsetRate = g_AHRS.gOnsetRate;


    // Snapshot the active flap entry plus the full flap vector once
    // under xAhrsMutex.  Two consumers below read this snapshot:
    //
    //   - ComputePercentLift on the snapped active detent — drives the
    //     live `percentLift` reading from the current AOA.  Audio
    //     compares against this same active-detent calibration, so the
    //     live percent stays consistent with the audio path.
    //
    //   - ComputeDisplayPctAnchors on the entire vector + the raw
    //     lever ADC — drives the four interpolated percent anchors
    //     (`tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`,
    //     `stallWarnPctLift`) and the interpolated `flapsDeg`.  These
    //     slide smoothly between adjacent detents during flap
    //     deployment so the indexer pip and band edges track the
    //     aerodynamic transition rather than jumping at the midpoint.
    //
    // HandleConfigSave on Core 0 swaps g_Config.aFlaps under the same
    // mutex; without this snapshot the producer could see the vector
    // shrink mid-frame (4->2 detents) and index past the new vector's
    // end, or mix old-and-new setpoints in a single frame.  20 Hz
    // cadence has plenty of headroom for a 10 ms timeout; on timeout
    // we emit zero setpoints (display reads "uncalibrated").
    FOSConfig::SuFlaps flapSnapshot{};
    bool bFlapSnapshotValid = false;
    int  iFlapsMinDeg       = 0;
    int  iFlapsMaxDeg       = 0;

    // MAX_AOA_CURVES is the canonical cap for flap entries; the flap
    // detector and config parser already enforce the same bound.
    FOSConfig::SuFlaps aFlapsSnapshot[onspeed::MAX_AOA_CURVES]{};
    size_t  nFlapsSnapshot = 0;
    size_t  iActiveFlapIdx = 0;
    uint16_t uFlapsRawAdc  = 0;
    if (xSemaphoreTake(xAhrsMutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
        const size_t nFlaps = g_Config.aFlaps.size();
        const int    iIdx   = g_Flaps.iIndex;
        if (iIdx >= 0 && (size_t)iIdx < nFlaps)
            {
            flapSnapshot       = g_Config.aFlaps[iIdx];
            bFlapSnapshotValid = true;
            iActiveFlapIdx     = static_cast<size_t>(iIdx);
            }

        // Capture the configured flap travel range for the display widget
        // endpoints.  aFlaps is sorted by degree at config-load time, so
        // first/last entries are min/max; do not assume otherwise here.
        if (nFlaps > 0)
            {
            int iMin = g_Config.aFlaps[0].iDegrees;
            int iMax = iMin;
            for (size_t i = 1; i < nFlaps; ++i)
                {
                const int d = g_Config.aFlaps[i].iDegrees;
                if (d < iMin) iMin = d;
                if (d > iMax) iMax = d;
                }
            iFlapsMinDeg = iMin;
            iFlapsMaxDeg = iMax;
            }

        // Copy the full flap vector for the interpolated-anchor pass.
        // Capped at MAX_AOA_CURVES; configurations larger than this (none
        // in practice) get truncated to the first N — interpolation still
        // produces a continuous curve over those entries.
        const size_t nCopy = std::min<size_t>(nFlaps,
                                              static_cast<size_t>(onspeed::MAX_AOA_CURVES));
        for (size_t i = 0; i < nCopy; ++i)
            aFlapsSnapshot[i] = g_Config.aFlaps[i];
        nFlapsSnapshot = nCopy;

        // Raw flap-lever ADC reading for the interpolation.  g_Flaps.uValue
        // is a uint16_t written outside this mutex by Flaps::Update() at
        // 1 Hz from SensorIO; aligned 16-bit stores are atomic on the
        // ESP32-S3 so a torn read is impossible regardless of mutex state.
        // Reading inside this window is for consistency with the rest of
        // the snapshot rather than for synchronization.
        uFlapsRawAdc = g_Flaps.uValue;
        xSemaphoreGive(xAhrsMutex);
        }

    // PercentLift in whole-percent float (0.0..99.9). The wire encoder
    // (BuildDisplayFrame in onspeed_core) multiplies by 10 and truncates
    // to int for the `%03u` tenths field; the band-edge anchors below
    // stay at integer percents.  See onspeed_core/aoa/PercentLift.h for
    // the honest single-linear formula.  The G3X `=11` subset further
    // down rounds down to integer to keep its own integer-percent
    // contract.
    if (bFlapSnapshotValid)
        fPercentLiftPct = ComputePercentLift(g_Sensors.AOA,
                                             flapSnapshot,
                                             bIasValidForOutput);
    else
        fPercentLiftPct = 0.0f;

    // Display percent anchors for the M5 indexer (Vac, ld_max.pdf §8 —
    // aerodynamic references and operational cues must remain
    // independent):
    //   * tonesOnPctLift — SNAPPED to active detent's L/Dmax pct.  The
    //     M5 bottom chevron gates on this; matches the audio low-tone
    //     gate exactly.  Operational cue.
    //   * onSpeedFast/Slow/StallWarn — SNAPPED to active detent so the
    //     donut/chevron screen positions stay in lockstep with the
    //     audio cues.  Operational cues.
    //   * pipPctLift — INTERPOLATED linearly across the entire pot range
    //     from cleanest detent's L/Dmax pct to most-deployed detent's
    //     bottom-half-of-donut target ((3*fast + slow) / 4).  Slides
    //     smoothly with the lever; ignores intermediate detents.  Visual
    //     aerodynamic reference.
    //   * flapsDeg — INTERPOLATED per-bracket between adjacent detents'
    //     iDegrees so the numeric flap-angle readout slides smoothly.
    //     Mechanical cue, distinct from pipPctLift.
    //
    // iasValid=true is forwarded so the anchor geometry stays stable
    // across the audio mute threshold; only the live `percentLift`
    // reading above gates on bIasValidForOutput.
    DisplayPctAnchors anchors = ComputeDisplayPctAnchors(uFlapsRawAdc,
                                                         aFlapsSnapshot,
                                                         nFlapsSnapshot,
                                                         iActiveFlapIdx,
                                                         true);

    if (bIasValidForOutput)
        fDisplayAOA = g_Sensors.AOA;
    else
        fDisplayAOA = 0;
    (void)fDisplayAOA;   // body-angle AOA is no longer on the wire

    // Output the data in the appropriate format

    if (g_Config.enSerialOutFormat == FOSConfig::EnSerialFmtG3X)
        {
        // Clamp to fixed-width protocol fields to prevent buffer overruns and
        // malformed output when values go out of range.
        const int      iPitch10   = SafeScaledInt(g_AHRS.SmoothedPitch, 10.0f, -999,    999);
        const int      iRoll10    = SafeScaledInt(g_AHRS.SmoothedRoll,  10.0f, -9999,  9999);
        const unsigned uIas10     = SafeScaledUInt(fIasForOutput,       10.0f, 0,      9999);
        const int      iPaltFt    = SafeScaledInt(fPAltFt,         1.0f, -99999, 99999);
        // G3X format negates intentionally — the Garmin `=11` wire's
        // lateralG field has its own convention separate from the `#1`
        // wire's body-frame at v4.23.  Pilots with Garmin G3X EFIS
        // receiving OnSpeed output rely on this negation to match what
        // their EFIS already expects from a Garmin-format AHRS source.
        // Do NOT remove without coordinating with Garmin G3X users.
        const int      iLatG100   = SafeScaledInt(-g_AHRS.AccelLatFilter.get(),  100.0f, -99,      99);
        const int      iVertG10   = ClampInt(iDisplayVerticalG,                -99,      99);
        // G3X format keeps integer percent (0..99) on its own wire.
        // Use SafeScaledInt (which guards against NaN/Inf and out-of-
        // range floats) instead of a direct float-to-unsigned cast,
        // since `static_cast<unsigned>(NaN)` is UB.  Truncation matches
        // the v4.22 int-tenths-then-/10 path.
        const int      iPctLiftG3X = SafeScaledInt(fPercentLiftPct, 1.0f, 0, 99);
        const unsigned uPctLift    = static_cast<unsigned>(iPctLiftG3X);

        const int iChars = snprintf(
            serialOutString,
            sizeof(serialOutString),
            "=1100000000%+04i%+05i___%04u%+06i____%+03i%+03i%02u__________",
            iPitch10,
            iRoll10,
            uIas10,
            iPaltFt,
            iLatG100,
            iVertG10,
            uPctLift);

        // Expected fixed-length payload is 55 bytes.
        if (iChars != 55)
            return;

        SerialCRC = 0x00;
        for (int i = 0; i < 55; i++)
            SerialCRC += (byte)serialOutString[i];

        // Send data out the appropriate serial port
        pSerial->print(serialOutString);
        pSerial->printf("%02X",SerialCRC);
        pSerial->println();
        } // end if G3X

    else if (g_Config.enSerialOutFormat == FOSConfig::EnSerialFmtOnSpeed)
        {
        // Build the #1 frame via the shared core module so the layout
        // is defined in exactly one place (onspeed_core/proto/DisplaySerial.h).
        //
        // The per-field scale factors, clamps, and sign conventions are
        // documented there.

        // OAT source selection — shared with the WebSocket JSON path so
        // both surfaces report the same value.  See
        // onspeed_core/efis/OatSelect.h for the decision rule and the
        // gates that lock the EFIS branch out when the EFIS feed is
        // disabled or stale.
        const float fOatC = onspeed::efis::SelectDisplayOatC(
            g_Config.bCalSourceEfis,
            g_Config.bReadEfisData,
            g_EfisSerial.IsDataFresh(2000),
            g_Config.bOatSensor,
            g_EfisSerial.suEfis.OAT,
            g_Sensors.OatC);
        // Round to nearest integer so the M5 reads the same whole
        // degrees the LiveView shows (which keeps the fractional part).
        // Truncating would bias every reading half a degree colder than
        // the JSON.  Proto's BuildFrame clamps the result to [-99, +99]
        // before emitting the %+03d field.
        const int iOATc = static_cast<int>(lroundf(fOatC));

        DisplayBuildInputs inputs;
        inputs.pitchDeg           = g_AHRS.SmoothedPitch;
        inputs.rollDeg            = g_AHRS.SmoothedRoll;
        inputs.iasKt              = fIasForOutput;
        // Sentinel-encode IAS on the wire when air data is not valid;
        // the M5 consumer renders "--" for IAS and percentLift digits.
        // See proto/DisplaySerial.h for the iasValid contract.
        inputs.iasValid           = bIasValidForOutput;
        inputs.paltFt             = fPAltFt;
        inputs.turnRateDps        = g_AHRS.gYaw;
        // Body-frame: positive = airframe accelerating rightward, matching
        // the IMU, SD log, and WebSocket JSON conventions. Slip-skid ball
        // renderers negate locally at the rendering site (the M5's
        // SerialRead::SerialProcess does, and the LiveView's slipBall.js
        // does the same). See DisplaySerial.h's DisplayBuildInputs::lateralG block and #383.
        inputs.lateralG           = g_AHRS.AccelLatFilter.get();
        inputs.verticalGScaled10  = static_cast<float>(iDisplayVerticalG);
        inputs.percentLiftPct     = fPercentLiftPct;     // 0.0..99.9, encoder scales ×10 to tenths
        inputs.vsiFpm10           = ClampInt(
                                        (int)floorf(mps2fpm(g_AHRS.KalmanVSI) / 10.0f),
                                        -999, 999);
        inputs.oatC               = iOATc;
        inputs.flightPathDeg      = g_AHRS.FlightPath;
        // flapsDeg is the lever-interpolated value from
        // ComputeDisplayPctAnchors, so the displayed flap angle slides
        // smoothly with the lever rather than stepping at the snap
        // midpoint.  When the flap snapshot is empty (uncalibrated),
        // anchors.flapsDeg is 0; fall through to the snapped
        // g_Flaps.iPosition for that case to preserve the prior
        // "uncalibrated" display shape.
        inputs.flapsDeg           = (nFlapsSnapshot > 0)
                                        ? anchors.flapsDeg
                                        : (int)g_Flaps.iPosition;
        // Per-flap band-edge percents — the consumer's calibrated
        // indexer anchors.  tonesOnPctLift and the band edges all snap
        // to the active detent (they drive audio cues and the donut /
        // chevron snap with iIndex transitions, in lockstep with the
        // audio path).  pipPctLift slides smoothly clean→fullflap.
        // See onspeed_core/aoa/DisplayPctAnchors.h for the design rule.
        inputs.tonesOnPctLift     = anchors.tonesOnPctLift;
        inputs.onSpeedFastPctLift = anchors.onSpeedFastPctLift;
        inputs.onSpeedSlowPctLift = anchors.onSpeedSlowPctLift;
        inputs.stallWarnPctLift   = anchors.stallWarnPctLift;
        inputs.pipPctLift         = anchors.pipPctLift;
        inputs.flapsMinDeg        = iFlapsMinDeg;
        inputs.flapsMaxDeg        = iFlapsMaxDeg;
        inputs.gOnsetRate         = fGOnsetRate;
        inputs.spinRecoveryCue    = 0;
        inputs.dataMark           = (int)((unsigned)g_iDataMark % 100u);

        uint8_t frameBuf[kDisplayFrameSizeBytes];
        const size_t nBytes = BuildDisplayFrame(inputs, frameBuf, sizeof(frameBuf));

        if (nBytes != kDisplayFrameSizeBytes)
            return;

        // Write the complete frame (already includes CRC + CRLF).
        pSerial->write(frameBuf, kDisplayFrameSizeBytes);
        } // end if ONSPEED

    } // end Write()
