// Panning.cpp — 3D audio left/right gain calculation from lateral G.
// See Panning.h for pipeline notes.

#include <audio/Panning.h>

#include <algorithm>
#include <cmath>

namespace onspeed {
namespace audio {

namespace {

// Saturating linear gain curve.  Monotonic in |lateralG|, no descending
// tail: pan ramps from 0 at 0 G to fully saturated at 0.125 G (~1.5
// slip-skid ball widths) and holds at 1.0 for every larger lateral
// load.  This keeps the directional cue audible through the entire
// spin / snap-roll regime (0.3–0.8 G sustained), where the prior
// downward-opening parabola crossed zero at |x| ≈ 0.216 G and the
// caller's [0, 1] clamp pinned the gain to 0 — i.e. centered audio
// with no L/R difference exactly when the cue matters most.
//   f(0.000)  =  0.000
//   f(0.050)  =  0.400
//   f(0.080)  =  0.640
//   f(0.125)  =  1.000  (saturation point)
//   f(0.250)  =  1.000
//   f(0.500)  =  1.000
//   f(1.000)  =  1.000
// Closes #371.
inline float CurveGain(float absLateralG)
{
    return std::min(1.0f, 8.0f * absLateralG);
}

inline float Clamp(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

}  // namespace

PanResult Apply3DPan(float lateralG, PanState& state, const PanConfig& cfg)
{
    const float fAbs    = std::fabs(lateralG);
    const float fSign   = (lateralG >= 0.0f) ? 1.0f : -1.0f;

    float fCurve = CurveGain(fAbs);
    fCurve = Clamp(fCurve, 0.0f, 1.0f);
    fCurve *= fSign;

    state.channelGain = cfg.smoothingFactor * fCurve
                      + (1.0f - cfg.smoothingFactor) * state.channelGain;
    state.channelGain = Clamp(state.channelGain, -1.0f, 1.0f);

    float fLeft  = std::fabs(-1.0f + state.channelGain);
    float fRight = std::fabs( 1.0f + state.channelGain);

    // Max-gain normalization.  Preserves L/R ratio (panning direction)
    // without ever exceeding 1.0 on either channel.
    const float fMax = (fLeft > fRight) ? fLeft : fRight;
    if (fMax > 1.0f)
    {
        fLeft  /= fMax;
        fRight /= fMax;
    }

    PanResult out;
    out.leftGain  = fLeft;
    out.rightGain = fRight;
    return out;
}

}  // namespace audio
}  // namespace onspeed
