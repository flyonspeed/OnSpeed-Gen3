// LogReplayTask.cpp — see LogReplayTask.h for design rationale.

#include <replay/LogReplayTask.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <aoa/DisplayPctAnchors.h>
#include <aoa/PercentLift.h>
#include <proto/DisplaySerial.h>
#include <sensors/IasAlive.h>
#include <util/OnSpeedTypes.h>            // mps2fpm

namespace onspeed::replay {

namespace {

// Clamp helper for the int wire fields. The DisplayBuildInputs spec
// pins certain ranges (vsiFpm10 in [-999..+999], dataMark in [0..99]);
// other consumers expect values to be reasonable. Out-of-range inputs
// would clamp inside BuildDisplayFrame itself, but we mirror the
// firmware's pre-clamp here to preserve the same overflow envelope.
int ClampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Locate the active flap index given the snapped flap position.
// Mirrors LogReplayEngine's ResolveFlapIndex (which is private; we
// re-derive here from cfg). Returns 0 when the cfg has no flap entries
// or no exact match (caller's anchors path tolerates either).
size_t ResolveFlapIndex(const ::onspeed::config::OnSpeedConfig& cfg, int flapsPos)
{
    for (size_t i = 0; i < cfg.aFlaps.size(); ++i) {
        if (cfg.aFlaps[i].iDegrees == flapsPos) return i;
    }
    return 0;
}

// Compute flap-travel min/max for the wire-side widget endpoints.
// Mirrors DisplaySerial.cpp:210-222 in the firmware. aFlaps may not
// be sorted in cfg order, so iterate.
void FlapsRange(const ::onspeed::config::OnSpeedConfig& cfg, int& outMin, int& outMax)
{
    if (cfg.aFlaps.empty()) {
        outMin = 0;
        outMax = 0;
        return;
    }
    outMin = cfg.aFlaps[0].iDegrees;
    outMax = outMin;
    for (size_t i = 1; i < cfg.aFlaps.size(); ++i) {
        const int d = cfg.aFlaps[i].iDegrees;
        if (d < outMin) outMin = d;
        if (d > outMax) outMax = d;
    }
}

}   // namespace

LogReplayTask::LogReplayTask(const ::onspeed::config::OnSpeedConfig& cfg,
                             int                                     logSampleRateHz,
                             bool                                    flapsRawAdcAvailable)
  : cfg_(cfg)
  , engine_(cfg, logSampleRateHz, flapsRawAdcAvailable)
{
}

std::vector<uint8_t> LogReplayTask::processRow(const LogRow& row)
{
    // Apply the firmware's hysteretic IAS-alive gate before stepping
    // the engine. The CSV parser sets row.iasValid based on whether
    // the IAS column was empty (the firmware writes "" when bIasAlive
    // is false), but for browser replay we re-derive deterministically
    // from row.iasKt. This handles both modern logs (where the empty-
    // column convention is faithful) and older logs that always
    // populated IAS (where empty-column inference would never trip).
    iasAlive_ = onspeed::sensors::UpdateIasAlive(iasAlive_, row.iasKt);

    // Copy and override row.iasValid so the engine and downstream
    // consumers see the same gate the firmware would have set.
    LogRow gatedRow      = row;
    gatedRow.iasValid    = iasAlive_;

    const std::optional<ReplayStepResult> opt = engine_.step(gatedRow);
    if (!opt.has_value()) return {};        // synth-path lag period
    lastStep_ = opt.value();
    return EncodeFrame_(lastStep_);
}

std::vector<std::vector<uint8_t>> LogReplayTask::flush()
{
    std::vector<std::vector<uint8_t>> out;
    for (const ReplayStepResult& r : engine_.flush()) {
        out.push_back(EncodeFrame_(r));
    }
    return out;
}

void LogReplayTask::reset()
{
    engine_.reset();
    iasAlive_ = false;
}

std::vector<uint8_t> LogReplayTask::EncodeFrame_(
    const ReplayStepResult& r) const
{
    using onspeed::aoa::ComputeDisplayPctAnchors;
    using onspeed::aoa::ComputePercentLift;
    using onspeed::aoa::DisplayPctAnchors;
    using onspeed::proto::BuildDisplayFrame;
    using onspeed::proto::DisplayBuildInputs;
    using onspeed::proto::kDisplayFrameSizeBytes;

    const size_t activeIdx = ResolveFlapIndex(cfg_, r.flapsPos);

    // Per-flap anchors: pip / tonesOn / fast / slow / warn / flapsDeg.
    // Anchors don't gate on live-AOA validity (mirrors DisplaySerial.cpp).
    const uint16_t rawAdc = static_cast<uint16_t>(r.flapsRawAdc);
    const DisplayPctAnchors anchors = ComputeDisplayPctAnchors(
        rawAdc,
        cfg_.aFlaps.empty() ? nullptr : cfg_.aFlaps.data(),
        cfg_.aFlaps.size(),
        activeIdx,
        /*iasValid=*/true);

    // Percent-lift from body-angle AOA. Falls to 0 when iasValid is
    // false (matches firmware behavior).
    float percentLiftPct = 0.0f;
    if (!cfg_.aFlaps.empty()) {
        percentLiftPct = ComputePercentLift(
            r.aoa, cfg_.aFlaps[activeIdx], r.iasValid);
    }

    int flapsMinDeg = 0;
    int flapsMaxDeg = 0;
    FlapsRange(cfg_, flapsMinDeg, flapsMaxDeg);

    DisplayBuildInputs in;
    in.pitchDeg           = r.pitchDeg;
    in.rollDeg            = r.rollDeg;
    in.iasKt              = r.iasKt;
    in.iasValid           = r.iasValid;
    in.paltFt             = r.paltFt;
    in.turnRateDps        = r.turnRateDps;
    // Body-frame, positive = airframe accel rightward. Smoothed by the
    // engine's accel EMA; the encoder consumes the smoothed value.
    in.lateralG           = r.accelLatSmoothed;
    // The firmware caches a pre-rounded int (iDisplayVerticalG) and
    // casts it back to float. Reproduce that rounding here so the
    // wire byte for verticalG matches what the M5 sees in flight.
    {
        const int rounded = static_cast<int>(
            std::lround(r.accelVertSmoothed * 10.0f));
        in.verticalGScaled10 = static_cast<float>(rounded);
    }
    in.percentLiftPct     = percentLiftPct;
    in.vsiFpm10           = ClampInt(
        static_cast<int>(std::floor(mps2fpm(r.kalmanVSI) / 10.0f)),
        -999, 999);
    in.oatC               = r.oatC;
    in.flightPathDeg      = r.flightPathDeg;
    // Anchors.flapsDeg is the lever-interpolated value; fall back to
    // the snapped flapsPos when there is no calibrated flap snapshot
    // (anchors.flapsDeg=0 in that case). Mirrors DisplaySerial.cpp:383.
    in.flapsDeg           = (!cfg_.aFlaps.empty())
                                ? anchors.flapsDeg
                                : r.flapsPos;
    in.tonesOnPctLift     = anchors.tonesOnPctLift;
    in.onSpeedFastPctLift = anchors.onSpeedFastPctLift;
    in.onSpeedSlowPctLift = anchors.onSpeedSlowPctLift;
    in.stallWarnPctLift   = anchors.stallWarnPctLift;
    in.pipPctLift         = anchors.pipPctLift;
    in.flapsMinDeg        = flapsMinDeg;
    in.flapsMaxDeg        = flapsMaxDeg;
    in.gOnsetRate         = r.gOnsetRate;
    in.spinRecoveryCue    = 0;     // reserved; firmware writes 0 too
    in.dataMark           = static_cast<int>(
        static_cast<unsigned>(r.dataMark) % 100u);

    std::vector<uint8_t> buf(kDisplayFrameSizeBytes);
    const size_t n = BuildDisplayFrame(in, buf.data(), buf.size());
    if (n != kDisplayFrameSizeBytes) {
        // BuildDisplayFrame returns 0 on snprintf-payload mismatch (a
        // clamping/format bug). Should never happen with valid inputs.
        // Mirror that by returning empty so callers see a clean signal.
        return {};
    }
    return buf;
}

} // namespace onspeed::replay
