
#include <Arduino.h>

#include <algorithm>

#include "src/Globals.h"
#include <aoa/DisplayPctAnchors.h>
#include <aoa/PercentLift.h>
#include <filters/GOnsetFilter.h>
#include <proto/DisplaySerial.h>
#include <sensors/SpinDetector.h>

using onspeed::GOnsetFilter;
using onspeed::SpinDetector;
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

    int     iPercentLift;
    float   fDisplayAOA;
    float   fDisplayIAS;
    int     iDisplayVerticalG;

    fDisplayIAS = g_Sensors.IAS;
    // Display-serial gate uses the user-tunable `iMuteAudioUnderIAS`
    // rather than the sensor-level `bIasAlive` flag (see DataServer.cpp
    // for the full rationale): the pilot's configured "below which
    // nothing should show up" choice is the right UX threshold for
    // external displays too.
    const bool bIasValidForOutput = (fDisplayIAS >= g_Config.iMuteAudioUnderIAS);
    const float fIasForOutput = bIasValidForOutput ? fDisplayIAS : 0.0f;
    const float fPAltFt = m2ft(g_AHRS.KalmanAlt);

    const float fAccelVert = g_AHRS.AccelVertFilter.get();
    if (IsFiniteFloat(fAccelVert))
        iDisplayVerticalG = (int)ceilf(fAccelVert * 10.0f);
    else
        iDisplayVerticalG = 0;

    // G-onset rate: low-pass-filtered d(verticalG)/dt for the M5's right-edge
    // rate-tape widget. Tau = 250 ms; ticked at the wire rate (20 Hz) means
    // alpha ≈ 0.167 per sample — heavy enough to reject single-sample noise,
    // responsive enough to catch a half-second pull. Sign convention follows
    // AccelVertFilter (production reaction-force convention: +1 g level,
    // +2 g pull-up), so positive output means "G load increasing".
    static GOnsetFilter sGOnsetFilter(0.25f);
    static uint32_t     sLastOnsetMicros = 0;
    const uint32_t      uNowMicros = micros();
    const float fGOnsetRate = (sLastOnsetMicros == 0)
        ? sGOnsetFilter.Update(fAccelVert, 1.0f)  // seed; dt ignored on first call
        : sGOnsetFilter.Update(fAccelVert,
                               (uNowMicros - sLastOnsetMicros) * 1.0e-6f);
    sLastOnsetMicros = uNowMicros;


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

    // PercentLift is computed by the shared core helper so the M5 display
    // and any future native consumer get the identical 0..99 scalar.  See
    // onspeed_core/aoa/PercentLift.h for the honest single-linear formula.
    if (bFlapSnapshotValid)
        iPercentLift = ComputePercentLift(g_Sensors.AOA,
                                          flapSnapshot,
                                          bIasValidForOutput);
    else
        iPercentLift = 0;

    // Spin-recovery cue: latched directional rudder cue derived from
    // body-frame yaw rate and stalled AOA.  Algorithm and provenance
    // (F/A-18 SRM with three GA deltas) live in
    // onspeed_core/sensors/SpinDetector.h.  The detector is a static
    // local since it is owned by this task and ticked once per wire
    // frame; the resulting cue is published to g_iSpinRecoveryCue for
    // the LiveView consumer in DataServer.cpp.
    //
    // Without a valid flap snapshot we cannot evaluate the AOA gate;
    // emit 0 (no cue) and skip the detector update so its filter
    // doesn't drift from no-input ticks.
    static SpinDetector sSpinDetector;
    static uint32_t     sLastSpinMicros = 0;
    int                 iSpinCue        = 0;
    if (bFlapSnapshotValid) {
        const uint32_t uSpinNowMicros = micros();
        const float fSpinDt = (sLastSpinMicros == 0)
            ? (kDisplaySerialPeriodMs * 1.0e-3f)
            : ((uSpinNowMicros - sLastSpinMicros) * 1.0e-6f);
        sLastSpinMicros = uSpinNowMicros;

        iSpinCue = sSpinDetector.Update(fSpinDt,
                                        g_AHRS.gYaw,
                                        g_Sensors.AOA,
                                        flapSnapshot.fSTALLAOA);
    }
    g_iSpinRecoveryCue = iSpinCue;

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
    //     OnSpeed-band center.  Slides smoothly with the lever; ignores
    //     intermediate detents.  Visual aerodynamic reference.
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
        const int      iLatG100   = SafeScaledInt(-g_AHRS.AccelLatFilter.get(),  100.0f, -99,      99);
        const int      iVertG10   = ClampInt(iDisplayVerticalG,                -99,      99);
        const unsigned uPctLift   = ClampUInt((unsigned)iPercentLift,           0,       99);

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
        // documented there. BuildDisplayFrame produces the same bytes that
        // the previous hand-coded snprintf/CRC block did for any given set
        // of inputs.

        const int iOATc = g_Config.bOatSensor ? int(g_Sensors.OatC) : 0;

        DisplayBuildInputs inputs;
        inputs.pitchDeg           = g_AHRS.SmoothedPitch;
        inputs.rollDeg            = g_AHRS.SmoothedRoll;
        inputs.iasKt              = fIasForOutput;
        inputs.paltFt             = fPAltFt;
        inputs.turnRateDps        = g_AHRS.gYaw;
        inputs.lateralG           = -g_AHRS.AccelLatFilter.get();  // negated: positive = leftward
        inputs.verticalGScaled10  = static_cast<float>(iDisplayVerticalG);
        inputs.percentLift        = iPercentLift;
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
        inputs.spinRecoveryCue    = iSpinCue;
        inputs.dataMark           = (int)((unsigned)g_iDataMark % 100u);

        uint8_t frameBuf[kDisplayFrameSizeBytes];
        const size_t nBytes = BuildDisplayFrame(inputs, frameBuf, sizeof(frameBuf));

        if (nBytes != kDisplayFrameSizeBytes)
            return;

        // Write the complete frame (already includes CRC + CRLF).
        pSerial->write(frameBuf, kDisplayFrameSizeBytes);
        } // end if ONSPEED

    } // end Write()
