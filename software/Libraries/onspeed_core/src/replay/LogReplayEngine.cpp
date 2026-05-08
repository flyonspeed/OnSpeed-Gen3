// replay/LogReplayEngine.cpp — platform-free per-row pipeline for SD log replay.
//
// See LogReplayEngine.h for the architecture overview.
//
// This implementation mirrors ReadLogLine() in
// software/sketch_common/src/tasks/LogReplay.cpp, extracted verbatim
// except that the global side-effects (g_Sensors.*, g_Flaps.*, g_AHRS.*,
// g_pIMU->*, g_fCoeffP, g_iDataMark) are written into a ReplayStepResult
// returned by value instead.  The calling task wrapper publishes that
// struct into the sketch globals and then calls
// g_AudioPlay.UpdateTones(SnapshotActiveFlap()).
//
// Flap-pot ADC synthesis (Sub-task 3 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md):
// When the log lacks the flapsRawADC column, prepare() synthesizes ADC values
// with a two-pass algorithm that mirrors tools/web/lib/replay/logReplay.js::
// synthLeverSweep. Pass 1 fills steady-state values; pass 2 paints smoothstep
// transition windows. Math is ported verbatim from the JS reference.

#include <replay/LogReplayEngine.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <util/OnSpeedTypes.h>

using onspeed::pressureCoeff;
using onspeed::fpm2mps;
using onspeed::AOACalculatorResult;
using onspeed::config::OnSpeedConfig;

