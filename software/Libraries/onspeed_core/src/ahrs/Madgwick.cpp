// Madgwick.cpp — AHRS-stage Madgwick attitude pipeline. See Madgwick.h.

#include <ahrs/Madgwick.h>

#include <util/OnSpeedTypes.h>
#include <util/Perf.h>

namespace onspeed::ahrs {

Madgwick::Madgwick()
    : accelFwdFilter_(kAccelEmaAlpha)
    , accelLatFilter_(kAccelEmaAlpha)
    , accelVertFilter_(kAccelEmaAlpha)
{
    // Seed accel filters with the production "level on the ground" rest
    // state (Z = +1 g, X = Y = 0).  Seeding here means the first
    // frames — before the IMU has produced a real sample — yield a
    // sane attitude rather than a degenerate (0, 0, 0).
    accelFwdFilter_.seed(0.0f);
    accelLatFilter_.seed(0.0f);
    accelVertFilter_.seed(+1.0f);
}

void Madgwick::Init(float sampleRateHz, float seedPitchDeg, float seedRollDeg,
                    float seedAltMeters)
{
    // Madgwick convention: begin() takes (sampleFreq, -pitch, roll). The
    // output negation in Step() (-fusion_.getPitch() / -fusion_.getRoll())
    // is the matching half of the same convention.
    fusion_.begin(sampleRateHz, -seedPitchDeg, seedRollDeg);

    // Seed the standalone altitude/VSI Kalman with the supplied baro
    // altitude.  Madgwick doesn't track altitude in its fusion state,
    // so this 3-state Kalman (baro + earth-vert-G) fills the vertical
    // channel role.
    kalman_.Configure(kKalZVariance, kKalAccelVariance, kKalAccelBiasVar,
                      seedAltMeters, 0.0f, 0.0f);

    // Do NOT reset compFadeIn_, iasGate_, or the accel EMA state here.
    // Init() is called on every web UI config save; zeroing those
    // mid-flight would inject transients (re-disabling comp factors for
    // ~0.5 s, re-zeroing the filter's smoothed view of accel). The
    // constructor seeds these once at boot; we leave them alone on
    // Init().
}

Madgwick::Outputs Madgwick::Step(const Inputs& in)
{
    Outputs out;

    // 1) Internal hysteretic IAS gate. Decides whether centripetal /
    //    forward compensation factors should be applied this frame.
    //    Below the rising threshold, the pitot signal is below its
    //    noise floor and tasDot is dominated by sensor jitter — leaving
    //    the comp factors active would inject that noise straight into
    //    the smoothed accel that feeds the fusion algorithm. Hysteresis
    //    prevents chatter on takeoff/landing rollouts crossing the
    //    threshold band.
    if (!iasGate_ && in.iasKt >= kIasGateRisingKt) {
        iasGate_ = true;
    } else if (iasGate_ && in.iasKt < kIasGateFallingKt) {
        iasGate_ = false;
    }

    if (iasGate_) {
        // Apply forward + centripetal compensation. The comp factors are
        // computed against the sensor-stage TAS / TASdot / corrected
        // gyro rates — the algorithm doesn't re-derive them.
        const float AccelFwdCompFactor  = onspeed::mps2g(in.tasDotMps2);
        const float AccelLatCompFactor  =
            onspeed::mps2g(onspeed::deg2rad(in.tasMps * in.yawRateCorrDps));
        const float AccelVertCompFactor =
            onspeed::mps2g(onspeed::deg2rad(in.tasMps * in.pitchRateCorrDps));

        // Ramp the comp factors in smoothly after the gate rising edge.
        // Variable-dt EMA form (α = dt / (τ + dt)) keeps the time
        // constant independent of IMU rate. See issue #114.
        const float fadeAlpha = in.dtSec / (kCompFadeTauSec + in.dtSec);
        compFadeIn_ += fadeAlpha * (1.0f - compFadeIn_);

        out.accelFwdCompG  = accelFwdFilter_.update(in.accelFwdCorrG)
                             - compFadeIn_ * AccelFwdCompFactor;
        out.accelLatCompG  = accelLatFilter_.update(in.accelLatCorrG)
                             - compFadeIn_ * AccelLatCompFactor;
        out.accelVertCompG = accelVertFilter_.update(in.accelVertCorrG)
                             + compFadeIn_ * AccelVertCompFactor;
    } else {
        // Gate closed — tick the accel EMA so it tracks raw inputs
        // (otherwise the smoothed accel grows stale across a long
        // ground pause), but skip the TAS/TASdot-derived comp factors.
        //
        // Reset the comp fade-in coefficient so the next rising edge
        // ramps in again from zero. See issue #114.
        out.accelFwdCompG  = accelFwdFilter_.update(in.accelFwdCorrG);
        out.accelLatCompG  = accelLatFilter_.update(in.accelLatCorrG);
        out.accelVertCompG = accelVertFilter_.update(in.accelVertCorrG);
        compFadeIn_        = 0.0f;
    }

    // 2) Run the fusion algorithm on the compensated accels.
    fusion_.setDeltaTime(in.dtSec);
    fusion_.UpdateIMU(in.rollRateCorrDps,
                      in.pitchRateCorrDps,
                      in.yawRateCorrDps,
                      out.accelFwdCompG,
                      out.accelLatCompG,
                      out.accelVertCompG);

    // Madgwick output negation — opposite half of the input-sign
    // convention applied in Init() / begin(). Pitch and roll come out
    // in OnSpeed's published sign convention.
    out.pitchDeg = -fusion_.getPitch();
    out.rollDeg  = -fusion_.getRoll();

    fusion_.getQuaternion(&out.q0, &out.q1, &out.q2, &out.q3);

    // 3) EarthVertG from the fusion quaternion. The body→earth rotation
    //    of the unsmoothed installation-corrected vertical accel, minus
    //    the +1g level reaction-force convention. For level flight
    //    (q = identity, accelVertCorr = +1g) the formula gives 0.
    out.earthVertG =
        2.0f * (out.q1 * out.q3 - out.q0 * out.q2)                         * in.accelFwdCorrG  +
        2.0f * (out.q0 * out.q1 + out.q2 * out.q3)                         * in.accelLatCorrG  +
        (out.q0 * out.q0 - out.q1 * out.q1 - out.q2 * out.q2 + out.q3 * out.q3) * in.accelVertCorrG - 1.0f;

    out.compFadeIn = compFadeIn_;
    out.iasGate    = iasGate_;

    // 4) Standalone altitude/VSI Kalman on baro + earth-vert-G.  Same
    //    role for Madgwick that EKFQ's z/vz/b_az states fill for that
    //    algorithm.  PerfScope here preserves the same Kalman
    //    attribution the old standalone-Kalman site in Ahrs::Step had.
    {
        onspeed::util::perf::PerfScope guard(
            onspeed::util::perf::ScopeId::Kalman);
        kalman_.Update(in.baroAltMeters,
                       onspeed::g2mps(out.earthVertG),
                       in.dtSec,
                       &out.kalmanAltMeters,
                       &out.kalmanVsiMps);
    }

    return out;
}

}   // namespace onspeed::ahrs
