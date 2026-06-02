// replay/LogReplayEngine.cpp — platform-free per-row pipeline for SD log replay.
//
// See LogReplayEngine.h for the architecture overview.
//
// Accel smoothing (Sub-task 2 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md):
// Raw IMU columns (imuLateralG, imuVerticalG, imuForwardG) are fed through
// RateAdjustedAccelEma instances and the smoothed values are written to
// accelLatSmoothed, accelVertSmoothed, accelFwdSmoothed.  Raw values still appear
// in imuLateralG/VerticalG/ForwardG for task wrappers that need g_pIMU->*.
//
// Streaming synth ADC (Sub-task 3 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md):
// When the log lacks the flapsRawADC column, the engine synthesises a
// smoothstep sweep across detent transitions using a streaming circular
// buffer. The invariants:
//
//   - circBuf_ holds at most synthHalfWindowTicks_+1 pre-computed results
//     (AOA, pressure, all passthrough fields already filled in). The buffer
//     is sized at construction from kSynthHalfWindowSec × logSampleRateHz:
//       50 Hz  → 101 slots (~10 KB)   208 Hz → 417 slots (~42 KB)
//   - step() returns empty until synthHalfWindowTicks_ rows have been fed
//     (lag period). After that, each step() emits the oldest buffered result
//     after applying the synth ADC value for that row's absolute tick.
//   - A detent transition at absolute tick T means rows in
//     [T-synthHalfWindowTicks_, T+synthHalfWindowTicks_] get a smoothstep
//     blend. Since the emit point is always exactly synthHalfWindowTicks_
//     ticks behind the write point, both edges of the transition window are
//     guaranteed visible in the buffer when emission occurs.
//   - flush() drains the remaining buffered rows after the input stream ends.
//
// Output bit-identical to a batch two-pass implementation: the smoothstep for
// any emit tick depends only on transitions within ±synthHalfWindowTicks_ of
// that tick, which are always in the buffer. The streaming window is exactly
// ±synthHalfWindowTicks_, so no wider context is ever needed.

#include <replay/LogReplayEngine.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filters/RateAdjustedAccelEma.h>

#include <util/OnSpeedTypes.h>

using onspeed::pressureCoeff;
using onspeed::fpm2mps;
using onspeed::AOACalculatorResult;
using onspeed::config::OnSpeedConfig;
using onspeed::filters::kAccelEmaTauSec;
using onspeed::filters::RateAdjustedAccelEma;