namespace onspeed::replay {

// ============================================================================
// Internal helpers
// ============================================================================

// Standard smoothstep: maps [0,1] -> [0,1] via 3t² - 2t³.
// Matches the JS reference: function smoothstep(u) { return u*u*(3-2*u); }
static float Smoothstep(float u)
{
    if (u <= 0.0f) return 0.0f;
    if (u >= 1.0f) return 1.0f;
    return u * u * (3.0f - 2.0f * u);
}

// ============================================================================

LogReplayEngine::LogReplayEngine(const OnSpeedConfig& cfg,
                                 int  logSampleRateHz,
                                 bool flapsRawAdcAvailable)
    : cfg_(cfg)
    , logSampleRateHz_(logSampleRateHz)
    , flapsRawAdcAvailable_(flapsRawAdcAvailable)
    , aoaCalc_(cfg.iAoaSmoothing)
    , synthAdc_()
    , synthAdcIdx_(0)
{
}

// ============================================================================

void LogReplayEngine::reset()
{
    aoaCalc_.reset();
    synthAdcIdx_ = 0;
    // synthAdc_ is intentionally preserved: reset() clears state for a new
    // replay session over the SAME rows. If different rows need to be
    // replayed, call prepare() again after reset().
}

// ============================================================================

int LogReplayEngine::ResolveFlapIndex_(int flapPosDeg) const
{
    for (int i = 0; i < (int)cfg_.aFlaps.size(); i++)
    {
        if (flapPosDeg == cfg_.aFlaps[i].iDegrees)
            return i;
    }
    return 0;   // no match: fall back to first entry (same as task)
}

// ============================================================================

int LogReplayEngine::PotPositionForFlap_(int flapPosDeg) const
{
    for (const auto& f : cfg_.aFlaps)
    {
        if (flapPosDeg == f.iDegrees)
            return f.iPotPosition;
    }
    return cfg_.aFlaps.empty() ? 0 : cfg_.aFlaps[0].iPotPosition;
}

// ============================================================================

void LogReplayEngine::prepare(const std::vector<onspeed::LogRow>& rows)
{
    synthAdcIdx_ = 0;

    if (flapsRawAdcAvailable_)
    {
        // Log has the column — no synth needed.
        synthAdc_.clear();
        return;
    }

    const size_t N = rows.size();
    if (N == 0)
    {
        synthAdc_.clear();
        return;
    }

    // Estimate sample period from the log rate. logSampleRateHz_ is 50 or
    // 208; kSynthHalfWindowSec is in seconds. Convert to ticks.
    const float dtSec = (logSampleRateHz_ > 0)
                        ? (1.0f / static_cast<float>(logSampleRateHz_))
                        : (1.0f / 50.0f);
    // half-window in ticks (≥1 so division is always safe)
    const int half = std::max(1, static_cast<int>(
                        std::roundf(kSynthHalfWindowSec / dtSec)));

    synthAdc_.resize(N);

    // --- Pass 1: fill every row with the current detent's nominal pot value.
    //
    // Without this pass, rows outside the smoothstep windows would stay at
    // the initial value (zero), so any long stretch at one detent would have
    // wrong ADC → wrong pip position. The JS reference has the same pass and
    // documents this bug explicitly.
    for (size_t i = 0; i < N; i++)
    {
        const int pot = PotPositionForFlap_(rows[i].flapsPos);
        synthAdc_[i] = static_cast<uint16_t>(
            std::max(0, std::min(pot, 65535)));
    }

    // --- Pass 2: paint smoothstep windows over each detent transition.
    //
    // At each tick i where flapsPos changes, interpolate from the previous
    // pot value to the new pot value over ±half ticks centred on i.
    //
    // u = 0.5 + 0.5 * (k - i) / half → u=0 at k=i-half, u=0.5 at k=i,
    // u=1 at k=i+half. lambda = smoothstep(u).
    //
    // Matches the JS reference:
    //   const signed = (k - i) / half;
    //   const u = 0.5 + 0.5 * signed;
    //   const lambda = smoothstep(u);
    //   out[k] = (1 - lambda) * prevFs.potValue + lambda * newFs.potValue;
    int lastFlap = rows[0].flapsPos;
    for (size_t i = 1; i < N; i++)
    {
        const int curFlap = rows[i].flapsPos;
        if (curFlap != lastFlap)
        {
            const float prevPot = static_cast<float>(PotPositionForFlap_(lastFlap));
            const float newPot  = static_cast<float>(PotPositionForFlap_(curFlap));

            const size_t start = (i >= static_cast<size_t>(half))
                                 ? (i - static_cast<size_t>(half))
                                 : 0;
            const size_t end   = std::min(N - 1u, i + static_cast<size_t>(half));

            for (size_t k = start; k <= end; k++)
            {
                const float signed_frac = static_cast<float>(static_cast<int>(k) -
                                          static_cast<int>(i))
                                          / static_cast<float>(half);
                const float u      = 0.5f + 0.5f * signed_frac;
                const float lambda = Smoothstep(u);
                const float val    = (1.0f - lambda) * prevPot + lambda * newPot;
                synthAdc_[k] = static_cast<uint16_t>(
                    std::max(0.0f, std::min(val, 65535.0f)));
            }

            lastFlap = curFlap;
        }
    }
}

// ============================================================================

ReplayStepResult LogReplayEngine::step(const onspeed::LogRow& row)
{
    ReplayStepResult out;

    // --- Unpack pressure, flap, and sensor fields from the row ---
    // Mirrors the global write-back in ReadLogLine() lines 222-252.

    out.pfwdSmoothed       = row.pfwdSmoothed;
    out.p45Smoothed        = row.p45Smoothed;
    out.flapsPos           = row.flapsPos;

    if (flapsRawAdcAvailable_)
    {
        // Log carries the real ADC column — pass through as-is.
        out.flapsRawAdcPresent = row.flapsRawAdcPresent;
        if (row.flapsRawAdcPresent)
            out.flapsRawAdc = row.flapsRawAdc;
    }
    else if (!synthAdc_.empty() && synthAdcIdx_ < synthAdc_.size())
    {
        // Use the pre-computed synth value for this row.
        // flapsRawAdcPresent stays false so the task wrapper still knows
        // the original log lacked the column, but flapsRawAdc carries the
        // synthesised value for the pip-position computation.
        out.flapsRawAdcPresent = false;
        out.flapsRawAdc        = synthAdc_[synthAdcIdx_];
        ++synthAdcIdx_;
    }
    else
    {
        // No synth table (prepare() not called for old log, or empty rows).
        // Fall back to the row's own field — will be zero for old logs.
        // LCOV_EXCL_LINE: guarding against callers that skip prepare() on
        // old logs; should not happen in correct usage but cheap to guard.
        out.flapsRawAdcPresent = false;
        out.flapsRawAdc        = row.flapsRawAdc;
    }

    // Pressure coefficient from smoothed pitot + 45-degree sensor.
    // Mirrors: g_fCoeffP = pressureCoeff(g_Sensors.PfwdSmoothed, g_Sensors.P45Smoothed)
    out.coeffP = pressureCoeff(out.pfwdSmoothed, out.p45Smoothed);

    // Resolve flap index. Mirrors the for-loop in ReadLogLine() that sets
    // g_Flaps.iIndex when g_Flaps.iPosition matches a configured detent.
    out.flapsIndex = ResolveFlapIndex_(out.flapsPos);

    // --- Air data ---
    out.paltFt  = row.paltFt;
    out.iasKt   = row.iasKt;
    out.iasValid = row.iasValid;

    // --- Data mark ---
    out.dataMark = row.dataMark;

    // --- Kalman VSI: convert fpm from log to m/s for g_AHRS.KalmanVSI ---
    // Mirrors: g_AHRS.KalmanVSI = fpm2mps(row.vsiFpm)
    out.kalmanVSI = fpm2mps(row.vsiFpm);

    // --- IMU state ---
    out.imuForwardG     = row.imuForwardG;
    out.imuLateralG     = row.imuLateralG;
    out.imuVerticalG    = row.imuVerticalG;
    out.imuRollRateDps  = row.imuRollRateDps;
    out.imuPitchRateDps = row.imuPitchRateDps;  // un-negated raw value
    out.imuYawRateDps   = row.imuYawRateDps;

    // --- AHRS state ---
    out.pitchDeg      = row.pitchDeg;
    out.rollDeg       = row.rollDeg;
    out.flightPathDeg = row.flightPathDeg;

    // --- Corrected accel: g_AHRS.AccelLatCorr  = g_pIMU->Ay ---
    //                      g_AHRS.AccelVertCorr = g_pIMU->Az ---
    out.accelLatCorr  = row.imuLateralG;
    out.accelVertCorr = row.imuVerticalG;

    // --- AOA calculation ---
    // Mirrors ReadLogLine() lines 269-273. Uses the resolved flap index to
    // select the calibration curve. aoaCalc_ holds smoothing state across
    // calls just as g_Sensors.AoaCalc does in the live path.
    if (!cfg_.aFlaps.empty())
    {
        const onspeed::SuCalibrationCurve& curve =
            cfg_.aFlaps[out.flapsIndex].AoaCurve;
        AOACalculatorResult result =
            aoaCalc_.calculate(out.pfwdSmoothed, out.p45Smoothed, curve);
        out.aoa    = result.aoa;
        out.coeffP = result.coeffP;
    }
    // else: no flap config — aoa stays 0.0, coeffP from pressureCoeff above

    return out;
}

}  // namespace onspeed::replay