namespace onspeed::replay {

// ============================================================================
// Constructor / reset
// ============================================================================

LogReplayEngine::LogReplayEngine(const OnSpeedConfig& cfg,
                                 int  logSampleRateHz,
                                 bool flapsRawAdcAvailable)
    : cfg_(cfg)
    , flapsRawAdcAvailable_(flapsRawAdcAvailable)
    // Compute at engine construct time. Per-instance, fixed for the lifetime
    // of one replay session. Per Issue #492, one log file = one sample rate
    // (firmware enforces); the rate doesn't change mid-stream.
    , synthHalfWindowTicks_(static_cast<int>(kSynthHalfWindowSec *
                                             static_cast<float>(logSampleRateHz)))
    , aoaCalc_(cfg.iAoaSmoothing)
    , accelLatEma_ (static_cast<float>(logSampleRateHz), kAccelEmaTauSec)
    , accelVertEma_(static_cast<float>(logSampleRateHz), kAccelEmaTauSec)
    , accelFwdEma_ (static_cast<float>(logSampleRateHz), kAccelEmaTauSec)
    // GOnsetFilter default tau (250 ms) matches the firmware's default
    // for the AHRS-rate path; the M5 wire-rate path uses the same tau.
    , gOnsetFilter_()
    , dtSec_(1.0f / static_cast<float>(logSampleRateHz))
    , circBuf_(static_cast<size_t>(synthHalfWindowTicks_ + 1))
    , bufHead_(0)
    , bufSize_(0)
    , rowsFed_(0)
    , lastFlapPosDeg_(-1)
    , numTransitions_(0)
{
}

// ============================================================================

void LogReplayEngine::reset()
{
    aoaCalc_.reset();
    accelLatEma_.reset();
    accelVertEma_.reset();
    accelFwdEma_.reset();
    gOnsetFilter_.Reset();
    bufHead_        = 0;
    bufSize_        = 0;
    rowsFed_        = 0;
    lastFlapPosDeg_ = -1;
    numTransitions_ = 0;
}

// ============================================================================
// Private helpers
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

uint16_t LogReplayEngine::PotForFlapPos_(int flapPosDeg) const
{
    for (int i = 0; i < (int)cfg_.aFlaps.size(); i++)
    {
        if (flapPosDeg == cfg_.aFlaps[i].iDegrees)
            return static_cast<uint16_t>(cfg_.aFlaps[i].iPotPosition);
    }
    return 0u;
}

// Compute the synth ADC for the row at absolute tick emitTick.
//
// For each stored transition:
//   - The transition centred at snapTick paints the range
//     [snapTick - synthHalfWindowTicks_, snapTick + synthHalfWindowTicks_].
//   - If emitTick falls inside that range, compute:
//       t = (emitTick - (snapTick - synthHalfWindowTicks_)) / (2 * synthHalfWindowTicks_)
//       s = 3t^2 - 2t^3   (smoothstep)
//       result = lerp(prevPot, nextPot, s)
//   - If multiple transitions overlap (unusual), the LAST one wins
//     (the transition that started most recently takes precedence).
//
// If no transition window contains emitTick, return the nominal pot for
// flapPosDeg (steady-state value).
uint16_t LogReplayEngine::ComputeSynthAdc_(int emitTick, int flapPosDeg) const
{
    const float steadyPot = static_cast<float>(PotForFlapPos_(flapPosDeg));

    float resultPot = steadyPot;
    bool  inWindow  = false;

    for (int i = 0; i < numTransitions_; i++)
    {
        const int snapTick  = transitions_[i].snapTick;
        const int winStart  = snapTick - synthHalfWindowTicks_;
        const int winEnd    = snapTick + synthHalfWindowTicks_;

        if (emitTick < winStart || emitTick > winEnd)
            continue;

        // t in [0, 1] across the window
        const float span = static_cast<float>(2 * synthHalfWindowTicks_);
        float t = static_cast<float>(emitTick - winStart) / span;
        // Clamp to [0, 1] for safety
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        // Smoothstep: s = 3t^2 - 2t^3
        const float s = t * t * (3.0f - 2.0f * t);

        const float prev = static_cast<float>(transitions_[i].prevPot);
        const float next = static_cast<float>(transitions_[i].nextPot);
        resultPot = prev + s * (next - prev);
        inWindow = true;
    }

    if (!inWindow)
        resultPot = steadyPot;

    // Clamp to uint16 range and return
    if (resultPot < 0.0f)   resultPot = 0.0f;
    if (resultPot > 65535.0f) resultPot = 65535.0f;
    return static_cast<uint16_t>(resultPot + 0.5f);
}

// ============================================================================

ReplayStepResult LogReplayEngine::ComputeBase_(const onspeed::LogRow& row)
{
    ReplayStepResult out;

    // --- Unpack pressure, flap, and sensor fields from the row ---
    out.pfwdSmoothed       = row.pfwdSmoothed;
    out.p45Smoothed        = row.p45Smoothed;
    out.flapsPos           = row.flapsPos;
    // flapsRawAdcPresent and flapsRawAdc are intentionally NOT set here;
    // they are filled by the caller (fast path: copy from row; synth path:
    // overlay from ComputeSynthAdc_).

    // Pressure coefficient — computed below after AOA calc updates it.
    out.coeffP = pressureCoeff(out.pfwdSmoothed, out.p45Smoothed);

    out.flapsIndex = ResolveFlapIndex_(out.flapsPos);

    // --- Air data ---
    out.paltFt  = row.paltFt;
    out.iasKt   = row.iasKt;
    out.iasValid = row.iasValid;

    // --- Data mark ---
    out.dataMark = row.dataMark;

    // --- VSI: convert fpm from log to m/s for g_AHRS.VsiMps ---
    out.vsiMps = fpm2mps(row.vsiFpm);

    // --- IMU state ---
    out.imuForwardG     = row.imuForwardG;
    out.imuLateralG     = row.imuLateralG;
    out.imuVerticalG    = row.imuVerticalG;
    out.imuRollRateDps  = row.imuRollRateDps;
    out.imuPitchRateDps = row.imuPitchRateDps;
    out.imuYawRateDps   = row.imuYawRateDps;

    // --- AHRS state ---
    out.pitchDeg      = row.pitchDeg;
    out.rollDeg       = row.rollDeg;
    out.flightPathDeg = row.flightPathDeg;

    // --- Rate-adjusted-EMA smoothed accel: wire-shaped output ---
    // Feeds raw IMU through the rate-adjusted EMA filters, producing the
    // same continuous-time smoothing as the firmware's 208 Hz AHRS accel
    // filter (kAccSmoothing=0.060899, tau≈0.076516 s), applied at the log
    // rate.  Smoothed values go to the wire-facing fields.
    // Raw values remain in imuLateralG/VerticalG/ForwardG for g_pIMU->* consumers.
    out.accelLatSmoothed  = accelLatEma_ .update(row.imuLateralG);
    out.accelVertSmoothed = accelVertEma_.update(row.imuVerticalG);
    out.accelFwdSmoothed = accelFwdEma_ .update(row.imuForwardG);

    // --- G onset rate (g/s) ---
    // Mirrors the firmware's GOnsetFilter on the smoothed vertical-G axis.
    // Per-row dt is `1 / logSampleRateHz`. First sample seeds prev and
    // returns 0 (no spurious derivative spike).
    out.gOnsetRate = gOnsetFilter_.Update(out.accelVertSmoothed, dtSec_);

    // --- Turn rate (deg/s) ---
    // Same source as the IMU yaw axis; carried separately so the
    // wireBridge assignment for `DisplayBuildInputs::turnRateDps`
    // is a one-line copy.
    out.turnRateDps = row.imuYawRateDps;

    // --- OAT (°C) ---
    // Round to int to match the wire field's resolution (signed ±99 in
    // a %+03d field, see proto/DisplaySerial.h). Defaults to 0 when the
    // log column is missing or the original flight had no OAT sensor —
    // same wire byte the M5 receives in that case.
    out.oatC = static_cast<int>(std::round(row.oatCelsius));

    // --- AOA calculation ---
    if (!cfg_.aFlaps.empty())
    {
        const onspeed::SuCalibrationCurve& curve =
            cfg_.aFlaps[out.flapsIndex].AoaCurve;
        AOACalculatorResult result =
            aoaCalc_.calculate(out.pfwdSmoothed, out.p45Smoothed, curve);
        out.aoa    = result.aoa;
        out.coeffP = result.coeffP;
    }

    return out;
}

// ============================================================================

ReplayStepResult LogReplayEngine::EmitOldest_()
{
    // Read the oldest buffered entry (circBuf_[bufHead_]).
    // Apply synth ADC for that row's absolute tick.
    //
    // The absolute tick of the oldest buffered entry:
    //   rowsFed_ is the total count of rows fed (1-indexed).
    //   bufSize_ entries are buffered. The oldest was fed at tick
    //   (rowsFed_ - bufSize_ + 1).
    const int emitTick = rowsFed_ - bufSize_ + 1;
    ReplayStepResult res = circBuf_[bufHead_];

    // Overlay synth ADC.
    res.flapsRawAdcPresent = true;
    res.flapsRawAdc        = ComputeSynthAdc_(emitTick, res.flapsPos);

    // Advance the circular buffer head.
    bufHead_ = (bufHead_ + 1) % static_cast<int>(circBuf_.size());
    --bufSize_;

    // Evict transitions that are no longer reachable from the emit point.
    // A transition at snapTick covers [snapTick - synthHalfWindowTicks_, snapTick +
    // synthHalfWindowTicks_]. Once emitTick > snapTick + synthHalfWindowTicks_
    // the transition is done; remove it.  We keep the array compact.
    int keep = 0;
    for (int i = 0; i < numTransitions_; i++)
    {
        if (emitTick <= transitions_[i].snapTick + synthHalfWindowTicks_)
            transitions_[keep++] = transitions_[i];
    }
    numTransitions_ = keep;

    return res;
}

// ============================================================================

std::optional<ReplayStepResult>
LogReplayEngine::PushAndMaybeEmit_(const ReplayStepResult& res)
{
    // Write into the circular buffer at the next available slot.
    const int capacity = static_cast<int>(circBuf_.size());
    const int writeIdx = (bufHead_ + bufSize_) % capacity;
    circBuf_[writeIdx] = res;
    ++bufSize_;

    // If the buffer has exceeded its capacity (shouldn't happen by construction
    // because we drain one entry whenever bufSize_ reaches capacity), cap it.
    // Guarded below.

    if (bufSize_ < capacity)
    {
        // Buffer not yet full — still in the lag period. No output yet.
        return std::nullopt;
    }

    // Buffer is full: emit the oldest entry.
    return EmitOldest_();
}

// ============================================================================
// Public API
// ============================================================================

std::optional<ReplayStepResult>
LogReplayEngine::step(const onspeed::LogRow& row)
{
    // Compute AOA, pressure, passthrough fields. Does NOT fill flapsRawAdc.
    ReplayStepResult base = ComputeBase_(row);

    ++rowsFed_;

    if (flapsRawAdcAvailable_)
    {
        // Fast path: column present in log — copy verbatim, emit immediately.
        base.flapsRawAdcPresent = row.flapsRawAdcPresent;
        if (row.flapsRawAdcPresent)
            base.flapsRawAdc = row.flapsRawAdc;
        return base;
    }

    // Synth path: detect transitions and buffer the row.

    // Detect a detent transition on this row.
    if (lastFlapPosDeg_ >= 0 && row.flapsPos != lastFlapPosDeg_)
    {
        // Record the transition if there's room. If the table is full,
        // drop the oldest entry to make room (shouldn't happen in practice —
        // at most one transition per 2*synthHalfWindowTicks_ rows on a real
        // flight).
        if (numTransitions_ >= kMaxTransitions)
        {
            // Evict the oldest (index 0) — shift left.
            for (int i = 0; i < numTransitions_ - 1; i++)
                transitions_[i] = transitions_[i + 1];
            --numTransitions_;
        }

        TransitionRecord tr;
        tr.snapTick = rowsFed_;   // rowsFed_ was already incremented above
        tr.prevPot  = PotForFlapPos_(lastFlapPosDeg_);
        tr.nextPot  = PotForFlapPos_(row.flapsPos);
        transitions_[numTransitions_++] = tr;
    }
    lastFlapPosDeg_ = row.flapsPos;

    return PushAndMaybeEmit_(base);
}

// ============================================================================

std::vector<ReplayStepResult> LogReplayEngine::flush()
{
    std::vector<ReplayStepResult> out;

    if (flapsRawAdcAvailable_)
        return out;   // nothing buffered on the fast path

    out.reserve(static_cast<size_t>(bufSize_));
    while (bufSize_ > 0)
        out.push_back(EmitOldest_());

    return out;
}

}  // namespace onspeed::replay
